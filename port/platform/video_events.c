// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform.h"

// step 4: identify the block (by content hash of a known ROM block or by ROM address)
// and note its role for the active screen; never keep a copy indexed by video address

void video_upload_vram(uint16_t vram_word, uint32_t src, uint32_t size) {
    (void)vram_word;
    (void)src;
    (void)size;
}

void video_fill_vram(uint16_t vram_word, uint8_t value, uint32_t size) {
    (void)vram_word;
    (void)value;
    (void)size;
}

void video_write_vram_word(uint16_t vram_word, uint16_t value) {
    (void)vram_word;
    (void)value;
}

void video_upload_cgram(uint8_t index, uint32_t src, uint32_t size) {
    (void)index;
    (void)src;
    (void)size;
}

void video_set_color(uint8_t index, uint16_t bgr) {
    (void)index;
    (void)bgr;
}
