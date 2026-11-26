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

/* The pre-race screen (race_start_screen, $80:D17F-$80:D668), task 1 alone, drawn natively.
   Measured facts: notes/probe/RACESTART.md; decisions: docs/DECISIONS.md, "The pre-race screen
   drawn natively". Three phases, told apart by the game's NMI target $0008:

   1. $E175 (the bar): the championship's two closed shutters slide into one. Each line shows a
      row of the championship's top-half BG2 map; the row comes from the band layout the game's
      nmi_target_80E175 writes at $1B00 for the bar height h.
   2. $D784, then $8934 while the queued pictures are sent: the closed shutter at rest, from the
      shutter map unpacked from $9C:8FC3 (two 32x32 halves), with the fixed 7-byte band layout
      $81:C207 at $1B00.
   3. $E28C: the shutter opens by v = $017A (0x6E, 0x6A, ... 0x02, 0). Lines outside the open
      band show the shutter; lines inside show the course view: the course chart (lines up to
      126), the planets' viewport (127..179) and the names bar (180..223).

   Nothing here writes game RAM or calls a game routine. Anything outside what was measured stops
   with a message. */

/* ---- Fixed line geometry of the course view (measured) ---- */
#define RS_VIEWPORT_FIRST_LINE 127 /* irq_split_top: IRQ at line 126, takes effect on 127 */
#define RS_VIEWPORT_LAST_LINE 179  /* irq_split_bottom: IRQ at line 179, takes effect on 180 */
#define RS_DISC_LINES 52           /* the disc block of the window tables: lines 127..178 */
#define RS_VIEWPORT_MIN_OPEN 0x64u /* irq_split_top switches the viewport on only below this v */

#define RS_BACKDROP_CHART 0x1BE6u    /* nmi_target_80E28C: colour 0 for the chart's lines */
#define RS_BACKDROP_VIEWPORT 0x7CC6u /* irq_split_top: colour 0 from line 127 on */

/* ---- Drawings (VRAM-free tile numbering as the game's maps use it) ---- */
#define RS_SHUTTER_TILES 30u     /* $8F:83CC: BG1/BG2 tiles 0..29 (tile 0 blank) */
#define RS_CHAMP_B_FIRST 32u     /* $98:8000: the championship's BG tiles 32..350 (phase 1) */
#define RS_CHAMP_B_COUNT 319u
#define RS_COURSE_FIRST 64u      /* $92:80B2 [0..$24DF]: BG1/BG2 tiles 64..358 */
#define RS_COURSE_COUNT 295u
#define RS_OBJ_FONT_COUNT 80u    /* $87:8190: OBJ tiles 0..79 */
#define RS_OBJ_HI_FIRST 256u     /* $92:80B2 [$24E0..$3F1F]: OBJ tiles 256..465 */
#define RS_OBJ_HI_COUNT 210u
#define RS_BG3_FIXED_TILES 2u    /* $92:80B2 [$3F20..$3F3F]: BG3 tiles 0..1 (the starry black) */
#define RS_SHADOW_FIRST_TILE 2u  /* BG3 tiles 2..: 16 per planet, their second plane streamed */
#define RS_SHADOW_TILES 16u
#define RS_MAX_PLANETS 6u

/* ---- The meter (draw_meter, $80:DA5F): row 6, columns 2..11 of BG2 (its words captured) ---- */
#define RS_METER_ROW 6
#define RS_METER_COL 2
#define RS_METER_TILES 10

#define RS_SPRITES 128u

/* The last NMI target the port dispatched, and the glow word that NMI sent (it reads $017C).
   The screen is drawn before the next NMI (see race_start_screen_note_nmi). */
static uint16_t g_last_nmi = 0;
static uint16_t g_glow_sent = 0;

static bool g_built = false;
static uint32_t g_gens[6];
static uint8_t g_shutter_px[RS_SHUTTER_TILES * 64u];
static uint8_t g_champ_b_px[RS_CHAMP_B_COUNT * 64u];
static uint8_t g_course_px[RS_COURSE_COUNT * 64u];
static uint8_t g_obj_font_px[RS_OBJ_FONT_COUNT * 64u];
static uint8_t g_obj_hi_px[RS_OBJ_HI_COUNT * 64u];
static uint8_t g_bg3_fixed_px[RS_BG3_FIXED_TILES * 64u];

static void rs_fatal_u(const char* what, unsigned v) {
    char msg[128];
    snprintf(msg, sizeof(msg), "race_start: %s (0x%04X)", what, v);
    port_fatal(msg);
}

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static const uint8_t* rs_asset(int id, size_t expected) {
    size_t sz = 0;
    const uint8_t* p = asset_bytes(id, &sz);
    if (p == NULL || sz != expected) {
        rs_fatal_u("a converted picture has an unexpected size", (unsigned)sz);
    }
    return p;
}

static void build_drawings_once(void) {
    static const int ids[6] = {
        ASSET_CHAMP_BG_TILES_A, ASSET_CHAMP_BG_OBJ_TILES_B, ASSET_RACESTART_BG_TILES,
        ASSET_CHAMP_OBJ_FONT,   ASSET_RACESTART_OBJ_TILES,  ASSET_RACESTART_BG3_TILES,
    };
    bool same = g_built;
    for (int i = 0; i < 6; ++i) {
        if (asset_generation(ids[i]) != g_gens[i]) {
            same = false;
        }
    }
    if (same) {
        return;
    }
    convert_tiles_4bpp(rs_asset(ASSET_CHAMP_BG_TILES_A, RS_SHUTTER_TILES * 32u), RS_SHUTTER_TILES,
                       g_shutter_px);
    convert_tiles_4bpp(rs_asset(ASSET_CHAMP_BG_OBJ_TILES_B, RS_CHAMP_B_COUNT * 32u),
                       RS_CHAMP_B_COUNT, g_champ_b_px);
    convert_tiles_4bpp(rs_asset(ASSET_RACESTART_BG_TILES, RS_COURSE_COUNT * 32u), RS_COURSE_COUNT,
                       g_course_px);
    convert_tiles_4bpp(rs_asset(ASSET_CHAMP_OBJ_FONT, RS_OBJ_FONT_COUNT * 32u), RS_OBJ_FONT_COUNT,
                       g_obj_font_px);
    convert_tiles_4bpp(rs_asset(ASSET_RACESTART_OBJ_TILES, RS_OBJ_HI_COUNT * 32u), RS_OBJ_HI_COUNT,
                       g_obj_hi_px);
    convert_tiles_2bpp(rs_asset(ASSET_RACESTART_BG3_TILES, RS_BG3_FIXED_TILES * 16u),
                       RS_BG3_FIXED_TILES, g_bg3_fixed_px);
    for (int i = 0; i < 6; ++i) {
        g_gens[i] = asset_generation(ids[i]);
    }
    g_built = true;
}

/* One 8-pixel tile row of a map entry (tile, palette, priority, flips) into colour indices. */
typedef struct {
    uint16_t tile;
    uint8_t pal;
    bool prio;
    bool hflip;
    bool vflip;
} map_entry;

static map_entry split_entry(uint16_t e) {
    map_entry m;
    m.tile = e & 0x03FFu;
    m.pal = (uint8_t)((e >> 10) & 7u);
    m.prio = (e & 0x2000u) != 0u;
    m.hflip = (e & 0x4000u) != 0u;
    m.vflip = (e & 0x8000u) != 0u;
    return m;
}

static uint8_t tile_pixel(const uint8_t* px64, const map_entry* m, int tx, int ty) {
    int x = m->hflip ? 7 - tx : tx;
    int y = m->vflip ? 7 - ty : ty;
    return px64[y * 8 + x];
}

/* ======================================================================================
   Phase 1 ($E175): the bar. Per-line effect "the merging shutter" ($1B00).
   nmi_target_80E175 writes, for the bar height h (8, 12, ... 56, 58), a list of bands, each a
   line count and the BG2 row offset of its lines, then stops (the last offset holds to line 223):
     upper shutter  h+$38 lines, $FFDF-h
     gap            $34-h lines, $001F                    (only while h < $34)
     seam           n lines, one offset each: $FFE3+n-k   (n = 8; 4 at h=$38; none at h=$3A)
     lower shutter  $38-h lines, $FF6B                    (only while h < $38)
     rest           1 line, $FFAB+h
   The height is read from the first band (its count is h+$38); every other byte is checked
   against this layout at the place the routine writes it.
   ====================================================================================== */
static void read_merging_shutter(int16_t offs[SCREEN_HEIGHT]) {
    unsigned h = (unsigned)rd8(0x1B00u) - 0x38u;
    if (h < 0x08u || h > 0x3Au || (h != 0x3Au && (h & 3u) != 0u)) {
        rs_fatal_u("unexpected bar height in the $1B00 band list", h);
    }
    /* $0114 already holds the next height; $0116 = 1 once the bar is closed (h = $3A). */
    uint16_t next = rd16(0x0114u);
    uint16_t closed = rd16(0x0116u);
    bool ok = (closed == 1u) ? (h == 0x3Au && next == 0x3Au)
                             : (closed == 0u && next == ((h + 4u >= 0x3Au) ? 0x3Au : h + 4u));
    if (!ok) {
        rs_fatal_u("bar height in $1B00 does not follow $0114/$0116", next);
    }

    unsigned gap = (h < 0x34u) ? 0x34u - h : 0u;
    unsigned seam = (h <= 0x34u) ? 8u : (h == 0x38u ? 4u : 0u);
    unsigned lower = (h < 0x38u) ? 0x38u - h : 0u;
    struct {
        unsigned lines;
        uint16_t first;
        bool each;
    } bands[5] = {
        {h + 0x38u, (uint16_t)(0xFFDFu - h), false},
        {gap, 0x001Fu, false},
        {seam, (uint16_t)(0xFFE3u + seam), true},
        {lower, 0xFF6Bu, false},
        {1u, (uint16_t)(0xFFABu + h), false},
    };

    uint32_t at = 0x1B00u;
    int y = 0;
    uint16_t last = 0;
    for (int b = 0; b < 5; ++b) {
        if (bands[b].lines == 0u) {
            continue;
        }
        uint8_t count = bands[b].each ? (uint8_t)(0x80u | bands[b].lines) : (uint8_t)bands[b].lines;
        if (rd8(at) != count) {
            rs_fatal_u("unexpected band count in the $1B00 band list", rd8(at));
        }
        at += 1u;
        for (unsigned k = 0; k < bands[b].lines; ++k) {
            if (bands[b].each || k == 0u) {
                last = rd16(at);
                if (last != (uint16_t)(bands[b].first - (bands[b].each ? k : 0u))) {
                    rs_fatal_u("unexpected row offset in the $1B00 band list", last);
                }
                at += 2u;
            }
            if (y < SCREEN_HEIGHT) {
                offs[y++] = (int16_t)last;
            }
        }
    }
    if (rd8(at) != 0u) {
        port_fatal("race_start: the $1B00 band list does not end where measured");
    }
    while (y < SCREEN_HEIGHT) {
        offs[y++] = (int16_t)last;
    }
}

static void draw_bar(uint32_t* fb) {
    const champ_half_pieces* hp = assets_champ_half(0);
    if (hp == NULL || !hp->has_bg2_map) {
        port_fatal("race_start: the championship's top-half BG2 map is missing in the bar phase");
    }
    /* Mode 1, BG1+BG2+OBJ (the championship's set-up, kept), backdrop black: measured. The
       shutter rows are opaque priority-1 tiles over every column where BG1 or objects could
       show (0 pixels different at four measured moments). */
    if (rd16(0x0500u) != 0x0000u || rd8(0x01A8u) != 0x01u || rd8(0x01BCu) != 0x13u) {
        port_fatal("race_start: unexpected colour 0 / mode / layers in the bar phase");
    }
    int16_t offs[SCREEN_HEIGHT];
    read_merging_shutter(offs);

    uint32_t pal[32];
    for (unsigned i = 0; i < 32u; ++i) {
        pal[i] = convert_color(rd16(0x0500u + i * 2u));
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        int my = (y + (int)offs[y] + 1) & 255;
        int row = my >> 3;
        uint32_t* dst = &fb[y * SCREEN_WIDTH];
        for (int col = 0; col < 32; ++col) {
            size_t idx = (size_t)(row * 32 + col) * 2u;
            uint16_t e = (hp->has_bg2_panel_rows && row < 14) ? le16(&hp->bg2_panel_rows[idx])
                                                               : le16(&hp->bg2_map[idx]);
            for (int px = 0; px < 8; ++px) {
                dst[col * 8 + px] = pal[0];
            }
            if (e == 0u) {
                continue;
            }
            map_entry m = split_entry(e);
            const uint8_t* t = NULL;
            if (m.tile < RS_SHUTTER_TILES) {
                t = &g_shutter_px[(size_t)m.tile * 64u];
            } else if (m.tile >= RS_CHAMP_B_FIRST && m.tile < RS_CHAMP_B_FIRST + RS_CHAMP_B_COUNT) {
                t = &g_champ_b_px[(size_t)(m.tile - RS_CHAMP_B_FIRST) * 64u];
            }
            if (t == NULL || m.pal > 1u || !m.prio || m.vflip) {
                rs_fatal_u("unexpected BG2 entry in the bar phase", e);
            }
            for (int px = 0; px < 8; ++px) {
                uint8_t p = tile_pixel(t, &m, px, my & 7);
                if (p != 0u) {
                    dst[col * 8 + px] = pal[m.pal * 16u + p];
                }
            }
        }
    }
}

/* ======================================================================================
   The shutter map ($9C:8FC3, converted at start-up): BG2 row offset applied to line y gives
   map row (y + offset + 1) & 511; rows 0..31 are its first half, rows 32..63 its second.
   In phase 3 the first half's rows 0..27 were replaced by the course map, so only rows 28..31
   of it may show there.
   ====================================================================================== */
static void draw_shutter_line(uint32_t* dst, int y, int16_t offset, bool rows_28_on,
                              const uint32_t pal0[16], uint32_t empty_rgba) {
    size_t sz_a = 0;
    size_t sz_b = 0;
    const uint8_t* half_a = asset_bytes(ASSET_RACESTART_SHUTTER_MAP_A, &sz_a);
    const uint8_t* half_b = asset_bytes(ASSET_RACESTART_SHUTTER_MAP_B, &sz_b);
    if (half_a == NULL || sz_a != 0x0800u || half_b == NULL || sz_b != 0x0800u) {
        port_fatal("race_start: the shutter map has an unexpected size");
    }
    int my = (y + (int)offset + 1) & 511;
    int row = (my & 255) >> 3;
    const uint8_t* map = (my >= 256) ? half_b : half_a;
    if (rows_28_on && my < 256 && row < 28) {
        rs_fatal_u("a shutter line reached the course map's rows", (unsigned)my);
    }
    for (int col = 0; col < 32; ++col) {
        uint16_t e = le16(&map[(size_t)(row * 32 + col) * 2u]);
        map_entry m = split_entry(e);
        if (e != 0u && (m.tile >= RS_SHUTTER_TILES || m.pal != 0u || !m.prio || m.vflip)) {
            rs_fatal_u("unexpected shutter map entry", e);
        }
        for (int px = 0; px < 8; ++px) {
            uint8_t p = (e == 0u) ? 0u : tile_pixel(&g_shutter_px[(size_t)m.tile * 64u], &m, px, my & 7);
            if (p == 0u) {
                if (rows_28_on) {
                    /* Not measured: the course view's layers would show through here. */
                    port_fatal("race_start: a see-through shutter pixel while the shutter opens");
                }
                dst[col * 8 + px] = empty_rgba;
            } else if (p < 3u) {
                port_fatal("race_start: a shutter pixel uses colour 1 or 2 (not measured)");
            } else {
                dst[col * 8 + px] = pal0[p];
            }
        }
    }
}

/* Phase 2 ($D784, $8934): the closed shutter. $1B00 holds $81:C207 (72 8D FF 01 6D 00 00):
   lines 0..113 at offset -115 (second half), lines 114..223 at +109 (first half). */
static bool closed_shutter_table(void) {
    return rd8(0x1B00u) == 0x72u && rd16(0x1B01u) == 0xFF8Du && rd8(0x1B03u) == 0x01u
        && rd16(0x1B04u) == 0x006Du && rd8(0x1B06u) == 0x00u;
}

static void draw_closed_shutter(uint32_t* fb) {
    if (!closed_shutter_table()) {
        port_fatal("race_start: unexpected $1B00 band list for the closed shutter");
    }
    uint32_t pal0[16];
    for (unsigned i = 0; i < 16u; ++i) {
        pal0[i] = convert_color(rd16(0x0500u + i * 2u));
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        int16_t offset = (y < 0x72) ? (int16_t)0xFF8D : (int16_t)0x006D;
        draw_shutter_line(&fb[y * SCREEN_WIDTH], y, offset, false, pal0, 0xFF000000u);
    }
}

/* ======================================================================================
   Phase 3 ($E28C): the course view.
   ====================================================================================== */

/* Per-line effect "the shutter opening" ($017A with $1B00, $0D00, $0D28, $0D14).
   nmi_target_80E28C writes three lists in step (BG2 row offset; mode; masked layers) and a
   fourth (BG2 horizontal scroll reset): top shutter v lines; open 2 x ($72-v) lines (the second
   run only while $72-v < $6E); bottom shutter from line 228-v (1 line entry, held to 223). The
   open band starts at v and ends at 227-v (or 223 when there is no bottom shutter). */
typedef struct {
    unsigned v;
    int open_first;
    int open_end; /* first line of the bottom shutter, or 224 */
} shutter_opening;

static void check_entry(uint32_t at, uint8_t count, uint16_t value, const char* what) {
    if (rd8(at) != count || (count != 0u && rd16(at + 1u) != value)) {
        rs_fatal_u(what, (unsigned)(at & 0xFFFFu));
    }
}

static shutter_opening read_shutter_opening(void) {
    unsigned v = rd16(0x017Au);
    if (v > 0x6Eu || (v != 0u && (v & 3u) != 2u)) {
        rs_fatal_u("unexpected shutter opening $017A", v);
    }
    shutter_opening s;
    s.v = v;
    s.open_first = (int)v;
    unsigned run = 0x72u - v;
    /* the three lists in step: (count, BG2 offset), (count, mode), (count, masked layers) */
    static const uint32_t lists[3] = {0x1B00u, 0x0D00u, 0x0D28u};
    for (int l = 0; l < 3; ++l) {
        uint32_t at = lists[l];
        uint16_t open_val = (l == 0) ? 0xFFFFu : (l == 1 ? 0x0009u : 0x0017u);
        if (v == 0u) {
            check_entry(at, 0x72u, open_val, "unexpected open-shutter list");
            check_entry(at + 3u, 0x00u, 0, "unexpected open-shutter list end");
            continue;
        }
        uint16_t top_val = (l == 0) ? (uint16_t)(0xFFFFu - v) : (l == 1 ? 0x0001u : 0x0000u);
        uint16_t bot_val = (l == 0) ? (uint16_t)(0xFFFBu + v) : (l == 1 ? 0x0001u : 0x0000u);
        check_entry(at, (uint8_t)v, top_val, "unexpected top-shutter list entry");
        check_entry(at + 3u, (uint8_t)run, open_val, "unexpected open list entry");
        if (run >= 0x6Eu) {
            check_entry(at + 6u, 0x00u, 0, "unexpected list end (no bottom shutter)");
        } else {
            check_entry(at + 6u, (uint8_t)run, open_val, "unexpected second open list entry");
            check_entry(at + 9u, 0x01u, bot_val, "unexpected bottom-shutter list entry");
            check_entry(at + 12u, 0x00u, 0, "unexpected list end");
        }
    }
    /* $0D14 (BG2 horizontal scroll): 0 at line 0, at line $72 and at line 228-v. */
    check_entry(0x0D14u, 0x72u, 0x0000u, "unexpected BG2 scroll-reset list");
    if (run != 0u) {
        check_entry(0x0D17u, (uint8_t)run, 0x0000u, "unexpected BG2 scroll-reset list");
        check_entry(0x0D1Au, 0x01u, 0x0000u, "unexpected BG2 scroll-reset list");
        check_entry(0x0D1Du, 0x00u, 0, "unexpected BG2 scroll-reset list end");
    }
    s.open_end = (v != 0u && run < 0x6Eu) ? (int)(228u - v) : SCREEN_HEIGHT;
    return s;
}

/* Per-line effect "the leader lines and the discs" ($7F:0000 window 1, $7F:0100 window 2).
   window_shape (func_80DC5A + the disc rows) writes: 49 empty lines; an optional vertical
   leader line (high lines, one column); one horizontal leader line ($69..low); empty lines to
   line 126; then a 52-line block (lines 127..178) of disc edges, the last one empty, and the
   end mark (line 179 on keeps the empty edge). */
typedef struct {
    uint8_t left[SCREEN_HEIGHT];
    uint8_t right[SCREEN_HEIGHT];
} edges;

static void read_window_table(uint32_t base, edges* e) {
    uint32_t p = base;
    int y = 0;
    if (rd8(p) != 0x31u || rd8(p + 1u) != 0xFFu || rd8(p + 2u) != 0x00u) {
        rs_fatal_u("unexpected window table start", (unsigned)(base & 0xFFFFu));
    }
    for (; y < 0x31; ++y) {
        e->left[y] = 0xFFu;
        e->right[y] = 0x00u;
    }
    p += 3u;
    unsigned high = 0u;
    if (rd8(p + 3u) == 0x01u) { /* a vertical leader line comes first */
        high = rd8(p);
        uint8_t col = rd8(p + 1u);
        if (high == 0u || high >= 0x4Du || rd8(p + 2u) != col) {
            rs_fatal_u("unexpected vertical leader line", (unsigned)(base & 0xFFFFu));
        }
        for (unsigned k = 0; k < high; ++k, ++y) {
            e->left[y] = col;
            e->right[y] = col;
        }
        p += 3u;
    }
    if (rd8(p) != 0x01u || rd8(p + 1u) != 0x69u) {
        rs_fatal_u("unexpected horizontal leader line", (unsigned)(base & 0xFFFFu));
    }
    e->left[y] = 0x69u;
    e->right[y] = rd8(p + 2u);
    ++y;
    p += 3u;
    if (rd8(p) != (uint8_t)(0x4Du - high) || rd8(p + 1u) != 0xFFu || rd8(p + 2u) != 0x00u) {
        rs_fatal_u("unexpected empty run after the leader lines", (unsigned)(base & 0xFFFFu));
    }
    for (unsigned k = 0; k < 0x4Du - high; ++k, ++y) {
        e->left[y] = 0xFFu;
        e->right[y] = 0x00u;
    }
    p += 3u;
    if (y != RS_VIEWPORT_FIRST_LINE || rd8(p) != 0x80u + RS_DISC_LINES) {
        rs_fatal_u("the disc block does not start at line 127", (unsigned)(base & 0xFFFFu));
    }
    ++p;
    for (int k = 0; k < RS_DISC_LINES; ++k, ++y, p += 2u) {
        e->left[y] = rd8(p);
        e->right[y] = rd8(p + 1u);
    }
    if (e->left[y - 1] != 0xFFu || e->right[y - 1] != 0x00u || rd8(p) != 0x00u) {
        rs_fatal_u("unexpected end of the disc block", (unsigned)(base & 0xFFFFu));
    }
    for (; y < SCREEN_HEIGHT; ++y) {
        e->left[y] = 0xFFu;
        e->right[y] = 0x00u;
    }
}

static bool inside(const edges* e, int y, int x) {
    return e->left[y] <= x && x <= e->right[y];
}

/* The leader lines' glow ($017C/$017D, from $81:CAFA): the fixed colour the NMI sets, three
   channels at once (low byte red+blue, high byte green); 0 leaves the 0 set at line 127 of the
   frame before. */
typedef struct {
    unsigned r, g, b;
} rgb5;

static rgb5 read_leader_glow(uint16_t w) {
    rgb5 c = {0u, 0u, 0u};
    if (w == 0u) {
        return c;
    }
    if ((w & 0x00E0u) != 0x00A0u || (w & 0xE000u) != 0x4000u) {
        rs_fatal_u("unexpected leader-line glow $017C", w);
    }
    c.r = w & 0x1Fu;
    c.b = w & 0x1Fu;
    c.g = (w >> 8) & 0x1Fu;
    return c;
}

static uint16_t add_clamped(uint16_t c, unsigned r, unsigned g, unsigned b) {
    unsigned r5 = (c & 0x1Fu) + r;
    unsigned g5 = ((c >> 5) & 0x1Fu) + g;
    unsigned b5 = ((c >> 10) & 0x1Fu) + b;
    if (r5 > 31u) r5 = 31u;
    if (g5 > 31u) g5 = 31u;
    if (b5 > 31u) b5 = 31u;
    return (uint16_t)(r5 | (g5 << 5) | (b5 << 10));
}

/* The objects (the sprite table as the NMI sent it, $81:0900 low/$81:0B00 high, 8x8 and
   16x16): the front object pixel of each screen point (lowest number wins), with its priority
   and palette. */
typedef struct {
    uint16_t colour[SCREEN_HEIGHT][SCREEN_WIDTH];
    int8_t prio[SCREEN_HEIGHT][SCREEN_WIDTH]; /* -1: none */
    uint8_t pal[SCREEN_HEIGHT][SCREEN_WIDTH];
} object_layer;

static const uint8_t* obj_tile(uint16_t tile) {
    if (tile < RS_OBJ_FONT_COUNT) {
        return &g_obj_font_px[(size_t)tile * 64u];
    }
    if (tile >= RS_OBJ_HI_FIRST && tile < RS_OBJ_HI_FIRST + RS_OBJ_HI_COUNT) {
        return &g_obj_hi_px[(size_t)(tile - RS_OBJ_HI_FIRST) * 64u];
    }
    return NULL;
}

static void draw_objects(const uint8_t oam[RACESTART_OAM_BYTES], const uint16_t colours[256],
                         object_layer* L) {
    memset(L->prio, -1, sizeof(L->prio));
    int per_line[SCREEN_HEIGHT];
    int slivers[SCREEN_HEIGHT];
    memset(per_line, 0, sizeof(per_line));
    memset(slivers, 0, sizeof(slivers));
    for (int i = (int)RS_SPRITES - 1; i >= 0; --i) {
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
            if (((oy + py) & 255) < SCREEN_HEIGHT && ox > -dim && ox < SCREEN_WIDTH) {
                on_screen = true;
            }
        }
        if (!on_screen) {
            continue;
        }
        bool ok = obj_tile(tile) != NULL;
        if (dim == 16) {
            ok = ok && (tile & 0x0Fu) != 0x0Fu && obj_tile((uint16_t)(tile + 1u)) != NULL
                 && obj_tile((uint16_t)(tile + 16u)) != NULL
                 && obj_tile((uint16_t)(tile + 17u)) != NULL;
        }
        if (!ok || pal == 3u || (prio != 0u && prio != 2u) || hf) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "race_start: unexpected object %d (tile %u, palette %u, priority %u, attr 0x%02X)",
                     i, (unsigned)tile, (unsigned)pal, (unsigned)prio, (unsigned)attr);
            port_fatal(msg);
        }
        for (int py = 0; py < dim; ++py) {
            int sy = (oy + py) & 255;
            if (sy >= SCREEN_HEIGHT) {
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
                const uint8_t* t = obj_tile((uint16_t)(tile + (fy >> 3) * 16 + (px >> 3)));
                uint8_t p = t[(fy & 7) * 8 + (px & 7)];
                if (p != 0u) {
                    L->colour[sy][sx] = colours[128u + pal * 16u + p];
                    L->prio[sy][sx] = (int8_t)prio;
                    L->pal[sy][sx] = pal;
                }
            }
        }
    }
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        if (per_line[y] > 32 || slivers[y] > 34) {
            rs_fatal_u("more objects on a line than measured", (unsigned)y);
        }
    }
}

/* The course view's maps, as captured when the game sent them (by WRAM source and size). */
typedef struct {
    const racestart_pieces* maps;
} course_maps;

/* BG1: the scenery (tiles 64..358, palettes 1..4) and the blank tile 0. */
static uint8_t bg1_pixel(const course_maps* c, int mx, int my, uint8_t* pal) {
    const uint8_t* map = (mx >= 256) ? c->maps->bg1_right_map : c->maps->bg1_left_map;
    uint16_t e = le16(&map[(size_t)((my >> 3) * 32 + ((mx & 255) >> 3)) * 2u]);
    map_entry m = split_entry(e);
    const uint8_t* t = NULL;
    if (m.tile == 0u) {
        t = &g_shutter_px[0];
    } else if (m.tile >= RS_COURSE_FIRST && m.tile < RS_COURSE_FIRST + RS_COURSE_COUNT
               && m.pal >= 1u && m.pal <= 4u) {
        t = &g_course_px[(size_t)(m.tile - RS_COURSE_FIRST) * 64u];
    }
    if (t == NULL || m.prio) {
        rs_fatal_u("unexpected BG1 entry in the course view", e);
    }
    *pal = m.pal;
    return tile_pixel(t, &m, mx & 7, my & 7);
}

/* BG2: the chart and the planets (tiles 218..358), the meter (65..72), palettes 0-2, 5-7. */
static uint8_t bg2_pixel(const course_maps* c, int mx, int my, uint8_t* pal) {
    int row = my >> 3;
    int col = (mx & 255) >> 3;
    const uint8_t* map = (mx >= 256) ? c->maps->bg2_tr_map : c->maps->bg2_tl_map;
    uint16_t e = le16(&map[(size_t)(row * 32 + col) * 2u]);
    if (mx < 256 && row == RS_METER_ROW && col >= RS_METER_COL && col < RS_METER_COL + RS_METER_TILES
        && c->maps->has_meter) {
        e = c->maps->meter[col - RS_METER_COL]; /* "the meter": the words draw_meter wrote */
    }
    map_entry m = split_entry(e);
    if (m.tile < RS_COURSE_FIRST || m.tile >= RS_COURSE_FIRST + RS_COURSE_COUNT || m.pal == 3u
        || m.pal == 4u || m.prio) {
        rs_fatal_u("unexpected BG2 entry in the course view", e);
    }
    *pal = m.pal;
    return tile_pixel(&g_course_px[(size_t)(m.tile - RS_COURSE_FIRST) * 64u], &m, mx & 7, my & 7);
}

/* BG3: the starry black (tiles 0..1, priority 0) and the planets' shadows (priority 1). */
static uint8_t bg3_pixel(const course_maps* c, int mx, int my, bool* prio) {
    const uint8_t* map = (mx >= 256) ? c->maps->bg3_right_map : c->maps->bg3_left_map;
    uint16_t e = le16(&map[(size_t)((my >> 3) * 32 + ((mx & 255) >> 3)) * 2u]);
    map_entry m = split_entry(e);
    unsigned planet = (m.tile - RS_SHADOW_FIRST_TILE) / RS_SHADOW_TILES;
    if (m.pal != 0u || m.hflip || m.vflip
        || (m.tile >= RS_SHADOW_FIRST_TILE
            && (planet >= RS_MAX_PLANETS || ((c->maps->shadows_sent >> planet) & 1u) == 0u))) {
        rs_fatal_u("unexpected BG3 entry in the course view", e);
    }
    *prio = m.prio;
    if (m.tile < RS_BG3_FIXED_TILES) {
        return g_bg3_fixed_px[(size_t)m.tile * 64u + (size_t)((my & 7) * 8 + (mx & 7))];
    }
    /* "The planets' shadows": the tile's second plane as the game streamed it this frame. */
    uint8_t bits = c->maps->shadow[planet][((m.tile - RS_SHADOW_FIRST_TILE) % RS_SHADOW_TILES) * 8u
                                           + (unsigned)(my & 7)];
    return ((bits >> (7 - (mx & 7))) & 1u) ? 2u : 0u;
}

/* One line of the course view. The layers in front-to-back order of the open band (BG3's
   priority tiles in front): BG3 prio 1, objects prio 2, BG1, BG2, objects prio 0, BG3 prio 0,
   colour 0. `masked` removes every layer (colour 0 shows); `add` gets what the line's colour
   addition adds. */
typedef enum { LINE_CHART, LINE_VIEWPORT, LINE_NAMES } line_kind;

static void draw_course_line(uint32_t* dst, int y, line_kind kind, const course_maps* c,
                             const uint16_t colours[256], const object_layer* obj,
                             const edges* w1, const edges* w2, rgb5 glow, int scroll) {
    uint16_t backdrop = (kind == LINE_CHART) ? RS_BACKDROP_CHART : RS_BACKDROP_VIEWPORT;
    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        bool in1 = inside(w1, y, x);
        bool in2 = inside(w2, y, x);
        /* "The leader lines" (chart lines): window 1 masks every layer.
           "The discs' rims" (viewport and names lines): window 1 XOR window 2 masks every layer
           (the names lines' tables are empty, so nothing is masked there). */
        bool masked = (kind == LINE_CHART) ? in1 : (in1 != in2);
        if (masked) {
            dst[x] = convert_color(backdrop);
            continue;
        }
        int mx = (x + scroll) & 511;
        uint8_t pal1 = 0, pal2 = 0;
        bool pr3 = false;
        uint8_t p3 = bg3_pixel(c, mx, y, &pr3);
        uint8_t p1 = bg1_pixel(c, mx, y, &pal1);
        uint8_t p2 = bg2_pixel(c, mx, y, &pal2);
        int8_t po = obj->prio[y][x];

        uint16_t col = backdrop;
        enum { L_BACK, L_BG1, L_BG2, L_BG3, L_OBJ } layer = L_BACK;
        if (p3 != 0u && pr3) {
            col = colours[p3];
            layer = L_BG3;
        } else if (po == 2) {
            col = obj->colour[y][x];
            layer = L_OBJ;
        } else if (p1 != 0u) {
            col = colours[pal1 * 16u + p1];
            layer = L_BG1;
        } else if (p2 != 0u) {
            col = colours[pal2 * 16u + p2];
            layer = L_BG2;
        } else if (po == 0) {
            col = obj->colour[y][x];
            layer = L_OBJ;
        } else if (p3 != 0u) {
            col = colours[p3];
            layer = L_BG3;
        }

        if (kind == LINE_CHART && in2) {
            /* "The leader lines' glow": inside window 2, the fixed colour is added to BG1-3 and
               to objects of palettes 4-7. */
            bool takes = (layer == L_BG1 || layer == L_BG2 || layer == L_BG3)
                      || (layer == L_OBJ && obj->pal[y][x] >= 4u);
            if (takes) {
                col = add_clamped(col, glow.r, glow.g, glow.b);
            }
        } else if (kind == LINE_VIEWPORT && (layer == L_BG1 || layer == L_BG3) && po >= 0) {
            /* "The starlight": on BG1 and BG3 (the black, the shadows) the front object pixel at
               the same point is added. */
            uint16_t o = obj->colour[y][x];
            col = add_clamped(col, o & 0x1Fu, (o >> 5) & 0x1Fu, (o >> 10) & 0x1Fu);
        }
        dst[x] = convert_color(col);
    }
}

static void draw_course_view(uint32_t* fb) {
    const racestart_pieces* maps = assets_racestart();
    uint16_t irq = rd16(0x0005u);
    if (irq != 0xE3F7u && irq != 0xE48Au) {
        rs_fatal_u("unexpected raster split target $0005", irq);
    }
    /* Colours: $0500..$06FF; the screen's fixed ones checked (colour 0 is set per region). */
    if (rd16(0x0500u) != RS_BACKDROP_CHART || rd16(0x0502u) != 0x2D6Bu || rd16(0x0504u) != 0x0000u
        || rd16(0x0622u) != 0x17FFu || rd16(0x0624u) != 0x00C6u) {
        port_fatal("race_start: unexpected fixed colours at $0500/$0622");
    }
    uint16_t colours[256];
    for (unsigned i = 0; i < 256u; ++i) {
        colours[i] = rd16(0x0500u + i * 2u);
    }

    shutter_opening s = read_shutter_opening();
    static edges w1;
    static edges w2;
    read_window_table(0x7F0000u, &w1);
    read_window_table(0x7F0100u, &w2);
    rgb5 glow = read_leader_glow(g_glow_sent);

    /* The sun's animation (BG1 tiles 476..511, when $0700 is not 0) was never compared with the
       original: stop. */
    if (rd16(0x0700u) != 0u) {
        port_fatal("race_start: an animated sun (not compared with the original yet)");
    }
    if (!maps->has_oam) {
        port_fatal("race_start: the sprite table was not sent before the course view");
    }
    static course_maps c;
    c.maps = maps;

    /* "The viewport scroll": $0120 moves BG1-3 on the viewport lines (the BG2 reset at 228-v
       only touches shutter lines, drawn without scroll). Drawn before the next NMI, $0120 is
       the frame code's copy of the camera that the viewport split reads (measured with the
       camera moving: explore_pw.txt). */
    uint16_t scroll = rd16(0x0120u);
    if (scroll > 0x0100u) {
        rs_fatal_u("unexpected viewport scroll $0120", scroll);
    }

    static object_layer obj;
    draw_objects(maps->oam, colours, &obj);

    uint32_t pal0[16];
    for (unsigned i = 0; i < 16u; ++i) {
        pal0[i] = convert_color(colours[i]);
    }

    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        uint32_t* dst = &fb[y * SCREEN_WIDTH];
        /* "The shutter": lines outside the open band show the shutter map. */
        if (y < s.open_first) {
            draw_shutter_line(dst, y, (int16_t)(0xFFFFu - s.v), true, pal0, 0u);
            continue;
        }
        if (y >= s.open_end) {
            draw_shutter_line(dst, y, (int16_t)(0xFFFBu + s.v), true, pal0, 0u);
            continue;
        }
        line_kind kind = LINE_CHART;
        if (y >= RS_VIEWPORT_FIRST_LINE) {
            if (s.v >= RS_VIEWPORT_MIN_OPEN) {
                port_fatal("race_start: an open viewport line before the viewport is switched on");
            }
            kind = (y <= RS_VIEWPORT_LAST_LINE) ? LINE_VIEWPORT : LINE_NAMES;
        }
        draw_course_line(dst, y, kind, &c, colours, &obj, &w1, &w2, glow,
                         (kind == LINE_VIEWPORT) ? (int)scroll : 0);
    }
}

/* Called by the frame loop just before it runs an NMI target. The picture of this screen is
   drawn at the start of the next frame wait, before the next NMI: by then the game's frame code
   has sent what it sends right after the NMI (the planets' shadow frames, the meter's words, the
   window tables), as on the console where that code still lands in the same picture; the
   sprite table and the glow are taken as that NMI sent them. */
void race_start_screen_note_nmi(uint16_t target) {
    g_last_nmi = target;
    if (target == 0xE28Cu) {
        g_glow_sent = rd16(0x017Cu);
    }
}

bool race_start_screen_active(void) {
    if (port_split_active()) {
        return false;
    }
    uint16_t sub = g_last_nmi;
    if (sub == 0xE175u) {
        return rd16(0x0008u) == 0xE175u || rd16(0x0008u) == 0xD784u ? assets_champ_loaded() : false;
    }
    if (sub == 0xD784u || (sub == 0x8934u && rd8(0x019Bu) != 0u && closed_shutter_table())) {
        return assets_racestart_shutter_loaded();
    }
    if (sub == 0xE28Cu) {
        return rd16(0x0008u) == 0xE28Cu && assets_racestart_loaded();
    }
    return false;
}

bool race_start_screen_first_after_nmi(void) {
    return g_last_nmi == 0xE175u && race_start_screen_active();
}

void race_start_screen_draw(uint32_t* fb) {
    uint8_t b = screen_decode_brightness("race_start", rd8(0x019Bu));
    if (b == 0u) {
        screen_apply_brightness(fb, 0u);
        return;
    }
    build_drawings_once();
    uint16_t sub = g_last_nmi;
    if (sub == 0xE175u) {
        draw_bar(fb);
    } else if (sub == 0xD784u || sub == 0x8934u) {
        draw_closed_shutter(fb);
    } else if (sub == 0xE28Cu) {
        draw_course_view(fb);
    } else {
        port_fatal("race_start: unexpected NMI target");
    }
    screen_apply_brightness(fb, b);
}
