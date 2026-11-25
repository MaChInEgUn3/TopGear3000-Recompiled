// SPDX-License-Identifier: GPL-3.0-or-later

#include "palette.h"

#include "tg3000.h"

static uint16_t g_palette[256];

void palette_set(uint8_t index, uint16_t bgr) {
    g_palette[index] = bgr;
}

void palette_load(uint8_t index, uint32_t src, uint32_t size) {
    uint32_t count = size / 2u;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t bgr = bus_rd16(src + i * 2u);
        g_palette[(uint8_t)(index + i)] = bgr;
    }
}

uint16_t palette_get(uint8_t index) {
    return g_palette[index];
}
