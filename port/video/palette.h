// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_VIDEO_PALETTE_H
#define TG3000_VIDEO_PALETTE_H

#include <stdint.h>

typedef enum {
    PAL_SRC_UNSET = 0,
    PAL_SRC_SINGLE_COLOR,
    PAL_SRC_MENU_BG,
    PAL_SRC_TITLE_OBJ,
    PAL_SRC_TITLE_TEXT,
    PAL_SRC_PASSWORD_TEXT,
    PAL_SRC_VS,
    PAL_SRC_RESULTS_TEXT_6_7,
    PAL_SRC_RESULTS_PAL_1,
    PAL_SRC_RESULTS_STARS_OBJ,
    PAL_SRC_RESULTS_TEXT_0,
    PAL_SRC_COUNT
} palette_source;

/* Set one palette colour (SNES BGR555). */
void palette_set(uint8_t index, uint16_t bgr);

/* Load size bytes (2 bytes per colour, read via bus_rd16) starting at index. */
void palette_load(uint8_t index, uint32_t src, uint32_t size);

/* Read one palette colour (SNES BGR555). */
uint16_t palette_get(uint8_t index);

/* Read the source of one palette colour. */
palette_source palette_source_of(uint8_t index);

#endif
