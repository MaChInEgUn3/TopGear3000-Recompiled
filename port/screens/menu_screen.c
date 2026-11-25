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

#define BG_TILE_COUNT 205u
#define BG_ARR_ENTRIES 864u
#define MAX_TEXT_TILES 186u
#define MAX_OBJ_TILES 28u

typedef struct {
    const char* name;
    int roles[ROLE_COUNT];      /* required asset id per role, or -1 if role must be empty */
    int bg_shift_x;             /* horizontal picture shift in pixels */
    int bg_shift_y;             /* vertical picture shift in lines */
    uint16_t text_first_tile;   /* first tile number of the text drawings block */
    uint16_t text_tile_count;   /* number of 4bpp tiles in the text drawings block */
    uint16_t text_blank_entry;  /* blank tilemap entry value at $7F:0000 */
    uint8_t text_pal_mask;      /* bitmask of allowed palettes (bits 0..7) */
    bool text_require_in_front; /* whether bit 13 (in-front) must be set */
    bool text_flips_allowed;    /* whether h-flip (bit 14) / v-flip (bit 15) are allowed */
    bool text_per_line_effects; /* menus' band tables: per-band centring ($0D00) and glow ($0D20) */
    bool has_objects;           /* whether objects are used on this screen */
    uint16_t obj_first_tile;    /* first tile number of the object drawings block */
    uint16_t obj_tile_count;    /* number of 4bpp tiles in the object drawings block */
    size_t obj_expected_bytes;  /* expected raw asset size in bytes */
    uint16_t obj_priority;      /* required object priority (0..3) */
    uint8_t obj_pal_mask;       /* bitmask of allowed object palettes (bits 0..7) */
    uint16_t obj_used_word;     /* expected non-zero used word at $0500+x */
    bool obj_require_small;     /* true = 8x8 (bit 15 of $0500+x set), false = 16x16 (bit 15 clear) */
    bool obj_flips_allowed;     /* whether object h/v flips are allowed */
} menu_screen_desc;

/* Per-screen table of measured game facts (docs/DECISIONS.md, "The title card drawn natively",
   "The password display drawn natively", "The options menu drawn natively"; each value checked against
   the original's picture). */
static const menu_screen_desc menu_screen_table[] = {
    {
        .name = "title_card",
        /* measured on the title card (docs/DECISIONS.md, "The title card drawn natively")-C: $96:EE52, $85:FA8B, $87:F861, $87:F5BC */
        .roles = {
            [ROLE_BG_DRAWINGS] = ASSET_MENU_BG_DRAWINGS,
            [ROLE_BG_ARRANGEMENT] = ASSET_MENU_BG_ARRANGEMENT,
            [ROLE_TEXT_DRAWINGS_128] = ASSET_TITLE_TEXT_DRAWINGS,
            [ROLE_TEXT_DRAWINGS_256] = -1,
            [ROLE_OBJ_DRAWINGS] = ASSET_TITLE_OBJ_DRAWINGS,
        },
        /* Setup $8A:C4B0 HOFS -4, title_card_show VOFS -65 (-1 sits at line 0) */
        .bg_shift_x = 4,
        .bg_shift_y = 64,
        /* measured on the title card (docs/DECISIONS.md, "The title card drawn natively"): $87:F861 (39 tiles) at tile 128, blank $003D, prio=1, flips=0 */
        .text_first_tile = 128u,
        .text_tile_count = 39u,
        .text_blank_entry = 0x003Du,
        .text_pal_mask = (uint8_t)((1u << 6) | (1u << 7)),
        .text_require_in_front = true,
        .text_flips_allowed = false,
        .text_per_line_effects = false,
        /* measured on the title card (docs/DECISIONS.md, "The title card drawn natively"): $87:F5BC (28 tiles) at tile 0, 16x16, priority 2, palette 0 */
        .has_objects = true,
        .obj_first_tile = 0u,
        .obj_tile_count = 28u,
        .obj_expected_bytes = 28u * 32u,
        .obj_priority = 2u,
        .obj_pal_mask = (uint8_t)(1u << 0),
        .obj_used_word = 0x0001u,
        .obj_require_small = false,
        .obj_flips_allowed = false,
    },
    {
        .name = "password_show",
        /* measured on the password display (docs/DECISIONS.md, "The password display drawn natively")-D: $96:EE52, $85:FA8B, $88:8601, no objects */
        .roles = {
            [ROLE_BG_DRAWINGS] = ASSET_MENU_BG_DRAWINGS,
            [ROLE_BG_ARRANGEMENT] = ASSET_MENU_BG_ARRANGEMENT,
            [ROLE_TEXT_DRAWINGS_128] = -1,
            [ROLE_TEXT_DRAWINGS_256] = ASSET_PASSWORD_TEXT_DRAWINGS,
            [ROLE_OBJ_DRAWINGS] = -1,
        },
        /* Setup $8A:C4B0 BG1 HOFS 0xFFFC (-4), VOFS 0xFFFF (-1 sits at line 0) */
        .bg_shift_x = 4,
        .bg_shift_y = 0,
        /* measured on the password display (docs/DECISIONS.md, "The password display drawn natively"): $88:8601 (186 tiles) at tile 256, blank $003D, prio=1, h/v-flip used */
        .text_first_tile = 256u,
        .text_tile_count = 186u,
        .text_blank_entry = 0x003Du,
        .text_pal_mask = (uint8_t)((1u << 6) | (1u << 7)),
        .text_require_in_front = true,
        .text_flips_allowed = true,
        .text_per_line_effects = false,
        /* measured on the password display (docs/DECISIONS.md, "The password display drawn natively"): 0 used object slots */
        .has_objects = false,
        .obj_first_tile = 0u,
        .obj_tile_count = 0u,
        .obj_expected_bytes = 0u,
        .obj_priority = 0u,
        .obj_pal_mask = 0u,
        .obj_used_word = 0u,
        .obj_require_small = false,
        .obj_flips_allowed = false,
    },
    {
        .name = "options",
        /* measured on the options menu (docs/DECISIONS.md): $96:EE52, $85:FA8B, $88:FEC3 (A), $88:F87F (B), $8A:C8E4 (cursor word run) */
        .roles = {
            [ROLE_BG_DRAWINGS] = ASSET_MENU_BG_DRAWINGS,
            [ROLE_BG_ARRANGEMENT] = ASSET_MENU_BG_ARRANGEMENT,
            [ROLE_TEXT_DRAWINGS_128] = ASSET_MENU_TEXT_A,
            [ROLE_TEXT_DRAWINGS_256] = ASSET_MENU_TEXT_B,
            [ROLE_OBJ_DRAWINGS] = ASSET_MENU_CURSOR_DRAWINGS,
        },
        /* Setup $8A:C4B0 BG1 HOFS 0xFFFC (-4), VOFS 0xFFFF (-1 sits at line 0) */
        .bg_shift_x = 4,
        .bg_shift_y = 0,
        /* measured on the options menu (docs/DECISIONS.md): $88:F87F (88 tiles) at tile 256..343, blank $003D, pal=7, prio=1, flips=0 */
        .text_first_tile = 256u,
        .text_tile_count = 88u,
        .text_blank_entry = 0x003Du,
        .text_pal_mask = (uint8_t)(1u << 7),
        .text_require_in_front = true,
        .text_flips_allowed = false,
        .text_per_line_effects = true,
        /* measured on the options menu (docs/DECISIONS.md): 97 words at $8A:C8E4 (tiles 0..5), 8x8 (size flag set), priority 3, palette 0, h/v-flip used */
        .has_objects = true,
        .obj_first_tile = 0u,
        .obj_tile_count = 6u,
        .obj_expected_bytes = 97u * 2u,
        .obj_priority = 3u,
        .obj_pal_mask = (uint8_t)(1u << 0),
        .obj_used_word = 0xFFFFu,
        .obj_require_small = true,
        .obj_flips_allowed = true,
    },
    {
        .name = "password_entry",
        /* measured on password_entry (docs/DECISIONS.md, "The password entry drawn natively"): $96:EE52, $85:FA8B, $88:FEC3 (A, inherited), $88:8601 (256..441), $8A:C8E4 (cursor word run) */
        .roles = {
            [ROLE_BG_DRAWINGS] = ASSET_MENU_BG_DRAWINGS,
            [ROLE_BG_ARRANGEMENT] = ASSET_MENU_BG_ARRANGEMENT,
            [ROLE_TEXT_DRAWINGS_128] = ASSET_MENU_TEXT_A,
            [ROLE_TEXT_DRAWINGS_256] = ASSET_PASSWORD_TEXT_DRAWINGS,
            [ROLE_OBJ_DRAWINGS] = ASSET_MENU_CURSOR_DRAWINGS,
        },
        /* Setup $8A:C4B0 BG1 HOFS 0xFFFC (-4), VOFS 0xFFFF (-1 sits at line 0) */
        .bg_shift_x = 4,
        .bg_shift_y = 0,
        /* measured on password_entry (docs/DECISIONS.md, "The password entry drawn natively"): $88:8601 (186 tiles, 256..441; 266..434 used), blank $003D, pal 6 & 7, prio=1, flips allowed */
        .text_first_tile = 256u,
        .text_tile_count = 186u,
        .text_blank_entry = 0x003Du,
        .text_pal_mask = (uint8_t)((1u << 6) | (1u << 7)),
        .text_require_in_front = true,
        .text_flips_allowed = true,
        .text_per_line_effects = true,
        /* measured on password_entry (docs/DECISIONS.md, "The password entry drawn natively"): 97 words at $8A:C8E4 (tiles 0..2 used), 8x8 (size flag set), priority 3, palette 0, h/v-flip used */
        .has_objects = true,
        .obj_first_tile = 0u,
        .obj_tile_count = 3u,
        .obj_expected_bytes = 97u * 2u,
        .obj_priority = 3u,
        .obj_pal_mask = (uint8_t)(1u << 0),
        .obj_used_word = 0xFFFFu,
        .obj_require_small = true,
        .obj_flips_allowed = true,
    },
};

#define MENU_SCREEN_COUNT (sizeof(menu_screen_table) / sizeof(menu_screen_table[0]))

typedef struct {
    bool built;
    uint32_t text_gen;
    uint32_t obj_gen;
    uint8_t text_tiles[MAX_TEXT_TILES * 64u];
    uint8_t obj_tiles[MAX_OBJ_TILES * 64u];
} menu_screen_cache;

static bool g_bg_built = false;
static uint32_t g_bg_draw_gen = 0;
static uint32_t g_bg_arr_gen = 0;
static uint8_t g_bg_image[256 * 256];
static menu_screen_cache g_cache[MENU_SCREEN_COUNT];

/* VS screen measured facts (notes/probe/VS.md, docs/DECISIONS.md "The VS screen drawn natively"):
   - BG1: $8D:F696 (134 4bpp tiles), arrangement $8A:C86C (896 words, fill $0023), palettes 0..3,
     HOFS -4 / VOFS -4 (-1 sits at line 0 -> shift x=4, y=3).
   - Text (BG2): MENU_TEXT_A ($88:FEC3) at tile 128, blank entry $003D, blank tile 189,
     used tiles 128..157, palette 6, in-front (prio 1), flips=0.
   - Objects: 8x8 (bit 15 set), prio 3; cursor tiles 0..5 (MENU_CURSOR_DRAWINGS, pal 0, h/v-flip
     allowed) and text tiles 128..157 (MENU_TEXT_A, pal 7, flips=0). */
#define VS_BG_TILE_COUNT 134u
#define VS_BG_ARR_ENTRIES 896u
#define VS_BG_FILL_ENTRY 0x0023u
#define VS_BG_MAX_PAL 3u
#define VS_BG_SHIFT_X 4
#define VS_BG_SHIFT_Y 3
#define VS_TEXT_FIRST_TILE 128u
#define VS_TEXT_TILE_COUNT 30u
#define VS_TEXT_MIN_TILE 128u
#define VS_TEXT_MAX_TILE 157u
#define VS_TEXT_BLANK_ENTRY 0x003Du
#define VS_TEXT_BLANK_TILE 189u
#define VS_TEXT_PAL 6u
#define VS_TEXT_FLIPS_ALLOWED false
#define VS_CURSOR_TILE_COUNT 6u
#define VS_OBJ_USED_WORD 0xFFFFu
#define VS_OBJ_PRIORITY 3u
#define VS_OBJ_CURSOR_MAX_TILE 5u
#define VS_OBJ_CURSOR_PAL 0u
#define VS_OBJ_CURSOR_FLIPS_ALLOWED true
#define VS_OBJ_TEXT_MIN_TILE 128u
#define VS_OBJ_TEXT_MAX_TILE 157u
#define VS_OBJ_TEXT_PAL 7u
#define VS_OBJ_TEXT_FLIPS_ALLOWED false

static bool g_vs_built = false;
static uint32_t g_vs_bg_draw_gen = 0;
static uint32_t g_vs_arr_gen = 0;
static uint32_t g_vs_text_gen = 0;
static uint32_t g_vs_obj_gen = 0;
static uint8_t g_vs_bg_image[256 * 256];
static uint8_t g_vs_text_tiles[MAX_TEXT_TILES * 64u];
static uint8_t g_vs_cursor_tiles[VS_CURSOR_TILE_COUNT * 64u];

static void menu_bg_build_once(void) {
    uint32_t draw_gen = asset_generation(ASSET_MENU_BG_DRAWINGS);
    uint32_t arr_gen = asset_generation(ASSET_MENU_BG_ARRANGEMENT);
    if (g_bg_built && g_bg_draw_gen == draw_gen && g_bg_arr_gen == arr_gen) {
        return;
    }

    size_t bg_draw_size = 0;
    const uint8_t* bg_draw = asset_bytes(ASSET_MENU_BG_DRAWINGS, &bg_draw_size);
    if (bg_draw == NULL || bg_draw_size != BG_TILE_COUNT * 64u) {
        port_fatal("menu screen: unexpected MENU_BG_DRAWINGS size");
    }

    size_t bg_arr_size = 0;
    const uint8_t* bg_arr = asset_bytes(ASSET_MENU_BG_ARRANGEMENT, &bg_arr_size);
    if (bg_arr == NULL || bg_arr_size != BG_ARR_ENTRIES * 2u) {
        port_fatal("menu screen: unexpected MENU_BG_ARRANGEMENT size");
    }

    uint8_t bg_tiles[BG_TILE_COUNT * 64u];
    convert_tiles_8bpp(bg_draw, BG_TILE_COUNT, bg_tiles);

    for (unsigned tr = 0; tr < 32u; ++tr) {
        for (unsigned tc = 0; tc < 32u; ++tc) {
            uint16_t tile = 0;
            if (tr < 27u) {
                size_t idx = (size_t)(tr * 32u + tc) * 2u;
                uint16_t entry = (uint16_t)(bg_arr[idx] | ((uint16_t)bg_arr[idx + 1u] << 8));
                tile = entry & 0x03FFu;
                if (tile >= BG_TILE_COUNT || (entry & 0xFC00u) != 0u) {
                    port_fatal("menu screen: unexpected background entry");
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

    g_bg_draw_gen = draw_gen;
    g_bg_arr_gen = arr_gen;
    g_bg_built = true;
}

static void menu_screen_build_once(int which) {
    menu_bg_build_once();

    const menu_screen_desc* s = &menu_screen_table[which];
    if (s->text_first_tile != 128u && s->text_first_tile != 256u) {
        port_fatal("menu screen: unexpected text_first_tile");
    }
    asset_role text_role =
        (s->text_first_tile == 128u) ? ROLE_TEXT_DRAWINGS_128 : ROLE_TEXT_DRAWINGS_256;
    uint32_t text_gen = asset_generation(s->roles[text_role]);
    uint32_t obj_gen = s->has_objects ? asset_generation(s->roles[ROLE_OBJ_DRAWINGS]) : 0u;

    menu_screen_cache* cache = &g_cache[which];
    if (cache->built && cache->text_gen == text_gen
        && (!s->has_objects || cache->obj_gen == obj_gen)) {
        return;
    }

    if (s->text_tile_count > MAX_TEXT_TILES || s->obj_tile_count > MAX_OBJ_TILES) {
        port_fatal("menu screen: tile count exceeds cache capacity");
    }

    size_t text_size = 0;
    const uint8_t* text_raw = asset_bytes(s->roles[text_role], &text_size);
    if (text_raw == NULL || text_size != (size_t)s->text_tile_count * 32u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: unexpected TEXT_DRAWINGS size", s->name);
        port_fatal(msg);
    }
    convert_tiles_4bpp(text_raw, s->text_tile_count, cache->text_tiles);

    if (s->has_objects) {
        if (s->obj_expected_bytes < (size_t)s->obj_tile_count * 32u) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: obj_expected_bytes smaller than obj_tile_count * 32",
                     s->name);
            port_fatal(msg);
        }
        size_t obj_size = 0;
        const uint8_t* obj_raw = asset_bytes(s->roles[ROLE_OBJ_DRAWINGS], &obj_size);
        if (obj_raw == NULL || obj_size != s->obj_expected_bytes) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: unexpected OBJ_DRAWINGS size", s->name);
            port_fatal(msg);
        }
        convert_tiles_4bpp(obj_raw, s->obj_tile_count, cache->obj_tiles);
    }

    cache->text_gen = text_gen;
    cache->obj_gen = obj_gen;
    cache->built = true;
}

static void draw_obj_tile(const menu_screen_desc* s, const menu_screen_cache* cache,
                          uint32_t* fb, const uint32_t pal_rgba[256], uint16_t tile,
                          uint16_t pal, int ox, int oy, bool hflip, bool vflip) {
    if (tile < s->obj_first_tile || tile >= s->obj_first_tile + s->obj_tile_count) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: object tile %u out of range", s->name, (unsigned)tile);
        port_fatal(msg);
    }
    const uint8_t* pixels = &cache->obj_tiles[(size_t)(tile - s->obj_first_tile) * 64u];
    for (int py = 0; py < 8; ++py) {
        int sy = oy + py;
        if (sy < 0 || sy >= SCREEN_HEIGHT) {
            continue;
        }
        int ty = vflip ? (7 - py) : py;
        for (int px = 0; px < 8; ++px) {
            int sx = ox + px;
            if (sx < 0 || sx >= SCREEN_WIDTH) {
                continue;
            }
            int tx = hflip ? (7 - px) : px;
            uint8_t p = pixels[ty * 8 + tx];
            if (p != 0) {
                fb[sy * SCREEN_WIDTH + sx] = pal_rgba[128u + pal * 16u + p];
            }
        }
    }
}

static void validate_text_entry(const menu_screen_desc* s, uint16_t entry) {
    uint16_t tile = entry & 0x03FFu;
    uint16_t pal = (entry >> 10) & 7u;
    bool prio = (entry & 0x2000u) != 0;
    bool hflip = (entry & 0x4000u) != 0;
    bool vflip = (entry & 0x8000u) != 0;
    if (tile < s->text_first_tile
        || tile >= s->text_first_tile + s->text_tile_count
        || ((s->text_pal_mask & (1u << pal)) == 0u)
        || (s->text_require_in_front && !prio)
        || (!s->text_flips_allowed && (hflip || vflip))) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: unexpected text entry 0x%04X", s->name, (unsigned)entry);
        port_fatal(msg);
    }
}

/* Menus' band-table per-line text effects (options and password_entry; docs/DECISIONS.md):
   (a) "menu item centring": WRAM $0D00 has 76 lines ($4C) with 16-bit horizontal offset at $0D01,
       then 7 bands of 24 lines ($18 at $0D03+3k) with 16-bit offsets at $0D04+3k, terminated by
       $0D18 == 0. Positive offset shifts the text layer LEFT by that many pixels.
   (b) "menu item glow": WRAM $0D20 has 76 lines ($4C) with byte at $0D21, then 7 bands of 24
       lines ($18 at $0D22+2k) with bytes at $0D23+2k, terminated by $0D30 == 0. Each byte must
       have either no plane bits (0x00..0x1F: no change) or all three ($E0 | n), adding n to each
       5-bit R, G, B channel of every text pixel on those lines (clamped at 31) before 5->8 bit
       expansion and before brightness. Background and objects are unchanged. */
static void draw_menu_text_per_line(const menu_screen_desc* s, const menu_screen_cache* cache,
                                     uint32_t* fb) {
    int band_hofs[8];
    uint8_t band_glow[8];

    if (rd8(0x0D00u) != 0x4Cu || rd8(0x0D18u) != 0x00u) {
        port_fatal("options: unexpected centring table header/terminator at $0D00");
    }
    band_hofs[0] = (int)(int16_t)rd16(0x0D01u);
    if (band_hofs[0] != 0 && band_hofs[0] != 4) {
        port_fatal("options: unexpected centring offset at $0D00");
    }
    for (int k = 0; k < 7; ++k) {
        if (rd8(0x0D03u + (uint32_t)k * 3u) != 0x18u) {
            port_fatal("options: unexpected centring band count at $0D00");
        }
        band_hofs[k + 1] = (int)(int16_t)rd16(0x0D04u + (uint32_t)k * 3u);
        if (band_hofs[k + 1] != 0 && band_hofs[k + 1] != 4) {
            port_fatal("options: unexpected centring offset at $0D00");
        }
    }

    if (rd8(0x0D20u) != 0x4Cu || rd8(0x0D30u) != 0x00u) {
        port_fatal("options: unexpected glow table header/terminator at $0D20");
    }
    for (int b_idx = 0; b_idx < 8; ++b_idx) {
        uint8_t v;
        if (b_idx == 0) {
            v = rd8(0x0D21u);
        } else {
            int k = b_idx - 1;
            if (rd8(0x0D22u + (uint32_t)k * 2u) != 0x18u) {
                port_fatal("options: unexpected glow band count at $0D20");
            }
            v = rd8(0x0D23u + (uint32_t)k * 2u);
        }
        uint8_t planes = v & 0xE0u;
        uint8_t amount = v & 0x1Fu;
        if (amount > 15u) {
            port_fatal("options: unexpected glow amount at $0D20");
        }
        if (planes == 0x00u) {
            band_glow[b_idx] = 0u;
        } else if (planes == 0xE0u) {
            band_glow[b_idx] = amount;
        } else {
            port_fatal("options: unexpected glow plane bits at $0D20");
        }
    }

    for (int row = 0; row < 28; ++row) {
        for (int col = 0; col < 32; ++col) {
            uint16_t entry = rd16((uint32_t)(0x10000 + 2 * (row * 32 + col)));
            if (entry != s->text_blank_entry) {
                validate_text_entry(s, entry);
            }
        }
    }

    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        int band = (y < 76) ? 0 : (1 + (y - 76) / 24);
        int hofs = band_hofs[band];
        uint8_t glow = band_glow[band];
        int row = y / 8;
        int py = y & 7;
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];

        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            unsigned mx = (unsigned)(x + hofs) & 255u;
            int col = (int)(mx / 8u);
            int px = (int)(mx & 7u);
            uint16_t entry = rd16((uint32_t)(0x10000 + 2 * (row * 32 + col)));
            if (entry == s->text_blank_entry) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            bool hflip = (entry & 0x4000u) != 0;
            bool vflip = (entry & 0x8000u) != 0;
            const uint8_t* pixels =
                &cache->text_tiles[(size_t)(tile - s->text_first_tile) * 64u];
            int ty = vflip ? (7 - py) : py;
            int tx = hflip ? (7 - px) : px;
            uint8_t p = pixels[ty * 8 + tx];
            if (p != 0) {
                uint16_t bgr = palette_get((uint8_t)(pal * 16u + p));
                if (glow != 0u) {
                    uint32_t r5 = (uint32_t)(bgr & 0x1Fu) + glow;
                    uint32_t g5 = (uint32_t)((bgr >> 5) & 0x1Fu) + glow;
                    uint32_t b5 = (uint32_t)((bgr >> 10) & 0x1Fu) + glow;
                    if (r5 > 31u) r5 = 31u;
                    if (g5 > 31u) g5 = 31u;
                    if (b5 > 31u) b5 = 31u;
                    bgr = (uint16_t)(r5 | (g5 << 5) | (b5 << 10));
                }
                dst_row[x] = convert_color(bgr);
            }
        }
    }
}

static void draw_tile_8x8(uint32_t* fb, const uint32_t pal_rgba[256], const uint8_t* pixels,
                          uint16_t pal, int ox, int oy, bool hflip, bool vflip) {
    for (int py = 0; py < 8; ++py) {
        int sy = oy + py;
        if (sy < 0 || sy >= SCREEN_HEIGHT) {
            continue;
        }
        int ty = vflip ? (7 - py) : py;
        for (int px = 0; px < 8; ++px) {
            int sx = ox + px;
            if (sx < 0 || sx >= SCREEN_WIDTH) {
                continue;
            }
            int tx = hflip ? (7 - px) : px;
            uint8_t p = pixels[ty * 8 + tx];
            if (p != 0) {
                fb[sy * SCREEN_WIDTH + sx] = pal_rgba[pal * 16u + p];
            }
        }
    }
}

static void vs_build_once(void) {
    uint32_t bg_draw_gen = asset_generation(ASSET_VS_BG_DRAWINGS);
    uint32_t arr_gen = asset_generation(ASSET_VS_PANELS_ARRANGEMENT);
    uint32_t text_gen = asset_generation(ASSET_MENU_TEXT_A);
    uint32_t obj_gen = asset_generation(ASSET_MENU_CURSOR_DRAWINGS);
    if (g_vs_built && g_vs_bg_draw_gen == bg_draw_gen && g_vs_arr_gen == arr_gen
        && g_vs_text_gen == text_gen && g_vs_obj_gen == obj_gen) {
        return;
    }

    size_t bg_draw_size = 0;
    const uint8_t* bg_draw = asset_bytes(ASSET_VS_BG_DRAWINGS, &bg_draw_size);
    if (bg_draw == NULL || bg_draw_size != VS_BG_TILE_COUNT * 32u) {
        port_fatal("vs: unexpected VS_BG_DRAWINGS size");
    }

    size_t bg_arr_size = 0;
    const uint8_t* bg_arr = asset_bytes(ASSET_VS_PANELS_ARRANGEMENT, &bg_arr_size);
    if (bg_arr == NULL || bg_arr_size != VS_BG_ARR_ENTRIES * 2u) {
        port_fatal("vs: unexpected VS_PANELS_ARRANGEMENT size");
    }

    uint8_t bg_tiles[VS_BG_TILE_COUNT * 64u];
    convert_tiles_4bpp(bg_draw, VS_BG_TILE_COUNT, bg_tiles);

    for (unsigned tr = 0; tr < 32u; ++tr) {
        for (unsigned tc = 0; tc < 32u; ++tc) {
            uint16_t entry = VS_BG_FILL_ENTRY;
            if (tr < 28u) {
                size_t idx = (size_t)(tr * 32u + tc) * 2u;
                entry = (uint16_t)(bg_arr[idx] | ((uint16_t)bg_arr[idx + 1u] << 8));
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            if (tile >= VS_BG_TILE_COUNT || pal > VS_BG_MAX_PAL || (entry & 0xE000u) != 0u) {
                port_fatal("vs: unexpected background entry");
            }
            const uint8_t* src_tile = &bg_tiles[(size_t)tile * 64u];
            for (unsigned py = 0; py < 8u; ++py) {
                uint8_t* dst_row = &g_vs_bg_image[(tr * 8u + py) * 256u + tc * 8u];
                for (unsigned px = 0; px < 8u; ++px) {
                    uint8_t p = src_tile[py * 8u + px];
                    dst_row[px] = (p != 0u) ? (uint8_t)(pal * 16u + p) : 0u;
                }
            }
        }
    }

    size_t text_size = 0;
    const uint8_t* text_raw = asset_bytes(ASSET_MENU_TEXT_A, &text_size);
    if (text_raw == NULL || text_size != (size_t)VS_TEXT_TILE_COUNT * 32u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "vs: unexpected MENU_TEXT_A size (%zu)", text_size);
        port_fatal(msg);
    }
    convert_tiles_4bpp(text_raw, VS_TEXT_TILE_COUNT, g_vs_text_tiles);

    size_t obj_size = 0;
    const uint8_t* obj_raw = asset_bytes(ASSET_MENU_CURSOR_DRAWINGS, &obj_size);
    if (obj_raw == NULL || obj_size != 97u * 2u) {
        port_fatal("vs: unexpected MENU_CURSOR_DRAWINGS size");
    }
    convert_tiles_4bpp(obj_raw, VS_CURSOR_TILE_COUNT, g_vs_cursor_tiles);

    g_vs_bg_draw_gen = bg_draw_gen;
    g_vs_arr_gen = arr_gen;
    g_vs_text_gen = text_gen;
    g_vs_obj_gen = obj_gen;
    g_vs_built = true;
}

static void vs_screen_draw(uint32_t* fb, const uint32_t pal_rgba[256]) {
    vs_build_once();

    /* 1. Backdrop */
    uint32_t backdrop = pal_rgba[0];
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = backdrop;
    }

    /* 2. Background picture (BG1 scrolls -4/-4 -> shift x 4, y 3) */
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        unsigned r = (unsigned)(y - VS_BG_SHIFT_Y) & 255u;
        const uint8_t* src_row = &g_vs_bg_image[r * 256u];
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            uint8_t c = src_row[(unsigned)(x - VS_BG_SHIFT_X) & 255u];
            if (c != 0u) {
                dst_row[x] = pal_rgba[c];
            }
        }
    }

    /* 3. Text map at WRAM $7F:0000 */
    for (int row = 0; row < 28; ++row) {
        for (int col = 0; col < 32; ++col) {
            uint16_t entry = rd16((uint32_t)(0x10000 + 2 * (row * 32 + col)));
            if (entry == VS_TEXT_BLANK_ENTRY) {
                continue;
            }
            uint16_t tile = entry & 0x03FFu;
            uint16_t pal = (entry >> 10) & 7u;
            bool prio = (entry & 0x2000u) != 0;
            bool flips = (entry & 0xC000u) != 0;
            if (pal != VS_TEXT_PAL || !prio || (!VS_TEXT_FLIPS_ALLOWED && flips)) {
                char msg[96];
                snprintf(msg, sizeof(msg), "vs: unexpected text entry 0x%04X", (unsigned)entry);
                port_fatal(msg);
            }
            if (tile == VS_TEXT_BLANK_TILE) {
                continue;
            }
            if (tile < VS_TEXT_MIN_TILE || tile > VS_TEXT_MAX_TILE) {
                char msg[96];
                snprintf(msg, sizeof(msg), "vs: unexpected text entry 0x%04X", (unsigned)entry);
                port_fatal(msg);
            }
            const uint8_t* pixels =
                &g_vs_text_tiles[(size_t)(tile - VS_TEXT_FIRST_TILE) * 64u];
            draw_tile_8x8(fb, pal_rgba, pixels, VS_TEXT_PAL, col * 8, row * 8, false, false);
        }
    }

    /* 4. Objects from $0500-$08FF (last to first so lower slot ends on top) */
    for (int x = 254; x >= 0; x -= 2) {
        uint16_t used = rd16((uint32_t)(0x0500 + x));
        if (used == 0u) {
            continue;
        }
        if (used != VS_OBJ_USED_WORD) {
            char msg[96];
            snprintf(msg, sizeof(msg), "vs: unexpected object used word 0x%04X", (unsigned)used);
            port_fatal(msg);
        }
        if ((used & 0x8000u) == 0u) {
            char msg[96];
            snprintf(msg, sizeof(msg), "vs: unexpected object size flag 0x%04X", (unsigned)used);
            port_fatal(msg);
        }
        uint16_t xraw = rd16((uint32_t)(0x0600 + x));
        if ((xraw & 0xFE00u) != 0u) {
            char msg[96];
            snprintf(msg, sizeof(msg), "vs: unexpected object X word 0x%04X", (unsigned)xraw);
            port_fatal(msg);
        }
        int ox = (int)(xraw & 0x01FFu);
        if (ox >= 256) {
            ox -= 512;
        }
        uint16_t yraw = rd16((uint32_t)(0x0700 + x));
        if ((yraw & 0x8000u) != 0u || yraw >= 0x00F0u) {
            continue;
        }
        int oy = (int)yraw;

        uint16_t w = rd16((uint32_t)(0x0800 + x));
        uint16_t tile = w & 0x01FFu;
        uint16_t pal = (w >> 9) & 7u;
        uint16_t prio = (w >> 12) & 3u;
        bool hflip = (w & 0x4000u) != 0u;
        bool vflip = (w & 0x8000u) != 0u;
        if (prio != VS_OBJ_PRIORITY) {
            char msg[96];
            snprintf(msg, sizeof(msg), "vs: unexpected object attr 0x%04X", (unsigned)w);
            port_fatal(msg);
        }
        if (tile <= VS_OBJ_CURSOR_MAX_TILE && pal == VS_OBJ_CURSOR_PAL
            && (VS_OBJ_CURSOR_FLIPS_ALLOWED || (!hflip && !vflip))) {
            draw_tile_8x8(fb, pal_rgba, &g_vs_cursor_tiles[(size_t)tile * 64u],
                          8u + VS_OBJ_CURSOR_PAL, ox, oy, hflip, vflip);
        } else if (tile >= VS_OBJ_TEXT_MIN_TILE && tile <= VS_OBJ_TEXT_MAX_TILE
                   && pal == VS_OBJ_TEXT_PAL
                   && (VS_OBJ_TEXT_FLIPS_ALLOWED || (!hflip && !vflip))) {
            draw_tile_8x8(fb, pal_rgba,
                          &g_vs_text_tiles[(size_t)(tile - VS_TEXT_FIRST_TILE) * 64u],
                          8u + VS_OBJ_TEXT_PAL, ox, oy, hflip, vflip);
        } else {
            char msg[96];
            snprintf(msg, sizeof(msg), "vs: unexpected object attr 0x%04X", (unsigned)w);
            port_fatal(msg);
        }
    }
}

int menu_screen_active(void) {
    for (size_t i = 0; i < MENU_SCREEN_COUNT; ++i) {
        bool match = true;
        for (int r = 0; r < ROLE_COUNT; ++r) {
            if (asset_current((asset_role)r) != menu_screen_table[i].roles[r]) {
                match = false;
                break;
            }
        }
        if (match) {
            return (int)i;
        }
    }
    int text_256 = asset_current(ROLE_TEXT_DRAWINGS_256);
    if (asset_current(ROLE_BG_DRAWINGS) == ASSET_VS_BG_DRAWINGS
        && asset_current(ROLE_BG_ARRANGEMENT) == ASSET_VS_PANELS_ARRANGEMENT
        && asset_current(ROLE_TEXT_DRAWINGS_128) == ASSET_MENU_TEXT_A
        && (text_256 == -1 || text_256 == ASSET_MENU_TEXT_B)
        && asset_current(ROLE_OBJ_DRAWINGS) == ASSET_MENU_CURSOR_DRAWINGS) {
        return (int)MENU_SCREEN_COUNT;
    }
    return -1;
}

const char* menu_screen_name(int which) {
    if (which == (int)MENU_SCREEN_COUNT) {
        return "vs";
    }
    if (which < 0 || (size_t)which >= MENU_SCREEN_COUNT) {
        return "none";
    }
    return menu_screen_table[which].name;
}

void menu_screen_draw(int which, uint32_t* fb) {
    if (which < 0 || (size_t)which > MENU_SCREEN_COUNT) {
        port_fatal("menu_screen_draw: invalid screen index");
    }

    /* Brightness 0 or bit 7 set is black (the game also uses $7F:0000 as scratch while faded out).
       Bits 4..6 are unmeasured and must be zero. */
    uint8_t b_raw = rd8(0x019Bu);
    if ((b_raw & 0x70u) != 0u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: unexpected brightness byte 0x%02X", menu_screen_name(which),
                 (unsigned)b_raw);
        port_fatal(msg);
    }
    uint8_t b = ((b_raw & 0x80u) != 0u) ? 0u : (b_raw & 0x0Fu);
    if (b == 0u) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            fb[i] = 0xFF000000u;
        }
        return;
    }

    uint32_t pal_rgba[256];
    for (unsigned i = 0; i < 256u; ++i) {
        pal_rgba[i] = convert_color(palette_get((uint8_t)i));
    }

    if (which == (int)MENU_SCREEN_COUNT) {
        vs_screen_draw(fb, pal_rgba);
        if (b < 15u) {
            for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
                uint32_t px = fb[i];
                uint32_t r = (((px >> 16) & 0xFFu) * b * 17u) >> 8;
                uint32_t g = (((px >> 8) & 0xFFu) * b * 17u) >> 8;
                uint32_t bl = ((px & 0xFFu) * b * 17u) >> 8;
                fb[i] = 0xFF000000u | (r << 16) | (g << 8) | bl;
            }
        }
        return;
    }

    menu_screen_build_once(which);
    const menu_screen_desc* s = &menu_screen_table[which];
    const menu_screen_cache* cache = &g_cache[which];

    /* 1. Backdrop */
    uint32_t backdrop = pal_rgba[0];
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
        fb[i] = backdrop;
    }

    /* 2. Background picture */
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        unsigned r = (unsigned)(y - s->bg_shift_y) & 255u;
        const uint8_t* src_row = &g_bg_image[r * 256u];
        uint32_t* dst_row = &fb[y * SCREEN_WIDTH];
        for (int x = 0; x < SCREEN_WIDTH; ++x) {
            uint8_t c = src_row[(unsigned)(x - s->bg_shift_x) & 255u];
            if (c != 0) {
                dst_row[x] = pal_rgba[c];
            }
        }
    }

    /* 3. Text map at WRAM $7F:0000 */
    if (s->text_per_line_effects) {
        draw_menu_text_per_line(s, cache, fb);
    } else {
        /* Screens without band effects (title_card, password_show) do not enable the menus'
           band tables; $0D00/$0D20 are not checked here because they may hold leftovers. */
        for (int row = 0; row < 28; ++row) {
            for (int col = 0; col < 32; ++col) {
                uint16_t entry = rd16((uint32_t)(0x10000 + 2 * (row * 32 + col)));
                if (entry == s->text_blank_entry) {
                    continue;
                }
                validate_text_entry(s, entry);
                uint16_t tile = entry & 0x03FFu;
                uint16_t pal = (entry >> 10) & 7u;
                bool hflip = (entry & 0x4000u) != 0;
                bool vflip = (entry & 0x8000u) != 0;

                const uint8_t* pixels =
                    &cache->text_tiles[(size_t)(tile - s->text_first_tile) * 64u];
                for (int py = 0; py < 8; ++py) {
                    int sy = row * 8 + py;
                    int ty = vflip ? (7 - py) : py;
                    for (int px = 0; px < 8; ++px) {
                        int sx = col * 8 + px;
                        int tx = hflip ? (7 - px) : px;
                        uint8_t p = pixels[ty * 8 + tx];
                        if (p != 0) {
                            fb[sy * SCREEN_WIDTH + sx] = pal_rgba[pal * 16u + p];
                        }
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
        if (!s->has_objects) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: unexpected used object slot %d", s->name, x / 2);
            port_fatal(msg);
        }
        if (used != s->obj_used_word) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: unexpected object used word 0x%04X", s->name,
                     (unsigned)used);
            port_fatal(msg);
        }
        bool is_small = (used & 0x8000u) != 0;
        if (is_small != s->obj_require_small) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: unexpected object size flag 0x%04X", s->name,
                     (unsigned)used);
            port_fatal(msg);
        }
        uint16_t xraw = rd16((uint32_t)(0x0600 + x));
        if ((xraw & 0xFE00u) != 0u) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: unexpected object X word 0x%04X", s->name,
                     (unsigned)xraw);
            port_fatal(msg);
        }
        int ox = (int)(xraw & 0x01FFu);
        if (ox >= 256) {
            ox -= 512;
        }
        uint16_t yraw = rd16((uint32_t)(0x0700 + x));
        if ((yraw & 0x8000u) != 0 || yraw >= 0x00F0u) {
            continue;
        }
        int oy = (int)yraw;

        uint16_t w = rd16((uint32_t)(0x0800 + x));
        uint16_t tile = w & 0x01FFu;
        uint16_t pal = (w >> 9) & 7u;
        uint16_t prio = (w >> 12) & 3u;
        bool hflip = (w & 0x4000u) != 0;
        bool vflip = (w & 0x8000u) != 0;
        if (prio != s->obj_priority
            || ((s->obj_pal_mask & (1u << pal)) == 0u)
            || (!s->obj_flips_allowed && (hflip || vflip))
            || (!is_small && (hflip || vflip))) {
            char msg[96];
            snprintf(msg, sizeof(msg), "%s: unexpected object attr 0x%04X", s->name, (unsigned)w);
            port_fatal(msg);
        }

        if (!is_small) {
            draw_obj_tile(s, cache, fb, pal_rgba, tile, pal, ox, oy, hflip, vflip);
            draw_obj_tile(s, cache, fb, pal_rgba, (uint16_t)(tile + 1u), pal, ox + 8, oy, hflip,
                          vflip);
            draw_obj_tile(s, cache, fb, pal_rgba, (uint16_t)(tile + 16u), pal, ox, oy + 8, hflip,
                          vflip);
            draw_obj_tile(s, cache, fb, pal_rgba, (uint16_t)(tile + 17u), pal, ox + 8, oy + 8,
                          hflip, vflip);
        } else {
            draw_obj_tile(s, cache, fb, pal_rgba, tile, pal, ox, oy, hflip, vflip);
        }
    }

    /* 5. Brightness (b == 0 handled above) */
    if (b < 15u) {
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
