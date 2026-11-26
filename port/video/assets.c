// SPDX-License-Identifier: GPL-3.0-or-later

#include "assets.h"

#include <stdlib.h>
#include <string.h>

#include "decompress.h"
#include "port.h"
#include "tg3000.h"

typedef enum {
    ASSET_KIND_TILES_8BPP,
    ASSET_KIND_TILES_4BPP,
    ASSET_KIND_MAP_16,
    ASSET_KIND_WORD_RUN,
    ASSET_KIND_CAPTURED_WORD_RUN,
    ASSET_KIND_TITLE_PACK
} asset_kind;

typedef struct {
    const char* name;
    uint32_t rom_bus;
    asset_kind kind;
    asset_role role;
    size_t word_count; /* non-zero only for WORD_RUN / CAPTURED_WORD_RUN */
    uint8_t* data;
    size_t size;
    uint32_t hash;
    uint32_t generation;
} asset_entry;

static asset_entry g_assets[ASSET_COUNT] = {
    [ASSET_MENU_BG_DRAWINGS] = {"MENU_BG_DRAWINGS", 0x96EE52u, ASSET_KIND_TILES_8BPP, ROLE_BG_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_MENU_BG_ARRANGEMENT] = {"MENU_BG_ARRANGEMENT", 0x85FA8Bu, ASSET_KIND_MAP_16, ROLE_BG_ARRANGEMENT, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_TEXT_DRAWINGS] = {"TITLE_TEXT_DRAWINGS", 0x87F861u, ASSET_KIND_TILES_4BPP, ROLE_TEXT_DRAWINGS_128, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_OBJ_DRAWINGS] = {"TITLE_OBJ_DRAWINGS", 0x87F5BCu, ASSET_KIND_TILES_4BPP, ROLE_OBJ_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_PASSWORD_TEXT_DRAWINGS] = {"PASSWORD_TEXT_DRAWINGS", 0x888601u, ASSET_KIND_TILES_4BPP, ROLE_TEXT_DRAWINGS_256, 0, NULL, 0, 0, 0},
    [ASSET_MENU_TEXT_A] = {"MENU_TEXT_A", 0x88FEC3u, ASSET_KIND_TILES_4BPP, ROLE_TEXT_DRAWINGS_128, 0, NULL, 0, 0, 0},
    [ASSET_MENU_TEXT_B] = {"MENU_TEXT_B", 0x88F87Fu, ASSET_KIND_TILES_4BPP, ROLE_TEXT_DRAWINGS_256, 0, NULL, 0, 0, 0},
    [ASSET_MENU_CURSOR_DRAWINGS] = {"MENU_CURSOR_DRAWINGS", 0x8AC8E4u, ASSET_KIND_WORD_RUN, ROLE_OBJ_DRAWINGS, 97u, NULL, 0, 0, 0},
    [ASSET_VS_BG_DRAWINGS] = {"VS_BG_DRAWINGS", 0x8DF696u, ASSET_KIND_TILES_4BPP, ROLE_BG_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_VS_PANELS_ARRANGEMENT] = {"VS_PANELS_ARRANGEMENT", 0u, ASSET_KIND_CAPTURED_WORD_RUN, ROLE_BG_ARRANGEMENT, 896u, NULL, 0, 0, 0},
    [ASSET_RESULTS_STARS_DRAWINGS] = {"RESULTS_STARS_DRAWINGS", 0x91AADFu, ASSET_KIND_TILES_4BPP, ROLE_OBJ_DRAWINGS_HI, 0, NULL, 0, 0, 0},
    [ASSET_INTRO_TEXT_DRAWINGS] = {"INTRO_TEXT_DRAWINGS", 0x8D8000u, ASSET_KIND_TILES_4BPP, ROLE_TEXT_DRAWINGS_128, 0, NULL, 0, 0, 0},
    [ASSET_INTRO_M7_DRAWINGS] = {"INTRO_M7_DRAWINGS", 0x948020u, ASSET_KIND_TILES_8BPP, ROLE_BG_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_INTRO_M7_MAP] = {"INTRO_M7_MAP", 0x90C343u, ASSET_KIND_MAP_16, ROLE_BG_ARRANGEMENT, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_M7_TILES_A] = {"TITLE_M7_TILES_A", 0x9AEB44u, ASSET_KIND_TITLE_PACK, ROLE_BG_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_M7_TILES_B] = {"TITLE_M7_TILES_B", 0x9AEB44u, ASSET_KIND_TITLE_PACK, ROLE_BG_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_M7_MAP] = {"TITLE_M7_MAP", 0x9AEB44u, ASSET_KIND_TITLE_PACK, ROLE_BG_ARRANGEMENT, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_OBJ_0] = {"TITLE_OBJ_0", 0x9AEB44u, ASSET_KIND_TITLE_PACK, ROLE_OBJ_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_OBJ_1] = {"TITLE_OBJ_1", 0x9AEB44u, ASSET_KIND_TITLE_PACK, ROLE_OBJ_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_OBJ_2] = {"TITLE_OBJ_2", 0x9AEB44u, ASSET_KIND_TITLE_PACK, ROLE_OBJ_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_TITLE_OBJ_3] = {"TITLE_OBJ_3", 0x9AEB44u, ASSET_KIND_TITLE_PACK, ROLE_OBJ_DRAWINGS, 0, NULL, 0, 0, 0},
    [ASSET_CHAMP_OBJ_FONT] = {"CHAMP_OBJ_FONT", 0x878190u, ASSET_KIND_TILES_4BPP, ROLE_NONE, 0, NULL, 0, 0, 0},
    [ASSET_CHAMP_OBJ_BAR] = {"CHAMP_OBJ_BAR", 0x90C320u, ASSET_KIND_TILES_4BPP, ROLE_NONE, 0, NULL, 0, 0, 0},
    [ASSET_CHAMP_OBJ_GRAPHICS] = {"CHAMP_OBJ_GRAPHICS", 0x8786BEu, ASSET_KIND_TILES_4BPP, ROLE_NONE, 0, NULL, 0, 0, 0},
    [ASSET_CHAMP_BG_TILES_A] = {"CHAMP_BG_TILES_A", 0x8F83CCu, ASSET_KIND_TILES_4BPP, ROLE_NONE, 0, NULL, 0, 0, 0},
    [ASSET_CHAMP_BG_OBJ_TILES_B] = {"CHAMP_BG_OBJ_TILES_B", 0x988000u, ASSET_KIND_TILES_4BPP, ROLE_NONE, 0, NULL, 0, 0, 0},
};

static int g_current[ROLE_COUNT] = {-1, -1, -1, -1, -1, -1};
static bool g_intro_pieces_sent[INTRO_PIECE_COUNT];
static bool g_title_objs_loaded[4];
static uint8_t g_champ_streams_mask[CHAMP_HALF_COUNT];
static champ_half_pieces g_champ_half[CHAMP_HALF_COUNT];

typedef struct {
    uint32_t wram_bus;
    uint32_t size;
} intro_piece_desc;

static const intro_piece_desc g_intro_pieces[INTRO_PIECE_COUNT] = {
    [INTRO_PIECE_BG1_A]    = {0x7E2000u, 0x04A0u},
    [INTRO_PIECE_BG1_B]    = {0x7E24A0u, 0x04A0u},
    [INTRO_PIECE_SLICE_0]  = {0x7E3040u + 0u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_1]  = {0x7E3040u + 1u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_2]  = {0x7E3040u + 2u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_3]  = {0x7E3040u + 3u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_4]  = {0x7E3040u + 4u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_5]  = {0x7E3040u + 5u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_6]  = {0x7E3040u + 6u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_7]  = {0x7E3040u + 7u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_8]  = {0x7E3040u + 8u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_9]  = {0x7E3040u + 9u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_10] = {0x7E3040u + 10u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_11] = {0x7E3040u + 11u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_12] = {0x7E3040u + 12u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_13] = {0x7E3040u + 13u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_14] = {0x7E3040u + 14u * 0x0614u, 0x0614u},
    [INTRO_PIECE_SLICE_15] = {0x7E3040u + 15u * 0x0614u, 0x0614u},
    [INTRO_PIECE_FRAME_0]  = {0x7E9180u, 0x0614u},
    [INTRO_PIECE_FRAME_1]  = {0x7E9768u, 0x0614u},
    [INTRO_PIECE_FRAME_2]  = {0x7E9D50u, 0x0614u},
    [INTRO_PIECE_FRAME_3]  = {0x7EA338u, 0x0614u},
};

#define MAX_WORD_RUN_WORDS 2048u
static uint8_t g_word_run_buf[MAX_WORD_RUN_WORDS * 2u];
static size_t g_word_run_words = 0;

static uint32_t fnv1a_32(const uint8_t* data, size_t len) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

void assets_init(void) {
    assets_clear_loaded();
    assets_reset_word_run();
    bool title_pack_ready = false;
    for (int i = 0; i < ASSET_COUNT; ++i) {
        free(g_assets[i].data);
        g_assets[i].data = NULL;
        g_assets[i].size = 0;
        g_assets[i].hash = 0;

        if (g_assets[i].kind == ASSET_KIND_CAPTURED_WORD_RUN) {
            continue;
        }

        if (g_assets[i].kind == ASSET_KIND_TITLE_PACK) {
            if (!title_pack_ready) {
                uint16_t end = 0;
                if (decompress(DP, 0x9Au, 0xEB44u, 0x7Eu, 0x8000u, &end) == 0u) {
                    port_fatal("assets_init: title pack decompression failed");
                }
                title_pack_ready = true;
            }
            const uint8_t* src_ptr = NULL;
            size_t len = 0;
            uint16_t end = 0;
            if (i == ASSET_TITLE_M7_TILES_A) {
                src_ptr = &g_ram[0x8633u];
                len = 0x1200u;
            } else if (i == ASSET_TITLE_M7_TILES_B) {
                src_ptr = &g_ram[0x9833u];
                len = 0x06C0u;
            } else if (i == ASSET_TITLE_M7_MAP) {
                src_ptr = &g_ram[0x80BAu];
                len = 90u;
            } else if (i == ASSET_TITLE_OBJ_0) {
                len = decompress_near(DP, 0x7Eu, 0x8256u, 0x7Fu, 0x0000u, &end);
                src_ptr = &g_ram[0x10000u];
            } else if (i == ASSET_TITLE_OBJ_1) {
                len = decompress_near(DP, 0x7Eu, 0xB385u, 0x7Fu, 0x0000u, &end);
                src_ptr = &g_ram[0x10000u];
            } else if (i == ASSET_TITLE_OBJ_2) {
                len = decompress_near(DP, 0x7Eu, 0x9F07u, 0x7Fu, 0x0000u, &end);
                src_ptr = &g_ram[0x10000u];
            } else if (i == ASSET_TITLE_OBJ_3) {
                src_ptr = &g_ram[0xA071u];
                len = 0x0800u;
            }
            if (len == 0u || src_ptr == NULL) {
                port_fatal("assets_init: title pack sub-block has zero length");
            }
            uint8_t* buf = (uint8_t*)malloc(len);
            if (buf == NULL) {
                port_fatal("assets_init: out of memory");
            }
            memcpy(buf, src_ptr, len);
            g_assets[i].data = buf;
            g_assets[i].size = len;
            g_assets[i].hash = fnv1a_32(buf, len);
            g_assets[i].generation++;
            continue;
        }

        if (g_assets[i].kind == ASSET_KIND_WORD_RUN) {
            size_t len = g_assets[i].word_count * 2u;
            if (len == 0 || g_assets[i].word_count > MAX_WORD_RUN_WORDS) {
                port_fatal("assets_init: invalid word_run length");
            }
            uint8_t* buf = (uint8_t*)malloc(len);
            if (buf == NULL) {
                port_fatal("assets_init: out of memory");
            }
            for (size_t w = 0; w < g_assets[i].word_count; ++w) {
                uint16_t val = bus_rd16(g_assets[i].rom_bus + (uint32_t)w * 2u);
                buf[w * 2u] = (uint8_t)(val & 0xFFu);
                buf[w * 2u + 1u] = (uint8_t)(val >> 8);
            }
            g_assets[i].data = buf;
            g_assets[i].size = len;
            g_assets[i].hash = fnv1a_32(buf, len);
            continue;
        }

        uint8_t bank = (uint8_t)(g_assets[i].rom_bus >> 16);
        uint16_t addr = (uint16_t)(g_assets[i].rom_bus & 0xFFFFu);
        uint16_t src_end = 0;
        uint16_t len = decompress_near(DP, bank, addr, 0x7F, 0x0000, &src_end);
        if (len == 0) {
            port_fatal("assets_init: decompressed block has zero length");
        }

        uint8_t* buf = (uint8_t*)malloc(len);
        if (buf == NULL) {
            port_fatal("assets_init: out of memory");
        }
        memcpy(buf, &g_ram[0x10000], len);

        g_assets[i].data = buf;
        g_assets[i].size = len;
        g_assets[i].hash = fnv1a_32(buf, len);
        g_assets[i].generation++;
    }

    memset(g_ram, 0, sizeof(g_ram));
}

void assets_reset_word_run(void) {
    g_word_run_words = 0;
}

void assets_note_word(uint16_t value) {
    if (g_word_run_words >= MAX_WORD_RUN_WORDS) {
        memmove(g_word_run_buf, g_word_run_buf + 2u, (MAX_WORD_RUN_WORDS - 1u) * 2u);
        g_word_run_words = MAX_WORD_RUN_WORDS - 1u;
    }
    g_word_run_buf[g_word_run_words * 2u] = (uint8_t)(value & 0xFFu);
    g_word_run_buf[g_word_run_words * 2u + 1u] = (uint8_t)(value >> 8);
    g_word_run_words++;

    for (int i = 0; i < ASSET_COUNT; ++i) {
        if (g_assets[i].kind == ASSET_KIND_WORD_RUN &&
            g_word_run_words >= g_assets[i].word_count) {
            const uint8_t* tail =
                &g_word_run_buf[(g_word_run_words - g_assets[i].word_count) * 2u];
            uint32_t h = fnv1a_32(tail, g_assets[i].size);
            if (h == g_assets[i].hash) {
                g_current[g_assets[i].role] = i;
            }
        }
    }
}

/* Request J (decomp f22a687): the game names the VS panels' arrangement before writing it. It is
   two halves, each the same 14 rows of the WRAM buffer `rows` ($20 bytes a row), each row two
   copies of its 16 words, ORed with $0000/$0400 (first half) and $0800/$0C00 (second half): the
   decomp's platform.h text. The port builds it from those rows; the word writes that follow are
   only checked by the word-run matcher for other assets. */
void assets_note_vs_panels(uint32_t rows) {
    enum { ROWS = 14, ROW_WORDS = 16, ARR_WORDS = 2 * ROWS * 2 * ROW_WORDS };
    static const uint16_t or_bits[2][2] = {{0x0000u, 0x0400u}, {0x0800u, 0x0C00u}};
    uint32_t idx = 0;
    if (rows != 0x7F0000u || !bus_wram(rows, &idx)) {
        port_fatal("VS panels named from an unexpected buffer (measured: $7F:0000)");
    }
    if (g_current[ROLE_BG_DRAWINGS] != ASSET_VS_BG_DRAWINGS) {
        port_fatal("VS panels arrangement named without VS background drawings loaded");
    }
    asset_entry* vs_arr = &g_assets[ASSET_VS_PANELS_ARRANGEMENT];
    size_t arr_bytes = (size_t)ARR_WORDS * 2u;
    if (vs_arr->data == NULL || vs_arr->size != arr_bytes) {
        free(vs_arr->data);
        vs_arr->data = (uint8_t*)malloc(arr_bytes);
        if (vs_arr->data == NULL) {
            port_fatal("assets_note_vs_panels: out of memory");
        }
        vs_arr->size = arr_bytes;
    }
    size_t out = 0;
    for (int half = 0; half < 2; ++half) {
        for (int r = 0; r < ROWS; ++r) {
            for (int copy = 0; copy < 2; ++copy) {
                for (int w = 0; w < ROW_WORDS; ++w) {
                    uint32_t a = idx + (uint32_t)r * 0x20u + (uint32_t)w * 2u;
                    uint16_t v = (uint16_t)(g_ram[a] | (g_ram[a + 1u] << 8));
                    v |= or_bits[half][copy];
                    vs_arr->data[out++] = (uint8_t)(v & 0xFFu);
                    vs_arr->data[out++] = (uint8_t)(v >> 8);
                }
            }
        }
    }
    vs_arr->hash = fnv1a_32(vs_arr->data, arr_bytes);
    vs_arr->generation++;
    g_current[ROLE_BG_ARRANGEMENT] = ASSET_VS_PANELS_ARRANGEMENT;
}

void assets_note_source(uint32_t rom) {
    static const uint32_t champ_roms[5] = {
        0x878190u, 0x90C320u, 0x8786BEu, 0x8F83CCu, 0x988000u,
    };
    int half = port_current_task_half();
    for (unsigned i = 0; i < 5u; ++i) {
        if (rom == champ_roms[i]) {
            if (i == 0u && g_champ_streams_mask[half] == 0x1Fu) {
                /* Starting a fresh championship load on this half: reset its captured pieces. */
                g_champ_streams_mask[half] = 0u;
                memset(&g_champ_half[half], 0, sizeof(g_champ_half[half]));
            }
            g_champ_streams_mask[half] |= (uint8_t)(1u << i);
            return;
        }
    }
    /* A non-championship stream was unpacked (e.g. shop, ticker, or single-task menu):
       clear championship loaded state so other $B97B screens are not misidentified. */
    memset(g_champ_streams_mask, 0, sizeof(g_champ_streams_mask));
    memset(g_champ_half, 0, sizeof(g_champ_half));
}

static bool capture_champ_piece(uint32_t src, uint32_t size) {
    if ((src >> 16) != 0x7Fu) {
        return false;
    }
    uint16_t addr = (uint16_t)(src & 0xFFFFu);
    int half = (addr >= 0x8000u) ? 1 : 0;
    if (g_champ_streams_mask[half] != 0x1Fu) {
        return false;
    }
    uint16_t rel = (uint16_t)(addr - (half ? 0x8000u : 0x0000u));
    const uint8_t* ptr = &g_ram[0x10000u + (uint32_t)addr];
    champ_half_pieces* hp = &g_champ_half[half];

    if (rel == 0x0000u && size == CHAMP_MAP_BYTES) {
        memcpy(hp->bg1_map, ptr, CHAMP_MAP_BYTES);
        hp->has_bg1_map = true;
        return true;
    }
    if (rel == 0x0800u && size == CHAMP_MAP_BYTES) {
        memcpy(hp->bg2_map, ptr, CHAMP_MAP_BYTES);
        hp->has_bg2_map = true;
        return true;
    }
    if (rel == 0x0800u && size == CHAMP_PANEL_ROWS_BYTES) {
        memcpy(hp->bg2_panel_rows, ptr, CHAMP_PANEL_ROWS_BYTES);
        hp->has_bg2_panel_rows = true;
        return true;
    }
    if (rel == 0x0000u && size == CHAMP_NOISE_TILES_BYTES) {
        memcpy(hp->noise_tiles, ptr, CHAMP_NOISE_TILES_BYTES);
        hp->has_noise_tiles = true;
        hp->noise_gen++;
        return true;
    }
    if (size == CHAMP_FIRE_BAND_BYTES && rel >= 0x11C0u && rel <= 0x21C0u
        && ((rel - 0x11C0u) & 0x01FFu) == 0u) {
        memcpy(hp->fire_band, ptr, CHAMP_FIRE_BAND_BYTES);
        hp->has_fire_band = true;
        return true;
    }
    return false;
}

void assets_note_upload(uint32_t src, uint32_t size) {
    if (capture_champ_piece(src, size)) {
        return;
    }

    if (src == 0x7F0000u) {
        if (size == 0 || size > 0x10000u) {
            return;
        }
        uint32_t h = fnv1a_32(&g_ram[0x10000], size);
        for (int i = 0; i < ASSET_COUNT; ++i) {
            if (g_assets[i].role != ROLE_NONE
                && g_assets[i].size == size && g_assets[i].hash == h) {
                g_current[g_assets[i].role] = i;
                if (i >= ASSET_TITLE_OBJ_0 && i <= ASSET_TITLE_OBJ_2) {
                    g_title_objs_loaded[i - ASSET_TITLE_OBJ_0] = true;
                }
            }
        }
        return;
    }

    uint32_t wram_idx = 0;
    if (!bus_wram(src, &wram_idx)) {
        for (int i = 0; i < ASSET_COUNT; ++i) {
            if (g_assets[i].role != ROLE_NONE && g_assets[i].rom_bus == src) {
                g_current[g_assets[i].role] = i;
            }
        }
    } else {
        if (src == 0x7EA071u && size == 0x0800u) {
            uint32_t h = fnv1a_32(&g_ram[wram_idx], size);
            if (h == g_assets[ASSET_TITLE_OBJ_3].hash) {
                g_title_objs_loaded[3] = true;
            }
        }
        for (int i = 0; i < INTRO_PIECE_COUNT; ++i) {
            if (g_intro_pieces[i].wram_bus == src && g_intro_pieces[i].size == size) {
                g_intro_pieces_sent[i] = true;
            }
        }
    }
}

void assets_clear_loaded(void) {
    for (int r = 0; r < ROLE_COUNT; ++r) {
        g_current[r] = -1;
    }
    memset(g_intro_pieces_sent, 0, sizeof(g_intro_pieces_sent));
    memset(g_title_objs_loaded, 0, sizeof(g_title_objs_loaded));
    memset(g_champ_streams_mask, 0, sizeof(g_champ_streams_mask));
    memset(g_champ_half, 0, sizeof(g_champ_half));
}

bool assets_champ_loaded(void) {
    for (int h = 0; h < CHAMP_HALF_COUNT; ++h) {
        if (g_champ_streams_mask[h] != 0x1Fu
            || !g_champ_half[h].has_bg1_map
            || !g_champ_half[h].has_bg2_map
            || !g_champ_half[h].has_fire_band) {
            return false;
        }
    }
    return true;
}

const champ_half_pieces* assets_champ_half(int half) {
    if (half < 0 || half >= CHAMP_HALF_COUNT) {
        return NULL;
    }
    return &g_champ_half[half];
}

bool assets_intro_piece_sent(intro_piece_id id) {
    if ((int)id < 0 || (int)id >= INTRO_PIECE_COUNT) {
        return false;
    }
    return g_intro_pieces_sent[id];
}

bool assets_title_objs_loaded(void) {
    return g_title_objs_loaded[0] && g_title_objs_loaded[1]
        && g_title_objs_loaded[2] && g_title_objs_loaded[3];
}

int asset_current(asset_role r) {
    if ((int)r < 0 || (int)r >= ROLE_COUNT) {
        return -1;
    }
    return g_current[r];
}

const uint8_t* asset_bytes(int id, size_t* size) {
    if (id < 0 || id >= ASSET_COUNT) {
        if (size != NULL) {
            *size = 0;
        }
        return NULL;
    }
    if (size != NULL) {
        *size = g_assets[id].size;
    }
    return g_assets[id].data;
}

uint32_t asset_generation(int id) {
    if (id < 0 || id >= ASSET_COUNT) {
        return 0u;
    }
    return g_assets[id].generation;
}
