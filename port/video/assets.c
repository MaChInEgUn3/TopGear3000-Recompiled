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
};

static int g_current[ROLE_COUNT] = {-1, -1, -1, -1, -1, -1};
static bool g_intro_pieces_sent[INTRO_PIECE_COUNT];
static bool g_title_objs_loaded[4];

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

    /* The arrangement is data the game hands over word by word; the port names it by the
       game's write pattern (1024 words of $0023 followed by 896 arrangement words), never by
       a video address, and never recomputes it. */
    const size_t vs_fill_words = 1024u;
    const size_t vs_arr_words = 896u;
    if (g_word_run_words == vs_fill_words + vs_arr_words) {
        bool is_fill = true;
        for (size_t w = 0; w < vs_fill_words; ++w) {
            uint16_t v = (uint16_t)(g_word_run_buf[w * 2u] |
                                    ((uint16_t)g_word_run_buf[w * 2u + 1u] << 8));
            if (v != 0x0023u) {
                is_fill = false;
                break;
            }
        }
        if (is_fill) {
            if (g_current[ROLE_BG_DRAWINGS] != ASSET_VS_BG_DRAWINGS) {
                port_fatal("VS panels arrangement written without VS background drawings loaded");
            }
            asset_entry* vs_arr = &g_assets[ASSET_VS_PANELS_ARRANGEMENT];
            size_t arr_bytes = vs_arr_words * 2u;
            if (vs_arr->data == NULL || vs_arr->size != arr_bytes) {
                free(vs_arr->data);
                vs_arr->data = (uint8_t*)malloc(arr_bytes);
                if (vs_arr->data == NULL) {
                    port_fatal("assets_note_word: out of memory");
                }
                vs_arr->size = arr_bytes;
            }
            memcpy(vs_arr->data, &g_word_run_buf[vs_fill_words * 2u], arr_bytes);
            vs_arr->hash = fnv1a_32(vs_arr->data, arr_bytes);
            vs_arr->generation++;
            g_current[ROLE_BG_ARRANGEMENT] = ASSET_VS_PANELS_ARRANGEMENT;
        }
    }
}

void assets_note_upload(uint32_t src, uint32_t size) {
    if (src == 0x7F0000u) {
        if (size == 0 || size > 0x10000u) {
            return;
        }
        uint32_t h = fnv1a_32(&g_ram[0x10000], size);
        for (int i = 0; i < ASSET_COUNT; ++i) {
            if (g_assets[i].size == size && g_assets[i].hash == h) {
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
            if (g_assets[i].rom_bus == src) {
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
