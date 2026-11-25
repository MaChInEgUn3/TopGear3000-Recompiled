// SPDX-License-Identifier: GPL-3.0-or-later

#include "palette.h"

#include <stdbool.h>
#include <stdio.h>

#include "port.h"
#include "tg3000.h"

/* The port keeps the screen's 256 colours as the game sets them; this is the one table the chip
   also keeps; it holds only named cartridge palettes and colours the game writes. */
static uint16_t g_palette[256];

typedef struct {
    uint32_t rom_bus;
    uint32_t size;
    const char* name;
} palette_entry;

static const palette_entry g_palette_catalogue[] = {
    {0x8AC658u, 0x200u, "menu background colours"},
    {0x8AC964u, 0x20u,  "title card object colours"},
    {0x8AC984u, 0x40u,  "title card text colours"},
    {0x85FA37u, 0x40u,  "password screen text colours"},
    {0x8AC5D8u, 0x80u,  "VS screen colours"},
};

void palette_set(uint8_t index, uint16_t bgr) {
    g_palette[index] = bgr;
}

void palette_load(uint8_t index, uint32_t src, uint32_t size) {
    uint32_t wram_idx = 0;
    if (bus_wram(src, &wram_idx)) {
        /* Screens that keep their colours in game RAM are drawn by reading that RAM. */
        return;
    }

    bool known = false;
    for (size_t i = 0; i < sizeof(g_palette_catalogue) / sizeof(g_palette_catalogue[0]); ++i) {
        if (g_palette_catalogue[i].rom_bus == src && g_palette_catalogue[i].size == size) {
            known = true;
            break;
        }
    }
    if (!known) {
        char msg[96];
        snprintf(msg, sizeof(msg), "unknown palette block $%02X:%04X at colour %u",
                 (unsigned)((src >> 16) & 0xFFu), (unsigned)(src & 0xFFFFu), (unsigned)index);
        port_fatal(msg);
    }

    uint32_t count = size / 2u;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t bgr = bus_rd16(src + i * 2u);
        g_palette[(uint8_t)(index + i)] = bgr;
    }
}

uint16_t palette_get(uint8_t index) {
    return g_palette[index];
}
