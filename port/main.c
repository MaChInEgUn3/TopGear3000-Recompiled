// SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "rom.h"
#include "sha1.h"

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

int main(int argc, char* argv[]) {
    bool selftest = false;
    bool check_rom = false;
    const char* rom_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--selftest") == 0) {
            selftest = true;
        } else if (strcmp(argv[i], "--check-rom") == 0) {
            check_rom = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Usage: %s [--selftest] [--check-rom] [path/to/TG3000.sfc]\n",
                    argv[0]);
            return 1;
        } else if (rom_path == NULL) {
            rom_path = argv[i];
        } else {
            fprintf(stderr, "Usage: %s [--selftest] [--check-rom] [path/to/TG3000.sfc]\n",
                    argv[0]);
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
        char msg[512];
        snprintf(msg, sizeof(msg), "Failed to load ROM (%s): %s",
                 used_path, rom_status_text(st));
        fprintf(stderr, "%s\n", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Top Gear 3000", msg, NULL);
        return 1;
    }

    rom_free(rom_data);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Top Gear 3000", 768, 672, SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, NULL);
    if (renderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderVSync(renderer, 1);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
