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

/* Where the title card's layers sit on the screen. The game moves its background picture with
   scroll values: the screen set-up at $8A:C4B0 gives the background a horizontal scroll of -4
   and title_card_show (misc.c) a vertical one of -65; with the game's convention that a
   vertical scroll of -1 shows the picture at the top line (its text layer uses -1 and sits at
   line 0), the picture sits 64 lines down and 4 pixels right. Checked pixel by pixel against
   the original's picture from the decomp's bench. */
#define TITLE_BG_SHIFT_X 4
#define TITLE_BG_SHIFT_Y 64
#define TITLE_TEXT_SHIFT_Y 0
#define TITLE_OBJ_SHIFT_Y 0

#define BG_TILE_COUNT 205u
#define BG_ARR_ENTRIES 864u
#define TEXT_TILE_FIRST 128u
#define TEXT_TILE_COUNT 39u
#define OBJ_TILE_COUNT 28u

static bool g_title_built = false;
static uint8_t g_bg_image[256 * 256];
static uint8_t g_text_tiles[TEXT_TILE_COUNT * 64u];
static uint8_t g_obj_tiles[OBJ_TILE_COUNT * 64u];

static void title_card_build_once(void) {
    if (g_title_built) {
        return;
    }

    size_t bg_draw_size = 0;
    const uint8_t* bg_draw = asset_bytes(ASSET_MENU_BG_DRAWINGS, &bg_draw_size);
    if (bg_draw == NULL || bg_draw_size != BG_TILE_COUNT * 64u) {
        port_fatal("title card: unexpected MENU_BG_DRAWINGS size");
    }

    size_t bg_arr_size = 0;
    const uint8_t* bg_arr = asset_bytes(ASSET_MENU_BG_ARRANGEMENT, &bg_arr_size);
    if (bg_arr == NULL || bg_arr_size != BG_ARR_ENTRIES * 2u) {
        port_fatal("title card: unexpected MENU_BG_ARRANGEMENT size");
    }

    size_t text_size = 0;
    const uint8_t* text_raw = asset_bytes(ASSET_TITLE_TEXT_DRAWINGS, &text_size);
    if (text_raw == NULL || text_size != TEXT_TILE_COUNT * 32u) {
        port_fatal("title card: unexpected TITLE_TEXT_DRAWINGS size");
    }

    size_t obj_size = 0;
    const uint8_t* obj_raw = asset_bytes(ASSET_TITLE_OBJ_DRAWINGS, &obj_size);
    if (obj_raw == NULL || obj_size != OBJ_TILE_COUNT * 32u) {
        port_fatal("title card: unexpected TITLE_OBJ_DRAWINGS size");
    }

    uint8_t bg_tiles[BG_TILE_COUNT * 64u];
    convert_tiles_8bpp(bg_draw, BG_TILE_COUNT, bg_tiles);
    convert_tiles_4bpp(text_raw, TEXT_TILE_COUNT, g_text_tiles);
    convert_tiles_4bpp(obj_raw, OBJ_TILE_COUNT, g_obj_tiles);

    for (unsigned tr = 0; tr < 32u; ++tr) {
        for (unsigned tc = 0; tc < 32u; ++tc) {
            uint16_t tile = 0;
            if (tr < 27u) {
                size_t idx = (size_t)(tr * 32u + tc) * 2u;
                uint16_t entry = (uint16_t)(bg_arr[idx] | ((uint16_t)bg_arr[idx + 1u] << 8));
                tile = entry & 0x03FFu;
                if (tile >= BG_TILE_COUNT || (entry & 0xFC00u) != 0u) {
                    port_fatal("title card: unexpected background entry");
                }
            }
            const uint8_t* src_tile = &bg_tiles[(size_t)tile * 64u];
            for (unsigned py = 0; py < 8u; ++py) {
                uint8_t* dst_row = &g_bg_image[(tr * 8u + py) * 256u + tc * 8u];
                for (unsigned px = 0; px < 8u; ++px) {
                    dst_row[px] = src_tile[py * 8u + px];
                }
            }
        }
    }

    g_title_built = true;
}

static void draw_obj_tile(uint32_t* fb, const uint32_t pal_rgba[256], uint16_t tile,
                          uint16_t pal, int ox, int oy) {
    if (tile >= OBJ_TILE_COUNT) {
        char msg[96];
        snprintf(msg, sizeof(msg), "title card: object tile %u out of range", (unsigned)tile);
        port_fatal(msg);
    }
    const uint8_t* pixels = &g_obj_tiles[(size_t)tile * 64u];
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
            if (p != 0) {
                fb[sy * SCREEN_WIDTH + sx] = pal_rgba[128u + pal * 16u + p];
            }
        }
    }
}

bool title_card_active(void) {
    return asset_current(ROLE_BG_DRAWINGS) == ASSET_MENU_BG_DRAWINGS
        && asset_current(ROLE_BG_ARRANGEMENT) == ASSET_MENU_BG_ARRANGEMENT
        && asset_current(ROLE_TEXT_DRAWINGS) == ASSET_TITLE_TEXT_DRAWINGS
        && asset_current(ROLE_OBJ_DRAWINGS) == ASSET_TITLE_OBJ_DRAWINGS;
}

void title_card_draw(uint32_t* fb) {
    title_card_build_once();

    uint32_t pal_rgba[256];
    for (unsigned i = 0; i < 256u; ++i) {
        pal_rgba[i] = convert_color(palette_get((uint8_t)i));
    }

    /* 1. Backdrop */
    uint32_t backdrop = pal_rgba[0];
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = backdrop;
    }

    /* 2. Background picture */
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        unsigned r = (unsigned)(y - TITLE_BG_SHIFT_Y) & 255u;
        const uint8_t* src_row = &g_bg_image[r * 256u];
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            uint8_t c = src_row[(unsigned)(x - TITLE_BG_SHIFT_X) & 255u];
            if (c != 0) {
                dst_row[x] = pal_rgba[c];
            }
        }
    }

    /* 3. Text map at WRAM $7F:0000 */
    for (int row = 0; row < 28; ++row) {
        for (int col = 0; col < 32; ++col) {
            uint16_t entry = rd16((uint32_t)(0x10000 + 2 * (row * 32 + col)));
            if (entry == 0x003Du) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            bool prio = (entry & 0x2000u) != 0;
            uint16_t flips = entry & 0xC000u;
            if (tile < TEXT_TILE_FIRST || tile >= TEXT_TILE_FIRST + TEXT_TILE_COUNT || !prio || flips != 0u) {
                char msg[96];
                snprintf(msg, sizeof(msg), "title card: unexpected text entry 0x%04X", (unsigned)entry);
                port_fatal(msg);
            }

            const uint8_t* pixels = &g_text_tiles[(size_t)(tile - TEXT_TILE_FIRST) * 64u];
            for (int py = 0; py < 8; ++py) {
                int sy = row * 8 + py + TITLE_TEXT_SHIFT_Y;
                if (sy < 0 || sy >= SCREEN_HEIGHT) {
                    continue;
                }
                for (int px = 0; px < 8; ++px) {
                    int sx = col * 8 + px;
                    uint8_t p = pixels[py * 8 + px];
                    if (p != 0) {
                        fb[sy * SCREEN_WIDTH + sx] = pal_rgba[pal * 16u + p];
                    }
                }
            }
        }
    }

    /* 4. Objects from $0500-$08FF (last to first so lower slot ends on top) */
    for (int x = 254; x >= 0; x -= 2) {
        uint16_t used = rd16((uint32_t)(0x0500 + x));
        if (used == 0u) {
            continue;
        }
        int ox = (int)(rd16((uint32_t)(0x0600 + x)) & 0x01FFu);
        if (ox >= 256) {
            ox -= 512;
        }
        uint16_t yraw = rd16((uint32_t)(0x0700 + x));
        if ((yraw & 0x8000u) != 0 || yraw >= 0x00F0u) {
            continue;
        }
        int oy = (int)yraw + TITLE_OBJ_SHIFT_Y;

        uint16_t w = rd16((uint32_t)(0x0800 + x));
        uint16_t tile = w & 0x01FFu;
        uint16_t pal = (w >> 9) & 7u;
        uint16_t prio = (w >> 12) & 3u;
        uint16_t flips = (w >> 14) & 3u;
        if (prio != 2u || flips != 0u) {
            char msg[96];
            snprintf(msg, sizeof(msg), "title card: unexpected object attr 0x%04X", (unsigned)w);
            port_fatal(msg);
        }

        if ((used & 0x8000u) == 0) {
            draw_obj_tile(fb, pal_rgba, tile, pal, ox, oy);
            draw_obj_tile(fb, pal_rgba, (uint16_t)(tile + 1u), pal, ox + 8, oy);
            draw_obj_tile(fb, pal_rgba, (uint16_t)(tile + 16u), pal, ox, oy + 8);
            draw_obj_tile(fb, pal_rgba, (uint16_t)(tile + 17u), pal, ox + 8, oy + 8);
        } else {
            draw_obj_tile(fb, pal_rgba, tile, pal, ox, oy);
        }
    }

    /* 5. Brightness */
    uint8_t b = rd8(0x019Bu) & 0x0Fu;
    if (b == 0u) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            fb[i] = 0xFF000000u;
        }
    } else if (b < 15u) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            uint32_t px = fb[i];
            /* linear in the game's brightness 0-15: b * 17 runs 0..255 */
            uint32_t r = (((px >> 16) & 0xFFu) * b * 17u) >> 8;
            uint32_t g = (((px >> 8) & 0xFFu) * b * 17u) >> 8;
            uint32_t bl = ((px & 0xFFu) * b * 17u) >> 8;
            fb[i] = 0xFF000000u | (r << 16) | (g << 8) | bl;
        }
    }
}
