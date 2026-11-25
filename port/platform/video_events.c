// SPDX-License-Identifier: GPL-3.0-or-later

#include "assets.h"
#include "palette.h"
#include "platform.h"

/* Nothing is kept by video address: graphics uploads identify a known cartridge block
   and mark it loaded; palette uploads update the screen's 256-colour palette. */

void video_upload_vram(uint16_t vram_word, uint32_t src, uint32_t size) {
    (void)vram_word;
    assets_reset_word_run();
    assets_note_upload(src, size);
}

void video_fill_vram(uint16_t vram_word, uint8_t value, uint32_t size) {
    (void)vram_word;
    (void)value;
    assets_reset_word_run();
    if (size == 0x10000u) {
        assets_clear_loaded();
    }
}

void video_write_vram_word(uint16_t vram_word, uint16_t value) {
    (void)vram_word;
    assets_note_word(value);
}

void video_upload_cgram(uint8_t index, uint32_t src, uint32_t size) {
    assets_reset_word_run();
    palette_load(index, src, size);
}

void video_set_color(uint8_t index, uint16_t bgr) {
    assets_reset_word_run();
    palette_set(index, bgr);
}
