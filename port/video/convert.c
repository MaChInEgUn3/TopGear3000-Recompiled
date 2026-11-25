// SPDX-License-Identifier: GPL-3.0-or-later

#include "convert.h"

void convert_tiles_4bpp(const uint8_t* data, size_t ntiles, uint8_t* out) {
    for (size_t t = 0; t < ntiles; ++t) {
        const uint8_t* tile = data + t * 32u;
        uint8_t* dst = out + t * 64u;
        for (size_t r = 0; r < 8u; ++r) {
            uint8_t p0 = tile[2u * r];
            uint8_t p1 = tile[2u * r + 1u];
            uint8_t p2 = tile[16u + 2u * r];
            uint8_t p3 = tile[16u + 2u * r + 1u];
            for (size_t c = 0; c < 8u; ++c) {
                unsigned shift = (unsigned)(7u - c);
                uint8_t px = (uint8_t)(((p0 >> shift) & 1u)
                                     | (((p1 >> shift) & 1u) << 1)
                                     | (((p2 >> shift) & 1u) << 2)
                                     | (((p3 >> shift) & 1u) << 3));
                dst[r * 8u + c] = px;
            }
        }
    }
}

void convert_tiles_8bpp(const uint8_t* data, size_t ntiles, uint8_t* out) {
    for (size_t t = 0; t < ntiles; ++t) {
        const uint8_t* tile = data + t * 64u;
        uint8_t* dst = out + t * 64u;
        for (size_t r = 0; r < 8u; ++r) {
            uint8_t p0 = tile[2u * r];
            uint8_t p1 = tile[2u * r + 1u];
            uint8_t p2 = tile[16u + 2u * r];
            uint8_t p3 = tile[16u + 2u * r + 1u];
            uint8_t p4 = tile[32u + 2u * r];
            uint8_t p5 = tile[32u + 2u * r + 1u];
            uint8_t p6 = tile[48u + 2u * r];
            uint8_t p7 = tile[48u + 2u * r + 1u];
            for (size_t c = 0; c < 8u; ++c) {
                unsigned shift = (unsigned)(7u - c);
                uint8_t px = (uint8_t)(((p0 >> shift) & 1u)
                                     | (((p1 >> shift) & 1u) << 1)
                                     | (((p2 >> shift) & 1u) << 2)
                                     | (((p3 >> shift) & 1u) << 3)
                                     | (((p4 >> shift) & 1u) << 4)
                                     | (((p5 >> shift) & 1u) << 5)
                                     | (((p6 >> shift) & 1u) << 6)
                                     | (((p7 >> shift) & 1u) << 7));
                dst[r * 8u + c] = px;
            }
        }
    }
}

uint32_t convert_color(uint16_t bgr555) {
    uint32_t r5 = (uint32_t)(bgr555 & 0x1Fu);
    uint32_t g5 = (uint32_t)((bgr555 >> 5) & 0x1Fu);
    uint32_t b5 = (uint32_t)((bgr555 >> 10) & 0x1Fu);
    uint32_t r = (r5 << 3) | (r5 >> 2);
    uint32_t g = (g5 << 3) | (g5 >> 2);
    uint32_t b = (b5 << 3) | (b5 >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
