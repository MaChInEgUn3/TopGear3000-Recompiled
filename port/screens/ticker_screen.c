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

/* The ticker ($80:E4E6, ticker_screen), measured facts (notes/probe/TICKER.md):
   - Active when: $0008 == $B97B, the two-task split is on, and the ticker's load was seen in
     its order (assets.c): the font $88:8601 noted and sent, the Mode 7 drawings (unpacked
     $88:8C7D) streamed, the map cleared with the fixed byte 0, the map's 7 rows of 17 tiles
     (unpacked $95:8000) streamed; then the top half's sprite table and colours as the NMI sent
     them.
   - The top half (task 1, screen lines 0..107) is the ticker; the bottom half (task 2) takes the
     early leave in one-player games and stays dark ($029B = 0): lines 108..223 are black. A lit
     bottom half was never measured and stops.
   - The top half's shadow block ($019C..$01C1) is one fixed block for the whole run: Mode 7
     (BG1 + objects on the main screen), objects 8x8/16x16 with their drawings at the font
     (OBJSEL $02), no windows, no colour maths.
   - The Mode 7 picture: a 128x128 map, tile 0 everywhere but its top-left 17x7 tiles
     ($95:8000), drawings the 101 8bpp tiles of $88:8C7D; colour 0 see-through; wraps around.
     Its view is the game's own block at $012E..$013D (ticker_scene_step, DP $0100: horizontal
     and vertical offset, the matrix, the centre), which the screen's line tables ($81:CBF9..)
     hand to the picture from its first line to past the split. In every measured frame (the
     pass, $03A8 = 5) the matrix is the identity and the centre (64, 28): the picture is the
     map moved by the two offsets. The zoom-in ($03A8 = 0: a turning matrix) was never reached
     and stops.
   - Objects: the sprite table as sent; drawings 0..185 the font $88:8601 (name table bit 0);
     palettes 0..7, priorities 0, 1 and 3 seen (2 never: it stops), 8x8 and 16x16, any flip, at
     most 19 on a line; 1 and 3 in front of the picture's opaque pixels; 0 never met one (it
     stops: the title has it behind). Colour 0 (the backdrop) shows through the picture's
     see-through pixels and the objects'. */

#define TICKER_TOP_END_LINE 108

#define TICKER_M7_TILE_COUNT 101u
#define TICKER_M7_MAP_DIM 128u
#define TICKER_FONT_TILES 186u
#define TICKER_SPRITES 128

/* The top half's shadow block $019C..$01C1 (autopilot_60k.txt, every ticker frame). */
static const uint8_t k_top_block[0x26] = {
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x07,
    0x00, 0x20, 0x40, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0x30,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* The view block ($012E..$013D, 8 words): horizontal and vertical offset, then the matrix
   (A, B, C, D) and the centre (X, Y), whose measured values are fixed. */
#define TICKER_VIEW_ADDR 0x012Eu
#define TICKER_VIEW_WORDS 8u
static const uint16_t k_view_fixed[6] = {0x0100u, 0x0000u, 0x0000u, 0x0100u, 0x0040u, 0x001Cu};

/* The view the picture shows: the block as it is when the NMI runs. Task 1 writes the next
   step's block after the NMI, while the picture is being drawn; the screen's line tables take
   the block at the picture's first line, before that write (measured: the bench's picture
   after the NMI of step N shows step N-1's block, with the sprite table that NMI sent). */
static uint16_t g_view[TICKER_VIEW_WORDS];
static bool g_has_view = false;

static bool g_built = false;
static uint32_t g_gens[3];
static uint8_t g_m7_tiles[TICKER_M7_TILE_COUNT * 64u];
static uint8_t g_m7_map[TICKER_M7_MAP_DIM * TICKER_M7_MAP_DIM];
static uint8_t g_font[TICKER_FONT_TILES * 64u];

static void ticker_fatal(const char* what, unsigned v) {
    char msg[160];
    snprintf(msg, sizeof(msg), "ticker: %s (0x%X)", what, v);
    port_fatal(msg);
}

static void ticker_build(void) {
    static const int ids[3] = {ASSET_TICKER_M7_TILES, ASSET_TICKER_M7_MAP,
                               ASSET_PASSWORD_TEXT_DRAWINGS};
    bool same = g_built;
    for (int i = 0; i < 3; ++i) {
        if (asset_generation(ids[i]) != g_gens[i]) {
            same = false;
        }
    }
    if (same) {
        return;
    }
    size_t sz = 0;
    const uint8_t* t = asset_bytes(ASSET_TICKER_M7_TILES, &sz);
    if (t == NULL || sz != sizeof(g_m7_tiles)) {
        ticker_fatal("unexpected size of $88:8C7D", (unsigned)sz);
    }
    memcpy(g_m7_tiles, t, sizeof(g_m7_tiles)); /* 8bpp: one byte a pixel, 64 a tile */

    const uint8_t* m = asset_bytes(ASSET_TICKER_M7_MAP, &sz);
    if (m == NULL || sz < TICKER_MAP_ROWS * TICKER_MAP_COLS) {
        ticker_fatal("unexpected size of $95:8000", (unsigned)sz);
    }
    memset(g_m7_map, 0, sizeof(g_m7_map));
    for (unsigned r = 0; r < TICKER_MAP_ROWS; ++r) {
        for (unsigned c = 0; c < TICKER_MAP_COLS; ++c) {
            uint8_t tile = m[r * TICKER_MAP_COLS + c];
            if (tile >= TICKER_M7_TILE_COUNT) {
                ticker_fatal("Mode 7 map tile out of range", tile);
            }
            g_m7_map[r * TICKER_M7_MAP_DIM + c] = tile;
        }
    }

    const uint8_t* f = asset_bytes(ASSET_PASSWORD_TEXT_DRAWINGS, &sz);
    if (f == NULL || sz != TICKER_FONT_TILES * 32u) {
        ticker_fatal("unexpected size of $88:8601", (unsigned)sz);
    }
    convert_tiles_4bpp(f, TICKER_FONT_TILES, g_font);

    for (int i = 0; i < 3; ++i) {
        g_gens[i] = asset_generation(ids[i]);
    }
    g_built = true;
}

static void validate_registers(void) {
    for (unsigned i = 0; i < sizeof(k_top_block); ++i) {
        if (rd8(0x019Cu + i) != k_top_block[i]) {
            ticker_fatal("unexpected top shadow block byte at $019C +", i);
        }
    }
    if (rd8(0x01C2u) != 0u || rd8(0x01C4u) != 0u) {
        ticker_fatal("unexpected $01C2/$01C4", rd16(0x01C2u));
    }
    for (unsigned i = 0; i < 6u; ++i) {
        if (g_view[2u + i] != k_view_fixed[i]) {
            ticker_fatal("a turned or moved Mode 7 view (the zoom-in?) not measured: $0132 +",
                         2u * i);
        }
    }
}

/* A Mode 7 offset: 13 bits, signed. */
static int offset_13(uint16_t v) {
    uint16_t m = v & 0x1FFFu;
    return (m & 0x1000u) ? (int)m - 0x2000 : (int)m;
}

typedef struct {
    uint8_t colour;
    int8_t prio; /* -1: none */
} obj_px;

/* The objects (the sprite table as sent, 8x8 and 16x16): the front object pixel of each point
   (the lowest number wins), with its priority. */
static void draw_objects(const ticker_pieces* tp, obj_px L[TICKER_TOP_END_LINE][SCREEN_WIDTH]) {
    for (int y = 0; y < TICKER_TOP_END_LINE; ++y) {
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            L[y][x].prio = -1;
        }
    }
    int per_line[TICKER_TOP_END_LINE];
    int slivers[TICKER_TOP_END_LINE];
    memset(per_line, 0, sizeof(per_line));
    memset(slivers, 0, sizeof(slivers));
    const uint8_t* oam = tp->oam;
    for (int i = TICKER_SPRITES - 1; i >= 0; --i) {
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
            if (((oy + py) & 255) < TICKER_TOP_END_LINE && ox > -dim && ox < SCREEN_WIDTH) {
                on_screen = true;
            }
        }
        if (!on_screen) {
            continue;
        }
        bool ok = tile < TICKER_FONT_TILES && prio != 2u;
        if (dim == 16) {
            ok = ok && (tile & 0x0Fu) != 0x0Fu && tile + 17u < TICKER_FONT_TILES;
        }
        if (!ok) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "ticker: unexpected object %d (tile %u, palette %u, priority %u, size %d)", i,
                     (unsigned)tile, (unsigned)pal, (unsigned)prio, dim);
            port_fatal(msg);
        }
        for (int py = 0; py < dim; ++py) {
            int sy = (oy + py) & 255;
            if (sy >= TICKER_TOP_END_LINE) {
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
                const uint8_t* t = &g_font[(size_t)(tile + (fy >> 3) * 16 + (fx >> 3)) * 64u];
                uint8_t p = t[(fy & 7) * 8 + (fx & 7)];
                if (p != 0u) {
                    L[sy][sx].colour = (uint8_t)(128u + pal * 16u + p);
                    L[sy][sx].prio = (int8_t)prio;
                }
            }
        }
    }
    for (int y = 0; y < TICKER_TOP_END_LINE; ++y) {
        if (per_line[y] > 32 || slivers[y] > 34) {
            ticker_fatal("more objects on a line than measured", (unsigned)y);
        }
    }
}

bool ticker_screen_active(void) {
    return rd16(0x0008u) == 0xB97Bu && port_split_active() && assets_ticker_loaded()
        && g_has_view;
}

void ticker_screen_note_nmi(void) {
    if (rd16(0x0008u) != 0xB97Bu || !assets_ticker_loaded()) {
        g_has_view = false;
        return;
    }
    for (unsigned i = 0; i < TICKER_VIEW_WORDS; ++i) {
        g_view[i] = rd16(TICKER_VIEW_ADDR + 2u * i);
    }
    g_has_view = true;
}

void ticker_screen_draw(uint32_t* fb) {
    if (screen_decode_brightness("ticker", rd8(0x029Bu)) != 0u) {
        port_fatal("ticker: the bottom half (player 2's ticker) lit: not measured");
    }
    uint8_t b = screen_decode_brightness("ticker", rd8(0x019Bu));
    if (b == 0u) {
        screen_apply_brightness(fb, 0u);
        return;
    }
    validate_registers();
    ticker_build();
    const ticker_pieces* tp = assets_ticker();

    uint32_t rgb[256];
    for (unsigned i = 0; i < 256u; ++i) {
        rgb[i] = convert_color((uint16_t)(tp->colours[i * 2u] | (tp->colours[i * 2u + 1u] << 8)));
    }

    static obj_px objs[TICKER_TOP_END_LINE][SCREEN_WIDTH];
    draw_objects(tp, objs);

    /* The picture: the map moved by the game's two offsets (screen line y is the picture's
       line y + 1); the matrix is the identity (checked). */
    int hofs = offset_13(g_view[0]);
    int vofs = offset_13(g_view[1]);
    for (int y = 0; y < TICKER_TOP_END_LINE; ++y) {
        int my = (y + 1 + vofs) & 1023;
        const uint8_t* map_row = &g_m7_map[(size_t)(my >> 3) * TICKER_M7_MAP_DIM];
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            int mx = (x + hofs) & 1023;
            uint8_t tile = map_row[mx >> 3];
            uint8_t c = g_m7_tiles[(size_t)tile * 64u + (size_t)(my & 7) * 8u + (size_t)(mx & 7)];
            /* back to front: backdrop (colour 0), objects 0, the picture, objects 1 and 3. An
               object of priority 0 over an opaque picture pixel never happened in the
               measured run (the title has it behind): it stops. */
            uint8_t colour = 0u;
            const obj_px* o = &objs[y][x];
            if (o->prio == 0) {
                if (c != 0u) {
                    ticker_fatal("a priority-0 object over the Mode 7 picture: not measured",
                                 (unsigned)(y * SCREEN_WIDTH + x));
                }
                colour = o->colour;
            }
            if (c != 0u) {
                colour = c;
            }
            if (o->prio > 0) {
                colour = o->colour;
            }
            fb[y * SCREEN_WIDTH + x] = rgb[colour];
        }
    }
    for (int i = TICKER_TOP_END_LINE * SCREEN_WIDTH; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = 0xFF000000u;
    }
    screen_apply_brightness(fb, b);
}
