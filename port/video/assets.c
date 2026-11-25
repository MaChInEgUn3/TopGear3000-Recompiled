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
    asset_role role;
    uint8_t* data;
    size_t size;
    uint32_t hash;
} asset_entry;

static asset_entry g_assets[ASSET_COUNT] = {
    [ASSET_MENU_BG_DRAWINGS] = {"MENU_BG_DRAWINGS", 0x96EE52u, ASSET_KIND_TILES_8BPP, ROLE_BG_DRAWINGS, NULL, 0, 0},
    [ASSET_MENU_BG_ARRANGEMENT] = {"MENU_BG_ARRANGEMENT", 0x85FA8Bu, ASSET_KIND_MAP_16, ROLE_BG_ARRANGEMENT, NULL, 0, 0},
    [ASSET_TITLE_TEXT_DRAWINGS] = {"TITLE_TEXT_DRAWINGS", 0x87F861u, ASSET_KIND_TILES_4BPP, ROLE_TEXT_DRAWINGS, NULL, 0, 0},
    [ASSET_TITLE_OBJ_DRAWINGS] = {"TITLE_OBJ_DRAWINGS", 0x87F5BCu, ASSET_KIND_TILES_4BPP, ROLE_OBJ_DRAWINGS, NULL, 0, 0},
    [ASSET_PASSWORD_TEXT_DRAWINGS] = {"PASSWORD_TEXT_DRAWINGS", 0x888601u, ASSET_KIND_TILES_4BPP, ROLE_TEXT_DRAWINGS, NULL, 0, 0},
};

static int g_current[ROLE_COUNT] = {-1, -1, -1, -1};

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
    for (int i = 0; i < ASSET_COUNT; ++i) {
        free(g_assets[i].data);
        g_assets[i].data = NULL;
        g_assets[i].size = 0;
        g_assets[i].hash = 0;

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
