// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL3/SDL.h>

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "boot.h"
#include "frame.h"
#include "misc.h"
#include "mixer.h"
#include "password.h"
#include "race_frame.h"
#include "platform.h"
#include "port.h"
#include "screens.h"
#include "sequencer.h"
#include "sound.h"
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
static const char* g_start_wram_path = NULL;
static const char* g_shot_path = NULL;
static unsigned long g_shots_first = 0;
static unsigned long g_shots_last = 0;
static const char* g_shots_dir = NULL;
static unsigned long g_shots_every = 1;
static unsigned long g_wram_out_frame = 0;
static const char* g_wram_out_path = NULL;
static bool g_fast = false;

static bool g_split_active = false;
static bool g_task2_running = false;
static bool g_task2_abort = false;
static SDL_Thread* g_task2_thread = NULL;
static SDL_ThreadID g_task1_thread_id = 0;
static SDL_Semaphore* g_sem_task1 = NULL;
static SDL_Semaphore* g_sem_task2 = NULL;
static void (*g_task2_fn)(uint16_t) = NULL;
static jmp_buf g_task2_exit_jmp;

typedef struct {
    long step;
    uint16_t mask;
} MovieEvent;

static MovieEvent* g_movie_events = NULL;
static size_t g_movie_len = 0;
static size_t g_movie_idx = 0;
static uint16_t g_movie_held = 0;
static long g_movie_offset = 0;
static bool g_movie_offset_from_set = false;
static unsigned long g_movie_offset_from_frame = 0;
static long g_movie_offset_from_value = 0;

static uint16_t movie_buttons_for_frame(unsigned long frame) {
    if (g_movie_events == NULL) {
        return 0;
    }
    long offset = (g_movie_offset_from_set && frame >= g_movie_offset_from_frame)
                      ? g_movie_offset_from_value
                      : g_movie_offset;
    long step = (long)frame + offset;
    while (g_movie_idx < g_movie_len && g_movie_events[g_movie_idx].step <= step) {
        g_movie_held = g_movie_events[g_movie_idx].mask;
        ++g_movie_idx;
    }
    return g_movie_held;
}

void platform_set_interrupts(uint8_t value) {
    nmi_enabled = (value & 0x80) != 0;
}

bool video_is_pal(void) {
    return false;
}

bool sound_is_ready(void) {
    return true;
}

/* Test only (--sound-replay FILE): the messages the original game sent to its sound driver,
   recorded on the decomp's bench (one per line: a frame number, then the 16 bytes in hex). From
   the port's first message that asks for song 1, each message is replaced by the next recorded
   one (the file starts with the original's song request), so that the native player hears what
   the original's driver heard: in a race, the engine and the race's effects (the port cannot
   run a race yet). */
static uint8_t (*g_replay)[16];
static size_t g_replay_len;
static size_t g_replay_at;
static bool g_replay_on;

bool port_set_sound_replay(const char* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) return false;
    char line[256];
    size_t cap = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        unsigned b[16];
        unsigned long frame;
        if (sscanf(line, "%lu %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", &frame, &b[0], &b[1], &b[2],
                   &b[3], &b[4], &b[5], &b[6], &b[7], &b[8], &b[9], &b[10], &b[11], &b[12], &b[13], &b[14],
                   &b[15]) != 17) {
            fclose(f);
            port_fatal("--sound-replay: a line is not a frame and 16 bytes");
        }
        if (g_replay_len == cap) {
            cap = cap ? cap * 2 : 4096;
            uint8_t (*next)[16] = realloc(g_replay, cap * sizeof *g_replay);
            if (next == NULL) port_fatal("--sound-replay: out of memory");
            g_replay = next;
        }
        for (int i = 0; i < 16; ++i) g_replay[g_replay_len][i] = (uint8_t)b[i];
        ++g_replay_len;
    }
    fclose(f);
    if (g_replay_len == 0 || g_replay[0][2] != 1) port_fatal("--sound-replay: the file must start with a request for song 1");
    return true;
}

void sound_send_frame(const uint8_t msg[16]) {
    if (g_replay != NULL) {
        if (!g_replay_on && msg[2] == 1) g_replay_on = true;
        if (g_replay_on) {
            if (g_replay_at >= g_replay_len) return;  /* the recording has ended */
            msg = g_replay[g_replay_at++];
            if (g_replay_at == g_replay_len) {
                printf("sound replay: %zu messages played\n", g_replay_len);
                fflush(stdout);
            }
        }
    }
    memcpy(g_sound_last_msg, msg, sizeof(g_sound_last_msg));
    g_sound_last_frame = g_frame_count;
    (void)g_sound_last_frame;
    /* One step of the native player per message, as the original driver takes one. */
    sequencer_message(msg);
}

unsigned long port_frame_number(void) {
    return g_frame_count;
}

/* Test only (--sound-effect FRAME ID): at FRAME, the menus' effect request ID, written as the
   game's own menu code writes it (func_80894E: volume $FF, note 0, all voices). */
#define MAX_TEST_EFFECTS 32
static unsigned long g_test_effect_frame[MAX_TEST_EFFECTS];
static uint8_t g_test_effect_id[MAX_TEST_EFFECTS];
static int g_test_effects;

void port_add_test_effect(unsigned long frame, uint8_t id) {
    if (g_test_effects >= MAX_TEST_EFFECTS) port_fatal("--sound-effect: too many");
    g_test_effect_frame[g_test_effects] = frame;
    g_test_effect_id[g_test_effects] = id;
    ++g_test_effects;
}

static void test_effects_for_frame(unsigned long frame) {
    for (int i = 0; i < g_test_effects; ++i) {
        if (g_test_effect_frame[i] != frame) continue;
        wr8(0x0463, g_test_effect_id[i]);
        wr8(0x0464, 0xFF);
        wr8(0x0465, 0x00);
        wr8(0x0466, 0xFF);
        wr8(0x0467, 0x00);
    }
}

const uint8_t* port_sound_last_message(void) {
    return g_sound_last_msg;
}

/* The DSP-4 byte port: since decomp e152ffc only the VS race's C still calls it (request A). */
void dsp4_port_write16(uint16_t value) {
    (void)value;
    port_fatal("the VS race's DSP-4 commands are not native yet");
}

uint16_t dsp4_port_read16(void) {
    port_fatal("the VS race's DSP-4 commands are not native yet");
    return 0;
}

void port_fatal(const char* what) {
    fprintf(stderr, "FATAL (frame %lu): %s\n", g_frame_count, what);
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
        uint16_t sub = rd16(0x0008);
        race_start_screen_note_nmi(sub);
        nmi_prologue();
        switch (sub) {
            case 0x8934: nmi_tail(); break;
            case 0xFBD1: nmi_target_80FBD1(); break;
            case 0xFC14: nmi_target_80FC14(); break;
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
        /* The race NMI (set by race_load, $82:8428): its prologue, then its two targets
           (measured on the bench, autopilot_60k.txt: $838A on the two frames before the road's
           first frame, $83CE on every race frame after). Nothing is drawn for the race yet. */
        uint16_t sub = rd16(0x0008);
        nmi_race_prologue();
        switch (sub) {
            case 0x838A: nmi_race_tail(); break;
            case 0x83CE: nmi_race_full(); break;
            default: {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown race NMI target 0x%04X", sub);
                port_fatal(msg);
                break;
            }
        }
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown NMI vector target 0x%06X", (unsigned int)target);
        port_fatal(msg);
    }
}

static int SDLCALL task2_entry(void* userdata) {
    (void)userdata;
    SDL_WaitSemaphore(g_sem_task2);
    if (!g_task2_abort) {
        if (setjmp(g_task2_exit_jmp) == 0) {
            g_task2_fn(0x0200);
        }
    }
    g_task2_running = false;
    SDL_SignalSemaphore(g_sem_task1);
    return 0;
}

static void task_cleanup_split(void) {
    if (!g_split_active) {
        return;
    }
    if (g_task2_running) {
        g_task2_abort = true;
        SDL_SignalSemaphore(g_sem_task2);
    }
    SDL_WaitThread(g_task2_thread, NULL);
    g_task2_thread = NULL;
    SDL_DestroySemaphore(g_sem_task1);
    g_sem_task1 = NULL;
    SDL_DestroySemaphore(g_sem_task2);
    g_sem_task2 = NULL;
    g_task2_fn = NULL;
    g_task2_running = false;
    g_task2_abort = false;
    g_split_active = false;
}

void platform_task_split(void (*task)(uint16_t dp)) {
    if (g_split_active) {
        port_fatal("nested platform_task_split is not supported");
    }
    if (task == NULL) {
        port_fatal("platform_task_split called with NULL task");
    }
    g_task1_thread_id = SDL_GetCurrentThreadID();
    g_sem_task1 = SDL_CreateSemaphore(0);
    g_sem_task2 = SDL_CreateSemaphore(0);
    if (g_sem_task1 == NULL || g_sem_task2 == NULL) {
        port_fatal("SDL_CreateSemaphore failed");
    }
    g_task2_fn = task;
    champ_screen_note_split();
    g_task2_abort = false;
    g_task2_running = true;
    g_split_active = true;
    g_task2_thread = SDL_CreateThread(task2_entry, "tg3000-task2", NULL);
    if (g_task2_thread == NULL) {
        port_fatal("SDL_CreateThread failed");
    }
}

void platform_task_join(void) {
    if (!g_split_active) {
        return;
    }
    if (SDL_GetCurrentThreadID() != g_task1_thread_id) {
        port_fatal("platform_task_join called from task 2");
    }
    task_cleanup_split();
}

static void pump_events_and_input(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
            task_cleanup_split();
            longjmp(g_quit_jmp, 1);
        }
        port_input_event(&event);
    }
    /* Test-only scripted press (--press) and movie replay (--movie): OR'd into controller 1. */
    uint16_t test_mask = 0;
    if (g_test_press_frame != 0 && g_frame_count >= g_test_press_frame &&
        g_frame_count < g_test_press_frame + 6) {
        test_mask |= 0x8080u;
    }
    test_mask |= movie_buttons_for_frame(g_frame_count);
    port_input_set_test_mask(test_mask);
    test_effects_for_frame(g_frame_count);

    if (g_wram_out_path != NULL && g_frame_count == g_wram_out_frame) {
        FILE* wf = fopen(g_wram_out_path, "wb");
        if (wf == NULL || fwrite(g_ram, 1, sizeof(g_ram), wf) != sizeof(g_ram)) {
            if (wf != NULL) {
                fclose(wf);
            }
            port_fatal("--wram-out: failed to write game memory file");
        }
        fclose(wf);
        g_wram_out_path = NULL;
    }
}

bool port_split_active(void) {
    return g_split_active;
}

int port_current_task_half(void) {
    if (g_split_active && SDL_GetCurrentThreadID() != g_task1_thread_id) {
        return 1;
    }
    return 0;
}

/* The picture as a 24-bit BMP (tests). */
static void write_picture_bmp(const char* path) {
    FILE* f = fopen(path, "wb");
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
}

static void present_frame(bool before_nmi) {
    /* The pre-race screen alone is drawn before the NMI (race_start_screen_note_nmi); the
       others after it. */
    bool active_racestart = before_nmi || race_start_screen_first_after_nmi();
    bool active_intro = !active_racestart && intro_screen_active();
    bool active_title = !active_racestart && !active_intro && title_screen_active();
    bool active_results = !active_racestart && !active_intro && !active_title
                          && results_screen_active();
    bool active_champ = !active_racestart && !active_intro && !active_title && !active_results
                        && champ_screen_active();
    bool active_shop = !active_racestart && !active_intro && !active_title && !active_results
                       && !active_champ && shop_screen_active();
    bool active_ticker = !active_racestart && !active_intro && !active_title && !active_results
                         && !active_champ && !active_shop && ticker_screen_active();
    int active_menu = (active_intro || active_title || active_results || active_champ
                       || active_shop || active_ticker || active_racestart)
                          ? -1
                          : menu_screen_active();
    const char* drawn_name = "none";
    if (active_intro) {
        intro_screen_draw(g_fb);
        drawn_name = "intro";
    } else if (active_title) {
        title_screen_draw(g_fb);
        drawn_name = "title";
    } else if (active_results) {
        results_screen_draw(g_fb);
        drawn_name = "results";
    } else if (active_champ) {
        champ_screen_draw(g_fb);
        drawn_name = "champ";
    } else if (active_shop) {
        shop_screen_draw(g_fb);
        drawn_name = "shop";
    } else if (active_ticker) {
        ticker_screen_draw(g_fb);
        drawn_name = "ticker";
    } else if (active_racestart) {
        race_start_screen_draw(g_fb);
        drawn_name = "race_start";
    } else if (active_menu >= 0) {
        menu_screen_draw(active_menu, g_fb);
        drawn_name = menu_screen_name(active_menu);
    }
    if (active_intro || active_title || active_results || active_champ || active_shop
        || active_ticker || active_racestart || active_menu >= 0) {
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
        printf("frame %lu picture hash %016llx brightness %u drawn %s\n", g_frame_count,
               (unsigned long long)h, (unsigned)(rd8(0x019Bu) & 0x0Fu),
               drawn_name);

        SDL_Surface* surf = SDL_RenderReadPixels(g_renderer, NULL);
        if (surf == NULL) {
            printf("window check: unavailable (%s)\n", SDL_GetError());
        } else {
            SDL_FRect dst_rect;
            if (!SDL_GetRenderLogicalPresentationRect(g_renderer, &dst_rect)) {
                printf("window check: unavailable (%s)\n", SDL_GetError());
                SDL_DestroySurface(surf);
            } else {
                SDL_Surface* argb = surf;
                if (surf->format != SDL_PIXELFORMAT_ARGB8888) {
                    argb = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_ARGB8888);
                    SDL_DestroySurface(surf);
                }
                if (argb == NULL) {
                    printf("window check: unavailable (%s)\n", SDL_GetError());
                } else {
                    int rx = (int)(dst_rect.x + 0.5f);
                    int ry = (int)(dst_rect.y + 0.5f);
                    int rw = (int)(dst_rect.w + 0.5f);
                    int rh = (int)(dst_rect.h + 0.5f);
                    int mismatched = 0;
                    if (SDL_MUSTLOCK(argb)) {
                        SDL_LockSurface(argb);
                    }
                    const uint8_t* base = (const uint8_t*)argb->pixels;
                    for (int gy = 0; gy < SCREEN_HEIGHT; ++gy) {
                        int y0 = ry + (gy * rh) / SCREEN_HEIGHT;
                        int y1 = ry + ((gy + 1) * rh) / SCREEN_HEIGHT;
                        for (int gx = 0; gx < SCREEN_WIDTH; ++gx) {
                            int x0 = rx + (gx * rw) / SCREEN_WIDTH;
                            int x1 = rx + ((gx + 1) * rw) / SCREEN_WIDTH;
                            uint32_t expected_rgb = g_fb[gy * SCREEN_WIDTH + gx] & 0x00FFFFFFu;
                            bool match = (x1 > x0) && (y1 > y0);
                            for (int wy = y0; wy < y1 && match; ++wy) {
                                if (wy < 0 || wy >= argb->h) {
                                    match = false;
                                    break;
                                }
                                const uint32_t* row =
                                    (const uint32_t*)(base + (size_t)wy * (size_t)argb->pitch);
                                for (int wx = x0; wx < x1; ++wx) {
                                    if (wx < 0 || wx >= argb->w ||
                                        (row[wx] & 0x00FFFFFFu) != expected_rgb) {
                                        match = false;
                                        break;
                                    }
                                }
                            }
                            if (!match) {
                                ++mismatched;
                            }
                        }
                    }
                    if (SDL_MUSTLOCK(argb)) {
                        SDL_UnlockSurface(argb);
                    }
                    SDL_DestroySurface(argb);
                    printf("window check: scale %dx%d at (%d,%d), %d mismatched game pixels\n",
                           rw, rh, rx, ry, mismatched);
                }
            }
        }
        fflush(stdout);

        if (strcmp(g_shot_path, "-") != 0) {
            write_picture_bmp(g_shot_path);
        }
        g_shot_path = NULL;
    }
    /* Test only (--shots): every picture of a range of frames, for comparing with the bench. */
    if (g_shots_dir != NULL && g_frame_count >= g_shots_first && g_frame_count <= g_shots_last
        && (g_frame_count - g_shots_first) % g_shots_every == 0u) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/port_%05lu.bmp", g_shots_dir, g_frame_count);
        write_picture_bmp(path);
    }

    SDL_RenderPresent(g_renderer);
}

static void pace_frame(void) {
    if (!g_fast) {
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
    }
}

/* Race pacing (docs/DECISIONS.md, "Race pacing"): the screen refreshes each step of the
   one-player race loop takes. On the console a step takes as many refreshes as its CPU and DSP-4
   work lasts (2-4, measured); the game counts them ($00BA, kept in $1E9E by race_frame_rest) and
   moves everything by that count. The port has no CPU time, so its loop would take 1 refresh a
   step (the wait sees one NMI, which takes the frame) and never redraw the HUD (drawn only by
   NMIs that do not take a frame). A port setting, not a CPU model: 2 (default) or 3 refreshes a
   step, every refresh a whole NMI as the game has it (pads, sound message, the HUD), presented
   and paced to 60.0988 Hz. Test only: --race-refresh-replay plays the counts the original took,
   recorded on the decomp's bench (like --movie, a recording, not a model). */
static int g_race_refreshes = 2;
static uint8_t* g_rr_replay;
static size_t g_rr_len;
static size_t g_rr_at = 1;  /* the file's step 1 has no wait to go to (race_wait_refreshes) */

bool port_set_race_refreshes(int n) {
    if (n != 2 && n != 3) return false;
    g_race_refreshes = n;
    return true;
}

bool port_set_race_refresh_replay(const char* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) return false;
    char line[128];
    size_t cap = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char* end = NULL;
        unsigned long n = strtoul(line, &end, 10);
        if (end == line || n < 1 || n > 15) {
            fclose(f);
            port_fatal("--race-refresh-replay: a line is not a refresh count (1-15)");
        }
        if (g_rr_len == cap) {
            cap = cap ? cap * 2 : 4096;
            uint8_t* next = realloc(g_rr_replay, cap);
            if (next == NULL) port_fatal("--race-refresh-replay: out of memory");
            g_rr_replay = next;
        }
        g_rr_replay[g_rr_len++] = (uint8_t)n;
    }
    fclose(f);
    if (g_rr_len < 2) port_fatal("--race-refresh-replay: fewer than two refresh counts in the file");
    return true;
}

/* How many refreshes this wait takes, read from the game's state before its NMI runs: N at the
   race loop's wait (race_frame_wait), 1 at any other. The port reaches one wait per step, the
   loop's, with the frame requested ($00B8 bit 0): the first NMI takes it, the others are the
   HUD's (on the console they come during the step's body; the port can only place them at its
   wait). The race's other waits are frame waits (wait_frames: the frame counter must reach an
   exact value, so they get one refresh each): the pause's ($0026 bit 4), race_end's (the loop
   over: race_frame_rest's own end test, $0146 = 0 and no brightness in $014A, still holds; no NMI
   runs between the rest and the next wait in the port) and race_track_start_tail's before step 1
   (the frame not requested). So step 1 takes 1 refresh in the port: the console's step 1 counts
   from that frame wait, which the port cannot stretch. */
static int race_wait_refreshes(void) {
    if (!nmi_enabled || rd8(0x0000) != 0x5C || rd16(0x0001) != 0x8377 || rd8(0x0003) != 0x85
        || rd16(0x0008) != 0x83CE || (rd16(0x00B8) & 0x0001) == 0 || (rd16(0x0026) & 0x0010) != 0
        || (rd16(0x0146) == 0 && (rd16(0x014A) & 0x0F00) == 0)) {
        return 1;
    }
    if (g_rr_replay != NULL && g_rr_at < g_rr_len) {
        int n = g_rr_replay[g_rr_at++];
        if (g_rr_at == g_rr_len) {
            printf("race refresh replay: steps 2-%zu played\n", g_rr_len);
            fflush(stdout);
        }
        return n;
    }
    return g_race_refreshes;
}

static void refresh(void);

void platform_wait_vblank(void) {
    if (g_split_active && SDL_GetCurrentThreadID() != g_task1_thread_id) {
        SDL_SignalSemaphore(g_sem_task1);
        SDL_WaitSemaphore(g_sem_task2);
        if (g_task2_abort) {
            longjmp(g_task2_exit_jmp, 1);
        }
        return;
    }
    int n = g_split_active ? 1 : race_wait_refreshes();
    for (int i = 0; i < n; ++i) {
        refresh();
    }
}

/* One screen refresh: input, the NMI, the picture, a frame of sound, the 60.0988 Hz pacing. */
static void refresh(void) {
    /* a) Pump SDL events first, so the game's frame code (which reads the pads) sees this
       frame's input, not the last one's. */
    pump_events_and_input();

    if (g_split_active) {
        /* Two-task split active: IRQ -> task 2 turn -> draw+present -> NMI -> pacing. */
        champ_screen_note_irq();  /* the bottom half as irq_menu_frame sends it */
        irq_menu_frame();
        irq_menu_frame_tail();
        if (g_task2_running) {
            SDL_SignalSemaphore(g_sem_task2);
            SDL_WaitSemaphore(g_sem_task1);
        }
        present_frame(false);
        ticker_screen_note_nmi();
        nmi_prologue();   /* every NMI starts with it, then the $0008 target (here $B97B) */
        champ_screen_note_nmi();  /* the top half as nmi_menu_frame sends it */
        nmi_menu_frame();
        nmi_menu_frame_tail();
        nmi_tail();
    } else {
        /* b) The pre-race screen: drawn now, before the NMI (its frame code's output belongs to
              the picture the last NMI started; see race_start_screen_note_nmi) */
        bool early = race_start_screen_active();
        if (early) {
            present_frame(true);
        }
        /* c) Run the game's frame code */
        if (nmi_enabled) {
            run_nmi_dispatch();
        }
        /* d) Draw */
        if (!early) {
            present_frame(false);
        }
    }

    /* d) One frame of sound from the mixer, then pace to 60.0988 fps */
    mixer_output_frame();
    pace_frame();

    /* e) Frame counter and limit check */
    g_frame_count++;
    if (g_frame_limit > 0 && g_frame_count >= g_frame_limit) {
        task_cleanup_split();
        longjmp(g_quit_jmp, 1);
    }
}

void port_set_test_options(unsigned long press_frame, bool expect_return) {
    g_test_press_frame = press_frame;
    g_test_expect_return = expect_return;
}

void port_set_start_wram(const char* path) {
    g_start_wram_path = path;
}

void port_set_shot(unsigned long frame, const char* path) {
    g_shot_frame = frame;
    g_shot_path = path;
}

void port_set_shots(unsigned long first, unsigned long last, unsigned long every, const char* dir) {
    g_shots_first = first;
    g_shots_last = last;
    g_shots_every = every ? every : 1u;
    g_shots_dir = dir;
}

void port_set_wram_out(unsigned long frame, const char* path) {
    g_wram_out_frame = frame;
    g_wram_out_path = path;
}

void port_set_fast(bool fast) {
    g_fast = fast;
}

void port_set_movie(const char* path, long offset) {
    static const char button_chars[] = "BYsSUDLRAXlr";
    free(g_movie_events);
    g_movie_events = NULL;
    g_movie_len = 0;
    g_movie_idx = 0;
    g_movie_held = 0;
    g_movie_offset = offset;
    g_movie_offset_from_set = false;

    FILE* f = fopen(path, "r");
    if (f == NULL) {
        port_fatal("--movie: failed to open movie file");
    }
    char line[256];
    size_t cap = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        long step = 0;
        char buttons[64] = ".";
        if (sscanf(line, "%ld %63s", &step, buttons) < 1) {
            continue;
        }
        uint16_t mask = 0;
        for (const char* c = buttons; *c != '\0'; ++c) {
            const char* p = strchr(button_chars, *c);
            if (p != NULL) {
                mask |= (uint16_t)(0x8000u >> (p - button_chars));
            }
        }
        if (g_movie_len == cap) {
            cap = cap != 0 ? cap * 2 : 64;
            MovieEvent* next = (MovieEvent*)realloc(g_movie_events, cap * sizeof(*next));
            if (next == NULL) {
                fclose(f);
                port_fatal("--movie: out of memory");
            }
            g_movie_events = next;
        }
        g_movie_events[g_movie_len].step = step;
        g_movie_events[g_movie_len].mask = mask;
        ++g_movie_len;
    }
    fclose(f);
    port_input_set_pad2_present(true);
}

void port_set_movie_offset_from(unsigned long frame, long offset) {
    g_movie_offset_from_set = true;
    g_movie_offset_from_frame = frame;
    g_movie_offset_from_value = offset;
}

int port_run(SDL_Renderer* r, const char* screen, unsigned long frame_limit) {
    g_renderer = r;
    g_frame_limit = frame_limit;
    g_frame_count = 0;
    g_next_frame_ns = 0;
    g_screen_returned = false;
    g_movie_idx = 0;
    g_movie_held = 0;

    if (g_texture == NULL) {
        g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
        if (g_texture == NULL) {
            port_fatal("SDL_CreateTexture failed");
        }
        SDL_SetTextureScaleMode(g_texture, SDL_SCALEMODE_NEAREST);
    }

    if (setjmp(g_quit_jmp) != 0) {
        free(g_movie_events);
        g_movie_events = NULL;
        g_movie_len = 0;
        port_input_set_pad2_present(false);
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
    if (g_start_wram_path != NULL) {
        /* Test only: start from game memory saved elsewhere (e.g. the decomp's bench just before
           a screen), so the port shows the same game state as the original. */
        FILE* f = fopen(g_start_wram_path, "rb");
        if (f == NULL || fread(g_ram, 1, sizeof(g_ram), f) != sizeof(g_ram)) {
            port_fatal("--wram: cannot read a 131072-byte game memory file");
        }
        fclose(f);
    } else {
        interrupt_vectors_init();
    }
    nmi_enabled = true;

    /* `screen` may list several screens, comma-separated, run one after the other in the same
       game memory (tests: what the port draws when one screen follows another). */
    char names[256];
    snprintf(names, sizeof(names), "%s", screen);
    for (char* name = strtok(names, ","); name != NULL; name = strtok(NULL, ",")) {
        if (strcmp(name, "game") == 0) {
            game_main();  /* the whole game, the original's order (decomp f3f7d5a); never returns */
        } else if (strcmp(name, "boot") == 0) {
            game_boot();  /* the game's own start-up (decomp b284d72) */
        } else if (strcmp(name, "title_card") == 0) {
            title_card_show();
        } else if (strcmp(name, "intro") == 0) {
            intro_screen();
            results_init();
        } else if (strcmp(name, "title") == 0) {
            title_screen_run(0);
        } else if (strcmp(name, "password_show") == 0) {
            password_show();
        } else if (strcmp(name, "vs") == 0) {
            vs_screen();
        } else if (strcmp(name, "options") == 0) {
            options_screens(0);
        } else if (strcmp(name, "results") == 0) {
            results_screen_run();
        } else if (strcmp(name, "ticker") == 0) {
            ticker_screen();
        } else if (strcmp(name, "race") == 0) {
            /* The one-player race from its wrapper (with --wram notes/race/wram_before_race.bin,
               the original's memory at $82:8342, dumped on the decomp's bench by
               tools/make_reference_memory.sh) */
            race_wrapper();
        } else if (strcmp(name, "race_wait") == 0) {
            /* Test only: the race loop's wait alone (race_frame_wait), from the original's memory
               at that wait with the frame requested (with --wram notes/race/wram_race_wait.bin,
               dumped on the decomp's bench by tools/make_reference_memory.sh): the refreshes
               one race step takes in the port, and what its NMIs left (the count $00BA the
               step will keep, the request bits of $00B8: bit 2 cleared by the HUD's NMI). */
            unsigned long from = g_frame_count;
            uint16_t b8 = rd16(0x00B8);
            race_frame_wait();
            printf("race wait: %lu refreshes, $00BA %u, $00B8 %04X -> %04X\n", g_frame_count - from,
                   (unsigned)rd16(0x00BA), (unsigned)b8, (unsigned)rd16(0x00B8));
            fflush(stdout);
        } else if (strcmp(name, "sound_menu") == 0) {
            /* The menus' set as the game loads it after a race: the driver's start-up first
               (the native player reads its balance table), then the set and its song request,
               with the master volume faded up as the game does after loading it. */
            sound_boot(DP);
            sound_load_menu(DP, 1);
            wr16(0x03E0, 0x0001);
            port_sound_print_set_summary();
        } else if (strncmp(name, "sound_set", 9) == 0 && name[9] >= '0' && name[9] <= '7' && name[10] == '\0') {
            /* Test scaffold: set N (the race sets 3-6 cannot be reached in the port yet) loaded by
               the game's own loader after the driver's start-up, its song asked for, the music,
               effect and master volumes full (as the race and VS loaders leave them, and as the
               race sends them). With --sound-replay, the original's recorded messages follow. */
            sound_boot(DP);
            sound_load_set(DP, (uint16_t)(name[9] - '0'));
            wr8(0x045D, 0x7F);
            wr8(0x045E, 0x7F);
            wr8(0x045C, 0x7F);
            wr8(0x045F, 0x01);
            port_sound_print_set_summary();
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "unknown screen: %s", name);
            port_fatal(msg);
        }
        printf("screen %s returned at frame %lu (game frame counter %u)\n", name, g_frame_count,
               (unsigned)rd16(FRAME_COUNTER));
        fflush(stdout);
    }
    g_screen_returned = true;

    for (;;) {
        platform_wait_vblank();
    }
}


/* The decomp reached code it has not ported yet (tg3000_unported, decomp request 2): stop with
   its name instead of a bare crash. */
void platform_unported(const char* what) {
    char msg[160];
    snprintf(msg, sizeof(msg), "the game reached code not converted to C yet: %s", what);
    port_fatal(msg);
}

/* Decomp b284d72 (game_boot): the console's start-up waits. The port has no vertical blank
   to wait for before the first frame and no DSP-4 chip to wait for: return at once. */
void platform_wait_vblank_start(void) {}
void platform_wait_dsp4_start(void) {}
