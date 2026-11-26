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

/* Intro screen ($80:EE56) measured facts:
   - Active when: $013E <= 32, TEXT_DRAWINGS_128 = $8D:8000 (304 4bpp tiles, uploaded to VRAM
     word $5010 -> tiles 1..304, tile 0 blank), OBJ_DRAWINGS_HI = $91:AADF (15 4bpp tiles at
     320..334), and both BG_DRAWINGS and BG_ARRANGEMENT are empty (-1) because the Mode 7
     drawings ($94:8020) and map ($90:C343) go through dma_stream_* (ignored) and $7F:0000 is
     cleared immediately afterwards for the typed text map.
   - Palette: CGRAM 0 comes from palette_get(0) (updated per frame by video_set_color);
     CGRAM 1..255 come from game RAM $81:0500..$81:06FF ($0502..$06FF), except that during
     title_fade_step ($013E == 29) and after ($013E >= 30), palette_fade_60 writes the cycled
     16 colours into $0500..$051E and uploads them to CGRAM 0x60..0x6F.
   - Per-phase fixed layout (ROM band tables $81:D712/$81:D719, $81:D720/$81:D727, $81:D72E):
     - Phases 0..6, lines 0..183: Mode 7 picture (128x128 map of 8x8 8bpp linear tiles =
       1024x1024 px, with the 8x10 block from $90:C343 at map (11, 10) and the rest empty
       tile 0; outside 1024x1024 is transparent) scaled about map (128, 112) by $017A (8.8
       fixed point) at screen centre ($013D, $013B), then 128 stars from OAM $0900/$0B00
       (tiles 320..334, palette 0, priority 0). Phases 1..4 mask Mode 7 via $0D00/$0D40.
     - Phases 7..32, lines 0..183: Mode 1; BG1 map at $7E:2940 (VOFS = 3 -> my = (y + 4) & 255)
       with 4bpp tiles at 0x6000 in the unpacked $7E:2000.. logo buffer; subscreen BG2 map at
       $7F:0000 (VOFS = $0182) with 4bpp tiles at 0x0000.. (from upload_slice 0..15 and
       title_frame_step's 4 slices) added to main screen (CGWSEL=0x02, CGADSUB=0x25). From
       title_frame_tail ($0140 >= 0x015C, $0136 == 0x0800), curved window $1500 masks BG1/OBJ
       on the main screen while subscreen BG2/OBJ remain unmasked.
     - All phases, lines 184..223: Mode 1; stars (priority 0, behind BG2) then BG2 typed text
       from $7F:0000 (BG2VOFS at $0182 -> screen y reads map line (y + (int16_t)rd16(0x0182)
       + 1) & 255), tiles 0..304, palette 2..3, priority 0/1, no flips. */

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

static void decode_hdma_window(uint16_t base, uint8_t left_out[INTRO_M7_END_LINE],
                               uint8_t right_out[INTRO_M7_END_LINE]) {
    uint32_t p = base;
    uint32_t end = base + 0x40u;
    int y = 0;
    uint8_t cur_l = 0xFFu;
    uint8_t cur_r = 0x00u;

    while (y < INTRO_M7_END_LINE && p < end) {
        uint8_t cnt = rd8(p++);
        if (cnt == 0u) {
            break;
        }
        if ((cnt & 0x80u) != 0u) {
            port_fatal("intro: unexpected repeat HDMA window entry");
        }
        if (p + 2u > end) {
            port_fatal("intro: HDMA window table overflow");
        }
        cur_l = rd8(p++);
        cur_r = rd8(p++);
        for (uint8_t k = 0u; k < cnt && y < INTRO_M7_END_LINE; ++k) {
            left_out[y] = cur_l;
            right_out[y] = cur_r;
            ++y;
        }
    }
    while (y < INTRO_M7_END_LINE) {
        left_out[y] = cur_l;
        right_out[y] = cur_r;
        ++y;
    }
}

static void decode_curved_window(uint16_t base, uint8_t left_out[INTRO_M7_END_LINE],
                                 uint8_t right_out[INTRO_M7_END_LINE]) {
    uint32_t p = base;
    uint32_t end = base + 0x176u;
    int y = 0;
    uint8_t cur_l = 0xFFu;
    uint8_t cur_r = 0x00u;

    while (y < INTRO_M7_END_LINE && p < end) {
        uint8_t cnt = rd8(p++);
        if (cnt == 0u) {
            break;
        }
        if ((cnt & 0x80u) == 0u) {
            port_fatal("intro: unexpected non-repeat curved HDMA window entry");
        }
        uint8_t lines = cnt & 0x7Fu;
        for (uint8_t k = 0u; k < lines; ++k) {
            if (p + 2u > end) {
                port_fatal("intro: curved HDMA window table overflow");
            }
            cur_l = rd8(p++);
            cur_r = rd8(p++);
            if (y < INTRO_M7_END_LINE) {
                left_out[y] = cur_l;
                right_out[y] = cur_r;
                ++y;
            }
        }
    }
    while (y < INTRO_M7_END_LINE) {
        left_out[y] = cur_l;
        right_out[y] = cur_r;
        ++y;
    }
}

static uint32_t add_rgba_clamped(uint32_t a, uint32_t b) {
    uint32_t r = ((a >> 16) & 0xFFu) + ((b >> 16) & 0xFFu);
    uint32_t g = ((a >> 8) & 0xFFu) + ((b >> 8) & 0xFFu);
    uint32_t bl = (a & 0xFFu) + (b & 0xFFu);
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (bl > 255u) bl = 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

/* Measured per-tile-range mapping for Mode 1 BG1 (from $7E:2000.. slices).
   Returns false if the tile's piece has not been sent yet (drawn as nothing). */
static bool load_bg1_tile_4bpp(uint16_t tile, uint8_t out[32]) {
    if (tile == 0u) {
        return false;
    }
    uint32_t src = 0u;
    if (tile <= 9u) {
        if (!assets_intro_piece_sent(INTRO_PIECE_SLICE_15)) {
            return false;
        }
        src = 0x7E9040u + (uint32_t)tile * 32u;
    } else if (tile <= 15u) {
        return false; /* no piece covers tiles 10..15 */
    } else if (tile <= 52u) {
        if (!assets_intro_piece_sent(INTRO_PIECE_BG1_A)) {
            return false;
        }
        src = 0x7E2000u + (uint32_t)(tile - 16u) * 32u;
    } else if (tile <= 89u) {
        if (!assets_intro_piece_sent(INTRO_PIECE_BG1_B)) {
            return false;
        }
        src = 0x7E24A0u + (uint32_t)(tile - 53u) * 32u;
    } else {
        char msg[96];
        snprintf(msg, sizeof(msg), "intro: BG1 tile %u outside measured ranges (0..89)",
                 (unsigned)tile);
        port_fatal(msg);
    }
    for (uint32_t i = 0; i < 32u; ++i) {
        out[i] = rd8(src + i);
    }
    return true;
}

/* Measured per-tile-range mapping for Mode 1 BG2 (16 slices at $7E:3040.. and 4 frame slices
   replacing tiles 0..190). Returns false if any piece holding the tile has not been sent yet. */
static bool load_bg2_tile_4bpp(uint16_t tile, uint8_t out[32]) {
    if (tile == 0u) {
        return false;
    }
    if (tile >= 778u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "intro: BG2 tile %u outside measured ranges (0..777)",
                 (unsigned)tile);
        port_fatal(msg);
    }
    static const struct {
        intro_piece_id id;
        uint32_t byte_start;
        uint32_t wram_src;
    } frame_pieces[4] = {
        {INTRO_PIECE_FRAME_3, 0x11B8u, 0x7EA338u},
        {INTRO_PIECE_FRAME_2, 0x0BD0u, 0x7E9D50u},
        {INTRO_PIECE_FRAME_1, 0x05E8u, 0x7E9768u},
        {INTRO_PIECE_FRAME_0, 0x0000u, 0x7E9180u},
    };
    uint32_t base = (uint32_t)tile * 32u;
    for (uint32_t k = 0; k < 32u; ++k) {
        uint32_t b = base + k;
        bool found = false;
        for (unsigned p = 0; p < 4u; ++p) {
            if (b >= frame_pieces[p].byte_start
                && b < frame_pieces[p].byte_start + 0x0614u
                && assets_intro_piece_sent(frame_pieces[p].id)) {
                out[k] = rd8(frame_pieces[p].wram_src + (b - frame_pieces[p].byte_start));
                found = true;
                break;
            }
        }
        if (!found) {
            unsigned slice = (unsigned)(b / 0x0614u);
            if (slice >= 16u
                || !assets_intro_piece_sent((intro_piece_id)(INTRO_PIECE_SLICE_0 + slice))) {
                return false;
            }
            out[k] = rd8(0x7E3040u + b);
        }
    }
    return true;
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

static void draw_intro_title_mode1(uint32_t* fb, const uint32_t pal_rgba[256], uint16_t phase) {
    uint8_t w1_l[INTRO_M7_END_LINE];
    uint8_t w1_r[INTRO_M7_END_LINE];
    uint8_t w2_l[INTRO_M7_END_LINE];
    uint8_t w2_r[INTRO_M7_END_LINE];
    bool use_win1 = false;
    bool use_win2 = false;
    bool win2_bg2_inv = false;

    uint16_t off = rd16(0x0136u);
    if (off == 0x0800u) {
        decode_curved_window(0x1500u, w1_l, w1_r);
        use_win1 = true;
    } else if (off == 0x0000u || off == 0x0100u) {
        decode_hdma_window((uint16_t)(0x0D00u + off), w1_l, w1_r);
        decode_hdma_window((uint16_t)(0x0D40u + off), w2_l, w2_r);
        use_win1 = true;
        if (phase < 28u || (phase == 28u && rd16(0x0140u) < 4u)) {
            use_win2 = true;
            win2_bg2_inv = true;
        }
    } else {
        port_fatal("intro: unexpected HDMA window buffer offset");
    }

    int bg2_shift = (int)(int16_t)rd16(0x0182u) + 1;
    uint32_t backdrop = pal_rgba[0];

    /* Build main-screen top-half buffer (backdrop + BG1 prio 0) and subscreen (BG2 prio 0). */
    bool main_obj[INTRO_M7_END_LINE * SCREEN_WIDTH];
    memset(main_obj, 0, sizeof(main_obj));
    uint32_t sub_rgb[INTRO_M7_END_LINE * SCREEN_WIDTH];
    bool sub_has[INTRO_M7_END_LINE * SCREEN_WIDTH];
    memset(sub_has, 0, sizeof(sub_has));

    for (int prio_pass = 0; prio_pass < 2; ++prio_pass) {
        if (prio_pass == 1) {
            /* Between prio 0 and prio 1: draw OBJ prio 2 on main screen (masked by Win1)
               and on subscreen (unmasked, TSW=0x02 only masks BG2). */
            for (int i = (int)INTRO_SPRITE_COUNT - 1; i >= 0; --i) {
                uint32_t lo_addr = INTRO_OAM_LOW_BASE + (uint32_t)i * 4u;
                uint8_t x_lo = rd8(lo_addr);
                uint8_t y_raw = rd8(lo_addr + 1u);
                if (y_raw >= INTRO_M7_END_LINE) {
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
                    || (prio != 0u && prio != 2u)
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
                    if (sy < 0 || sy >= INTRO_M7_END_LINE) {
                        continue;
                    }
                    for (int px = 0; px < 8; ++px) {
                        int sx = ox + px;
                        if (sx < 0 || sx >= SCREEN_WIDTH) {
                            continue;
                        }
                        uint8_t p = pixels[py * 8 + px];
                        if (p != 0u) {
                            uint32_t col = pal_rgba[128u + p];
                            bool in1 = use_win1 && (sx >= (int)w1_l[sy] && sx <= (int)w1_r[sy]);
                            bool in2 = use_win2 && (sx >= (int)w2_l[sy] && sx <= (int)w2_r[sy]);
                            bool obj_masked = use_win2 ? (in1 || in2) : in1;
                            if (!obj_masked) {
                                fb[sy * SCREEN_WIDTH + sx] = col;
                                main_obj[sy * SCREEN_WIDTH + sx] = true;
                            }
                            sub_rgb[sy * SCREEN_WIDTH + sx] = col;
                            sub_has[sy * SCREEN_WIDTH + sx] = true;
                        }
                    }
                }
            }
        }

        for (int sy = 0; sy < INTRO_M7_END_LINE; ++sy) {
            int bg1_my = (sy + 4) & 255;
            int bg1_row = bg1_my >> 3;
            int bg1_py = bg1_my & 7;

            int bg2_my = (sy + bg2_shift) & 255;
            int bg2_row = bg2_my >> 3;
            int bg2_py = bg2_my & 7;

            uint32_t* main_dst = &fb[sy * SCREEN_WIDTH];
            uint32_t* sub_dst = &sub_rgb[sy * SCREEN_WIDTH];
            bool* sub_flag = &sub_has[sy * SCREEN_WIDTH];

            for (int col = 0; col < 32; ++col) {
                uint16_t e1 = rd16(0x7E2940u + 2u * (uint32_t)(bg1_row * 32 + col));
                int e1_prio = (e1 & 0x2000u) != 0u ? 1 : 0;
                uint16_t t1 = e1 & 0x03FFu;
                uint16_t p1 = (e1 >> 10) & 7u;
                bool hf1 = (e1 & 0x4000u) != 0u;
                bool vf1 = (e1 & 0x8000u) != 0u;

                uint16_t e2 = rd16(0x10000u + 2u * (uint32_t)(bg2_row * 32 + col));
                int e2_prio = (e2 & 0x2000u) != 0u ? 1 : 0;
                uint16_t t2 = e2 & 0x03FFu;
                uint16_t p2 = (e2 >> 10) & 7u;
                bool hf2 = (e2 & 0x4000u) != 0u;
                bool vf2 = (e2 & 0x8000u) != 0u;

                if (e1_prio != prio_pass && e2_prio != prio_pass) {
                    continue;
                }

                uint8_t raw1[32];
                uint8_t raw2[32];
                bool has1 = (e1_prio == prio_pass) && load_bg1_tile_4bpp(t1, raw1);
                bool has2 = (e2_prio == prio_pass) && load_bg2_tile_4bpp(t2, raw2);
                if (!has1 && !has2) {
                    continue;
                }

                for (int px = 0; px < 8; ++px) {
                    int sx = col * 8 + px;
                    bool in1 = use_win1 && (sx >= (int)w1_l[sy] && sx <= (int)w1_r[sy]);
                    bool in2 = use_win2 && (sx >= (int)w2_l[sy] && sx <= (int)w2_r[sy]);

                    if (has1) {
                        bool bg1_masked = use_win2 ? (in1 != in2) : in1;
                        if (!bg1_masked) {
                            uint8_t c = sample_4bpp_tile(raw1, px, bg1_py, hf1, vf1);
                            if (c != 0u) {
                                main_dst[sx] = pal_rgba[p1 * 16u + c];
                                main_obj[sy * SCREEN_WIDTH + sx] = false;
                            }
                        }
                    }

                    if (has2) {
                        bool bg2_masked = win2_bg2_inv && !in2;
                        if (!bg2_masked) {
                            uint8_t c = sample_4bpp_tile(raw2, px, bg2_py, hf2, vf2);
                            if (c != 0u) {
                                sub_dst[sx] = pal_rgba[p2 * 16u + c];
                                sub_flag[sx] = true;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Colour math: CGWSEL=0x02, CGADSUB=0x25 (add subscreen to BG1 and backdrop, not OBJ). */
    for (int i = 0; i < INTRO_M7_END_LINE * SCREEN_WIDTH; ++i) {
        if (!main_obj[i] && sub_has[i]) {
            fb[i] = add_rgba_clamped(fb[i], sub_rgb[i]);
        }
        (void)backdrop;
    }
}

static void draw_intro_m7(uint32_t* fb, const uint32_t pal_rgba[256], uint16_t phase) {
    int scale = (int)(int16_t)rd16(0x017Au);
    /* Phases 0 and 4 call mode7_zoom_view ($013D, $013B); phases 1..3 repurpose $013A/$013C for
       the window tables without updating Mode 7 scroll, which stays at its phase-0 centre. */
    int cx = (phase == 0u || phase == 4u) ? (int)rd8(0x013Du) : 0xC0;
    int cy = (phase == 0u || phase == 4u) ? (int)rd8(0x013Bu) : 0x00;

    uint8_t w1_l[INTRO_M7_END_LINE];
    uint8_t w1_r[INTRO_M7_END_LINE];
    uint8_t w2_l[INTRO_M7_END_LINE];
    uint8_t w2_r[INTRO_M7_END_LINE];
    bool use_win = (phase >= 1u && phase <= 4u);
    if (use_win) {
        uint16_t off = rd16(0x0136u);
        if (off != 0x0000u && off != 0x0100u) {
            port_fatal("intro: unexpected HDMA window buffer offset");
        }
        decode_hdma_window((uint16_t)(0x0D00u + off), w1_l, w1_r);
        decode_hdma_window((uint16_t)(0x0D40u + off), w2_l, w2_r);
    }

    for (int sy = 0; sy < INTRO_M7_END_LINE; ++sy) {
        int my = INTRO_M7_CENTRE_Y + floor_div256(scale * (sy - cy));
        bool my_ok = (my >= 0 && my < 1024);
        int ty = my >> 3;
        int py = my & 7;
        uint32_t* dst = &fb[sy * SCREEN_WIDTH];

        for (int sx = 0; sx < SCREEN_WIDTH; ++sx) {
            if (use_win) {
                bool in1 = (sx >= (int)w1_l[sy] && sx <= (int)w1_r[sy]);
                bool in2 = (sx >= (int)w2_l[sy] && sx <= (int)w2_r[sy]);
                if (in1 != in2) {
                    continue;
                }
            }
            uint8_t tile = 0u;
            int mx = INTRO_M7_CENTRE_X + floor_div256(scale * (sx - cx));
            int px = 0;
            int tpy = 0;
            if (my_ok && mx >= 0 && mx < 1024) {
                int tx = mx >> 3;
                tile = g_m7_map[(size_t)ty * INTRO_M7_MAP_DIM + (size_t)tx];
                px = mx & 7;
                tpy = py;
            }
            uint8_t p = g_m7_tiles[(size_t)tile * 64u + (size_t)tpy * 8u + (size_t)px];
            if (p != 0u) {
                dst[sx] = pal_rgba[p];
            }
        }
    }
}

static void draw_intro_stars(uint32_t* fb, const uint32_t pal_rgba[256], int min_y) {
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
            || (prio != 0u && prio != 2u)
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
            if (sy < min_y || sy >= SCREEN_HEIGHT) {
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
    return rd16(0x013Eu) <= 32u
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

    uint16_t phase = rd16(0x013Eu);

    uint32_t pal_rgba[256];
    pal_rgba[0] = convert_color(palette_get(0u));
    for (unsigned i = 1u; i < 256u; ++i) {
        pal_rgba[i] = convert_color(rd16(0x0500u + i * 2u));
    }
    /* palette_fade_60 writes the cycled 16 colours of palette 6 into $0500..$051E,
       leaving CGRAM 1..15 at their initial $89:84F2 colours while updating CGRAM 0x60..0x6F. */
    if (phase >= 30u || (phase == 29u && rd16(0x0142u) >= 0x12u)) {
        for (unsigned i = 1u; i < 16u; ++i) {
            pal_rgba[i] = convert_color(bus_rd16(0x8984F2u + (i - 1u) * 2u));
        }
        for (unsigned i = 0u; i < 16u; ++i) {
            pal_rgba[0x60u + i] = convert_color(rd16(0x0500u + i * 2u));
        }
    }

    /* 1. Backdrop (lines 0..183 use CGRAM 0; lines 184..223 are covered by BG3 black) */
    uint32_t backdrop = pal_rgba[0];
    for (int i = 0; i < INTRO_M7_END_LINE * SCREEN_WIDTH; ++i) {
        fb[i] = backdrop;
    }
    for (int i = INTRO_M7_END_LINE * SCREEN_WIDTH; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = 0xFF000000u;
    }

    /* 2. Top region (lines 0..183): Mode 7 in phases 0..6, Mode 1 BG1+BG2 in phases 7..32 */
    if (phase <= 6u) {
        draw_intro_m7(fb, pal_rgba, phase);
    } else {
        draw_intro_title_mode1(fb, pal_rgba, phase);
    }

    /* 3. Mode 1 BG2 priority-0 text (lines 184..223) */
    draw_intro_bg2_text(fb, pal_rgba, false);

    /* 4. Stars (both prio 0 and prio 2 are behind BG2 prio 1 and in front of BG2 prio 0) */
    draw_intro_stars(fb, pal_rgba, (phase <= 6u) ? 0 : INTRO_M7_END_LINE);

    /* 5. Mode 1 BG2 priority-1 text (lines 184..223) */
    draw_intro_bg2_text(fb, pal_rgba, true);

    /* 6. Brightness */
    screen_apply_brightness(fb, b);
}
