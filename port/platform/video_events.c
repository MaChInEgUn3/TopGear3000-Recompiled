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

void video_note_source(uint32_t rom, uint16_t vram_word) {
    (void)vram_word;
    assets_reset_word_run();
    assets_note_source(rom);
}

void video_note_vs_panels(uint16_t vram_word, uint32_t rows) {
    (void)vram_word;
    assets_reset_word_run();
    assets_note_vs_panels(rows);
}

void video_upload_oam(uint16_t oam_word, uint32_t src, uint32_t size) {
    (void)oam_word;
    assets_note_oam(src, size);
}

/* A streamed upload comes as three calls (source, size, start); only its WRAM source and size
   are kept, joined here, and handed to the catalogue at the start. The mode and the target
   register are ignored, as are the upload destinations. */
static uint32_t g_stream_src[8];
static uint32_t g_stream_size[8];

void dma_stream_setup(int slot, uint8_t mode, uint8_t target, uint32_t src) {
    (void)mode;
    (void)target;
    if (slot >= 0 && slot < 8) {
        g_stream_src[slot] = src;
    }
}

void dma_stream_set_source(int slot, uint16_t src) {
    if (slot >= 0 && slot < 8) {
        g_stream_src[slot] = (g_stream_src[slot] & 0xFF0000u) | src;
    }
}

void dma_stream_size(int slot, uint32_t size) {
    if (slot >= 0 && slot < 8) {
        g_stream_size[slot] = size;
    }
}

void dma_stream_kick(uint8_t slots) {
    for (int slot = 0; slot < 8; ++slot) {
        if ((slots >> slot) & 1u) {
            assets_note_stream(g_stream_src[slot], g_stream_size[slot]);
        }
    }
}
