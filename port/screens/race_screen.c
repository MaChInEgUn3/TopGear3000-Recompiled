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

/* The one-player race, first slice: the sky, the horizon and the HUD (notes/probe/RACE.md
   §12-21; docs/DECISIONS.md, "The race's sky and HUD drawn"). The road and the cars are not
   drawn yet: their pixels are left black and marked not drawn (race_screen_drawn_mask), so a
   test compares only what this function draws. Measured facts:
   - Active while the race NMI is the vector ($85:8377) and the race's pieces are in (assets.c).
     Drawn after the NMI. A lit picture needs the frame taken at least once in this race (the
     scroll tables are named by the taking NMI) and the full NMI ($83CE) last run; the pause
     ($0026 bit 4) was never measured: all three stop.
   - Brightness: $014B as nmi_race_tail sends it (0 or bit 7: forced blank).
   - Static for the race (the load's screen image, ROM $83:96DD): mode 1 with BG3 in front, BG1
     and BG2 tiles at BG tile 0 = $3000 (the track's from $2FE), maps 64x32; BG3 tiles $5000,
     map 32x32, scroll (0, -1): screen cell = map cell; objects 8x8/16x16; windows on BG1, BG2
     and objects; colour maths (subtract) on BG1 and the backdrop, prevented inside the colour
     window. Per NMI from WRAM: $01B0 (W12SEL) $C2, $01B1 (WOBJSEL) $B0, $01B2 (CGADSUB) $A1,
     checked.
   - Rows 0-3: the HUD's opaque frame (BG3) covers them (checked: every pixel).
   - BG1, rows 4 on: the game's scroll table for HDMA 5 at $83:(1E46), in the layout
     scroll_tables_build writes, only the entries the measured frames showed, in their order: 4
     rows (the pointer's high byte left over: unchecked, under the frame), the sky (repeat, ROM
     $83:A5DC, past 127 rows continued from $83:A7D8: every row shows BG pixel line 192), the
     horizon if any (one entry from WRAM $1536 or $153A: H, V), rows from ROM $83:A968.. if any
     (the road's map rows: not drawn), the road (repeat, WRAM rows), the end; 216 rows after the
     first 4. Any other layout stops. BG2's table ($1E48)
     must give the same rows ($1556/$155A) and BG2 the same map rows: then BG2 is a copy behind
     BG1 and never shows (mode $09, 8x8 tiles: request M; guard below).
   - The horizon is BG1 map rows 24-31 (lines 192-255), the track's tile set's slices; tiles
     from $2FE, palette 7 (colours $71-$7F, sent from $7E:6A35 each NMI). A line outside
     192..255 or another palette stops. The road rows (from the table's road entry or the first
     band's line, whichever is higher, to the bottom) are not drawn but where the HUD is opaque:
     from the first band the band interrupt changes BG1's tile size and the road's colours.
   - Windows: HDMA 4's table at $7E:4CCC (04 $7E:8AB7, 108 rows twice from $7E:(1E4E)); on every
     drawn row window 1 must be empty (it masks BG1; the colour window is then the whole line,
     so no colour maths): any masking stops.
   - M guard: the band list's first line ($0486 + $00BE) within 109..188, the lines lit
     pictures showed over three whole races; outside it the BG mode above the first band was
     never seen lit (the register-only $29 would give BG2 16x16 tiles).
   - BG3 (the HUD): the load's map as sent, then the markers' 4 words, the 52 words of the last
     HUD draw (race_hud_draw's fixed order, request T) and its two strips (rows 15-16, columns
     9-21). Tiles: the font $8F:85F5 ($158..$1EF; $15C is the 8 words each HUD draw sends), the
     minimap as the taking NMI sent it ($7E:230C, $0F4..$10C). Tile $157 was never sent in the race (left-over video memory): its
     cells are not drawn. Priority 1 everywhere (checked): in front of every other layer, colour
     maths never on it. Colours 0-31: the cartridge's race colours, colour 7 from $00A0.
   - Objects (the minimap dots, the cars, the scenery) are not drawn: every point an object of
     the sent sprite table covers is not drawn unless the HUD is opaque there. */

#define RACE_BG1_FIRST_TILE 0x2FEu
#define RACE_FONT_FIRST_TILE 0x158u
#define RACE_FONT_TILES 152u
#define RACE_MINIMAP_FIRST_TILE 0x0F4u
#define RACE_MINIMAP_TILES 25u
#define RACE_TILE_15C 0x15Cu
#define RACE_TILE_LEFT_OVER 0x157u
#define RACE_TABLE_ROWS_END 220
#define RACE_MAX_BG1_TILES 256u

/* race_hud_draw's 52 words in its order (decomp race_frame.c, race_hud_draw): the BG3 map cell
   (row * 32 + column) each goes to; -1: the 8 words to BG3 tile $15C. */
static const int16_t k_hud_cells[RACE_HUD_WORDS] = {
    527,                                  /* $7E0F: $1E94 */
    98, 99, 100, 101, 102, 103, 104,      /* $7C62..: the bar */
    801, 802, 803, 804, 833, 834, 835, 836, /* $7F21.., $7F41..: the speed */
    77, 79, 80, 82, 83,                   /* $7C4D, $7C4F-50, $7C52-53: the clock */
    34, 35,                               /* $7C22-23: the position */
    58,                                   /* $7C3A: the lap */
    734, 766, 798, 830, 862,              /* $7EDE + 32k: a column */
    -1, -1, -1, -1, -1, -1, -1, -1,       /* BG3 tile $15C ($5AE0) */
    828, 829, 860, 861,                   /* $7F3C-3D, $7F5C-5D */
    827, 855, 856, 857, 858, 859,         /* $7F3B, $7F57-5B: the gauge */
    118, 119, 120, 121, 122,              /* $7C76-7A: the five lights */
};
static const int k_marker_cell = 87;      /* $7C57..$7C5A: hud_markers_put's 4 words */
static const int k_strip_cell[2] = {489, 521}; /* $7DE9, $7E09: 13 words each */

static uint8_t g_drawn[SCREEN_HEIGHT * SCREEN_WIDTH];
static uint32_t g_gen;          /* the race load the notes below belong to */
static bool g_taken;            /* a frame taken by the NMI in this race */
static uint16_t g_last_target;  /* the race NMI's last target */

static uint32_t g_built_gen;
static uint8_t g_bg1_tiles[RACE_MAX_BG1_TILES * 64u];
static uint8_t g_font[RACE_FONT_TILES * 64u];

static void race_stop(const char* what, unsigned v) {
    char msg[192];
    snprintf(msg, sizeof(msg), "race: %s (0x%X)", what, v);
    port_fatal(msg);
}

bool race_screen_active(void) {
    return assets_race_vector() && assets_race_loaded();
}

void race_screen_note_nmi(uint16_t target) {
    const race_pieces* rp = assets_race();
    if (rp->generation != g_gen) {
        g_gen = rp->generation;
        g_taken = false;
    }
    g_last_target = target;
    /* nmi_race_full takes the frame when it is requested ($00B8 bit 0 at its entry). */
    if (target == 0x83CEu && (rd16(0x00B8u) & 0x0001u) != 0u) {
        g_taken = true;
    }
}

const uint8_t* race_screen_drawn_mask(void) {
    return g_drawn;
}

static void race_build(const race_pieces* rp) {
    if (g_built_gen == rp->generation) {
        return;
    }
    size_t sz = 0;
    const uint8_t* set = asset_bytes(rp->tile_set, &sz);
    if (set == NULL || rp->tile_count > RACE_MAX_BG1_TILES
        || (size_t)rp->tile_offset + (size_t)rp->tile_count * 32u > sz) {
        race_stop("the track's tiles lie outside its set", rp->tile_offset);
    }
    convert_tiles_4bpp_packed(&set[rp->tile_offset], rp->tile_count, g_bg1_tiles);
    const uint8_t* font = asset_bytes(ASSET_RACE_HUD_FONT, &sz);
    if (font == NULL || sz != RACE_FONT_TILES * 16u) {
        race_stop("unexpected size of $8F:85F5", (unsigned)sz);
    }
    convert_tiles_2bpp(font, RACE_FONT_TILES, g_font);
    g_built_gen = rp->generation;
}

/* The scroll table's rows, read in the layout scroll_tables_build writes (see the header): only
   the entries the measured frames showed, in their order. */
typedef struct {
    int sky;             /* rows 4 .. 4+sky-1: ROM $83:A5DC (then $83:A7D8, its continuation) */
    int horizon;         /* the next rows (0: none): one WRAM entry */
    uint16_t horizon_at; /* its address (H, then V) */
} race_rows;

static void race_table_stop(const char* which, uint16_t at, const uint8_t* b, int n) {
    char msg[160];
    int len = snprintf(msg, sizeof(msg), "%s scroll table in a layout not measured:", which);
    for (int i = 0; i < n && len > 0 && len < (int)sizeof(msg) - 4; ++i) {
        len += snprintf(msg + len, sizeof(msg) - (size_t)len, " %02X", b[i]);
    }
    race_stop(msg, at);
}

static race_rows race_table(uint16_t at, uint16_t horizon_base, const char* which) {
    enum { MAX_BYTES = 22 };
    if (at > 0x2000u - MAX_BYTES) {
        race_stop("a scroll table outside low WRAM", at);
    }
    uint8_t b[MAX_BYTES];
    for (unsigned i = 0; i < (unsigned)MAX_BYTES; ++i) {
        b[i] = rd8((uint32_t)at + i);
    }
#define RACE_PTR(i) ((uint16_t)(b[(i) + 1] | (b[(i) + 2] << 8)))
    race_rows r = {0, 0, 0};
    int i = 0;
    /* 4 rows (its pointer's high byte is left over in memory: not checked, under the frame) */
    if (b[0] != 0x04u || b[1] != 0x00u) {
        race_table_stop(which, at, b, 13);
    }
    i = 3;
    /* the sky: ROM $83:A5DC, repeat; at 127 rows the routine goes on from $83:A7D8 */
    if ((b[i] & 0x80u) == 0u || (b[i] & 0x7Fu) == 0u || RACE_PTR(i) != 0xA5DCu) {
        race_table_stop(which, at, b, 13);
    }
    r.sky = b[i] & 0x7F;
    i += 3;
    if (r.sky == 0x7F && (b[i] & 0x80u) != 0u && (b[i] & 0x7Fu) != 0u && RACE_PTR(i) == 0xA7D8u) {
        r.sky += b[i] & 0x7F;
        i += 3;
    }
    /* the horizon (may be absent): one entry from WRAM $1536/$153A (BG2: $1556/$155A) */
    if ((b[i] & 0x80u) == 0u && b[i] != 0u
        && (RACE_PTR(i) == horizon_base || RACE_PTR(i) == horizon_base + 4u)) {
        r.horizon = b[i];
        r.horizon_at = RACE_PTR(i);
        i += 3;
    }
    /* rows from ROM $83:A968.. (may be absent): BG1's road map rows, not drawn */
    int rest = 0;
    if ((b[i] & 0x80u) != 0u && RACE_PTR(i) >= 0xA968u && RACE_PTR(i) < 0xA968u + 4u * 0xD8u) {
        rest += b[i] & 0x7F;
        i += 3;
    }
    /* the road: WRAM rows, repeat; then the end */
    if ((b[i] & 0x80u) == 0u || (b[i] & 0x7Fu) == 0u || RACE_PTR(i) >= 0x2000u
        || b[i + 3] != 0x00u) {
        race_table_stop(which, at, b, MAX_BYTES);
    }
    rest += b[i] & 0x7F;
    if (r.sky + r.horizon + rest != RACE_TABLE_ROWS_END - 4) {
        race_table_stop(which, at, b, MAX_BYTES);
    }
#undef RACE_PTR
    return r;
}

/* The decoded HUD map: the load's words, the markers', the last HUD draw's, its strips. */
static void race_hud_words(const race_pieces* rp, uint16_t map[32 * 28]) {
    for (unsigned i = 0; i < 32u * 28u; ++i) {
        map[i] = (uint16_t)(rp->hud_map[2u * i] | (rp->hud_map[2u * i + 1u] << 8));
    }
    for (unsigned k = 0; k < RACE_MARKER_WORDS; ++k) {
        map[k_marker_cell + (int)k] = 0x0157u;
    }
    if (rp->has_hud) {
        for (unsigned k = 0; k < RACE_HUD_WORDS; ++k) {
            if (k_hud_cells[k] >= 0) {
                map[k_hud_cells[k]] = rp->hud[k];
            }
        }
    }
    if (rp->has_strips) {
        for (unsigned s = 0; s < 2u; ++s) {
            for (unsigned k = 0; k < RACE_STRIP_BYTES / 2u; ++k) {
                map[k_strip_cell[s] + (int)k] =
                    (uint16_t)(rp->strip[s][2u * k] | (rp->strip[s][2u * k + 1u] << 8));
            }
        }
    }
}

/* A BG3 pixel: its colour index (0 see-through), or -1 where the tile is left-over video
   memory (not drawn). */
static int race_bg3_pixel(const uint16_t* map, const uint8_t* minimap, const uint8_t* tile15c,
                          int x, int y) {
    uint16_t w = map[(y >> 3) * 32 + (x >> 3)];
    unsigned tile = w & 0x3FFu;
    int px = (w & 0x4000u) ? 7 - (x & 7) : (x & 7);
    int py = (w & 0x8000u) ? 7 - (y & 7) : (y & 7);
    uint8_t c;
    if (tile == RACE_TILE_15C) {
        c = tile15c[py * 8 + px];
    } else if (tile >= RACE_FONT_FIRST_TILE && tile < RACE_FONT_FIRST_TILE + RACE_FONT_TILES) {
        c = g_font[(tile - RACE_FONT_FIRST_TILE) * 64u + (unsigned)(py * 8 + px)];
    } else if (tile >= RACE_MINIMAP_FIRST_TILE && tile < RACE_MINIMAP_FIRST_TILE + RACE_MINIMAP_TILES) {
        c = minimap[(tile - RACE_MINIMAP_FIRST_TILE) * 64u + (unsigned)(py * 8 + px)];
    } else if (tile == RACE_TILE_LEFT_OVER) {
        return -1;
    } else {
        race_stop("a HUD tile never sent in the race", tile);
        return -1;
    }
    if (c != 0u && (w & 0x2000u) == 0u) {
        race_stop("a HUD tile of priority 0", w);
    }
    if (c == 0u) {
        return 0;
    }
    return (int)(((w >> 10) & 7u) * 4u + c);
}

static uint32_t race_colour(uint8_t index) {
    palette_source s = palette_source_of(index);
    if (s != PAL_SRC_RACE_COLOURS_0_31 && s != PAL_SRC_SINGLE_COLOR) {
        race_stop("a HUD colour the race did not set", index);
    }
    return convert_color(palette_get(index));
}

void race_screen_draw(uint32_t* fb) {
    const race_pieces* rp = assets_race();
    uint8_t braw = rd8(0x014Bu);
    if (braw == 0u || (braw & 0x80u) != 0u) {
        /* forced blank: the picture is black, all of it as on the console */
        screen_apply_brightness(fb, 0u);
        memset(g_drawn, 1, sizeof(g_drawn));
        return;
    }
    if ((braw & 0x70u) != 0u) {
        race_stop("unexpected brightness byte $014B", braw);
    }
    uint8_t bright = braw & 0x0Fu;
    if (!g_taken) {
        race_stop("a lit picture before the NMI took a frame: not measured", braw);
    }
    if (g_last_target != 0x83CEu) {
        race_stop("a lit picture after a race NMI other than $83CE", g_last_target);
    }
    if ((rd16(0x0026u) & 0x0010u) != 0u) {
        race_stop("the pause: not measured", rd16(0x0026u));
    }
    if (rd8(0x01B0u) != 0xC2u || rd8(0x01B1u) != 0xB0u || rd8(0x01B2u) != 0xA1u) {
        race_stop("window or colour-maths settings not measured: $01B0", rd16(0x01B0u));
    }
    if (!rp->has_oam || !rp->has_colours || !rp->has_minimap) {
        race_stop("the NMI's sprite table, colours or minimap missing", 0);
    }
    uint8_t first_band = rd8(0x0486u + rd16(0x00BEu));
    if (first_band < 109u || first_band > 188u) {
        race_stop("the road's first band line outside the measured 109..188 (request M)", first_band);
    }
    race_build(rp);

    /* The rows: BG1's table and BG2's, which must give the same rows (BG2 hidden behind). */
    race_rows r1 = race_table(rd16(0x1E46u), 0x1536u, "BG1");
    race_rows r2 = race_table(rd16(0x1E48u), 0x1556u, "BG2");
    if (r1.sky != r2.sky || r1.horizon != r2.horizon
        || (r1.horizon > 0 && (rd16(r1.horizon_at) != rd16(r2.horizon_at)
                               || rd16((uint16_t)(r1.horizon_at + 2u))
                                      != rd16((uint16_t)(r2.horizon_at + 2u))))
        || memcmp(rp->map[0], rp->map[2], RACE_MAP_SLICE_BYTES) != 0
        || memcmp(rp->map[1], rp->map[3], RACE_MAP_SLICE_BYTES) != 0) {
        race_stop("BG2 not a copy of BG1 on the sky and horizon rows: not measured", r1.horizon_at);
    }
    /* BG1 is drawn down to the road's table entry or the first band, whichever comes first:
       from the band's line the band interrupt switches BG1 to 16x16 tiles and the road's
       colours (measured: the line a band starts on is the first screen row it applies to). */
    int bg1_end = 4 + r1.sky + r1.horizon;
    if (bg1_end > (int)first_band) {
        bg1_end = (int)first_band;
    }

    /* The windows' table (HDMA 4). */
    static const uint8_t k_win_fixed[4] = {0x04u, 0xB7u, 0x8Au, 0xECu};
    for (unsigned i = 0; i < 4u; ++i) {
        if (rd8(0x4CCCu + i) != k_win_fixed[i]) {
            race_stop("the windows' table header not measured at $7E:4CCC +", i);
        }
    }
    uint16_t win_a = rd16(0x4CD0u);
    uint16_t win_b = rd16(0x4CD3u);
    if (rd8(0x4CD2u) != 0xECu || rd8(0x4CD5u) != 0x00u || win_b != (uint16_t)(win_a + 0x01B0u)
        || (win_a != 0x2E2Cu && win_a != 0x318Cu)) {
        race_stop("the windows' table not measured: $7E:4CD0", win_a);
    }

    uint16_t map[32 * 28];
    race_hud_words(rp, map);
    uint8_t minimap[RACE_MINIMAP_TILES * 64u];
    convert_tiles_2bpp(rp->minimap, RACE_MINIMAP_TILES, minimap);
    /* BG3 tile $15C: the font's until the first HUD draw, then the 8 words each draw sends. */
    uint8_t tile15c[64];
    if (rp->has_hud) {
        uint8_t raw[16];
        for (unsigned k = 0; k < 8u; ++k) {
            raw[2u * k] = (uint8_t)(rp->hud[29u + k] & 0xFFu);
            raw[2u * k + 1u] = (uint8_t)(rp->hud[29u + k] >> 8);
        }
        convert_tiles_2bpp(raw, 1u, tile15c);
    } else {
        memcpy(tile15c, &g_font[(RACE_TILE_15C - RACE_FONT_FIRST_TILE) * 64u], 64u);
    }

    /* Where the objects are (not drawn). */
    static uint8_t objs[SCREEN_HEIGHT * SCREEN_WIDTH];
    memset(objs, 0, sizeof(objs));
    for (int i = 0; i < 128; ++i) {
        const uint8_t* lo = &rp->oam[i * 4];
        uint8_t hi = (uint8_t)((rp->oam[0x200 + (i >> 2)] >> ((i & 3) * 2)) & 3u);
        int dim = (hi & 2u) ? 16 : 8;
        int x9 = lo[0] | ((hi & 1u) << 8);
        int ox = x9 >= 256 ? x9 - 512 : x9;
        for (int py = 0; py < dim; ++py) {
            int sy = (lo[1] + py) & 255;
            if (sy >= SCREEN_HEIGHT) {
                continue;
            }
            for (int px = 0; px < dim; ++px) {
                int sx = ox + px;
                if (sx >= 0 && sx < SCREEN_WIDTH) {
                    objs[sy * SCREEN_WIDTH + sx] = 1u;
                }
            }
        }
    }

    uint32_t back = convert_color(palette_get(0));
    if (palette_source_of(0) != PAL_SRC_SINGLE_COLOR) {
        race_stop("colour 0 not set by the NMI", 0);
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        /* BG1 for this row: H, V from the table's data (bank $83). */
        bool bg1_row = y >= 4 && y < bg1_end;
        int h = 0, line = 0;
        if (bg1_row) {
            uint16_t hv, vv;
            if (y < 4 + r1.sky) {
                hv = bus_rd16(0x83A5DCu + 4u * (uint32_t)(y - 4));
                vv = bus_rd16(0x83A5DEu + 4u * (uint32_t)(y - 4));
            } else {
                hv = rd16(r1.horizon_at);
                vv = rd16((uint16_t)(r1.horizon_at + 2u));
            }
            h = hv & 0x1FF;
            line = (y + 1 + vv) & 0xFF;
            if (line < 192) {
                race_stop("a sky or horizon row on BG1 line outside the horizon's map rows", (unsigned)line);
            }
            uint16_t wa = (y < 112) ? (uint16_t)(win_a + 4u * (unsigned)(y - 4))
                                    : (uint16_t)(win_b + 4u * (unsigned)(y - 112));
            if (rd8(wa) <= rd8((uint32_t)wa + 1u)) {
                race_stop("window 1 masks BG1 on a drawn row: not measured", (unsigned)y);
            }
        }
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            int i = y * SCREEN_WIDTH + x;
            int c3 = race_bg3_pixel(map, minimap, tile15c, x, y);
            if (c3 > 0) {
                fb[i] = race_colour((uint8_t)c3);
                g_drawn[i] = 1u;
                continue;
            }
            if (c3 < 0 || !bg1_row || objs[i]) {
                if (y < 4) {
                    race_stop("the HUD's frame not opaque on rows 0-3 at x", (unsigned)x);
                }
                fb[i] = 0xFF000000u;
                g_drawn[i] = 0u;
                continue;
            }
            int bx = (x + h) & 511;
            const uint8_t* slice = rp->map[bx >= 256 ? 1 : 0];
            unsigned cell = (unsigned)(((line >> 3) - 24) * 32 + ((bx >> 3) & 31));
            uint16_t w = (uint16_t)(slice[2u * cell] | (slice[2u * cell + 1u] << 8));
            unsigned tile = w & 0x3FFu;
            if (tile < RACE_BG1_FIRST_TILE || tile >= RACE_BG1_FIRST_TILE + rp->tile_count) {
                race_stop("a horizon tile outside the track's tile set", tile);
            }
            int px = (w & 0x4000u) ? 7 - (bx & 7) : (bx & 7);
            int py = (w & 0x8000u) ? 7 - (line & 7) : (line & 7);
            uint8_t c = g_bg1_tiles[(tile - RACE_BG1_FIRST_TILE) * 64u + (unsigned)(py * 8 + px)];
            if (c == 0u) {
                fb[i] = back;
            } else {
                if (((w >> 10) & 7u) != 7u) {
                    race_stop("a horizon tile of a palette other than 7", w);
                }
                unsigned k = (0x70u + c - 0x71u) * 2u;
                fb[i] = convert_color((uint16_t)(rp->colours[k] | (rp->colours[k + 1u] << 8)));
            }
            g_drawn[i] = 1u;
        }
    }
    screen_apply_brightness(fb, bright);
}
