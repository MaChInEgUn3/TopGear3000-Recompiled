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

/* The one-player race: the sky, the horizon and the HUD (first slice), and the road (second
   slice) (notes/probe/RACE.md §12-24; docs/DECISIONS.md, "The race's sky and HUD drawn" and
   "The race's road drawn"). The cars and the objects are not drawn yet: their pixels are left
   black and marked not drawn (race_screen_drawn_mask), so a test compares only what this
   function draws. Measured facts:
   - Active while the race NMI is the vector ($85:8377) and the race's pieces are in (assets.c).
     Drawn after the NMI. A lit picture needs the frame taken at least once in this race (the
     scroll tables are named by the taking NMI) and the full NMI ($83CE) last run; the pause
     ($0026 bit 4) was never measured: all three stop.
   - Brightness: $014B as nmi_race_tail sends it (0 or bit 7: forced blank).
   - Static for the race (the load's screen image, ROM $83:96DD): mode 1 with BG3 in front, BG1
     and BG2 tiles at BG tile 0 = $3000 (the road's from $080, the track's from $2FE), maps
     64x32; BG3 tiles $5000, map 32x32, scroll (0, -1): screen cell = map cell; objects
     8x8/16x16; windows on BG1, BG2 and objects; colour maths (subtract) on BG1 and the backdrop,
     prevented inside the colour window. Per NMI from WRAM: $01B0 (W12SEL) $C2, $01B1 (WOBJSEL)
     $B0, $01B2 (CGADSUB) $A1, checked.
   - Rows 0-3 and 220-223: the HUD's opaque frame (BG3) covers them (checked: every pixel).
   - BG1, rows 4-219: the game's scroll table for HDMA 5 at $83:(1E46), in the layout
     scroll_tables_build writes, only the entries the measured frames showed, in their order: 4
     rows (the pointer's high byte left over: unchecked, under the frame), the sky (repeat, ROM
     $83:A5DC, past 127 rows continued from $83:A7D8: every row shows BG pixel line 192), the
     horizon if any (one entry from WRAM $1536 or $153A: H, V), rows from ROM $83:A968.. if any
     (repeat: H $FFF0, one fixed line of the road's map rows), the road (repeat, rows the
     body wrote in low WRAM), the end; 216 rows after the first 4. Any other layout stops. BG2's table
     ($1E48) must give the same sky and horizon rows ($1556/$155A) and BG2 the same map rows:
     then BG2 is a copy behind BG1 where BG1 can be see-through (mode $09, 8x8 tiles: request M;
     guard below). Everywhere else BG1 is opaque (the road's tiles use colours 1-4 only).
   - The band list ($0486 + $00BE, masks $0596 + $00BE; bands_merge's layout): from a band's
     line on (the line it starts on is the first screen row it applies to) the band interrupt
     sends 4 colours to $21-$24 (8 bytes a band from $7E:(00C8), mask bit 1) and 4 to $31-$34
     (from $7E:(00CC), bits 2-3: BG2's, the second road), the first band also the BG mode byte
     ($7E:047E or $0480 by $00B4, bit 7): $39, BG1 and BG2 16x16 from the first band down. Masks
     seen: $8E for the first band, $0E for the rest; any other stops. Above the first band BG1
     has 8x8 tiles (both $09 and $29: request M only touches BG2), colour $21 is $003A (set by
     the NMI) and $22-$24 still hold the last picture's last band: a pixel needing them stops.
   - BG1's map: rows 0-23 the road's (track_backdrop_load's two slices of $98:9FEB, palette 2,
     tiles from $080), rows 24-31 the horizon's (the track's tile set's slices, palette 7, tiles
     from $2FE, colours $71-$7F from $7E:6A35 each NMI). Measured per row kind: above the first
     band, the sky and horizon rows show map rows 24-31 (8x8), the $A968 rows map rows 0-23
     (8x8); from the first band (16x16), the road entry's rows show map rows 0-23. Any other
     row, line, tile or palette stops.
   - Windows: HDMA 4's table at $7E:4CCC (04 $7E:8AB7, 108 rows twice from $7E:(1E4E)). Above the
     first band window 1 must be empty (it masks BG1). From the first band on, a row's window 1
     may cover a span, always with window 2 the same span (seen over a whole race on the bench):
     BG1 is masked there and BG2 shows (BG2 is masked outside window 2), the second road ($07):
     BG2's own scroll table row ($1E48, the same layout), its map rows 0-23 as sent (BG1's words,
     priority 0, palette 3), colours $31-$34 from the bands. The colour window ((not W1) or W2)
     is then the whole line: no colour maths. Any other window stops.
   - M guard: the band list's first line ($0486 + $00BE) within 109..188, the lines lit
     pictures showed over three whole races; outside it the BG mode above the first band was
     never seen lit (the register-only $29 would give BG2 16x16 tiles).
   - BG3 (the HUD): the load's map as sent, then the markers' 4 words, the 52 words of the last
     HUD draw (race_hud_draw's fixed order, request T) and its two strips (rows 15-16, columns
     9-21). Tiles: the font $8F:85F5 ($158..$1EF; $15C is the 8 words each HUD draw sends), the
     minimap as the taking NMI sent it ($7E:230C, $0F4..$10C). Tile $157 was never sent in the
     race (left-over video memory): its cells are not drawn. Priority 1 everywhere (checked): in front of every other layer, colour
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
#define RACE_MAX_ROAD_TILES 0x200u
#define RACE_MAX_BANDS 64
#define RACE_BAND_MODE 0x39u

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
static uint8_t g_road_tiles[RACE_MAX_ROAD_TILES * 64u];
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
    const uint8_t* road = asset_bytes(ASSET_RACE_BACKDROP, &sz);
    if (road == NULL || rp->road_tile_count > RACE_MAX_ROAD_TILES
        || RACE_ROAD_FIRST_TILE + rp->road_tile_count > RACE_BG1_FIRST_TILE
        || (size_t)rp->road_tile_offset + (size_t)rp->road_tile_count * 32u > sz) {
        race_stop("the road's tiles lie outside $98:9FEB or reach the track's", rp->road_tile_offset);
    }
    convert_tiles_4bpp(&road[rp->road_tile_offset], rp->road_tile_count, g_road_tiles);
    const uint8_t* font = asset_bytes(ASSET_RACE_HUD_FONT, &sz);
    if (font == NULL || sz != RACE_FONT_TILES * 16u) {
        race_stop("unexpected size of $8F:85F5", (unsigned)sz);
    }
    convert_tiles_2bpp(font, RACE_FONT_TILES, g_font);
    g_built_gen = rp->generation;
}

/* The scroll table's entries, read in the layout scroll_tables_build writes (see the header):
   only the entries the measured frames showed, in their order. Rows from 4 on. */
typedef enum { ROWS_SKY, ROWS_HORIZON, ROWS_A968, ROWS_ROAD } race_row_kind;
typedef struct {
    race_row_kind kind;
    int first;           /* the first screen row */
    int count;
    uint16_t at;         /* bank $83: the data of the first row (one entry: of every row) */
    bool repeat;         /* a data entry per row */
} race_entry;
typedef struct {
    race_entry e[6];
    int n;
    int sky;             /* sky rows */
    int horizon;         /* horizon rows (0: none) */
    uint16_t horizon_at; /* its data (H, then V) */
} race_rows;

static void race_table_stop(const char* which, uint16_t at, const uint8_t* b, int n) {
    char msg[160];
    int len = snprintf(msg, sizeof(msg), "%s scroll table in a layout not measured:", which);
    for (int i = 0; i < n && len > 0 && len < (int)sizeof(msg) - 4; ++i) {
        len += snprintf(msg + len, sizeof(msg) - (size_t)len, " %02X", b[i]);
    }
    race_stop(msg, at);
}

static void race_rows_add(race_rows* r, race_row_kind kind, int count, uint16_t at, bool repeat) {
    int first = 4;
    if (r->n > 0) {
        first = r->e[r->n - 1].first + r->e[r->n - 1].count;
    }
    race_entry e = {kind, first, count, at, repeat};
    r->e[r->n++] = e;
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
    race_rows r;
    memset(&r, 0, sizeof(r));
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
    race_rows_add(&r, ROWS_SKY, r.sky, 0xA5DCu, true);
    /* the horizon (may be absent): one entry from WRAM $1536/$153A (BG2: $1556/$155A) */
    if ((b[i] & 0x80u) == 0u && b[i] != 0u
        && (RACE_PTR(i) == horizon_base || RACE_PTR(i) == horizon_base + 4u)) {
        r.horizon = b[i];
        r.horizon_at = RACE_PTR(i);
        race_rows_add(&r, ROWS_HORIZON, r.horizon, r.horizon_at, false);
        i += 3;
    }
    /* rows from ROM $83:A968.. (may be absent) */
    int rest = 0;
    if ((b[i] & 0x80u) != 0u && RACE_PTR(i) >= 0xA968u && RACE_PTR(i) < 0xA968u + 4u * 0xD8u) {
        rest += b[i] & 0x7F;
        race_rows_add(&r, ROWS_A968, b[i] & 0x7F, RACE_PTR(i), true);
        i += 3;
    }
    /* the road: WRAM rows, repeat; then the end */
    if ((b[i] & 0x80u) == 0u || (b[i] & 0x7Fu) == 0u
        || RACE_PTR(i) + 4u * (unsigned)(b[i] & 0x7F) > 0x2000u || b[i + 3] != 0x00u) {
        race_table_stop(which, at, b, MAX_BYTES);
    }
    rest += b[i] & 0x7F;
    race_rows_add(&r, ROWS_ROAD, b[i] & 0x7F, RACE_PTR(i), true);
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

/* The road's bands, read in bands_merge's layout from the first band the taking NMI named
   ($00BE): each band's line, and the 4 colours $21-$24 it sends (its chunk of $7E:(00C8)). */
typedef struct {
    int n;
    uint8_t line[RACE_MAX_BANDS];
    uint16_t c21[RACE_MAX_BANDS][4];   /* BG1's road colours (stream 1) */
    uint16_t c31[RACE_MAX_BANDS][4];   /* BG2's (stream 3, after stream 2's fixed address $31) */
} race_bands;

static void race_bands_read(race_bands* bd) {
    uint16_t first = rd16(0x00BEu);
    uint16_t s1 = rd16(0x00C8u);
    uint16_t s3 = rd16(0x00CCu);
    int n = 0;
    for (;;) {
        uint8_t line = rd8(0x0486u + (uint32_t)first + (uint32_t)n);
        if (line == 0u) {
            break;
        }
        if (n >= RACE_MAX_BANDS) {
            race_stop("more road bands than the port keeps", (unsigned)n);
        }
        uint8_t mask = rd8(0x0596u + (uint32_t)first + (uint32_t)n);
        if (mask != (n == 0 ? 0x8Eu : 0x0Eu)) {
            race_stop("a road band's streams not measured: mask", mask);
        }
        if (n > 0 && line <= bd->line[n - 1]) {
            race_stop("road bands not in line order", line);
        }
        bd->line[n] = line;
        for (unsigned k = 0; k < 4u; ++k) {
            bd->c21[n][k] = rd16((uint16_t)(s1 + 2u * k));
            bd->c31[n][k] = rd16((uint16_t)(s3 + 2u * k));
        }
        s1 = (uint16_t)(s1 + 8u);
        s3 = (uint16_t)(s3 + 8u);
        ++n;
    }
    bd->n = n;
    uint8_t mode = rd8(rd16(0x00B4u) ? 0x047Eu : 0x0480u);
    if (mode != RACE_BAND_MODE) {
        race_stop("the first road band's BG mode byte not measured", mode);
    }
}

/* A road layer's pixel colour from its tile's colour index c (1..15) and palette, in a row
   whose last band is `band` (-1: above the first band). BG1's road tiles use palette 2 (colours
   $21-$24 from the bands), BG2's palette 3 ($31-$34), the horizon's palette 7. */
static uint32_t race_bg_colour(const race_pieces* rp, const race_bands* bd, int band, int layer,
                               unsigned palette, uint8_t c, bool road_tile) {
    if (road_tile) {
        if (palette != (layer == 0 ? 2u : 3u) || c > 4u) {
            race_stop("a road tile's colour not measured: layer, palette, colour",
                      (unsigned)layer * 0x100u + palette * 16u + c);
        }
        if (band >= 0) {
            return convert_color(layer == 0 ? bd->c21[band][c - 1u] : bd->c31[band][c - 1u]);
        }
        if (c != 1u || layer != 0) {
            race_stop("a road colour above the first band other than $21 (the last picture's)", c);
        }
        return convert_color(rd16(0x003Au));
    }
    if (palette != 7u || layer != 0) {
        race_stop("a horizon tile of a palette other than 7, or on BG2", palette);
    }
    unsigned k = (0x70u + c - 0x71u) * 2u;
    return convert_color((uint16_t)(rp->colours[k] | (rp->colours[k + 1u] << 8)));
}

/* A road layer's map word (layer 0 BG1, 1 BG2) at map column col (0..63) and row (0..31): rows
   0-23 the road's slices, 24-31 the horizon's (the track's tile set's), as each was sent. */
static uint16_t race_bg_word(const race_pieces* rp, int layer, int col, int row) {
    unsigned c = (unsigned)(col & 31);
    unsigned slice = (unsigned)(2 * layer + (col >= 32 ? 1 : 0));
    const uint8_t* m;
    unsigned cell;
    if (row < 24) {
        m = rp->road_map[slice];
        cell = (unsigned)row * 32u + c;
    } else {
        m = rp->map[slice];
        cell = (unsigned)(row - 24) * 32u + c;
    }
    return (uint16_t)(m[2u * cell] | (m[2u * cell + 1u] << 8));
}

/* A tile's colour index at (px, py) of its 8x8 drawing, and whether it is one of the road's. */
static uint8_t race_bg_tile_pixel(const race_pieces* rp, unsigned tile, int px, int py, bool* road) {
    if (tile >= RACE_ROAD_FIRST_TILE && tile < RACE_ROAD_FIRST_TILE + rp->road_tile_count) {
        *road = true;
        return g_road_tiles[(tile - RACE_ROAD_FIRST_TILE) * 64u + (unsigned)(py * 8 + px)];
    }
    if (tile >= RACE_BG1_FIRST_TILE && tile < RACE_BG1_FIRST_TILE + rp->tile_count) {
        *road = false;
        return g_bg1_tiles[(tile - RACE_BG1_FIRST_TILE) * 64u + (unsigned)(py * 8 + px)];
    }
    race_stop("a BG1/BG2 tile never sent in the race", tile);
    return 0;
}

/* A road layer's scroll on screen row y (4..219), from its table entry (H, V from bank $83),
   with its tile size: 16x16 from the first band on (the band's mode byte $39), 8x8 above it. */
typedef struct {
    int h;
    int line;   /* the BG pixel line: y + 1 + V */
    bool big;
} race_row;

static race_row race_row_scroll(const race_rows* r, int y, bool big, const char* which) {
    int k = 0;
    while (y >= r->e[k].first + r->e[k].count) {
        ++k;
    }
    const race_entry* e = &r->e[k];
    uint32_t at = 0x830000u | (e->repeat ? (uint16_t)(e->at + 4u * (unsigned)(y - e->first)) : e->at);
    uint16_t hv = bus_rd16(at);
    uint16_t vv = bus_rd16(at + 2u);
    race_row row;
    row.big = big;
    if (big) {
        /* 16x16 tiles: the map is 1024x512 pixels; measured: the road entry's rows, on the
           road's map rows (0-23). (The road entry starts at the first band: scroll_tables_build
           and bands_merge read the same line list. A horizon or sky row from the first band on
           was never seen at the draw point, in four whole races: it stops.) */
        row.h = hv & 0x3FF;
        row.line = (y + 1 + vv) & 0x1FF;
        if (e->kind != ROWS_ROAD || row.line >= 24 * 16) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: a row from the first band on not measured: kind, line", which);
            race_stop(msg, (unsigned)e->kind * 0x1000u + (unsigned)row.line);
        }
    } else {
        /* 8x8 tiles: the sky and horizon rows on the horizon's map rows (24-31), the $A968 rows
           on the road's */
        row.h = hv & 0x1FF;
        row.line = (y + 1 + vv) & 0xFF;
        if (e->kind == ROWS_ROAD || (e->kind == ROWS_A968 ? row.line >= 192 : row.line < 192)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: a row above the first band not measured: kind, line", which);
            race_stop(msg, (unsigned)e->kind * 0x1000u + (unsigned)row.line);
        }
    }
    return row;
}

/* A road layer's pixel (layer 0 BG1, 1 BG2) at x on a row. */
static uint32_t race_road_layer_pixel(const race_pieces* rp, const race_bands* bd, int layer,
                                      const race_row* row, int band, int x, uint32_t back) {
    uint16_t w;
    unsigned tile;
    int px, py;
    if (row->big) {
        int bx = (x + row->h) & 1023;
        w = race_bg_word(rp, layer, bx >> 4, row->line >> 4);
        px = (w & 0x4000u) ? 15 - (bx & 15) : (bx & 15);
        py = (w & 0x8000u) ? 15 - (row->line & 15) : (row->line & 15);
        tile = ((w & 0x3FFu) + (unsigned)(px >> 3) + ((unsigned)(py >> 3) << 4)) & 0x3FFu;
    } else {
        int bx = (x + row->h) & 511;
        w = race_bg_word(rp, layer, bx >> 3, row->line >> 3);
        px = (w & 0x4000u) ? 7 - (bx & 7) : (bx & 7);
        py = (w & 0x8000u) ? 7 - (row->line & 7) : (row->line & 7);
        tile = w & 0x3FFu;
    }
    bool road = false;
    uint8_t c = race_bg_tile_pixel(rp, tile, px & 7, py & 7, &road);
    if (c == 0u) {
        /* only the horizon's tiles are see-through: the backdrop (BG2, a copy, hides behind) */
        if (road || row->big || layer != 0) {
            race_stop("a see-through pixel on a road row: not measured", tile);
        }
        return back;
    }
    return race_bg_colour(rp, bd, band, layer, (w >> 10) & 7u, c, road);
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
    static race_bands bd;
    race_bands_read(&bd);

    /* The rows: BG1's table and BG2's, which must give the same sky and horizon rows (BG2
       hidden behind). */
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
    int band = -1;
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        /* This row: its band, BG1's scroll and tile size, and the road's window. */
        bool bg_row = y >= 4 && y < RACE_TABLE_ROWS_END;
        bool big = y >= (int)first_band;
        while (band + 1 < bd.n && bd.line[band + 1] <= y) {
            ++band;
        }
        race_row row1 = {0, 0, false}, row2 = {0, 0, false};
        int wl = 1, wr = 0; /* window 1 (= window 2) on this row: BG2 shows inside, BG1 outside */
        if (bg_row) {
            row1 = race_row_scroll(&r1, y, big, "BG1");
            uint16_t wa = (y < 112) ? (uint16_t)(win_a + 4u * (unsigned)(y - 4))
                                    : (uint16_t)(win_b + 4u * (unsigned)(y - 112));
            uint8_t w1l = rd8(wa), w1r = rd8((uint32_t)wa + 1u);
            uint8_t w2l = rd8((uint32_t)wa + 2u), w2r = rd8((uint32_t)wa + 3u);
            if (w1l <= w1r) {
                /* Measured on the road rows only, and always with window 2 the same span: BG1
                   masked and BG2 shown inside it; the colour window ((not W1) or W2) is then the
                   whole line: no colour maths. */
                if (!big || w2l != w1l || w2r != w1r) {
                    race_stop("window 1 on a row not measured: row, W1, W2",
                              ((unsigned)y << 24) | ((unsigned)w1l << 16) | ((unsigned)w1r << 8) | w2l);
                }
                wl = w1l;
                wr = w1r;
                row2 = race_row_scroll(&r2, y, big, "BG2");
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
            if (!bg_row) {
                race_stop("the HUD's frame not opaque on rows 0-3 or 220-223 at y*256+x", (unsigned)i);
            }
            if (c3 < 0 || objs[i]) {
                fb[i] = 0xFF000000u;
                g_drawn[i] = 0u;
                continue;
            }
            bool bg2 = x >= wl && x <= wr;
            fb[i] = race_road_layer_pixel(rp, &bd, bg2 ? 1 : 0, bg2 ? &row2 : &row1, big ? band : -1,
                                          x, back);
            g_drawn[i] = 1u;
        }
    }
    screen_apply_brightness(fb, bright);
}
