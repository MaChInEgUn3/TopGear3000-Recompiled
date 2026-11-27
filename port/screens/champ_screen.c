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

/* Championship screen ($80:BE32 / $80:BEC6) measured facts (notes/probe/CHAMP.md):
   - Active when: $0008 == 0xB97B, two-task split is active, and both halves have loaded the
     five unpacked ROM streams ($87:8190, $90:C320, $87:86BE, $8F:83CC, $98:8000) and captured
     their BG1 map, BG2 map, and fire band frame.
   - Two identical halves split at line 108:
     - Top half: task 1 (DP = $0100), lines 0..107, palette $81:0500, brightness $019B,
       OAM pointer $016E ($0900 or $0D00), BG1 vertical scroll $019E.
     - Bottom half: task 2 (DP = $0200), lines 108..223, palette $81:0700, brightness $029B,
       OAM pointer $026E ($1100 or $1500), BG1 vertical scroll $029E - 0x74.
   - Each half is Mode 1 (BGMODE = 0x01, TM = 0x13: BG1 + BG2 + OBJ, 8x8 tiles, no colour math,
     no windows):
     - BG drawings:
       - Tiles 0..29 (30 tiles): $8F:83CC (CHAMP_BG_TILES_A)
       - Tiles 32..350 (319 tiles): $98:8000 (CHAMP_BG_OBJ_TILES_B)
       - Tiles 352..367 (16 tiles): 512-byte noise tiles captured from $7F:(base+$0000)
     - OBJ drawings:
       - Tiles 0..94 (95 tiles): tail of $98:8000 (tiles 224..318 of CHAMP_BG_OBJ_TILES_B)
       - Tiles 256..335 (80 tiles): $87:8190 (CHAMP_OBJ_FONT)
       - Tiles 336..338 (3 tiles): $90:C320 (CHAMP_OBJ_BAR)
       - Tiles 340..499 (160 tiles): $87:86BE (CHAMP_OBJ_GRAPHICS)
     - BG1 map (32x32 words): captured 2048-byte map from $7F:(base+$0000) with rows 3..10
       (8 rows of 32 words = 512 bytes) patched by the current fire band frame captured from
       $7F:(base+$11C0 + k*$0200) (k = 0..8); tiles 256..350, palette 2, priority 0, h-flip
       allowed, v-flip disallowed.
     - BG2 map (32x32 words): captured 2048-byte map from $7F:(base+$0800) with rows 0..13
       (14 rows of 32 words = 896 bytes) updated by captured big-panel uploads from
       $7F:(base+$0800); normal entries use tiles 1..29 or 32..255, palettes 0..1, priorities
       0..1, h-flip allowed; noise entries use tiles 352..367, palette 3, priority 0, h/v-flip
       allowed.
     - Per-line effect ($81:1B00, 22 bytes): 3-entry direct 16-bit BG2VOFS sub-table per half
       (top at $1B00..$1B08 for lines 0..107; 8-line gap $1B09..$1B0B with vofs 0x0020 for
       lines 108..115; bottom at $1B0C..$1B14 for lines 116..223; terminator 0x00 at $1B15).
     - Mode 1 layer ordering: backdrop (CGRAM 0) -> BG2 prio 0 -> BG1 prio 0 -> OBJ prio 0 ->
       BG2 prio 1. */

#define CHAMP_TOP_END_LINE 108
#define CHAMP_BOT_START_LINE 116

#define CHAMP_BG_A_FIRST 0u
#define CHAMP_BG_A_COUNT 30u
#define CHAMP_BG_B_FIRST 32u
#define CHAMP_BG_B_COUNT 319u
#define CHAMP_BG_NAME_COUNT 173u
#define CHAMP_BG_CTRL_COUNT 207u
#define CHAMP_BG_GEARS_COUNT 166u
#define CHAMP_BG_NOISE_FIRST 352u
#define CHAMP_BG_NOISE_COUNT 16u

#define CHAMP_OBJ_B_FIRST 0u
#define CHAMP_OBJ_B_OFFSET_TILES 224u
#define CHAMP_OBJ_B_COUNT 95u
#define CHAMP_OBJ_GEARS_FIRST 208u
#define CHAMP_OBJ_GEARS_COUNT 48u
#define CHAMP_OBJ_FONT_FIRST 256u
#define CHAMP_OBJ_FONT_COUNT 80u
#define CHAMP_OBJ_BAR_FIRST 336u
#define CHAMP_OBJ_BAR_COUNT 3u
#define CHAMP_OBJ_GFX_FIRST 340u
#define CHAMP_OBJ_GFX_COUNT 160u

#define CHAMP_SPRITE_COUNT 128u

static bool g_built = false;
static uint32_t g_gens[9];
static uint8_t g_bg_a_tiles[CHAMP_BG_A_COUNT * 64u];
static uint8_t g_bg_b_tiles[CHAMP_BG_B_COUNT * 64u];
static uint8_t g_bg_name_tiles[CHAMP_BG_NAME_COUNT * 64u];
static uint8_t g_bg_ctrl_tiles[CHAMP_BG_CTRL_COUNT * 64u];
static uint8_t g_bg_gears_tiles[CHAMP_BG_GEARS_COUNT * 64u];
static uint8_t g_obj_gears_tiles[CHAMP_OBJ_GEARS_COUNT * 64u];
static uint8_t g_obj_font_tiles[CHAMP_OBJ_FONT_COUNT * 64u];
static uint8_t g_obj_bar_tiles[CHAMP_OBJ_BAR_COUNT * 64u];
static uint8_t g_obj_gfx_tiles[CHAMP_OBJ_GFX_COUNT * 64u];

static uint32_t g_noise_gen[CHAMP_HALF_COUNT];
static uint8_t g_noise_tiles[CHAMP_HALF_COUNT][CHAMP_BG_NOISE_COUNT * 64u];

/* Each half as its interrupt sends it (the draw point, listed for the veto): the top half's
   registers, colours and sprite table go out in the NMI (nmi_menu_frame, from $0100's block,
   $0500 and the table at $016E), the bottom half's in the line interrupt (irq_menu_frame, from
   $0200's block, $0700 and the table at $026E). Task 1 runs right after the NMI and task 2 right
   after the IRQ, so what they write next shows in the following picture; the port draws after
   task 2's turn, so it takes each half's values when its interrupt runs (champ_screen_note_nmi /
   champ_screen_note_irq) instead of reading them at the draw point. The tasks' direct pages
   $0100..$02FF are kept whole for the half (the fields the drawing reads, the window logic at
   $01B2/$01B3 included, are listed in validate_half_registers). */
typedef struct {
    bool taken;
    uint8_t page[0x200];                /* $0100..$02FF */
    uint8_t colours[0x200];             /* $0500 (top) or $0700 (bottom) */
    uint16_t oam_at;                    /* $016E or $026E */
    uint8_t oam[0x220];                 /* the sprite table at oam_at */
} champ_half_sent;
static champ_half_sent g_sent[CHAMP_HALF_COUNT];

static void take_half(int half) {
    champ_half_sent* s = &g_sent[half];
    memcpy(s->page, &g_ram[0x0100], sizeof s->page);
    memcpy(s->colours, &g_ram[half == 0 ? 0x0500 : 0x0700], sizeof s->colours);
    /* Taken on every two-task frame (the shop and the ticker too); a pointer the championship
       does not use stops the port only when it is drawn (draw_half_objects). */
    s->oam_at = rd16(half == 0 ? 0x016Eu : 0x026Eu);
    if (s->oam_at + sizeof s->oam <= 0x2000u) {
        memcpy(s->oam, &g_ram[s->oam_at], sizeof s->oam);
    } else {
        memset(s->oam, 0, sizeof s->oam);
    }
    s->taken = true;
}

void champ_screen_note_split(void) {
    g_sent[0].taken = false;
    g_sent[1].taken = false;
}

void champ_screen_note_nmi(void) { take_half(0); }
void champ_screen_note_irq(void) { take_half(1); }

/* A byte of the half's direct pages as sent. */
static uint8_t sent8(int half, uint16_t addr) {
    if (addr < 0x0100u || addr > 0x02FFu) {
        port_fatal("champ: read outside the sent direct pages");
    }
    return g_sent[half].page[addr - 0x0100u];
}
static uint16_t sent16(int half, uint16_t addr) {
    return (uint16_t)(sent8(half, addr) | (sent8(half, (uint16_t)(addr + 1u)) << 8));
}
static uint16_t sent_colour(int half, unsigned i) {
    return (uint16_t)(g_sent[half].colours[i * 2u] | (g_sent[half].colours[i * 2u + 1u] << 8));
}
static uint8_t sent_oam8(int half, unsigned off) {
    return g_sent[half].oam[off];
}

static void champ_build_once(void) {
    static const int ids[9] = {
        ASSET_CHAMP_BG_TILES_A,
        ASSET_CHAMP_BG_OBJ_TILES_B,
        ASSET_CHAMP_OBJ_FONT,
        ASSET_CHAMP_OBJ_BAR,
        ASSET_CHAMP_OBJ_GRAPHICS,
        ASSET_CHAMP_NAME_BG_TILES,
        ASSET_CHAMP_CTRL_BG_TILES,
        ASSET_CHAMP_GEARS_BG_TILES,
        ASSET_CHAMP_GEARS_OBJ_TILES,
    };
    bool same = g_built;
    for (int i = 0; i < 9; ++i) {
        if (asset_generation(ids[i]) != g_gens[i]) {
            same = false;
        }
    }
    if (!same) {
        size_t sz = 0;
        const uint8_t* bg_a = asset_bytes(ASSET_CHAMP_BG_TILES_A, &sz);
        if (bg_a == NULL || sz != CHAMP_BG_A_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_BG_TILES_A size");
        }
        convert_tiles_4bpp(bg_a, CHAMP_BG_A_COUNT, g_bg_a_tiles);

        const uint8_t* bg_b = asset_bytes(ASSET_CHAMP_BG_OBJ_TILES_B, &sz);
        if (bg_b == NULL || sz != CHAMP_BG_B_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_BG_OBJ_TILES_B size");
        }
        convert_tiles_4bpp(bg_b, CHAMP_BG_B_COUNT, g_bg_b_tiles);

        const uint8_t* bg_name = asset_bytes(ASSET_CHAMP_NAME_BG_TILES, &sz);
        if (bg_name == NULL || sz != CHAMP_BG_NAME_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_NAME_BG_TILES size");
        }
        convert_tiles_4bpp(bg_name, CHAMP_BG_NAME_COUNT, g_bg_name_tiles);

        const uint8_t* bg_ctrl = asset_bytes(ASSET_CHAMP_CTRL_BG_TILES, &sz);
        if (bg_ctrl == NULL || sz != CHAMP_BG_CTRL_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_CTRL_BG_TILES size");
        }
        convert_tiles_4bpp(bg_ctrl, CHAMP_BG_CTRL_COUNT, g_bg_ctrl_tiles);

        const uint8_t* bg_gears = asset_bytes(ASSET_CHAMP_GEARS_BG_TILES, &sz);
        if (bg_gears == NULL || sz != CHAMP_BG_GEARS_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_GEARS_BG_TILES size");
        }
        convert_tiles_4bpp(bg_gears, CHAMP_BG_GEARS_COUNT, g_bg_gears_tiles);

        const uint8_t* obj_gears = asset_bytes(ASSET_CHAMP_GEARS_OBJ_TILES, &sz);
        if (obj_gears == NULL || sz != CHAMP_OBJ_GEARS_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_GEARS_OBJ_TILES size");
        }
        convert_tiles_4bpp(obj_gears, CHAMP_OBJ_GEARS_COUNT, g_obj_gears_tiles);

        const uint8_t* obj_f = asset_bytes(ASSET_CHAMP_OBJ_FONT, &sz);
        if (obj_f == NULL || sz != CHAMP_OBJ_FONT_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_OBJ_FONT size");
        }
        convert_tiles_4bpp(obj_f, CHAMP_OBJ_FONT_COUNT, g_obj_font_tiles);

        const uint8_t* obj_bar = asset_bytes(ASSET_CHAMP_OBJ_BAR, &sz);
        if (obj_bar == NULL || sz != CHAMP_OBJ_BAR_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_OBJ_BAR size");
        }
        convert_tiles_4bpp(obj_bar, CHAMP_OBJ_BAR_COUNT, g_obj_bar_tiles);

        const uint8_t* obj_gfx = asset_bytes(ASSET_CHAMP_OBJ_GRAPHICS, &sz);
        if (obj_gfx == NULL || sz != CHAMP_OBJ_GFX_COUNT * 32u) {
            port_fatal("champ: unexpected CHAMP_OBJ_GRAPHICS size");
        }
        convert_tiles_4bpp(obj_gfx, CHAMP_OBJ_GFX_COUNT, g_obj_gfx_tiles);

        for (int i = 0; i < 9; ++i) {
            g_gens[i] = asset_generation(ids[i]);
        }
        g_built = true;
    }

    for (int h = 0; h < CHAMP_HALF_COUNT; ++h) {
        const champ_half_pieces* hp = assets_champ_half(h);
        if (hp != NULL && hp->has_noise_tiles && g_noise_gen[h] != hp->noise_gen) {
            convert_tiles_4bpp(hp->noise_tiles, CHAMP_BG_NOISE_COUNT, g_noise_tiles[h]);
            g_noise_gen[h] = hp->noise_gen;
        }
    }
}

static uint16_t read_le16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static const uint8_t* champ_bg_tile_pixels(int half, uint16_t tile) {
    if (tile < CHAMP_BG_A_FIRST + CHAMP_BG_A_COUNT) {
        return &g_bg_a_tiles[(size_t)(tile - CHAMP_BG_A_FIRST) * 64u];
    }
    const champ_half_pieces* hp = assets_champ_half(half);
    if (tile >= CHAMP_BG_B_FIRST && tile < CHAMP_BG_B_FIRST + CHAMP_BG_B_COUNT) {
        size_t sub_idx = (size_t)(tile - CHAMP_BG_B_FIRST);
        if (hp != NULL) {
            if (hp->bg_b_asset == ASSET_CHAMP_NAME_BG_TILES && sub_idx < CHAMP_BG_NAME_COUNT) {
                return &g_bg_name_tiles[sub_idx * 64u];
            }
            if (hp->bg_b_asset == ASSET_CHAMP_CTRL_BG_TILES && sub_idx < CHAMP_BG_CTRL_COUNT) {
                return &g_bg_ctrl_tiles[sub_idx * 64u];
            }
            if (hp->bg_b_asset == ASSET_CHAMP_GEARS_BG_TILES && sub_idx < CHAMP_BG_GEARS_COUNT) {
                return &g_bg_gears_tiles[sub_idx * 64u];
            }
        }
        return &g_bg_b_tiles[sub_idx * 64u];
    }
    if (hp != NULL && hp->has_noise_tiles
        && tile >= CHAMP_BG_NOISE_FIRST && tile < CHAMP_BG_NOISE_FIRST + CHAMP_BG_NOISE_COUNT) {
        return &g_noise_tiles[half][(size_t)(tile - CHAMP_BG_NOISE_FIRST) * 64u];
    }
    return NULL;
}

static const uint8_t* champ_obj_tile_pixels(int half, uint16_t tile) {
    if (tile < CHAMP_OBJ_B_FIRST + CHAMP_OBJ_B_COUNT) {
        return &g_bg_b_tiles[(size_t)(CHAMP_OBJ_B_OFFSET_TILES + (tile - CHAMP_OBJ_B_FIRST)) * 64u];
    }
    const champ_half_pieces* hp = assets_champ_half(half);
    if (hp != NULL && hp->has_gears_obj_tiles
        && tile >= CHAMP_OBJ_GEARS_FIRST && tile < CHAMP_OBJ_GEARS_FIRST + CHAMP_OBJ_GEARS_COUNT) {
        return &g_obj_gears_tiles[(size_t)(tile - CHAMP_OBJ_GEARS_FIRST) * 64u];
    }
    if (tile >= CHAMP_OBJ_FONT_FIRST && tile < CHAMP_OBJ_FONT_FIRST + CHAMP_OBJ_FONT_COUNT) {
        return &g_obj_font_tiles[(size_t)(tile - CHAMP_OBJ_FONT_FIRST) * 64u];
    }
    if (tile >= CHAMP_OBJ_BAR_FIRST && tile < CHAMP_OBJ_BAR_FIRST + CHAMP_OBJ_BAR_COUNT) {
        return &g_obj_bar_tiles[(size_t)(tile - CHAMP_OBJ_BAR_FIRST) * 64u];
    }
    if (tile >= CHAMP_OBJ_GFX_FIRST && tile < CHAMP_OBJ_GFX_FIRST + CHAMP_OBJ_GFX_COUNT) {
        return &g_obj_gfx_tiles[(size_t)(tile - CHAMP_OBJ_GFX_FIRST) * 64u];
    }
    return NULL;
}

static void validate_half_registers(int half) {
    uint16_t dp = (half == 0) ? 0x0100u : 0x0200u;
    int16_t bg1_vofs = (int16_t)sent16(half, (uint16_t)(dp + 0x9Eu));
    uint8_t exp_objsel = (half == 0) ? 0x01u : 0x03u;
    uint8_t exp_bg1sc = (half == 0) ? 0x00u : 0x40u;
    uint8_t exp_bg2sc = (half == 0) ? 0x04u : 0x44u;
    uint8_t exp_bg12nba = (half == 0) ? 0x11u : 0x55u;
    uint8_t tm = sent8(half, dp + 0xBCu);

    if (sent16(half, dp + 0x9Cu) != 0u
        || bg1_vofs < -1 || bg1_vofs > 39
        || (half == 0 && (sent8(half, 0x01C2u) != 0u || sent8(half, 0x01C4u) != 0u))
        || sent16(half, dp + 0xA0u) != 0u
        || sent8(half, dp + 0xA8u) != 0x01u
        || sent8(half, dp + 0xA9u) != 0x00u
        || sent8(half, dp + 0xADu) != exp_objsel
        || sent8(half, 0x01B2u) != 0x00u
        || sent8(half, 0x01B3u) != 0x00u
        || sent8(half, dp + 0xB4u) != exp_bg1sc
        || sent8(half, dp + 0xB5u) != exp_bg2sc
        || sent8(half, dp + 0xB8u) != exp_bg12nba
        || sent8(half, dp + 0xBAu) != 0x00u
        || sent8(half, dp + 0xBBu) != 0x00u
        || (tm != 0x12u && tm != 0x13u)
        || sent8(half, dp + 0xBDu) != 0x00u
        || sent8(half, dp + 0xBEu) != 0x00u
        || sent8(half, dp + 0xBFu) != 0x00u
        || sent8(half, dp + 0xC0u) != 0x00u
        || sent8(half, dp + 0xC1u) != 0x00u) {
        char msg[128];
        snprintf(msg, sizeof(msg), "champ: unexpected shadow register state in half %d", half);
        port_fatal(msg);
    }
}

/* Decode the 22-byte BG2 vertical-offset table at $81:1B00 ($1B00..$1B15) into one vertical
   offset per screen line 0..223.
   Task 1 occupies $1B00..$1B08 (lines 0..107); $1B09..$1B0B is the fixed 8-line gap (lines
   108..115, offset 0x0020); Task 2 occupies $1B0C..$1B14 (lines 116..223); $1B15 == 0x00.
   While a bar opens or closes, the frame hook writes entries 1 and 2 with this frame's l,
   then steps l by +/-4, and writes entry 0 for the next frame (so the three counts in RAM
   sum to 112 or 104 instead of 108). */
static void decode_half_bg2_vofs(uint16_t base, int y_start, int16_t vofs_out[SCREEN_HEIGHT]) {
    uint8_t cnt[3];
    int16_t val[3];
    for (unsigned k = 0; k < 3u; ++k) {
        cnt[k] = rd8((uint32_t)base + k * 3u);
        val[k] = (int16_t)rd16((uint32_t)base + k * 3u + 1u);
        if (cnt[k] == 0u || (cnt[k] & 0x80u) != 0u) {
            port_fatal("champ: unexpected BG2 line-offset entry count at $1B00");
        }
    }
    unsigned sum = (unsigned)cnt[0] + (unsigned)cnt[1] + (unsigned)cnt[2];
    if (sum == 112u) {
        if (cnt[0] <= 4u) {
            port_fatal("champ: unexpected BG2 line-offset entry 0 count during open step");
        }
        cnt[0] = (uint8_t)(cnt[0] - 4u);
        val[0] = (int16_t)(val[0] + 4);
    } else if (sum == 104u) {
        cnt[0] = (uint8_t)(cnt[0] + 4u);
        val[0] = (int16_t)(val[0] - 4);
    } else if (sum != 108u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "champ: unexpected BG2 line-offset sum %u at $%04X",
                 sum, (unsigned)base);
        port_fatal(msg);
    }

    int y = y_start;
    for (unsigned k = 0; k < 3u; ++k) {
        for (uint8_t i = 0; i < cnt[k]; ++i) {
            vofs_out[y++] = val[k];
        }
    }
}

static void decode_champ_bg2_vofs_table(int16_t vofs_out[SCREEN_HEIGHT]) {
    if (rd8(0x1B09u) != 0x08u || rd16(0x1B0Au) != 0x0020u || rd8(0x1B15u) != 0x00u) {
        port_fatal("champ: unexpected BG2 line-offset table separator/terminator at $1B00");
    }
    decode_half_bg2_vofs(0x1B00u, 0, vofs_out);
    for (int y = CHAMP_TOP_END_LINE; y < CHAMP_BOT_START_LINE; ++y) {
        vofs_out[y] = 0x0020;
    }
    decode_half_bg2_vofs(0x1B0Cu, CHAMP_BOT_START_LINE, vofs_out);
}

static uint16_t champ_bg1_entry(const champ_half_pieces* hp, int row, int col) {
    size_t idx = (size_t)(row * 32 + col) * 2u;
    if (row >= 3 && row < 11) {
        size_t band_idx = (size_t)((row - 3) * 32 + col) * 2u;
        return read_le16(&hp->fire_band[band_idx]);
    }
    return read_le16(&hp->bg1_map[idx]);
}

static uint16_t champ_bg2_entry(const champ_half_pieces* hp, int row, int col) {
    size_t idx = (size_t)(row * 32 + col) * 2u;
    if (hp->has_bg2_panel_rows && row < 14) {
        return read_le16(&hp->bg2_panel_rows[idx]);
    }
    return read_le16(&hp->bg2_map[idx]);
}

static void validate_half_maps(int half, const champ_half_pieces* hp) {
    for (int row = 0; row < 32; ++row) {
        for (int col = 0; col < 32; ++col) {
            uint16_t e1 = champ_bg1_entry(hp, row, col);
            if (e1 != 0u) {
                uint16_t tile = e1 & 0x03FFu;
                uint16_t pal = (e1 >> 10) & 7u;
                bool prio = (e1 & 0x2000u) != 0u;
                bool vflip = (e1 & 0x8000u) != 0u;
                bool ok_fire = (tile >= 256u && tile <= 350u && pal == 2u && !prio && !vflip);
                bool ok_ctrl = (tile >= 172u && tile <= 238u && pal == 0u && !prio && !vflip);
                if (!ok_fire && !ok_ctrl) {
                    char msg[96];
                    snprintf(msg, sizeof(msg), "champ: unexpected BG1 entry 0x%04X in half %d",
                             (unsigned)e1, half);
                    port_fatal(msg);
                }
            }

            uint16_t e2 = champ_bg2_entry(hp, row, col);
            if (e2 != 0u) {
                uint16_t tile = e2 & 0x03FFu;
                uint16_t pal = (e2 >> 10) & 7u;
                bool prio = (e2 & 0x2000u) != 0u;
                bool hflip = (e2 & 0x4000u) != 0u;
                bool vflip = (e2 & 0x8000u) != 0u;
                bool is_noise = (tile >= CHAMP_BG_NOISE_FIRST
                                 && tile < CHAMP_BG_NOISE_FIRST + CHAMP_BG_NOISE_COUNT);
                if (champ_bg_tile_pixels(half, tile) == NULL) {
                    char msg[96];
                    snprintf(msg, sizeof(msg), "champ: unexpected BG2 tile %u (0x%04X) in half %d",
                             (unsigned)tile, (unsigned)e2, half);
                    port_fatal(msg);
                }
                if (is_noise) {
                    if (pal != 3u || prio) {
                        char msg[96];
                        snprintf(msg, sizeof(msg),
                                 "champ: unexpected BG2 noise entry 0x%04X in half %d",
                                 (unsigned)e2, half);
                        port_fatal(msg);
                    }
                } else {
                    bool ok_vflip = !vflip
                        || (pal == 1u && !prio && !hflip && tile >= 32u && tile <= 204u);
                    if (tile == 0u || tile > 255u || pal > 1u || !ok_vflip) {
                        char msg[96];
                        snprintf(msg, sizeof(msg), "champ: unexpected BG2 entry 0x%04X in half %d",
                                 (unsigned)e2, half);
                        port_fatal(msg);
                    }
                }
            }
        }
    }
}

static void draw_half_bg2(int half, const champ_half_pieces* hp, uint32_t* fb,
                          const uint32_t pal_rgba[256], const int16_t bg2_vofs[SCREEN_HEIGHT],
                          int y_start, int y_end, bool want_prio) {
    for (int y = y_start; y < y_end; ++y) {
        int my = (y + (int)bg2_vofs[y] + 1) & 255;
        int row = my >> 3;
        int py = my & 7;
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];

        for (int col = 0; col < 32; ++col) {
            uint16_t entry = champ_bg2_entry(hp, row, col);
            if (entry == 0u) {
                continue;
            }
            bool prio = (entry & 0x2000u) != 0u;
            if (prio != want_prio) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            bool hflip = (entry & 0x4000u) != 0u;
            bool vflip = (entry & 0x8000u) != 0u;
            const uint8_t* pixels = champ_bg_tile_pixels(half, tile);
            int ty = vflip ? (7 - py) : py;
            for (int px = 0; px < 8; ++px) {
                int tx = hflip ? (7 - px) : px;
                uint8_t p = pixels[ty * 8 + tx];
                if (p != 0u) {
                    dst_row[col * 8 + px] = pal_rgba[pal * 16u + p];
                }
            }
        }
    }
}

static void draw_half_bg1(int half, const champ_half_pieces* hp, uint32_t* fb,
                          const uint32_t pal_rgba[256], int y_start, int y_end) {
    uint16_t dp = (half == 0) ? 0x0100u : 0x0200u;
    int vofs = (int)(int16_t)sent16(half, (uint16_t)(dp + 0x9Eu)) - (half == 0 ? 0 : 0x74);
    for (int y = y_start; y < y_end; ++y) {
        int my = (y + vofs + 1) & 255;
        int row = my >> 3;
        int py = my & 7;
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];

        for (int col = 0; col < 32; ++col) {
            uint16_t entry = champ_bg1_entry(hp, row, col);
            if (entry == 0u) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            bool hflip = (entry & 0x4000u) != 0u;
            const uint8_t* pixels = champ_bg_tile_pixels(half, tile);
            for (int px = 0; px < 8; ++px) {
                int tx = hflip ? (7 - px) : px;
                uint8_t p = pixels[py * 8 + tx];
                if (p != 0u) {
                    dst_row[col * 8 + px] = pal_rgba[pal * 16u + p];
                }
            }
        }
    }
}

static void draw_half_objects(int half, uint32_t* fb, const uint32_t pal_rgba[256],
                              int y_start, int y_end, bool want_front) {
    uint16_t dp = (half == 0) ? 0x0100u : 0x0200u;
    uint16_t oam_low = g_sent[half].oam_at;
    (void)dp;
    if (half == 0) {
        if (oam_low != 0x0900u && oam_low != 0x0D00u) {
            port_fatal("champ: unexpected top OAM pointer at $016E");
        }
    } else {
        if (oam_low != 0x1100u && oam_low != 0x1500u) {
            port_fatal("champ: unexpected bottom OAM pointer at $026E");
        }
    }

    for (int i = (int)CHAMP_SPRITE_COUNT - 1; i >= 0; --i) {
        unsigned lo_off = (unsigned)i * 4u;
        uint8_t x_lo = sent_oam8(half, lo_off);
        uint8_t y_raw = sent_oam8(half, lo_off + 1u);
        if (y_raw >= SCREEN_HEIGHT) {
            continue;
        }
        uint8_t tile_lo = sent_oam8(half, lo_off + 2u);
        uint8_t attr = sent_oam8(half, lo_off + 3u);
        uint8_t hi = (uint8_t)((sent_oam8(half, 0x200u + ((unsigned)i >> 2))
                                >> (((unsigned)i & 3u) * 2u)) & 3u);
        bool is_large = (hi & 2u) != 0u;
        uint16_t tile = (uint16_t)(tile_lo | ((uint16_t)(attr & 1u) << 8));
        uint16_t pal = (attr >> 1) & 7u;
        uint16_t prio = (attr >> 4) & 3u;
        bool hflip = (attr & 0x40u) != 0u;
        bool vflip = (attr & 0x80u) != 0u;

        bool tiles_ok = false;
        if (!is_large) {
            tiles_ok = (champ_obj_tile_pixels(half, tile) != NULL);
        } else {
            tiles_ok = ((tile & 0x000Fu) < 0x000Fu)
                && (champ_obj_tile_pixels(half, tile) != NULL)
                && (champ_obj_tile_pixels(half, (uint16_t)(tile + 1u)) != NULL)
                && (champ_obj_tile_pixels(half, (uint16_t)(tile + 16u)) != NULL)
                && (champ_obj_tile_pixels(half, (uint16_t)(tile + 17u)) != NULL);
        }
        if (!tiles_ok || pal > 2u || (prio != 0u && prio != 2u && prio != 3u)) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     "champ: unexpected OAM sprite %d in half %d (tile %u attr 0x%02X hi %u)",
                     i, half, (unsigned)tile, (unsigned)attr, (unsigned)hi);
            port_fatal(msg);
        }

        bool is_front = (prio >= 2u);
        if (is_front != want_front) {
            continue;
        }

        int x9 = (int)(x_lo | ((uint16_t)(hi & 1u) << 8));
        int ox = (x9 >= 256) ? (x9 - 512) : x9;
        int oy = (int)y_raw;
        int dim = is_large ? 16 : 8;

        for (int py = 0; py < dim; ++py) {
            int sy = oy + py;
            if (sy < y_start || sy >= y_end) {
                continue;
            }
            int fy = vflip ? (dim - 1 - py) : py;
            for (int px = 0; px < dim; ++px) {
                int sx = ox + px;
                if (sx < 0 || sx >= SCREEN_WIDTH) {
                    continue;
                }
                int fx = hflip ? (dim - 1 - px) : px;
                uint16_t sub_tile = (uint16_t)(tile + (uint16_t)((fy >> 3) * 16) + (uint16_t)(fx >> 3));
                const uint8_t* pixels = champ_obj_tile_pixels(half, sub_tile);
                uint8_t p = pixels[(fy & 7) * 8 + (fx & 7)];
                if (p != 0u) {
                    fb[sy * SCREEN_WIDTH + sx] = pal_rgba[128u + pal * 16u + p];
                }
            }
        }
    }
}

static void apply_half_brightness(uint32_t* fb, int y_start, int y_end, uint8_t b) {
    screen_apply_brightness_lines(fb, y_start, y_end, b);
}

bool champ_screen_active(void) {
    return rd16(0x0008u) == 0xB97Bu && port_split_active() && assets_champ_loaded();
}

void champ_screen_draw(uint32_t* fb) {
    uint8_t b_top = g_sent[0].taken ? screen_decode_brightness("champ", sent8(0, 0x019Bu)) : 0u;
    uint8_t b_bot = g_sent[1].taken ? screen_decode_brightness("champ", sent8(1, 0x029Bu)) : 0u;
    if (b_top == 0u && b_bot == 0u) {
        screen_apply_brightness(fb, 0u);
        return;
    }

    champ_build_once();

    int16_t bg2_vofs[SCREEN_HEIGHT];
    decode_champ_bg2_vofs_table(bg2_vofs);

    /* Lines 108..115 (the 8-line split gap at $1B09..$1B0B during irq_menu_frame's forced blank)
       are always black. */
    for (int i = CHAMP_TOP_END_LINE * SCREEN_WIDTH; i < CHAMP_BOT_START_LINE * SCREEN_WIDTH; ++i) {
        fb[i] = 0xFF000000u;
    }

    for (int half = 0; half < CHAMP_HALF_COUNT; ++half) {
        int y_start = (half == 0) ? 0 : CHAMP_BOT_START_LINE;
        int y_end = (half == 0) ? CHAMP_TOP_END_LINE : SCREEN_HEIGHT;
        uint8_t b = (half == 0) ? b_top : b_bot;
        if (b == 0u) {
            apply_half_brightness(fb, y_start, y_end, 0u);
            continue;
        }

        validate_half_registers(half);

        const champ_half_pieces* hp = assets_champ_half(half);
        if (hp == NULL) {
            port_fatal("champ: missing captured pieces");
        }
        validate_half_maps(half, hp);

        uint16_t pal_base = (half == 0) ? 0x0500u : 0x0700u;
        uint16_t w102 = sent_colour(half, 0x81u);
        uint16_t w104 = sent_colour(half, 0x82u);
        uint16_t w106 = sent_colour(half, 0x83u);
        uint16_t w108 = sent_colour(half, 0x84u);
        bool pal_ok = (sent_colour(half, 0u) == 0u && w108 == 0x0000u)
            && ((w102 == 0x0E03u && w104 == 0x0040u && w106 == 0x109Fu)
                || (w102 == 0x0FFFu && w104 == 0x018Cu && w106 == 0x109Fu)
                || (w102 == 0x0FFFu && w104 == 0x0042u && w106 == 0x14BFu));
        if (!pal_ok) {
            char msg[96];
            snprintf(msg, sizeof(msg), "champ: unexpected palette block at $%04X in half %d",
                     (unsigned)pal_base, half);
            port_fatal(msg);
        }

        uint32_t pal_rgba[256];
        for (unsigned i = 0; i < 256u; ++i) {
            pal_rgba[i] = convert_color(sent_colour(half, i));
        }

        uint16_t dp = (half == 0) ? 0x0100u : 0x0200u;
        bool bg1_enabled = (sent8(half, (uint16_t)(dp + 0xBCu)) == 0x13u);

        /* 1. Backdrop */
        uint32_t backdrop = pal_rgba[0];
        for (int i = y_start * SCREEN_WIDTH; i < y_end * SCREEN_WIDTH; ++i) {
            fb[i] = backdrop;
        }

        /* 2. BG2 priority-0 tiles (per-line vertical offset from $1B00) */
        draw_half_bg2(half, hp, fb, pal_rgba, bg2_vofs, y_start, y_end, false);

        /* 3. BG1 priority-0 tiles (fire band or control diagram, when BG1 is enabled in TM) */
        if (bg1_enabled) {
            draw_half_bg1(half, hp, fb, pal_rgba, y_start, y_end);
        }

        /* 4. Priority-0 objects (behind BG2 priority 1, clipped to this half's band) */
        draw_half_objects(half, fb, pal_rgba, y_start, y_end, false);

        /* 5. BG2 priority-1 tiles (per-line vertical offset from $1B00) */
        draw_half_bg2(half, hp, fb, pal_rgba, bg2_vofs, y_start, y_end, true);

        /* 6. Priority-2 and priority-3 objects (in front of BG2 priority 1, clipped to band) */
        draw_half_objects(half, fb, pal_rgba, y_start, y_end, true);

        /* 7. Half brightness */
        apply_half_brightness(fb, y_start, y_end, b);
    }
}
