// SPDX-License-Identifier: GPL-3.0-or-later

#include "screens.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "assets.h"
#include "convert.h"
#include "palette.h"
#include "port.h"
#include "tg3000.h"

/* Results screen measured facts (docs/DECISIONS.md, "The results screen drawn natively"):
   - Active when: TEXT_128 = $87:F861 (39 tiles at 128..166), TEXT_256 = $88:8601 (186 tiles at
     256..441, with 256..261 overwritten by the 97-word cursor run $8A:C8E4), OBJ_DRAWINGS_HI =
     $91:AADF (15 tiles at 320..334), and both BG_DRAWINGS and BG_ARRANGEMENT are empty (-1).
   - Backdrop: black (CGRAM 0). BG1: blank (all zeros, not drawn).
   - BG2 text map at $7F:0000: BG2VOFS = 8 -> screen line y reads map line (y + 9) & 255;
     blank $003D; palettes 0, 1, 6, 7; prio 0 and 1 both used; v-flip allowed, h-flip disallowed.
   - Objects from OAM $0900..$0AFF + $0B00..$0B1F (128 sprites, reverse order):
     8x8 (size bit 0), tiles 320..334, palette 0 (CGRAM 128..143), priority 0, flips=0, y >= 224
     off screen. Mode 1 priority order: backdrop -> BG2 prio 0 -> OBJ prio 0 -> BG2 prio 1.
   - Per-line effect ($0D00): 54 lines ($36) at $0D00/$0D01, then 21 bands of 8 lines ($08 at
     $0D02+2k, byte at $0D03+2k), terminator $00 at $0D2C. Each byte is $00-$1F (no change) or
     $E0|n; subtracts n from each 5-bit channel of BG2 text pixels on those lines (clamped at 0). */
#define RESULTS_TEXT_128_FIRST_TILE 128u
#define RESULTS_TEXT_128_COUNT 39u
#define RESULTS_TEXT_256_FIRST_TILE 256u
#define RESULTS_TEXT_256_COUNT 186u
#define RESULTS_CURSOR_FIRST_TILE 256u
#define RESULTS_CURSOR_COUNT 6u
#define RESULTS_CURSOR_WORDS 97u
#define RESULTS_STARS_FIRST_TILE 320u
#define RESULTS_STARS_COUNT 15u

#define RESULTS_TEXT_BLANK_ENTRY 0x003Du
#define RESULTS_TEXT_PAL_MASK ((uint8_t)((1u << 0) | (1u << 1) | (1u << 6) | (1u << 7)))
#define RESULTS_BG2_MAP_SHIFT 9

#define RESULTS_OAM_LOW_BASE 0x0900u
#define RESULTS_OAM_HIGH_BASE 0x0B00u
#define RESULTS_SPRITE_COUNT 128u
#define RESULTS_OBJ_PAL 0u
#define RESULTS_OBJ_PRIO 0u

#define RESULTS_COLDATA_FIRST_LINES 0x36u
#define RESULTS_COLDATA_BAND_LINES 0x08u
#define RESULTS_COLDATA_BANDS 21u
#define RESULTS_COLDATA_TOTAL_BANDS (1u + RESULTS_COLDATA_BANDS)

static bool g_built = false;
static uint32_t g_text128_gen = 0;
static uint32_t g_text256_gen = 0;
static uint32_t g_cursor_gen = 0;
static uint32_t g_stars_gen = 0;
static uint8_t g_text128_tiles[RESULTS_TEXT_128_COUNT * 64u];
static uint8_t g_text256_tiles[RESULTS_TEXT_256_COUNT * 64u];
static uint8_t g_cursor_tiles[RESULTS_CURSOR_COUNT * 64u];
static uint8_t g_stars_tiles[RESULTS_STARS_COUNT * 64u];

static void results_build_once(void) {
    uint32_t t128_gen = asset_generation(ASSET_TITLE_TEXT_DRAWINGS);
    uint32_t t256_gen = asset_generation(ASSET_PASSWORD_TEXT_DRAWINGS);
    uint32_t cur_gen = asset_generation(ASSET_MENU_CURSOR_DRAWINGS);
    uint32_t star_gen = asset_generation(ASSET_RESULTS_STARS_DRAWINGS);
    if (g_built && g_text128_gen == t128_gen && g_text256_gen == t256_gen
        && g_cursor_gen == cur_gen && g_stars_gen == star_gen) {
        return;
    }

    size_t t128_size = 0;
    const uint8_t* t128_raw = asset_bytes(ASSET_TITLE_TEXT_DRAWINGS, &t128_size);
    if (t128_raw == NULL || t128_size != RESULTS_TEXT_128_COUNT * 32u) {
        port_fatal("results: unexpected TITLE_TEXT_DRAWINGS size");
    }
    convert_tiles_4bpp(t128_raw, RESULTS_TEXT_128_COUNT, g_text128_tiles);

    size_t t256_size = 0;
    const uint8_t* t256_raw = asset_bytes(ASSET_PASSWORD_TEXT_DRAWINGS, &t256_size);
    if (t256_raw == NULL || t256_size != RESULTS_TEXT_256_COUNT * 32u) {
        port_fatal("results: unexpected PASSWORD_TEXT_DRAWINGS size");
    }
    convert_tiles_4bpp(t256_raw, RESULTS_TEXT_256_COUNT, g_text256_tiles);

    size_t cur_size = 0;
    const uint8_t* cur_raw = asset_bytes(ASSET_MENU_CURSOR_DRAWINGS, &cur_size);
    if (cur_raw == NULL || cur_size != RESULTS_CURSOR_WORDS * 2u) {
        port_fatal("results: unexpected MENU_CURSOR_DRAWINGS size");
    }
    convert_tiles_4bpp(cur_raw, RESULTS_CURSOR_COUNT, g_cursor_tiles);

    size_t star_size = 0;
    const uint8_t* star_raw = asset_bytes(ASSET_RESULTS_STARS_DRAWINGS, &star_size);
    if (star_raw == NULL || star_size != RESULTS_STARS_COUNT * 32u) {
        port_fatal("results: unexpected RESULTS_STARS_DRAWINGS size");
    }
    convert_tiles_4bpp(star_raw, RESULTS_STARS_COUNT, g_stars_tiles);

    g_text128_gen = t128_gen;
    g_text256_gen = t256_gen;
    g_cursor_gen = cur_gen;
    g_stars_gen = star_gen;
    g_built = true;
}

static uint8_t parse_coldata_byte(uint8_t v) {
    uint8_t planes = v & 0xE0u;
    if (planes == 0x00u) {
        return 0u;
    }
    if (planes == 0xE0u) {
        return v & 0x1Fu;
    }
    port_fatal("results: unexpected coldata plane bits at $0D00");
    return 0u;
}

static const uint8_t* results_text_tile_pixels(uint16_t tile) {
    if (tile >= RESULTS_TEXT_128_FIRST_TILE
        && tile < RESULTS_TEXT_128_FIRST_TILE + RESULTS_TEXT_128_COUNT) {
        return &g_text128_tiles[(size_t)(tile - RESULTS_TEXT_128_FIRST_TILE) * 64u];
    }
    if (tile >= RESULTS_CURSOR_FIRST_TILE
        && tile < RESULTS_CURSOR_FIRST_TILE + RESULTS_CURSOR_COUNT) {
        return &g_cursor_tiles[(size_t)(tile - RESULTS_CURSOR_FIRST_TILE) * 64u];
    }
    if (tile >= RESULTS_TEXT_256_FIRST_TILE + RESULTS_CURSOR_COUNT
        && tile < RESULTS_TEXT_256_FIRST_TILE + RESULTS_TEXT_256_COUNT) {
        return &g_text256_tiles[(size_t)(tile - RESULTS_TEXT_256_FIRST_TILE) * 64u];
    }
    return NULL;
}

static void validate_results_text_map(void) {
    for (unsigned i = 0; i < 1024u; ++i) {
        uint16_t entry = rd16((uint32_t)(0x10000u + i * 2u));
        if (entry == RESULTS_TEXT_BLANK_ENTRY) {
            continue;
        }
        uint16_t tile = entry & 0x03FFu;
        uint16_t pal = (entry >> 10) & 7u;
        bool hflip = (entry & 0x4000u) != 0;
        if (results_text_tile_pixels(tile) == NULL
            || ((RESULTS_TEXT_PAL_MASK & (1u << pal)) == 0u)
            || hflip) {
            char msg[96];
            snprintf(msg, sizeof(msg), "results: unexpected text entry 0x%04X", (unsigned)entry);
            port_fatal(msg);
        }
    }
}

static void draw_results_bg2(uint32_t* fb, bool want_prio, const uint8_t sub_band[RESULTS_COLDATA_TOTAL_BANDS]) {
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        unsigned band = (y < (int)RESULTS_COLDATA_FIRST_LINES)
                            ? 0u
                            : (1u + (unsigned)(y - (int)RESULTS_COLDATA_FIRST_LINES) / RESULTS_COLDATA_BAND_LINES);
        if (band >= RESULTS_COLDATA_TOTAL_BANDS) {
            band = RESULTS_COLDATA_TOTAL_BANDS - 1u;
        }
        uint8_t sub_n = sub_band[band];
        int my = (y + RESULTS_BG2_MAP_SHIFT) & 255;
        int row = my / 8;
        int py = my & 7;
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];

        for (int col = 0; col < 32; ++col) {
            uint16_t entry = rd16((uint32_t)(0x10000 + 2 * (row * 32 + col)));
            if (entry == RESULTS_TEXT_BLANK_ENTRY) {
                continue;
            }
            bool prio = (entry & 0x2000u) != 0;
            if (prio != want_prio) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            bool vflip = (entry & 0x8000u) != 0;
            const uint8_t* pixels = results_text_tile_pixels(tile);
            int ty = vflip ? (7 - py) : py;

            for (int px = 0; px < 8; ++px) {
                uint8_t p = pixels[ty * 8 + px];
                if (p != 0u) {
                    uint16_t bgr = palette_get((uint8_t)(pal * 16u + p));
                    if (sub_n != 0u) {
                        uint32_t r5 = bgr & 0x1Fu;
                        uint32_t g5 = (bgr >> 5) & 0x1Fu;
                        uint32_t b5 = (bgr >> 10) & 0x1Fu;
                        r5 = (r5 > sub_n) ? (r5 - sub_n) : 0u;
                        g5 = (g5 > sub_n) ? (g5 - sub_n) : 0u;
                        b5 = (b5 > sub_n) ? (b5 - sub_n) : 0u;
                        bgr = (uint16_t)(r5 | (g5 << 5) | (b5 << 10));
                    }
                    dst_row[col * 8 + px] = convert_color(bgr);
                }
            }
        }
    }
}

static void draw_results_objects(uint32_t* fb, const uint32_t pal_rgba[256]) {
    for (int i = (int)RESULTS_SPRITE_COUNT - 1; i >= 0; --i) {
        uint32_t lo_addr = RESULTS_OAM_LOW_BASE + (uint32_t)i * 4u;
        uint8_t x_lo = rd8(lo_addr);
        uint8_t y_raw = rd8(lo_addr + 1u);
        if (y_raw >= SCREEN_HEIGHT) {
            continue;
        }
        uint8_t tile_lo = rd8(lo_addr + 2u);
        uint8_t attr = rd8(lo_addr + 3u);
        uint8_t hi = (uint8_t)((rd8(RESULTS_OAM_HIGH_BASE + ((uint32_t)i >> 2))
                                >> (((uint32_t)i & 3u) * 2u)) & 3u);
        bool is_large = (hi & 2u) != 0u;
        uint16_t tile = (uint16_t)(tile_lo | ((uint16_t)(attr & 1u) << 8));
        uint16_t pal = (attr >> 1) & 7u;
        uint16_t prio = (attr >> 4) & 3u;
        bool flips = (attr & 0xC0u) != 0u;
        if (is_large
            || tile < RESULTS_STARS_FIRST_TILE
            || tile >= RESULTS_STARS_FIRST_TILE + RESULTS_STARS_COUNT
            || pal != RESULTS_OBJ_PAL
            || prio != RESULTS_OBJ_PRIO
            || flips) {
            char msg[96];
            snprintf(msg, sizeof(msg), "results: unexpected OAM sprite %d (tile %u attr 0x%02X hi %u)",
                     i, (unsigned)tile, (unsigned)attr, (unsigned)hi);
            port_fatal(msg);
        }

        int x9 = (int)(x_lo | ((uint16_t)(hi & 1u) << 8));
        int ox = (x9 >= 256) ? (x9 - 512) : x9;
        int oy = (int)y_raw;
        const uint8_t* pixels = &g_stars_tiles[(size_t)(tile - RESULTS_STARS_FIRST_TILE) * 64u];
        for (int py = 0; py < 8; ++py) {
            int sy = oy + py;
            if (sy < 0 || sy >= SCREEN_HEIGHT) {
                continue;
            }
            for (int px = 0; px < 8; ++px) {
                int sx = ox + px;
                if (sx < 0 || sx >= SCREEN_WIDTH) {
                    continue;
                }
                uint8_t p = pixels[py * 8 + px];
                if (p != 0u) {
                    fb[sy * SCREEN_WIDTH + sx] = pal_rgba[128u + RESULTS_OBJ_PAL * 16u + p];
                }
            }
        }
    }
}

bool results_screen_active(void) {
    return asset_current(ROLE_TEXT_DRAWINGS_128) == ASSET_TITLE_TEXT_DRAWINGS
        && asset_current(ROLE_TEXT_DRAWINGS_256) == ASSET_PASSWORD_TEXT_DRAWINGS
        && asset_current(ROLE_OBJ_DRAWINGS) == ASSET_MENU_CURSOR_DRAWINGS
        && asset_current(ROLE_OBJ_DRAWINGS_HI) == ASSET_RESULTS_STARS_DRAWINGS
        && asset_current(ROLE_BG_DRAWINGS) == -1
        && asset_current(ROLE_BG_ARRANGEMENT) == -1;
}

void results_screen_draw(uint32_t* fb) {
    uint8_t b_raw = rd8(0x019Bu);
    if ((b_raw & 0x70u) != 0u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "results: unexpected brightness byte 0x%02X", (unsigned)b_raw);
        port_fatal(msg);
    }
    uint8_t b = ((b_raw & 0x80u) != 0u) ? 0u : (b_raw & 0x0Fu);
    if (b == 0u) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            fb[i] = 0xFF000000u;
        }
        return;
    }

    results_build_once();

    uint8_t sub_band[RESULTS_COLDATA_TOTAL_BANDS];
    if (rd8(0x0D00u) != RESULTS_COLDATA_FIRST_LINES || rd8(0x0D2Cu) != 0x00u) {
        port_fatal("results: unexpected coldata table header/terminator at $0D00");
    }
    sub_band[0] = parse_coldata_byte(rd8(0x0D01u));
    for (unsigned k = 0; k < RESULTS_COLDATA_BANDS; ++k) {
        if (rd8(0x0D02u + k * 2u) != RESULTS_COLDATA_BAND_LINES) {
            port_fatal("results: unexpected coldata band count at $0D00");
        }
        sub_band[k + 1u] = parse_coldata_byte(rd8(0x0D03u + k * 2u));
    }

    validate_results_text_map();

    uint32_t pal_rgba[256];
    for (unsigned i = 0; i < 256u; ++i) {
        pal_rgba[i] = convert_color(palette_get((uint8_t)i));
    }

    /* 1. Backdrop */
    uint32_t backdrop = pal_rgba[0];
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = backdrop;
    }

    /* 2. BG2 priority-0 tiles (with $0D00 subtraction) */
    draw_results_bg2(fb, false, sub_band);

    /* 3. Objects (priority 0: above BG2 prio 0, below BG2 prio 1; unchanged by $0D00) */
    draw_results_objects(fb, pal_rgba);

    /* 4. BG2 priority-1 tiles (with $0D00 subtraction) */
    draw_results_bg2(fb, true, sub_band);

    /* 5. Brightness */
    if (b < 15u) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            uint32_t px = fb[i];
            uint32_t r = (((px >> 16) & 0xFFu) * b * 17u) >> 8;
            uint32_t g = (((px >> 8) & 0xFFu) * b * 17u) >> 8;
            uint32_t bl = ((px & 0xFFu) * b * 17u) >> 8;
            fb[i] = 0xFF000000u | (r << 16) | (g << 8) | bl;
        }
    }
}
