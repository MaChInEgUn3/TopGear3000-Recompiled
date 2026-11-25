// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_VIDEO_PALETTE_H
#define TG3000_VIDEO_PALETTE_H

#include <stdint.h>

/* Set one palette colour (SNES BGR555). */
void palette_set(uint8_t index, uint16_t bgr);

/* Load size bytes (2 bytes per colour, read via bus_rd16) starting at index. */
void palette_load(uint8_t index, uint32_t src, uint32_t size);

/* Read one palette colour (SNES BGR555). */
uint16_t palette_get(uint8_t index);

#endif
