// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL3/SDL.h>

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "misc.h"
#include "password.h"
#include "platform.h"
#include "port.h"
#include "tg3000.h"
#include "vs.h"

uint8_t g_ram[TG3000_WRAM_SIZE];
const uint8_t* g_rom = NULL;

static bool nmi_enabled = true;
static uint8_t g_sound_last_msg[16];
static unsigned long g_sound_last_frame = 0;

static SDL_Renderer* g_renderer = NULL;
static unsigned long g_frame_limit = 0;
static unsigned long g_frame_count = 0;
static uint64_t g_next_frame_ns = 0;
static jmp_buf g_quit_jmp;

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
    /* a) Run the game's frame code */
    if (nmi_enabled) {
        run_nmi_dispatch();
    }

    /* b) Pump SDL events */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
            longjmp(g_quit_jmp, 1);
        }
        port_input_event(&event);
    }

    /* c) Draw (no drawing in step 2) */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderPresent(g_renderer);

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

int port_run(SDL_Renderer* r, const char* screen, unsigned long frame_limit) {
    g_renderer = r;
    g_frame_limit = frame_limit;
    g_frame_count = 0;
    g_next_frame_ns = 0;

    if (setjmp(g_quit_jmp) != 0) {
        return 0;
    }

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

    for (;;) {
        platform_wait_vblank();
    }
}
