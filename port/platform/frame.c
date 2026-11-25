// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL3/SDL.h>

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "frame.h"
#include "misc.h"
#include "password.h"
#include "platform.h"
#include "port.h"
#include "screens.h"
#include "tg3000.h"
#include "vs.h"

uint8_t g_ram[TG3000_WRAM_SIZE];
const uint8_t* g_rom = NULL;

static bool nmi_enabled = true;
static uint8_t g_sound_last_msg[16];
static unsigned long g_sound_last_frame = 0;

static SDL_Renderer* g_renderer = NULL;
static SDL_Texture* g_texture = NULL;
static uint32_t g_fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static unsigned long g_frame_limit = 0;
static unsigned long g_frame_count = 0;
static uint64_t g_next_frame_ns = 0;
static jmp_buf g_quit_jmp;
static unsigned long g_test_press_frame = 0;
static bool g_test_expect_return = false;
static volatile bool g_screen_returned = false;  /* read after longjmp */
static unsigned long g_shot_frame = 0;
static const char* g_shot_path = NULL;

void platform_set_interrupts(uint8_t value) {
    nmi_enabled = (value & 0x80) != 0;
}

bool video_is_pal(void) {
    return false;
}

bool sound_is_ready(void) {
    return true;
}

void sound_send_frame(const uint8_t msg[16]) {
    memcpy(g_sound_last_msg, msg, sizeof(g_sound_last_msg));
    g_sound_last_frame = g_frame_count;
    (void)g_sound_last_frame;
}

const uint8_t* port_sound_last_message(void) {
    return g_sound_last_msg;
}

void dsp4_port_write16(uint16_t value) {
    (void)value;
    port_fatal("the race's DSP-4 commands are not native yet");
}

uint16_t dsp4_port_read16(void) {
    port_fatal("the race's DSP-4 commands are not native yet");
    return 0;
}

void port_fatal(const char* what) {
    fprintf(stderr, "FATAL: %s\n", what);
    if (getenv("TG3000_HEADLESS") == NULL) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear 3000", what, NULL);
    }
    exit(1);
}

static void run_nmi_dispatch(void) {
    uint8_t op = rd8(0x0000);
    if (op != 0x5C) {
        char msg[128];
        snprintf(msg, sizeof(msg), "unexpected NMI vector opcode 0x%02X at $0000", op);
        port_fatal(msg);
    }

    uint32_t target = (uint32_t)rd8(0x0001)
                    | ((uint32_t)rd8(0x0002) << 8)
                    | ((uint32_t)rd8(0x0003) << 16);

    if (target == 0x808807u) {
        nmi_prologue();
        uint16_t sub = rd16(0x0008);
        switch (sub) {
            case 0x8934: nmi_tail(); break;
            case 0xFBD1: nmi_target_80FBD1(); break;
            case 0xFC5F: nmi_target_80FC5F(); break;
            case 0xE28C: nmi_target_80E28C(); break;
            case 0xE175: nmi_target_80E175(); break;
            case 0xD784: nmi_target_80D784(); break;
            case 0xB97B:
                port_fatal("the two-task screens (championship, shop) are not supported yet");
                break;
            default: {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown NMI dispatch target 0x%04X", sub);
                port_fatal(msg);
                break;
            }
        }
    } else if (target == 0x858377u) {
        port_fatal("the race is not supported yet");
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown NMI vector target 0x%06X", (unsigned int)target);
        port_fatal(msg);
    }
}

void platform_wait_vblank(void) {
    /* a) Pump SDL events first, so the game's frame code (which reads the pads) sees this
       frame's input, not the last one's. */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
            longjmp(g_quit_jmp, 1);
        }
        port_input_event(&event);
    }
    /* Test-only scripted press (--press): the buttons of g_test_press_mask held for a few
       frames from g_test_press_frame. */
    port_input_set_test_mask(
        (g_test_press_frame != 0 && g_frame_count >= g_test_press_frame &&
         g_frame_count < g_test_press_frame + 6) ? 0x8080u : 0);

    /* b) Run the game's frame code */
    if (nmi_enabled) {
        run_nmi_dispatch();
    }

    /* c) Draw */
    if (title_card_active()) {
        title_card_draw(g_fb);
        SDL_UpdateTexture(g_texture, NULL, g_fb, SCREEN_WIDTH * (int)sizeof(uint32_t));
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_renderer);
        SDL_RenderTexture(g_renderer, g_texture, NULL, NULL);
    } else {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            g_fb[i] = 0xFF000000u;
        }
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
        SDL_RenderClear(g_renderer);
    }
    SDL_RenderPresent(g_renderer);

    if (g_shot_path != NULL && g_frame_count == g_shot_frame) {
        /* The picture's fingerprint (FNV-1a 64 of its RGB bytes, top to bottom): a test can
           check a screen against a known value without any picture in the repository. */
        uint64_t h = 0xCBF29CE484222325ULL;
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            for (int sh = 16; sh >= 0; sh -= 8) {
                h ^= (g_fb[i] >> sh) & 0xFFu;
                h *= 0x100000001B3ULL;
            }
        }
        printf("frame %lu picture hash %016llx\n", g_frame_count, (unsigned long long)h);
        fflush(stdout);
    }
    if (g_shot_path != NULL && g_frame_count == g_shot_frame && strcmp(g_shot_path, "-") != 0) {
        FILE* f = fopen(g_shot_path, "wb");
        if (f == NULL) {
            port_fatal("failed to open screenshot path for writing");
        }
        const uint32_t width = SCREEN_WIDTH;
        const uint32_t height = SCREEN_HEIGHT;
        const uint32_t image_size = width * height * 3u;
        const uint32_t file_size = 54u + image_size;
        uint8_t hdr[54];
        memset(hdr, 0, sizeof(hdr));
        hdr[0] = 'B';
        hdr[1] = 'M';
        hdr[2] = (uint8_t)(file_size & 0xFFu);
        hdr[3] = (uint8_t)((file_size >> 8) & 0xFFu);
        hdr[4] = (uint8_t)((file_size >> 16) & 0xFFu);
        hdr[5] = (uint8_t)((file_size >> 24) & 0xFFu);
        hdr[10] = 54u;
        hdr[14] = 40u;
        hdr[18] = (uint8_t)(width & 0xFFu);
        hdr[19] = (uint8_t)((width >> 8) & 0xFFu);
        hdr[22] = (uint8_t)(height & 0xFFu);
        hdr[23] = (uint8_t)((height >> 8) & 0xFFu);
        hdr[26] = 1u;
        hdr[28] = 24u;
        hdr[34] = (uint8_t)(image_size & 0xFFu);
        hdr[35] = (uint8_t)((image_size >> 8) & 0xFFu);
        hdr[36] = (uint8_t)((image_size >> 16) & 0xFFu);
        hdr[37] = (uint8_t)((image_size >> 24) & 0xFFu);
        fwrite(hdr, 1, sizeof(hdr), f);
        uint8_t row[SCREEN_WIDTH * 3];
        for (int y = SCREEN_HEIGHT - 1; y >= 0; --y) {
            const uint32_t* src = &g_fb[y * SCREEN_WIDTH];
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                uint32_t px = src[x];
                row[x * 3 + 0] = (uint8_t)(px & 0xFFu);
                row[x * 3 + 1] = (uint8_t)((px >> 8) & 0xFFu);
                row[x * 3 + 2] = (uint8_t)((px >> 16) & 0xFFu);
            }
            fwrite(row, 1, sizeof(row), f);
        }
        fclose(f);
        g_shot_path = NULL;
    }

    /* d) Pace to 60.0988 fps */
    const uint64_t frame_ns = 16639267ULL;
    uint64_t now = SDL_GetTicksNS();
    if (g_next_frame_ns == 0) {
        g_next_frame_ns = now + frame_ns;
    }
    if (now < g_next_frame_ns) {
        SDL_DelayPrecise(g_next_frame_ns - now);
        g_next_frame_ns += frame_ns;
    } else if (now - g_next_frame_ns > frame_ns * 3ULL) {
        g_next_frame_ns = now + frame_ns;
    } else {
        g_next_frame_ns += frame_ns;
    }

    /* e) Frame counter and limit check */
    g_frame_count++;    if (g_frame_limit > 0 && g_frame_count >= g_frame_limit) {
        longjmp(g_quit_jmp, 1);
    }
}

void port_set_test_options(unsigned long press_frame, bool expect_return) {
    g_test_press_frame = press_frame;
    g_test_expect_return = expect_return;
}

void port_set_shot(unsigned long frame, const char* path) {
    g_shot_frame = frame;
    g_shot_path = path;
}

int port_run(SDL_Renderer* r, const char* screen, unsigned long frame_limit) {
    g_renderer = r;
    g_frame_limit = frame_limit;
    g_frame_count = 0;
    g_next_frame_ns = 0;
    g_screen_returned = false;

    if (g_texture == NULL) {
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
        if (g_texture == NULL) {
            port_fatal("SDL_CreateTexture failed");
        }
        SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST);
    }

    if (setjmp(g_quit_jmp) != 0) {
        if (g_texture != NULL) {
            SDL_DestroyTexture(g_texture);
            g_texture = NULL;
        }
        if (g_test_expect_return && !g_screen_returned) {
            fprintf(stderr, "FAIL: screen %s did not return within %lu frames\n", screen,
                    g_frame_count);
            return 2;
        }
        return 0;
    }

    assets_init();
    memset(g_ram, 0, sizeof(g_ram));
    interrupt_vectors_init();
    nmi_enabled = true;

    if (strcmp(screen, "title_card") == 0) {
        title_card_show();
    } else if (strcmp(screen, "password_show") == 0) {
        password_show();
    } else if (strcmp(screen, "vs") == 0) {
        vs_screen();
    } else if (strcmp(screen, "options") == 0) {
        options_screens(0);
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown screen: %s", screen);
        port_fatal(msg);
    }
    g_screen_returned = true;
    printf("screen %s returned at frame %lu (game frame counter %u)\n", screen, g_frame_count,
           (unsigned)rd16(FRAME_COUNTER));
    fflush(stdout);

    for (;;) {
        platform_wait_vblank();
    }
}
