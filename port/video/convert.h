// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_VIDEO_CONVERT_H
#define TG3000_VIDEO_CONVERT_H

#include <stddef.h>
#include <stdint.h>

/* Convert ntiles 2bpp planar tiles (16 bytes each) into 64 colour indices (0..3) per tile. */
void convert_tiles_2bpp(const uint8_t* data, size_t ntiles, uint8_t* out);

/* Convert ntiles 4bpp planar tiles (32 bytes each) into 64 colour indices (0..15) per tile. */
void convert_tiles_4bpp(const uint8_t* data, size_t ntiles, uint8_t* out);

/* Convert ntiles 8bpp planar tiles (64 bytes each) into 64 colour indices (0..255) per tile. */
void convert_tiles_8bpp(const uint8_t* data, size_t ntiles, uint8_t* out);

/* Convert 15-bit SNES BGR555 colour into 0xAARRGGBB (A = 0xFF). */
uint32_t convert_color(uint16_t bgr555);

#endif
