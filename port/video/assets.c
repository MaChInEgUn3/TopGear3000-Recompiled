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
    ASSET_KIND_CAPTURED_WORD_RUN
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
};

static int g_current[ROLE_COUNT] = {-1, -1, -1, -1, -1, -1};

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
    for (int i = 0; i < ASSET_COUNT; ++i) {
        free(g_assets[i].data);
        g_assets[i].data = NULL;
        g_assets[i].size = 0;
        g_assets[i].hash = 0;

        if (g_assets[i].kind == ASSET_KIND_CAPTURED_WORD_RUN) {
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
    }
}

void assets_clear_loaded(void) {
    for (int r = 0; r < ROLE_COUNT; ++r) {
        g_current[r] = -1;
    }
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
