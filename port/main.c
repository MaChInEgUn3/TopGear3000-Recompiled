// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "mixer.h"
#include "port.h"
#include "rom.h"
#include "sequencer.h"
#include "sha1.h"
#include "tg3000.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void format_hex(const uint8_t digest[20], char out[41]) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 20; ++i) {
        out[i * 2] = hex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    out[40] = '\0';
}

static bool check_sha1_vector(const char* label, const uint8_t* data, size_t len,
                              const char* expected_hex) {
    uint8_t digest[20];
    char actual_hex[41];
    sha1(data, len, digest);
    format_hex(digest, actual_hex);

    if (strcmp(actual_hex, expected_hex) == 0) {
        printf("PASS %s: %s\n", label, actual_hex);
        return true;
    }
    printf("FAIL %s: expected %s, got %s\n", label, expected_hex, actual_hex);
    return false;
}

static int run_selftest(void) {
    bool ok = true;

    const char* v1 = "abc";
    ok &= check_sha1_vector("abc", (const uint8_t*)v1, strlen(v1),
                            "a9993e364706816aba3e25717850c26c9cd0d89d");

    ok &= check_sha1_vector("empty", (const uint8_t*)"", 0,
                            "da39a3ee5e6b4b0d3255bfef95601890afd80709");

    const char* v3 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    ok &= check_sha1_vector("448-bit", (const uint8_t*)v3, strlen(v3),
                            "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    const size_t million = 1000000U;
    uint8_t* big = (uint8_t*)malloc(million);
    if (big == NULL) {
        fprintf(stderr, "FAIL million-a: out of memory\n");
        return 1;
    }
    memset(big, 'a', million);
    ok &= check_sha1_vector("million-a", big, million,
                            "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    free(big);

    return ok ? 0 : 1;
}

static rom_status load_default_or_path(const char* explicit_path,
                                        uint8_t** out_data,
                                        size_t* out_size,
                                        const char** out_used_path) {
    if (explicit_path != NULL) {
        *out_used_path = explicit_path;
        return rom_load(explicit_path, out_data, out_size);
    }

    *out_used_path = "input/TG3000.sfc";
    rom_status st = rom_load(*out_used_path, out_data, out_size);
    if (st == ROM_NOT_FOUND) {
        *out_used_path = "TG3000.sfc";
        st = rom_load(*out_used_path, out_data, out_size);
    }
    return st;
}

static void print_usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [--selftest] [--check-rom] [--screen NAME] [--frames N] [--press F] "
            "[--movie FILE] [--movie-offset N] [--movie-offset-from FRAME N] [--expect-return] "
            "[--fast] [--shot F PATH] "
            "[--wram FILE] [--wram-out F PATH] [--sound-events FILE] [--sound-wav FILE] "
            "[--sound-effect FRAME ID] [--sound-replay FILE] [--race-refreshes 2|3] "
            "[--race-refresh-replay FILE] [--dsp4-tape FILE] [--dsp4-tape-nmis] [path/to/TG3000.sfc]\n",
            prog);
}

int main(int argc, char* argv[]) {
    bool selftest = false;
    bool check_rom = false;
    bool frames_given = false;
    const char* wram_path = NULL;
    bool expect_return = false;
    bool fast = false;
    unsigned long press_frame = 0;
    unsigned long frame_limit = 0;
    unsigned long shot_frame = 0;
    const char* shot_path = NULL;
    const char* movie_path = NULL;
    long movie_offset = 0;
    bool movie_offset_from_set = false;
    unsigned long movie_offset_from_frame = 0;
    long movie_offset_from_value = 0;
    unsigned long wram_out_frame = 0;
    const char* wram_out_path = NULL;
    const char* screen = "game";
    const char* rom_path = NULL;
    const char* sound_events_path = NULL;
    const char* sound_wav_path = NULL;
    const char* sound_replay_path = NULL;
    const char* race_refresh_replay_path = NULL;
    const char* dsp4_tape_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--selftest") == 0) {
            selftest = true;
        } else if (strcmp(argv[i], "--check-rom") == 0) {
            check_rom = true;
        } else if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            screen = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames_given = true;
            frame_limit = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--press") == 0 && i + 1 < argc) {
            press_frame = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--movie") == 0 && i + 1 < argc) {
            movie_path = argv[++i];
        } else if (strcmp(argv[i], "--movie-offset") == 0 && i + 1 < argc) {
            movie_offset = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--movie-offset-from") == 0 && i + 2 < argc) {
            movie_offset_from_set = true;
            movie_offset_from_frame = strtoul(argv[++i], NULL, 10);
            movie_offset_from_value = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--wram") == 0 && i + 1 < argc) {
            wram_path = argv[++i];
            port_set_start_wram(wram_path);
        } else if (strcmp(argv[i], "--wram-out") == 0 && i + 2 < argc) {
            wram_out_frame = strtoul(argv[++i], NULL, 10);
            wram_out_path = argv[++i];
        } else if (strcmp(argv[i], "--shot") == 0 && i + 2 < argc) {
            shot_frame = strtoul(argv[++i], NULL, 10);
            shot_path = argv[++i];
        } else if (strcmp(argv[i], "--sound-events") == 0 && i + 1 < argc) {
            sound_events_path = argv[++i];
        } else if (strcmp(argv[i], "--sound-wav") == 0 && i + 1 < argc) {
            sound_wav_path = argv[++i];
        } else if (strcmp(argv[i], "--sound-replay") == 0 && i + 1 < argc) {
            sound_replay_path = argv[++i];
        } else if (strcmp(argv[i], "--sound-effect") == 0 && i + 2 < argc) {
            unsigned long effect_frame = strtoul(argv[++i], NULL, 10);
            port_add_test_effect(effect_frame, (uint8_t)strtoul(argv[++i], NULL, 0));
        } else if (strcmp(argv[i], "--race-refreshes") == 0 && i + 1 < argc) {
            /* Screen refreshes per race step (docs/DECISIONS.md, "Race pacing"): 2 or 3. */
            const char* v = argv[++i];
            if (!((strcmp(v, "2") == 0 && port_set_race_refreshes(2)) ||
                  (strcmp(v, "3") == 0 && port_set_race_refreshes(3)))) {
                fprintf(stderr, "--race-refreshes: %s is not allowed (2: the game without the "
                                "console's slowdown, 3: the console's heavy-scene rate)\n", v);
                return 1;
            }
        } else if (strcmp(argv[i], "--race-refresh-replay") == 0 && i + 1 < argc) {
            race_refresh_replay_path = argv[++i];
        } else if (strcmp(argv[i], "--dsp4-tape") == 0 && i + 1 < argc) {
            /* TEST ONLY: the race's DSP-4 port calls from a bench recording (test_dsp4_tape.c) */
            dsp4_tape_path = argv[++i];
        } else if (strcmp(argv[i], "--dsp4-tape-nmis") == 0) {
            /* TEST ONLY: with --dsp4-tape, its recorded race NMIs where the bench ran them */
            port_set_dsp4_tape_nmis(true);
        } else if (strcmp(argv[i], "--expect-return") == 0) {
            expect_return = true;
        } else if (strcmp(argv[i], "--fast") == 0) {
            fast = true;
        } else if (argv[i][0] == '-') {
            print_usage(argv[0]);
            return 1;
        } else if (rom_path == NULL) {
            rom_path = argv[i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (selftest) {
        return run_selftest();
    }

    uint8_t* rom_data = NULL;
    size_t rom_size = 0;
    const char* used_path = NULL;
    rom_status st = load_default_or_path(rom_path, &rom_data, &rom_size, &used_path);

    if (check_rom) {
        if (st == ROM_OK) {
            printf("OK: %s (%zu bytes, SHA-1 verified)\n", used_path, rom_size);
            rom_free(rom_data);
            return 0;
        }
        fprintf(stderr, "ERROR (%s): %s\n", used_path, rom_status_text(st));
        return 1;
    }

    if (st != ROM_OK) {
        if (frames_given && st == ROM_NOT_FOUND) {
            fprintf(stderr, "SKIP: ROM not found (%s)\n", used_path);
            return 77;
        }
        char msg[512];
        snprintf(msg, sizeof(msg), "Failed to load ROM (%s): %s",
                 used_path, rom_status_text(st));
        fprintf(stderr, "%s\n", msg);
        if (getenv("TG3000_HEADLESS") == NULL) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear 3000", msg, NULL);
        }
        return 1;
    }

    g_rom = rom_data;

    /* Tests that start from saved game memory (--wram) are skipped where that local file is
       absent (it is game data and never in the repository). */
    if (wram_path != NULL && frames_given) {
        FILE* wf = fopen(wram_path, "rb");
        if (wf == NULL) {
            fprintf(stderr, "SKIP: game memory file not found (%s)\n", wram_path);
            rom_free(rom_data);
            return 77;
        }
        fclose(wf);
    }
    /* The same for a replay of the original's sound messages (recorded on the bench, local). */
    if (sound_replay_path != NULL && !port_set_sound_replay(sound_replay_path)) {
        fprintf(stderr, "SKIP: recorded sound messages not found (%s)\n", sound_replay_path);
        rom_free(rom_data);
        return 77;
    }

    /* The same for the original's recorded race refresh counts (bench, local). */
    if (race_refresh_replay_path != NULL && !port_set_race_refresh_replay(race_refresh_replay_path)) {
        fprintf(stderr, "SKIP: recorded race refresh counts not found (%s)\n", race_refresh_replay_path);
        rom_free(rom_data);
        return 77;
    }

    /* The same for the race's recorded DSP-4 port calls (bench, local; tests only). */
    if (dsp4_tape_path != NULL && !port_set_dsp4_tape(dsp4_tape_path)) {
        fprintf(stderr, "SKIP: recorded DSP-4 tape not found (%s)\n", dsp4_tape_path);
        rom_free(rom_data);
        return 77;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        rom_free(rom_data);
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Top Gear 3000", 768, 672, SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        rom_free(rom_data);
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        rom_free(rom_data);
        return 1;
    }

    SDL_SetRenderLogicalPresentation(renderer, 256, 224, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    /* Sound: the device only outside headless runs (tests); the test outputs on request. */
    if (getenv("TG3000_HEADLESS") == NULL) {
        mixer_open_device();
    }
    if (sound_wav_path != NULL) {
        mixer_open_wav(sound_wav_path);
    }
    if (sound_events_path != NULL) {
        sequencer_open_events(sound_events_path);
    }

    port_set_test_options(press_frame, expect_return);
    port_set_fast(fast);
    port_set_shot(shot_frame, shot_path);
    if (movie_path != NULL) {
        port_set_movie(movie_path, movie_offset);
        if (movie_offset_from_set) {
            port_set_movie_offset_from(movie_offset_from_frame, movie_offset_from_value);
        }
    }
    if (wram_out_path != NULL) {
        port_set_wram_out(wram_out_frame, wram_out_path);
    }
    int rc = port_run(renderer, screen, frame_limit);
    port_dsp4_tape_summary();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    rom_free(rom_data);
    return rc;
}
