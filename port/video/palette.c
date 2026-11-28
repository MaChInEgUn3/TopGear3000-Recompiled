// SPDX-License-Identifier: GPL-3.0-or-later

#include "palette.h"

#include <stdbool.h>
#include <stdio.h>

#include "port.h"
#include "tg3000.h"

/* The port keeps the screen's 256 colours as the game sets them; this is the one table the chip
   also keeps; it holds only named cartridge palettes and colours the game writes. */
static uint16_t g_palette[256];
static palette_source g_palette_source[256];

typedef struct {
    uint32_t rom_bus;
    uint32_t size;
    palette_source id;
    const char* name;
} palette_entry;

static const palette_entry g_palette_catalogue[] = {
    {0x8AC658u, 0x200u, PAL_SRC_MENU_BG,           "menu background colours"},
    {0x8AC964u, 0x20u,  PAL_SRC_TITLE_OBJ,         "title card object colours"},
    {0x8AC984u, 0x40u,  PAL_SRC_TITLE_TEXT,        "title card text colours"},
    {0x85FA37u, 0x40u,  PAL_SRC_PASSWORD_TEXT,     "password screen text colours"},
    {0x8AC5D8u, 0x80u,  PAL_SRC_VS,                "VS screen colours"},
    {0x86F40Bu, 0x40u,  PAL_SRC_RESULTS_TEXT_6_7,  "results text palette 6-7 colours"},
    {0x8A9D2Du, 0x20u,  PAL_SRC_RESULTS_PAL_1,     "results palette 1 colours"},
    {0x8C95C3u, 0x20u,  PAL_SRC_RESULTS_STARS_OBJ, "results stars object colours"},
    {0x86F44Bu, 0x20u,  PAL_SRC_RESULTS_TEXT_0,    "results text palette 0 colours"},
    /* race_load's last colours, the same for every track and car (a constant source in
       race_load, $82:8428): colours 0-31 */
    {0x8D8B70u, 0x40u,  PAL_SRC_RACE_COLOURS_0_31, "race colours 0-31"},
    /* vs_race_load's colours ($82:92BA, a constant source: upload_cgram_now(0, $8E, $8000, $40)):
       colours 0-31 of the VS race, the title's VS demo race included */
    {0x8E8000u, 0x40u,  PAL_SRC_VS_RACE_COLOURS_0_31, "VS race colours 0-31"},
};

void palette_set(uint8_t index, uint16_t bgr) {
    g_palette[index] = bgr;
    g_palette_source[index] = PAL_SRC_SINGLE_COLOR;
}

void palette_load(uint8_t index, uint32_t src, uint32_t size) {
    uint32_t wram_idx = 0;
    if (bus_wram(src, &wram_idx)) {
        /* Screens that keep their colours in game RAM are drawn by reading that RAM. */
        return;
    }

    palette_source src_id = PAL_SRC_UNSET;
    for (size_t i = 0; i < sizeof(g_palette_catalogue) / sizeof(g_palette_catalogue[0]); ++i) {
        if (g_palette_catalogue[i].rom_bus == src && g_palette_catalogue[i].size == size) {
            src_id = g_palette_catalogue[i].id;
            break;
        }
    }
    if (src_id == PAL_SRC_UNSET) {
        char msg[96];
        snprintf(msg, sizeof(msg), "unknown palette block $%02X:%04X at colour %u",
                 (unsigned)((src >> 16) & 0xFFu), (unsigned)(src & 0xFFFFu), (unsigned)index);
        port_fatal(msg);
    }

    uint32_t count = size / 2u;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t bgr = bus_rd16(src + i * 2u);
        uint8_t dst = (uint8_t)(index + i);
        g_palette[dst] = bgr;
        g_palette_source[dst] = src_id;
    }
}

uint16_t palette_get(uint8_t index) {
    return g_palette[index];
}

palette_source palette_source_of(uint8_t index) {
    return g_palette_source[index];
}
