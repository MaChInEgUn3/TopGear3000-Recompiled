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

/* The shop ($80:8BC5, shop_screen), measured facts (notes/probe/SHOP.md):
   - Active when: $0008 == $B97B, the two-task split is on, the shop's four streams were
     unpacked ($8D:8000, $88:8000, $87:8190, $8D:895F, in that order), the top half's BG2 map
     ($87:8000) was sent, and the top half's BG1 map, sprite table and colours were sent.
   - The top half (task 1, lines 0..107) is the shop; the bottom half (task 2) takes the early
     leave in one-player games and stays dark ($029B = 0): lines 108..223 are black. A lit
     bottom half was never measured and stops.
   - The top half's shadow block ($019C..$01C1) is one fixed block while lit (below): Mode 1,
     BG1 + BG2 + objects, BG1 scrolled (4, 5), BG2 (0, -1), no colour maths, and two fixed
     windows on the main screen: BG1 is hidden at x 0..15 (window 2) and 77..85 (window 1),
     objects at x 0..15 (window 2).
   - Drawings: BG tile 0 blank (the game clears its 16 words), 1..304 $8D:8000, 305..439
     $88:8000, 442..457 the panel's static tiles (captured from $7F:0700, 512 bytes); objects
     0..79 $87:8190, 80..163 $8D:895F, 256.. the item pictures (slices of the list blocks
     $8F:8000, $99:8000, $9D:8000, $9C:8040, $9E:8000, $9D:8F55; measured table in assets.c).
   - Maps: BG2 the $87:8000 block (14 rows); BG1 the panel map captured from $7F:0000 (15 rows).
   - Colours, the sprite table: as the frame code last sent them ($81:0500, $81:0900/$0D00).
   - Layers, back to front: backdrop, objects 0, objects 1, BG2 low, BG1 low, objects 2,
     BG2 high, BG1 high, objects 3. No per-line table (no HDMA channel on in the shop). */

#define SHOP_TOP_END_LINE 108

#define SHOP_BG_A_FIRST 1u
#define SHOP_BG_A_COUNT 304u
#define SHOP_BG_B_FIRST 305u
#define SHOP_BG_B_COUNT 135u
#define SHOP_BG_NOISE_FIRST 442u
#define SHOP_BG_NOISE_COUNT 16u

#define SHOP_OBJ_FONT_FIRST 0u
#define SHOP_OBJ_FONT_COUNT 80u
#define SHOP_OBJ_ICONS_FIRST 80u
#define SHOP_OBJ_ICONS_COUNT 84u

#define SHOP_BG1_ROWS 15
#define SHOP_BG2_ROWS 14
#define SHOP_SPRITES 128

/* The top half's shadow block $019C..$01C1 while lit (explore_shop.txt, every lit frame). */
static const uint8_t k_top_block[0x26] = {
    0x04, 0x00, 0x05, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x01,
    0x08, 0x20, 0x40, 0x80, 0x09, 0x4D, 0x55, 0x00, 0x0F, 0x00, 0x00, 0x34, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x13, 0x10, 0x11, 0x10, 0x02, 0x00,
};
/* What that block sets, as this screen draws it. */
#define SHOP_BG1_HOFS 4
#define SHOP_BG1_VOFS 5
#define SHOP_BG2_VOFS (-1)
#define SHOP_WIN2_LEFT 0x00
#define SHOP_WIN2_RIGHT 0x0F
#define SHOP_WIN1_LEFT 0x4D
#define SHOP_WIN1_RIGHT 0x55

static bool g_built = false;
static uint32_t g_gens[5];
static uint8_t g_bg_a[SHOP_BG_A_COUNT * 64u];
static uint8_t g_bg_b[SHOP_BG_B_COUNT * 64u];
static uint8_t g_obj_font[SHOP_OBJ_FONT_COUNT * 64u];
static uint8_t g_obj_icons[SHOP_OBJ_ICONS_COUNT * 64u];
static uint8_t g_blank_tile[64];
static uint16_t g_bg2_map[32 * SHOP_BG2_ROWS];

#define SHOP_MAX_SLICE_ROWS 48
#define SHOP_MAX_SLICE_TILES 52u
static uint8_t g_slice_px[SHOP_MAX_SLICE_ROWS][SHOP_MAX_SLICE_TILES * 64u];
static bool g_slice_built[SHOP_MAX_SLICE_ROWS];

static uint32_t g_noise_gen = 0;
static bool g_noise_built = false;
static uint8_t g_noise[SHOP_BG_NOISE_COUNT * 64u];

static void shop_fatal(const char* what, unsigned v) {
    char msg[160];
    snprintf(msg, sizeof(msg), "shop: %s (0x%X)", what, v);
    port_fatal(msg);
}

static const uint8_t* need_asset(int id, size_t size, const char* name) {
    size_t sz = 0;
    const uint8_t* p = asset_bytes(id, &sz);
    if (p == NULL || sz != size) {
        shop_fatal(name, (unsigned)sz);
    }
    return p;
}

static void shop_build(void) {
    static const int ids[5] = {ASSET_SHOP_BG_TILES_A, ASSET_SHOP_BG_TILES_B, ASSET_CHAMP_OBJ_FONT,
                               ASSET_SHOP_OBJ_ICONS, ASSET_SHOP_BG2_MAP};
    bool same = g_built;
    for (int i = 0; i < 5; ++i) {
        if (asset_generation(ids[i]) != g_gens[i]) {
            same = false;
        }
    }
    if (!same) {
        convert_tiles_4bpp(need_asset(ASSET_SHOP_BG_TILES_A, SHOP_BG_A_COUNT * 32u,
                                      "unexpected size of $8D:8000"),
                           SHOP_BG_A_COUNT, g_bg_a);
        convert_tiles_4bpp(need_asset(ASSET_SHOP_BG_TILES_B, SHOP_BG_B_COUNT * 32u,
                                      "unexpected size of $88:8000"),
                           SHOP_BG_B_COUNT, g_bg_b);
        convert_tiles_4bpp(need_asset(ASSET_CHAMP_OBJ_FONT, SHOP_OBJ_FONT_COUNT * 32u,
                                      "unexpected size of $87:8190"),
                           SHOP_OBJ_FONT_COUNT, g_obj_font);
        convert_tiles_4bpp(need_asset(ASSET_SHOP_OBJ_ICONS, SHOP_OBJ_ICONS_COUNT * 32u,
                                      "unexpected size of $8D:895F"),
                           SHOP_OBJ_ICONS_COUNT, g_obj_icons);
        const uint8_t* m = need_asset(ASSET_SHOP_BG2_MAP, sizeof(g_bg2_map),
                                      "unexpected size of $87:8000");
        for (size_t i = 0; i < 32u * SHOP_BG2_ROWS; ++i) {
            g_bg2_map[i] = (uint16_t)(m[i * 2u] | (m[i * 2u + 1u] << 8));
        }
        memset(g_slice_built, 0, sizeof(g_slice_built));
        for (int i = 0; i < 5; ++i) {
            g_gens[i] = asset_generation(ids[i]);
        }
        g_built = true;
    }

    const shop_half_pieces* hp = assets_shop_half(0);
    if (hp->has_noise_tiles && (!g_noise_built || g_noise_gen != hp->noise_gen)) {
        convert_tiles_4bpp(hp->noise_tiles, SHOP_BG_NOISE_COUNT, g_noise);
        g_noise_gen = hp->noise_gen;
        g_noise_built = true;
    }
}

/* One item picture's drawings, converted from its list block (never from the sent bytes). */
static const uint8_t* slice_pixels(int row) {
    const shop_picture_slice* sl = assets_shop_picture(row);
    if (sl == NULL || row >= SHOP_MAX_SLICE_ROWS || sl->size / 32u > SHOP_MAX_SLICE_TILES) {
        shop_fatal("item picture row out of range", (unsigned)row);
    }
    if (!g_slice_built[row]) {
        size_t sz = 0;
        const uint8_t* b = asset_bytes(sl->block, &sz);
        if (b == NULL || (size_t)sl->offset + sl->size > sz) {
            shop_fatal("item picture outside its block", (unsigned)row);
        }
        convert_tiles_4bpp(&b[sl->offset], sl->size / 32u, g_slice_px[row]);
        g_slice_built[row] = true;
    }
    return g_slice_px[row];
}

static const uint8_t* bg_tile(const shop_half_pieces* hp, uint16_t tile) {
    if (tile == 0u) {
        return g_blank_tile;
    }
    if (tile >= SHOP_BG_A_FIRST && tile < SHOP_BG_A_FIRST + SHOP_BG_A_COUNT) {
        return &g_bg_a[(size_t)(tile - SHOP_BG_A_FIRST) * 64u];
    }
    if (tile >= SHOP_BG_B_FIRST && tile < SHOP_BG_B_FIRST + SHOP_BG_B_COUNT) {
        return &g_bg_b[(size_t)(tile - SHOP_BG_B_FIRST) * 64u];
    }
    if (hp->has_noise_tiles && tile >= SHOP_BG_NOISE_FIRST
        && tile < SHOP_BG_NOISE_FIRST + SHOP_BG_NOISE_COUNT) {
        return &g_noise[(size_t)(tile - SHOP_BG_NOISE_FIRST) * 64u];
    }
    return NULL;
}

static const uint8_t* obj_tile(const shop_half_pieces* hp, uint16_t tile) {
    if (tile < SHOP_OBJ_FONT_FIRST + SHOP_OBJ_FONT_COUNT) {
        return &g_obj_font[(size_t)(tile - SHOP_OBJ_FONT_FIRST) * 64u];
    }
    if (tile >= SHOP_OBJ_ICONS_FIRST && tile < SHOP_OBJ_ICONS_FIRST + SHOP_OBJ_ICONS_COUNT) {
        return &g_obj_icons[(size_t)(tile - SHOP_OBJ_ICONS_FIRST) * 64u];
    }
    for (int j = 0; j < hp->piece_count; ++j) {
        const shop_picture_piece* p = &hp->piece[j];
        if (tile >= p->first_tile && tile < p->first_tile + p->count) {
            size_t k = (size_t)p->slice_tile + (size_t)(tile - p->first_tile);
            return &slice_pixels(p->slice)[k * 64u];
        }
    }
    return NULL;
}

static void validate_registers(void) {
    for (unsigned i = 0; i < sizeof(k_top_block); ++i) {
        if (rd8(0x019Cu + i) != k_top_block[i]) {
            shop_fatal("unexpected top shadow block byte at $019C +", i);
        }
    }
    if (rd8(0x01C2u) != 0u || rd8(0x01C4u) != 0u) {
        shop_fatal("unexpected $01C2/$01C4", rd16(0x01C2u));
    }
}

/* BG1 map entries measured: palette 0 (the panel), palette 2 or 3 at priority 1 (the typed
   text) and palette 2 at priority 0; the static's tiles with palette 4, any flip. */
static void validate_bg1(const shop_half_pieces* hp) {
    for (unsigned i = 0; i < 32u * SHOP_BG1_ROWS; ++i) {
        uint16_t e = (uint16_t)(hp->bg1_map[i * 2u] | (hp->bg1_map[i * 2u + 1u] << 8));
        uint16_t tile = e & 0x03FFu;
        unsigned attr = (e >> 10) & 0x3Fu; /* palette, priority, flips */
        bool noise = tile >= SHOP_BG_NOISE_FIRST && tile < SHOP_BG_NOISE_FIRST + SHOP_BG_NOISE_COUNT;
        bool ok = bg_tile(hp, tile) != NULL
                  && (attr == 0x00u || attr == 0x0Au || attr == 0x0Bu || attr == 0x02u
                      || (noise && (attr & 0x0Fu) == 0x04u));
        if (!ok) {
            shop_fatal("unexpected BG1 map entry", e);
        }
    }
}

static void validate_bg2(void) {
    for (unsigned i = 0; i < 32u * SHOP_BG2_ROWS; ++i) {
        uint16_t e = g_bg2_map[i];
        uint16_t tile = e & 0x03FFu;
        unsigned pal = (e >> 10) & 7u;
        if (tile < SHOP_BG_B_FIRST || tile >= SHOP_BG_B_FIRST + SHOP_BG_B_COUNT || pal > 1u) {
            shop_fatal("unexpected BG2 map entry", e);
        }
    }
}

typedef struct {
    uint16_t colour;
    int8_t prio; /* -1: none */
} obj_px;

/* The objects (the sprite table as sent, 8x8 and 16x16): the front object pixel of each point
   (the lowest number wins), with its priority. */
static void draw_objects(const shop_half_pieces* hp, obj_px L[SHOP_TOP_END_LINE][SCREEN_WIDTH]) {
    for (int y = 0; y < SHOP_TOP_END_LINE; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            L[y][x].prio = -1;
        }
    }
    int per_line[SCREEN_HEIGHT];
    int slivers[SCREEN_HEIGHT];
    memset(per_line, 0, sizeof(per_line));
    memset(slivers, 0, sizeof(slivers));
    const uint8_t* oam = hp->oam;
    for (int i = SHOP_SPRITES - 1; i >= 0; --i) {
        const uint8_t* lo = &oam[(size_t)i * 4u];
        uint8_t hi = (uint8_t)((oam[0x200u + ((unsigned)i >> 2)] >> (((unsigned)i & 3u) * 2u)) & 3u);
        uint8_t attr = lo[3];
        uint16_t tile = (uint16_t)(lo[2] | ((uint16_t)(attr & 1u) << 8));
        uint8_t pal = (uint8_t)((attr >> 1) & 7u);
        uint8_t prio = (uint8_t)((attr >> 4) & 3u);
        bool hf = (attr & 0x40u) != 0u;
        bool vf = (attr & 0x80u) != 0u;
        int dim = (hi & 2u) ? 16 : 8;
        int x9 = (int)(lo[0] | ((unsigned)(hi & 1u) << 8));
        int ox = (x9 >= 256) ? x9 - 512 : x9;
        int oy = lo[1];
        bool on_screen = false;
        for (int py = 0; py < dim; ++py) {
            if (((oy + py) & 255) < SHOP_TOP_END_LINE && ox > -dim && ox < SCREEN_WIDTH) {
                on_screen = true;
            }
        }
        if (!on_screen) {
            continue;
        }
        bool ok = obj_tile(hp, tile) != NULL;
        if (dim == 16) {
            ok = ok && (tile & 0x0Fu) != 0x0Fu && obj_tile(hp, (uint16_t)(tile + 1u)) != NULL
                 && obj_tile(hp, (uint16_t)(tile + 16u)) != NULL
                 && obj_tile(hp, (uint16_t)(tile + 17u)) != NULL;
        }
        if (!ok || pal == 3u || prio == 1u) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "shop: unexpected object %d (tile %u, palette %u, priority %u, size %d)", i,
                     (unsigned)tile, (unsigned)pal, (unsigned)prio, dim);
            port_fatal(msg);
        }
        for (int py = 0; py < dim; ++py) {
            int sy = (oy + py) & 255;
            if (sy >= SHOP_TOP_END_LINE) {
                continue;
            }
            per_line[sy]++;
            slivers[sy] += dim / 8;
            int fy = vf ? dim - 1 - py : py;
            for (int px = 0; px < dim; ++px) {
                int sx = ox + px;
                if (sx < 0 || sx >= SCREEN_WIDTH) {
                    continue;
                }
                int fx = hf ? dim - 1 - px : px;
                const uint8_t* t = obj_tile(hp, (uint16_t)(tile + (fy >> 3) * 16 + (fx >> 3)));
                uint8_t p = t[(fy & 7) * 8 + (fx & 7)];
                if (p != 0u) {
                    L[sy][sx].colour = (uint16_t)(128u + pal * 16u + p);
                    L[sy][sx].prio = (int8_t)prio;
                }
            }
        }
    }
    for (int y = 0; y < SHOP_TOP_END_LINE; ++y) {
        if (per_line[y] > 32 || slivers[y] > 34) {
            shop_fatal("more objects on a line than measured", (unsigned)y);
        }
    }
}

/* One BG pixel: its colour number (0: see-through) and priority. */
static uint8_t bg_pixel(const shop_half_pieces* hp, uint16_t e, int px, int py, uint8_t* colour) {
    const uint8_t* t = bg_tile(hp, e & 0x03FFu);
    int tx = (e & 0x4000u) ? 7 - px : px;
    int ty = (e & 0x8000u) ? 7 - py : py;
    uint8_t p = t[ty * 8 + tx];
    *colour = p ? (uint8_t)(((e >> 10) & 7u) * 16u + p) : 0u;
    return (uint8_t)((e >> 13) & 1u);
}

bool shop_screen_active(void) {
    return rd16(0x0008u) == 0xB97Bu && port_split_active() && assets_shop_loaded();
}

void shop_screen_draw(uint32_t* fb) {
    if (screen_decode_brightness("shop", rd8(0x029Bu)) != 0u) {
        port_fatal("shop: the bottom half (player 2's shop) lit: not measured");
    }
    uint8_t b = screen_decode_brightness("shop", rd8(0x019Bu));
    if (b == 0u) {
        screen_apply_brightness(fb, 0u);
        return;
    }
    validate_registers();
    shop_build();
    const shop_half_pieces* hp = assets_shop_half(0);
    validate_bg1(hp);
    validate_bg2();

    uint32_t rgb[256];
    for (unsigned i = 0; i < 256u; ++i) {
        rgb[i] = convert_color((uint16_t)(hp->colours[i * 2u] | (hp->colours[i * 2u + 1u] << 8)));
    }

    static obj_px objs[SHOP_TOP_END_LINE][SCREEN_WIDTH];
    draw_objects(hp, objs);

    for (int y = 0; y < SHOP_TOP_END_LINE; ++y) {
        int y1 = y + SHOP_BG1_VOFS + 1;
        int y2 = y + SHOP_BG2_VOFS + 1;
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            bool in_win2 = x >= SHOP_WIN2_LEFT && x <= SHOP_WIN2_RIGHT;
            bool in_win1 = x >= SHOP_WIN1_LEFT && x <= SHOP_WIN1_RIGHT;
            /* back to front: backdrop 0, objects 0/1 (1, 2), BG2 low (3), BG1 low (4),
               objects 2 (5), BG2 high (6), BG1 high (7), objects 3 (8) */
            int best = 0;
            uint8_t colour = 0u;
            const obj_px* o = &objs[y][x];
            if (o->prio >= 0 && !in_win2) {
                static const int rank[4] = {1, 2, 5, 8};
                best = rank[o->prio];
                colour = (uint8_t)o->colour;
            }
            {
                uint8_t c = 0u;
                uint16_t e = g_bg2_map[(y2 >> 3) * 32 + (x >> 3)];
                uint8_t pr = bg_pixel(hp, e, x & 7, y2 & 7, &c);
                int r = pr ? 6 : 3;
                if (c != 0u && r > best) {
                    best = r;
                    colour = c;
                }
            }
            if (!in_win1 && !in_win2) {
                int mx = (x + SHOP_BG1_HOFS) & 255;
                uint8_t c = 0u;
                const uint8_t* m = &hp->bg1_map[(size_t)((y1 >> 3) * 32 + (mx >> 3)) * 2u];
                uint16_t e = (uint16_t)(m[0] | (m[1] << 8));
                uint8_t pr = bg_pixel(hp, e, mx & 7, y1 & 7, &c);
                int r = pr ? 7 : 4;
                if (c != 0u && r > best) {
                    best = r;
                    colour = c;
                }
            }
            fb[y * SCREEN_WIDTH + x] = rgb[colour];
        }
    }
    for (int i = SHOP_TOP_END_LINE * SCREEN_WIDTH; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = 0xFF000000u;
    }
    screen_apply_brightness(fb, b);
}
