// SPDX-License-Identifier: GPL-3.0-or-later

#include "screens.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "convert.h"
#include "palette.h"
#include "port.h"
#include "tg3000.h"

/* Intro screen (zoom phase, $013E == 0) measured facts:
   - Active when: $013E == 0, TEXT_DRAWINGS_128 = $8D:8000 (304 4bpp tiles, uploaded to VRAM
     word $5010 -> tiles 1..304, tile 0 blank), OBJ_DRAWINGS_HI = $91:AADF (15 4bpp tiles at
     320..334), and both BG_DRAWINGS and BG_ARRANGEMENT are empty (-1) because the Mode 7
     drawings ($94:8020) and map ($90:C343) go through dma_stream_* (ignored) and $7F:0000 is
     cleared immediately afterwards for the typed text map.
   - Palette: CGRAM 0 comes from palette_get(0) (updated per frame by video_set_color);
     CGRAM 1..255 come from game RAM $81:0500..$81:06FF ($0502..$06FF), which is stable
     throughout the zoom phase.
   - Per-phase fixed layout (ROM band tables $81:D712, $81:D72E):
     - Lines 0..183: Mode 7 picture (128x128 map of 8x8 8bpp linear tiles = 1024x1024 px,
       with the 8x10 block from $90:C343 at map (11, 10) and the rest empty tile 0; outside
       1024x1024 is transparent) scaled about map (128, 112) by $017A (8.8 fixed point) at
       screen centre ($013D, $013B), then 128 stars from OAM $0900/$0B00 (tiles 320..334,
       palette 0, priority 0).
     - Lines 184..223: Mode 1; stars (priority 0, behind BG2) then BG2 typed text from $7F:0000
       (BG2VOFS at $0182 -> screen y reads map line (y + (int16_t)rd16(0x0182) + 1) & 255),
       tiles 0..304, palette 3, priority 1, no flips. */

#define INTRO_TEXT_COUNT 304u
#define INTRO_M7_TILE_COUNT 75u
#define INTRO_M7_MAP_DIM 128u
#define INTRO_M7_BLOCK_X 11u
#define INTRO_M7_BLOCK_Y 10u
#define INTRO_M7_BLOCK_W 10u
#define INTRO_M7_BLOCK_H 8u
#define INTRO_M7_CENTRE_X 128
#define INTRO_M7_CENTRE_Y 112
#define INTRO_M7_END_LINE 184

#define INTRO_STARS_FIRST_TILE 320u
#define INTRO_STARS_COUNT 15u
#define INTRO_OAM_LOW_BASE 0x0900u
#define INTRO_OAM_HIGH_BASE 0x0B00u
#define INTRO_SPRITE_COUNT 128u

static bool g_built = false;
static uint32_t g_text_gen = 0;
static uint32_t g_stars_gen = 0;
static uint32_t g_m7_draw_gen = 0;
static uint32_t g_m7_map_gen = 0;

static uint8_t g_text_tiles[(1u + INTRO_TEXT_COUNT) * 64u];
static uint8_t g_stars_tiles[INTRO_STARS_COUNT * 64u];
static uint8_t g_m7_tiles[INTRO_M7_TILE_COUNT * 64u];
static uint8_t g_m7_map[INTRO_M7_MAP_DIM * INTRO_M7_MAP_DIM];

static void intro_build_once(void) {
    uint32_t text_gen = asset_generation(ASSET_INTRO_TEXT_DRAWINGS);
    uint32_t stars_gen = asset_generation(ASSET_RESULTS_STARS_DRAWINGS);
    uint32_t m7d_gen = asset_generation(ASSET_INTRO_M7_DRAWINGS);
    uint32_t m7m_gen = asset_generation(ASSET_INTRO_M7_MAP);
    if (g_built && g_text_gen == text_gen && g_stars_gen == stars_gen
        && g_m7_draw_gen == m7d_gen && g_m7_map_gen == m7m_gen) {
        return;
    }

    memset(g_text_tiles, 0, 64u);
    size_t text_size = 0;
    const uint8_t* text_raw = asset_bytes(ASSET_INTRO_TEXT_DRAWINGS, &text_size);
    if (text_raw == NULL || text_size != INTRO_TEXT_COUNT * 32u) {
        port_fatal("intro: unexpected INTRO_TEXT_DRAWINGS size");
    }
    convert_tiles_4bpp(text_raw, INTRO_TEXT_COUNT, &g_text_tiles[64u]);

    size_t stars_size = 0;
    const uint8_t* stars_raw = asset_bytes(ASSET_RESULTS_STARS_DRAWINGS, &stars_size);
    if (stars_raw == NULL || stars_size != INTRO_STARS_COUNT * 32u) {
        port_fatal("intro: unexpected RESULTS_STARS_DRAWINGS size");
    }
    convert_tiles_4bpp(stars_raw, INTRO_STARS_COUNT, g_stars_tiles);

    size_t m7d_size = 0;
    const uint8_t* m7d_raw = asset_bytes(ASSET_INTRO_M7_DRAWINGS, &m7d_size);
    if (m7d_raw == NULL || m7d_size != INTRO_M7_TILE_COUNT * 64u) {
        port_fatal("intro: unexpected INTRO_M7_DRAWINGS size");
    }
    memcpy(g_m7_tiles, m7d_raw, m7d_size);

    size_t m7m_size = 0;
    const uint8_t* m7m_raw = asset_bytes(ASSET_INTRO_M7_MAP, &m7m_size);
    if (m7m_raw == NULL || m7m_size != INTRO_M7_BLOCK_W * INTRO_M7_BLOCK_H * 2u) {
        port_fatal("intro: unexpected INTRO_M7_MAP size");
    }
    memset(g_m7_map, 0, sizeof(g_m7_map));
    for (unsigned r = 0; r < INTRO_M7_BLOCK_H; ++r) {
        for (unsigned c = 0; c < INTRO_M7_BLOCK_W; ++c) {
            size_t idx = (size_t)(r * INTRO_M7_BLOCK_W + c) * 2u;
            uint16_t word = (uint16_t)(m7m_raw[idx] | ((uint16_t)m7m_raw[idx + 1u] << 8));
            if (word >= INTRO_M7_TILE_COUNT) {
                port_fatal("intro: Mode 7 tile index out of range");
            }
            g_m7_map[(INTRO_M7_BLOCK_Y + r) * INTRO_M7_MAP_DIM + (INTRO_M7_BLOCK_X + c)] =
                (uint8_t)word;
        }
    }

    g_text_gen = text_gen;
    g_stars_gen = stars_gen;
    g_m7_draw_gen = m7d_gen;
    g_m7_map_gen = m7m_gen;
    g_built = true;
}

static int floor_div256(int v) {
    return (v >= 0) ? (v >> 8) : -(((-v) + 255) >> 8);
}

static void draw_intro_m7(uint32_t* fb, const uint32_t pal_rgba[256]) {
    int scale = (int)(int16_t)rd16(0x017Au);
    int cx = (int)rd8(0x013Du);
    int cy = (int)rd8(0x013Bu);

    for (int sy = 0; sy < INTRO_M7_END_LINE; ++sy) {
        int my = INTRO_M7_CENTRE_Y + floor_div256(scale * (sy - cy));
        if (my < 0 || my >= 1024) {
            continue;
        }
        int ty = my >> 3;
        int py = my & 7;
        uint32_t* dst = &fb[sy * SCREEN_WIDTH];

        for (int sx = 0; sx < SCREEN_WIDTH; ++sx) {
            int mx = INTRO_M7_CENTRE_X + floor_div256(scale * (sx - cx));
            if (mx < 0 || mx >= 1024) {
                continue;
            }
            int tx = mx >> 3;
            uint8_t tile = g_m7_map[(size_t)ty * INTRO_M7_MAP_DIM + (size_t)tx];
            if (tile == 0u) {
                continue;
            }
            uint8_t p = g_m7_tiles[(size_t)tile * 64u + (size_t)py * 8u + (size_t)(mx & 7)];
            if (p != 0u) {
                dst[sx] = pal_rgba[p];
            }
        }
    }
}

static void draw_intro_stars(uint32_t* fb, const uint32_t pal_rgba[256]) {
    for (int i = (int)INTRO_SPRITE_COUNT - 1; i >= 0; --i) {
        uint32_t lo_addr = INTRO_OAM_LOW_BASE + (uint32_t)i * 4u;
        uint8_t x_lo = rd8(lo_addr);
        uint8_t y_raw = rd8(lo_addr + 1u);
        if (y_raw >= SCREEN_HEIGHT) {
            continue;
        }
        uint8_t tile_lo = rd8(lo_addr + 2u);
        uint8_t attr = rd8(lo_addr + 3u);
        uint8_t hi = (uint8_t)((rd8(INTRO_OAM_HIGH_BASE + ((uint32_t)i >> 2))
                                >> (((uint32_t)i & 3u) * 2u)) & 3u);
        bool is_large = (hi & 2u) != 0u;
        uint16_t tile = (uint16_t)(tile_lo | ((uint16_t)(attr & 1u) << 8));
        uint16_t pal = (attr >> 1) & 7u;
        uint16_t prio = (attr >> 4) & 3u;
        bool flips = (attr & 0xC0u) != 0u;
        if (is_large
            || tile < INTRO_STARS_FIRST_TILE
            || tile >= INTRO_STARS_FIRST_TILE + INTRO_STARS_COUNT
            || pal != 0u
            || prio != 0u
            || flips) {
            char msg[96];
            snprintf(msg, sizeof(msg), "intro: unexpected OAM sprite %d (tile %u attr 0x%02X hi %u)",
                     i, (unsigned)tile, (unsigned)attr, (unsigned)hi);
            port_fatal(msg);
        }

        int x9 = (int)(x_lo | ((uint16_t)(hi & 1u) << 8));
        int ox = (x9 >= 256) ? (x9 - 512) : x9;
        int oy = (int)y_raw;
        const uint8_t* pixels = &g_stars_tiles[(size_t)(tile - INTRO_STARS_FIRST_TILE) * 64u];
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
                    fb[sy * SCREEN_WIDTH + sx] = pal_rgba[128u + p];
                }
            }
        }
    }
}

static void draw_intro_bg2_text(uint32_t* fb, const uint32_t pal_rgba[256], bool want_prio) {
    int shift = (int)(int16_t)rd16(0x0182u) + 1;
    for (int y = INTRO_M7_END_LINE; y < SCREEN_HEIGHT; ++y) {
        int my = (y + shift) & 255;
        int row = my >> 3;
        int py = my & 7;
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];

        for (int col = 0; col < 32; ++col) {
            uint16_t entry = rd16((uint32_t)(0x10000u + 2u * (unsigned)(row * 32 + col)));
            if (entry == 0u) {
                continue;
            }
            bool prio = (entry & 0x2000u) != 0;
            if (prio != want_prio) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            bool flips = (entry & 0xC000u) != 0;
            if (tile > INTRO_TEXT_COUNT || (pal != 2u && pal != 3u) || flips) {
                char msg[96];
                snprintf(msg, sizeof(msg), "intro: unexpected text entry 0x%04X", (unsigned)entry);
                port_fatal(msg);
            }
            const uint8_t* pixels = &g_text_tiles[(size_t)tile * 64u];
            for (int px = 0; px < 8; ++px) {
                uint8_t p = pixels[py * 8 + px];
                if (p != 0u) {
                    dst_row[col * 8 + px] = pal_rgba[pal * 16u + p];
                }
            }
        }
    }
}

bool intro_screen_active(void) {
    return rd16(0x013Eu) == 0u
        && asset_current(ROLE_TEXT_DRAWINGS_128) == ASSET_INTRO_TEXT_DRAWINGS
        && asset_current(ROLE_OBJ_DRAWINGS_HI) == ASSET_RESULTS_STARS_DRAWINGS
        && asset_current(ROLE_BG_DRAWINGS) == -1
        && asset_current(ROLE_BG_ARRANGEMENT) == -1;
}

void intro_screen_draw(uint32_t* fb) {
    uint8_t b = screen_decode_brightness("intro", rd8(0x019Bu));
    if (b == 0u) {
        screen_apply_brightness(fb, 0u);
        return;
    }

    intro_build_once();

    uint32_t pal_rgba[256];
    pal_rgba[0] = convert_color(palette_get(0u));
    for (unsigned i = 1u; i < 256u; ++i) {
        pal_rgba[i] = convert_color(rd16(0x0500u + i * 2u));
    }

    /* 1. Backdrop */
    uint32_t backdrop = pal_rgba[0];
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = backdrop;
    }

    /* 2. Mode 7 picture (lines 0..183) */
    draw_intro_m7(fb, pal_rgba);

    /* 3. Mode 1 BG2 priority-0 text (lines 184..223) */
    draw_intro_bg2_text(fb, pal_rgba, false);

    /* 4. Stars (all lines; above Mode 7 and BG2 prio 0, behind BG2 prio 1) */
    draw_intro_stars(fb, pal_rgba);

    /* 5. Mode 1 BG2 priority-1 text (lines 184..223) */
    draw_intro_bg2_text(fb, pal_rgba, true);

    /* 6. Brightness */
    screen_apply_brightness(fb, b);
}
