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
    ASSET_KIND_MAP_16
} asset_kind;

typedef struct {
    const char* name;
    uint32_t rom_bus;
    asset_kind kind;
    uint8_t* data;
    size_t size;
    uint32_t hash;
    bool loaded;
} asset_entry;

static asset_entry g_assets[ASSET_COUNT] = {
    [ASSET_BG_TITLE_DRAWINGS] = {"BG_TITLE_DRAWINGS", 0x96EE52u, ASSET_KIND_TILES_8BPP, NULL, 0, 0, false},
    [ASSET_BG_TITLE_ARRANGEMENT] = {"BG_TITLE_ARRANGEMENT", 0x85FA8Bu, ASSET_KIND_MAP_16, NULL, 0, 0, false},
    [ASSET_TEXT_TITLE_DRAWINGS] = {"TEXT_TITLE_DRAWINGS", 0x87F861u, ASSET_KIND_TILES_4BPP, NULL, 0, 0, false},
    [ASSET_OBJ_TITLE_DRAWINGS] = {"OBJ_TITLE_DRAWINGS", 0x87F5BCu, ASSET_KIND_TILES_4BPP, NULL, 0, 0, false},
};

static uint32_t fnv1a_32(const uint8_t* data, size_t len) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

void assets_init(void) {
    for (int i = 0; i < ASSET_COUNT; ++i) {
        free(g_assets[i].data);
        g_assets[i].data = NULL;
        g_assets[i].size = 0;
        g_assets[i].hash = 0;
        g_assets[i].loaded = false;

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
    }

    memset(g_ram, 0, sizeof(g_ram));
}

void assets_note_upload(uint32_t src, uint32_t size) {
    if (src == 0x7F0000u) {
        if (size == 0 || size > 0x10000u) {
            return;
        }
        uint32_t h = fnv1a_32(&g_ram[0x10000], size);
        for (int i = 0; i < ASSET_COUNT; ++i) {
            if (g_assets[i].size == size && g_assets[i].hash == h) {
                g_assets[i].loaded = true;
            }
        }
        return;
    }

    uint32_t wram_idx = 0;
    if (!bus_wram(src, &wram_idx)) {
        for (int i = 0; i < ASSET_COUNT; ++i) {
            if (g_assets[i].rom_bus == src) {
                g_assets[i].loaded = true;
            }
        }
    }
}

void assets_clear_loaded(void) {
    for (int i = 0; i < ASSET_COUNT; ++i) {
        g_assets[i].loaded = false;
    }
}

bool asset_loaded(int id) {
    if (id < 0 || id >= ASSET_COUNT) {
        return false;
    }
    return g_assets[id].loaded;
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
