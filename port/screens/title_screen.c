// SPDX-License-Identifier: GPL-3.0-or-later

#include "screens.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "assets.h"
#include "convert.h"
#include "port.h"
#include "tg3000.h"

/* Title screen ($80:FBA5 / $8A:800B / $8A:80C8) measured facts:
   - Active when: $0008 == 0xFBD1 and the four OBJ blocks from $9A:EB44 are loaded
     (TITLE_OBJ_0..3).
   - Fixed region layout (ROM HDMA tables $8A:88DE, $8A:88E7, $8A:88F0, $8A:88F9):
     - Lines 0..167: Mode 7 (BGMODE=0x07, TM=0x11)
     - Lines 168..223: Mode 2 (BGMODE=0x02, TM=0x13); BG1/BG2 tilemap at VRAM $6000 is empty
       (never written after the 64 KB clear) and BG3 offset data are zero.
   - Sky gradient (HDMA channel 6 at $7E:2C00 -> $7E:2800 / $7E:2A00, mode 0x43 to $2121):
     lines 0..126 read $7E:2800 + 4*y, lines 127..223 read $7E:2A00 + 4*(y-127); each entry
     sets CGRAM 0 (backdrop) to its BGR555 colour.
   - Mode 7 picture (lines 0..167): 128x128 map (6x15 block from $7E:80BA at rows 6..11,
     cols 8..22; tile_anim_step updates rows 9..11, cols 22..26 from $7E:8000..$7E:809B);
     99 8bpp linear tiles (72 from $7E:8633, 27 from $7E:9833); matrix DP+VIEW_MATRIX
     ($0114..$011B), centre (128, 64), scroll tables $0D00 (M7VOFS) and $0D20 (M7HOFS)
     written by mode7_view; wraparound (M7SEL=0).
   - Palettes: CGRAM 0x01..0x0F from $7E:8114; OBJ palettes 0..2 (CGRAM 0x80..0xAF) from
     $7E:81E2, $7E:8202, $7E:8132.
   - Sprites: OAM $0900/$0B00 (128 sprites, reverse order), OBJSEL=0x01 (8x8 / 16x16),
     tiles in ranges 0..145, 160..221, 256..339, 352..415. */

#define TITLE_M7_END_LINE 168
#define TITLE_M7_MAP_DIM 128u
#define TITLE_M7_TILES_A 72u
#define TITLE_M7_TILES_B 27u
#define TITLE_M7_TILES_TOTAL (TITLE_M7_TILES_A + TITLE_M7_TILES_B)
#define TITLE_M7_CENTRE_X 128
#define TITLE_M7_CENTRE_Y 64

#define TITLE_OBJ0_FIRST 0u
#define TITLE_OBJ0_COUNT 146u
#define TITLE_OBJ1_FIRST 160u
#define TITLE_OBJ1_COUNT 62u
#define TITLE_OBJ2_FIRST 256u
#define TITLE_OBJ2_COUNT 84u
#define TITLE_OBJ3_FIRST 352u
#define TITLE_OBJ3_COUNT 64u

#define TITLE_OAM_LOW_BASE 0x0900u
#define TITLE_OAM_HIGH_BASE 0x0B00u
#define TITLE_SPRITE_COUNT 128u

static bool g_built = false;
static uint32_t g_gens[7];
static uint8_t g_m7_tiles[TITLE_M7_TILES_TOTAL * 64u];
static uint8_t g_m7_base_map[TITLE_M7_MAP_DIM * TITLE_M7_MAP_DIM];
static uint8_t g_obj0_tiles[TITLE_OBJ0_COUNT * 64u];
static uint8_t g_obj1_tiles[TITLE_OBJ1_COUNT * 64u];
static uint8_t g_obj2_tiles[TITLE_OBJ2_COUNT * 64u];
static uint8_t g_obj3_tiles[TITLE_OBJ3_COUNT * 64u];

static void title_build_once(void) {
    static const int ids[7] = {
        ASSET_TITLE_M7_TILES_A, ASSET_TITLE_M7_TILES_B, ASSET_TITLE_M7_MAP,
        ASSET_TITLE_OBJ_0, ASSET_TITLE_OBJ_1, ASSET_TITLE_OBJ_2, ASSET_TITLE_OBJ_3,
    };
    bool same = g_built;
    for (int i = 0; i < 7; ++i) {
        uint32_t g = asset_generation(ids[i]);
        if (g != g_gens[i]) {
            same = false;
        }
    }
    if (same) {
        return;
    }

    size_t sz = 0;
    const uint8_t* m7a = asset_bytes(ASSET_TITLE_M7_TILES_A, &sz);
    if (m7a == NULL || sz != TITLE_M7_TILES_A * 64u) {
        port_fatal("title: unexpected TITLE_M7_TILES_A size");
    }
    memcpy(g_m7_tiles, m7a, sz);

    const uint8_t* m7b = asset_bytes(ASSET_TITLE_M7_TILES_B, &sz);
    if (m7b == NULL || sz != TITLE_M7_TILES_B * 64u) {
        port_fatal("title: unexpected TITLE_M7_TILES_B size");
    }
    memcpy(&g_m7_tiles[TITLE_M7_TILES_A * 64u], m7b, sz);

    const uint8_t* m7m = asset_bytes(ASSET_TITLE_M7_MAP, &sz);
    if (m7m == NULL || sz != 90u) {
        port_fatal("title: unexpected TITLE_M7_MAP size");
    }
    memset(g_m7_base_map, 0, sizeof(g_m7_base_map));
    for (unsigned r = 0; r < 6u; ++r) {
        for (unsigned c = 0; c < 15u; ++c) {
            uint8_t tile = m7m[r * 15u + c];
            if (tile >= TITLE_M7_TILES_TOTAL) {
                port_fatal("title: Mode 7 base tile index out of range");
            }
            g_m7_base_map[(6u + r) * TITLE_M7_MAP_DIM + (8u + c)] = tile;
        }
    }

    const uint8_t* o0 = asset_bytes(ASSET_TITLE_OBJ_0, &sz);
    if (o0 == NULL || sz != TITLE_OBJ0_COUNT * 32u) {
        port_fatal("title: unexpected TITLE_OBJ_0 size");
    }
    convert_tiles_4bpp(o0, TITLE_OBJ0_COUNT, g_obj0_tiles);

    const uint8_t* o1 = asset_bytes(ASSET_TITLE_OBJ_1, &sz);
    if (o1 == NULL || sz != TITLE_OBJ1_COUNT * 32u) {
        port_fatal("title: unexpected TITLE_OBJ_1 size");
    }
    convert_tiles_4bpp(o1, TITLE_OBJ1_COUNT, g_obj1_tiles);

    const uint8_t* o2 = asset_bytes(ASSET_TITLE_OBJ_2, &sz);
    if (o2 == NULL || sz != TITLE_OBJ2_COUNT * 32u) {
        port_fatal("title: unexpected TITLE_OBJ_2 size");
    }
    convert_tiles_4bpp(o2, TITLE_OBJ2_COUNT, g_obj2_tiles);

    const uint8_t* o3 = asset_bytes(ASSET_TITLE_OBJ_3, &sz);
    if (o3 == NULL || sz != TITLE_OBJ3_COUNT * 32u) {
        port_fatal("title: unexpected TITLE_OBJ_3 size");
    }
    convert_tiles_4bpp(o3, TITLE_OBJ3_COUNT, g_obj3_tiles);

    for (int i = 0; i < 7; ++i) {
        g_gens[i] = asset_generation(ids[i]);
    }
    g_built = true;
}

static int floor_div256(int v) {
    return (v >= 0) ? (v >> 8) : -(((-v) + 255) >> 8);
}

static int sign_ext_13(uint16_t v) {
    uint16_t masked = v & 0x1FFFu;
    return (masked & 0x1000u) ? (int)(masked | 0xFFFFE000u) : (int)masked;
}

static uint8_t sample_4bpp_tile(const uint8_t* tile_raw, int px, int py, bool hflip, bool vflip) {
    int x = hflip ? (7 - px) : px;
    int y = vflip ? (7 - py) : py;
    unsigned shift = (unsigned)(7 - x);
    uint8_t p0 = tile_raw[2 * y];
    uint8_t p1 = tile_raw[2 * y + 1];
    uint8_t p2 = tile_raw[16 + 2 * y];
    uint8_t p3 = tile_raw[16 + 2 * y + 1];
    return (uint8_t)(((p0 >> shift) & 1u)
                   | (((p1 >> shift) & 1u) << 1)
                   | (((p2 >> shift) & 1u) << 2)
                   | (((p3 >> shift) & 1u) << 3));
}

static uint16_t read_mode7_scroll_table(uint16_t base, const char* label, uint16_t* mode2_out) {
    if (rd8(base) != 0x01u || rd8(base + 3u) != 0x7Fu || rd8(base + 6u) != 0x28u
        || rd8(base + 9u) != 0x01u || rd8(base + 12u) != 0x00u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "title: unexpected %s HDMA table layout at $%04X",
                 label, (unsigned)base);
        port_fatal(msg);
    }
    uint16_t v0 = rd16(base + 1u);
    uint16_t v1 = rd16(base + 4u);
    uint16_t v2 = rd16(base + 7u);
    if (v0 != v1 || v1 != v2) {
        char msg[96];
        snprintf(msg, sizeof(msg), "title: non-uniform %s scroll across Mode 7 lines", label);
        port_fatal(msg);
    }
    *mode2_out = rd16(base + 10u);
    return v0;
}

static const uint8_t* obj_tile_pixels(uint16_t tile) {
    if (tile < TITLE_OBJ0_FIRST + TITLE_OBJ0_COUNT) {
        return &g_obj0_tiles[(size_t)(tile - TITLE_OBJ0_FIRST) * 64u];
    }
    if (tile >= TITLE_OBJ1_FIRST && tile < TITLE_OBJ1_FIRST + TITLE_OBJ1_COUNT) {
        return &g_obj1_tiles[(size_t)(tile - TITLE_OBJ1_FIRST) * 64u];
    }
    if (tile >= TITLE_OBJ2_FIRST && tile < TITLE_OBJ2_FIRST + TITLE_OBJ2_COUNT) {
        return &g_obj2_tiles[(size_t)(tile - TITLE_OBJ2_FIRST) * 64u];
    }
    if (tile >= TITLE_OBJ3_FIRST && tile < TITLE_OBJ3_FIRST + TITLE_OBJ3_COUNT) {
        return &g_obj3_tiles[(size_t)(tile - TITLE_OBJ3_FIRST) * 64u];
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "title: OBJ tile %u outside measured ranges", (unsigned)tile);
    port_fatal(msg);
    return NULL;
}

bool title_screen_active(void) {
    return rd16(0x0008u) == 0xFBD1u && assets_title_objs_loaded();
}

void title_screen_draw(uint32_t* fb) {
    uint8_t b = screen_decode_brightness("title", rd8(0x019Bu));
    if (b == 0u) {
        screen_apply_brightness(fb, 0u);
        return;
    }

    title_build_once();

    /* 1. Palettes from game RAM ($7E:8000 block and $7F:6440): Mode 7 1..15, BG7 0x70..0x7F, OBJ 0..2 */
    uint32_t pal_rgba[256];
    memset(pal_rgba, 0, sizeof(pal_rgba));
    for (unsigned i = 1u; i < 16u; ++i) {
        pal_rgba[i] = convert_color(rd16(0x7E8114u + (i - 1u) * 2u));
    }
    for (unsigned i = 0u; i < 16u; ++i) {
        pal_rgba[0x70u + i] = convert_color(rd16(0x7F6440u + i * 2u));
        pal_rgba[0x80u + i] = convert_color(rd16(0x7E81E2u + i * 2u));
        pal_rgba[0x90u + i] = convert_color(rd16(0x7E8202u + i * 2u));
        pal_rgba[0xA0u + i] = convert_color(rd16(0x7E8132u + i * 2u));
    }

    /* 2. Sky gradient backdrop (HDMA channel 6: $7E:2C00 -> $7E:2800 / $7E:2A00) */
    if (rd8(0x7E2C00u) != 0xFFu || rd16(0x7E2C01u) != 0x2800u
        || rd8(0x7E2C03u) != 0xFFu || rd16(0x7E2C04u) != 0x2A00u
        || rd8(0x7E2C06u) != 0x00u) {
        port_fatal("title: unexpected sky gradient HDMA table at $7E:2C00");
    }
    for (int sy = 0; sy < SCREEN_HEIGHT; ++sy) {
        uint32_t addr = (sy < 127)
            ? (0x7E2800u + (uint32_t)sy * 4u)
            : (0x7E2A00u + (uint32_t)(sy - 127) * 4u);
        if (rd16(addr) != 0x0000u) {
            port_fatal("title: unexpected CGADD in sky gradient table");
        }
        uint32_t col = convert_color(rd16(addr + 2u));
        uint32_t* dst = &fb[sy * SCREEN_WIDTH];
        for (int sx = 0; sx < SCREEN_WIDTH; ++sx) {
            dst[sx] = col;
        }
    }

    /* 3. Mode 7 (lines 0..167) with tile_anim_step patch from $7E:8000..$7E:809B */
    uint8_t m7_map[TITLE_M7_MAP_DIM * TITLE_M7_MAP_DIM];
    memcpy(m7_map, g_m7_base_map, sizeof(m7_map));
    uint16_t anim_mode = rd16(0x0142u);
    if (anim_mode != 0u) {
        uint16_t anim_off = 0xFFFFu;
        if (anim_mode == 1u) {
            uint16_t f = rd16(0x0140u) & 7u;
            if (f == 1u || f == 2u) {
                anim_off = 0x005Au;
            } else if (f == 3u || f == 4u) {
                anim_off = 0x0078u;
            } else {
                anim_off = 0x003Cu;
            }
        } else {
            uint16_t f = rd16(0x0140u) & 0x000Fu;
            anim_off = (f & 2u) ? 0x0000u : 0x001Eu;
        }
        for (unsigned r = 0; r < 3u; ++r) {
            for (unsigned c = 0; c < 5u; ++c) {
                uint8_t tile = (uint8_t)(rd16(0x7E8000u + anim_off + 2u * (5u * r + c)) & 0xFFu);
                if (tile >= TITLE_M7_TILES_TOTAL) {
                    port_fatal("title: Mode 7 anim tile index out of range");
                }
                m7_map[(9u + r) * TITLE_M7_MAP_DIM + (22u + c)] = tile;
            }
        }
    }

    uint16_t bg1_vofs_raw = 0;
    uint16_t bg1_hofs_raw = 0;
    int m7_vofs = sign_ext_13(read_mode7_scroll_table(0x0D00u, "M7VOFS", &bg1_vofs_raw));
    int m7_hofs = sign_ext_13(read_mode7_scroll_table(0x0D20u, "M7HOFS", &bg1_hofs_raw));
    int m7a = (int)(int16_t)rd16(0x0114u);
    int m7b = (int)(int16_t)rd16(0x0116u);
    int m7c = (int)(int16_t)rd16(0x0118u);
    int m7d = (int)(int16_t)rd16(0x011Au);

    for (int sy = 0; sy < TITLE_M7_END_LINE; ++sy) {
        int dy = (sy + 1) + m7_vofs - TITLE_M7_CENTRE_Y;
        uint32_t* dst = &fb[sy * SCREEN_WIDTH];
        for (int sx = 0; sx < SCREEN_WIDTH; ++sx) {
            int dx = sx + m7_hofs - TITLE_M7_CENTRE_X;
            int mx = TITLE_M7_CENTRE_X + floor_div256(m7a * dx + m7b * dy);
            int my = TITLE_M7_CENTRE_Y + floor_div256(m7c * dx + m7d * dy);
            int tx = (mx & 1023) >> 3;
            int ty = (my & 1023) >> 3;
            int px = mx & 7;
            int py = my & 7;
            uint8_t tile = m7_map[(size_t)ty * TITLE_M7_MAP_DIM + (size_t)tx];
            uint8_t p = g_m7_tiles[(size_t)tile * 64u + (size_t)py * 8u + (size_t)px];
            if (p != 0u) {
                if (p >= 16u) {
                    port_fatal("title: Mode 7 colour index out of measured range (1..15)");
                }
                dst[sx] = pal_rgba[p];
            }
        }
    }

    /* 4. Mode 2 (lines 168..223): BG1 horizon from $7F:6460/$7F:6660 and $7F:6860 (tiles 766..954);
          BG2 is empty and Mode 2 per-tile offset is unused. */
    int bg1_vshift = (int)(int16_t)bg1_vofs_raw + 1;
    int bg1_h = (int)(bg1_hofs_raw & 0x03FFu);
    for (int sy = TITLE_M7_END_LINE; sy < SCREEN_HEIGHT; ++sy) {
        int my = (sy + bg1_vshift) & 255;
        int row = my >> 3;
        if (row < 20 || row > 27) {
            continue;
        }
        unsigned sub_row = (unsigned)(row - 20);
        int py = my & 7;
        uint32_t* dst = &fb[sy * SCREEN_WIDTH];
        for (int sx = 0; sx < SCREEN_WIDTH; ++sx) {
            int mx = (sx + bg1_h) & 511;
            uint32_t map_base = (mx >= 256) ? 0x7F6660u : 0x7F6460u;
            unsigned col = (unsigned)((mx & 255) >> 3);
            int px = mx & 7;
            uint16_t entry = rd16(map_base + 2u * (sub_row * 32u + col));
            if (entry == 0u) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            if (tile < 766u || tile >= 766u + 189u || pal != 7u) {
                char msg[96];
                snprintf(msg, sizeof(msg), "title: unexpected Mode 2 BG1 entry 0x%04X",
                         (unsigned)entry);
                port_fatal(msg);
            }
            bool hf = (entry & 0x4000u) != 0u;
            bool vf = (entry & 0x8000u) != 0u;
            uint8_t tile_raw[32];
            uint32_t taddr = 0x7F6860u + (uint32_t)(tile - 766u) * 32u;
            for (uint32_t k = 0; k < 32u; ++k) {
                tile_raw[k] = rd8(taddr + k);
            }
            uint8_t p = sample_4bpp_tile(tile_raw, px, py, hf, vf);
            if (p != 0u) {
                dst[sx] = pal_rgba[0x70u + p];
            }
        }
    }

    /* 5. Sprites from OAM $0900/$0B00 (8x8 or 16x16, reverse order) */
    for (int i = (int)TITLE_SPRITE_COUNT - 1; i >= 0; --i) {
        uint32_t lo_addr = TITLE_OAM_LOW_BASE + (uint32_t)i * 4u;
        uint8_t x_lo = rd8(lo_addr);
        uint8_t y_raw = rd8(lo_addr + 1u);
        if (y_raw >= SCREEN_HEIGHT) {
            continue;
        }
        uint8_t tile_lo = rd8(lo_addr + 2u);
        uint8_t attr = rd8(lo_addr + 3u);
        uint8_t hi = (uint8_t)((rd8(TITLE_OAM_HIGH_BASE + ((uint32_t)i >> 2))
                                >> (((uint32_t)i & 3u) * 2u)) & 3u);
        bool is_large = (hi & 2u) != 0u;
        uint16_t tile = (uint16_t)(tile_lo | ((uint16_t)(attr & 1u) << 8));
        uint16_t pal = (attr >> 1) & 7u;
        if (pal > 2u) {
            char msg[96];
            snprintf(msg, sizeof(msg), "title: unexpected OBJ palette %u on sprite %d",
                     (unsigned)pal, i);
            port_fatal(msg);
        }
        bool hflip = (attr & 0x40u) != 0u;
        bool vflip = (attr & 0x80u) != 0u;
        int x9 = (int)(x_lo | ((uint16_t)(hi & 1u) << 8));
        int ox = (x9 >= 256) ? (x9 - 512) : x9;
        int oy = (int)y_raw;
        int sub_dim = is_large ? 2 : 1;

        for (int ty = 0; ty < sub_dim; ++ty) {
            int src_ty = vflip ? (sub_dim - 1 - ty) : ty;
            for (int tx = 0; tx < sub_dim; ++tx) {
                int src_tx = hflip ? (sub_dim - 1 - tx) : tx;
                uint16_t sub_tile = (uint16_t)(tile + (uint16_t)src_ty * 16u + (uint16_t)src_tx);
                const uint8_t* pixels = obj_tile_pixels(sub_tile);
                for (int py = 0; py < 8; ++py) {
                    int sy = oy + ty * 8 + py;
                    if (sy < 0 || sy >= SCREEN_HEIGHT) {
                        continue;
                    }
                    int src_py = vflip ? (7 - py) : py;
                    for (int px = 0; px < 8; ++px) {
                        int sx = ox + tx * 8 + px;
                        if (sx < 0 || sx >= SCREEN_WIDTH) {
                            continue;
                        }
                        int src_px = hflip ? (7 - px) : px;
                        uint8_t p = pixels[src_py * 8 + src_px];
                        if (p != 0u) {
                            fb[sy * SCREEN_WIDTH + sx] = pal_rgba[128u + pal * 16u + p];
                        }
                    }
                }
            }
        }
    }

    /* 6. Brightness */
    screen_apply_brightness(fb, b);
}
