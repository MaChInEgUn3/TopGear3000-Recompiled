// SPDX-License-Identifier: GPL-3.0-or-later

#include "platform.h"

// Screen registers: the screen code reads game RAM or knows its own layout
void video_set_vram_increment(uint8_t value) { (void)value; }
void video_set_m7a(int16_t value) { (void)value; }
void video_set_m7b(int16_t value) { (void)value; }
void video_set_m7c(int16_t value) { (void)value; }
void video_set_m7d(int16_t value) { (void)value; }
void video_set_m7x(int16_t value) { (void)value; }
void video_set_m7y(int16_t value) { (void)value; }
void video_set_bg1_hofs(uint16_t value) { (void)value; }
void video_set_bg1_vofs(uint16_t value) { (void)value; }
void video_set_bg2_hofs(uint16_t value) { (void)value; }
void video_set_bg2_vofs(uint16_t value) { (void)value; }
void video_set_bg3_hofs(uint16_t value) { (void)value; }
void video_set_bg3_vofs(uint16_t value) { (void)value; }
void video_set_bg4_hofs(uint16_t value) { (void)value; }
void video_set_bg4_vofs(uint16_t value) { (void)value; }
void video_set_mosaic(uint8_t value) { (void)value; }
void video_set_m7_select(uint8_t value) { (void)value; }
void video_set_window_position(int edge, uint8_t value) { (void)edge; (void)value; }
void video_set_window_logic_bg(uint8_t value) { (void)value; }
void video_set_window_logic_obj(uint8_t value) { (void)value; }
void video_set_main_window_mask(uint8_t value) { (void)value; }
void video_set_sub_window_mask(uint8_t value) { (void)value; }
void video_set_screen_init(uint8_t value) { (void)value; }
void video_set_vram_address(uint16_t word) { (void)word; }
void video_set_bg_mode(uint8_t value) { (void)value; }
void video_set_bg_map(int bg, uint8_t value) { (void)bg; (void)value; }
void video_set_bg_tiles(int pair, uint8_t value) { (void)pair; (void)value; }
void video_set_obj_select(uint8_t value) { (void)value; }
void video_set_display(uint8_t value) { (void)value; }
void video_set_oam_address(uint16_t value) { (void)value; }
void video_set_main_screen(uint8_t layers) { (void)layers; }
void video_set_sub_screen(uint8_t layers) { (void)layers; }
void video_set_color_math(uint8_t value) { (void)value; }
void video_set_color_math_control(uint8_t value) { (void)value; }
void video_set_fixed_color(uint8_t value) { (void)value; }
void video_set_window_bg12(uint8_t value) { (void)value; }
void video_set_window_bg34(uint8_t value) { (void)value; }
void video_set_window_obj(uint8_t value) { (void)value; }
void video_set_cgram_address(uint8_t index) { (void)index; }
void video_upload_oam(uint16_t oam_word, uint32_t src, uint32_t size) {
    (void)oam_word;
    (void)src;
    (void)size;
}
void platform_set_rom_speed(uint8_t value) { (void)value; }
void platform_set_wram_address(uint32_t address) { (void)address; }

// Per-line (owner's grey zone): ignored; a screen's own code may read the table the game computed, case by case
void dma_hdma_setup(int slot, uint8_t mode, uint8_t target, uint32_t table) {
    (void)slot;
    (void)mode;
    (void)target;
    (void)table;
}
void dma_hdma_set_indirect_bank(int slot, uint8_t bank) { (void)slot; (void)bank; }
void dma_hdma_set_table(int slot, uint16_t table) { (void)slot; (void)table; }
void dma_hdma_set_mode(int slot, uint8_t mode, uint8_t target) {
    (void)slot;
    (void)mode;
    (void)target;
}
void dma_hdma_set_bank(int slot, uint8_t bank) { (void)slot; (void)bank; }
void video_hdma_enable(uint8_t slots) { (void)slots; }
void dma_stream_setup(int slot, uint8_t mode, uint8_t target, uint32_t src) {
    (void)slot;
    (void)mode;
    (void)target;
    (void)src;
}
void dma_stream_size(int slot, uint32_t size) { (void)slot; (void)size; }
void dma_stream_kick(uint8_t slots) { (void)slots; }
void platform_set_irq_v(uint16_t line) { (void)line; }
void platform_set_irq_h(uint16_t dot) { (void)dot; }
void platform_set_irq_h_low(uint8_t dot) { (void)dot; }
void platform_set_irq_v_low(uint8_t line) { (void)line; }
void platform_set_irq_v_high(uint8_t line) { (void)line; }

// Added by the decomp at 49b5583 (VS race work).
// The Mode 7 vertical offset alone: a screen register, ignored like the others.
void video_set_m7_vofs(uint16_t value) { (void)value; }
// A stream slot's source address alone: per-line machinery, ignored like the other dma_* calls.
void dma_stream_set_source(int slot, uint16_t src) { (void)slot; (void)src; }

// Sound driver start-up (decomp e966da4, request 4 step 1). These hand the sound CPU its
// program; the port runs no sound CPU program (the rule), so they are accepted and ignored.
// The sets of songs, instruments and samples arrive through the scene loader's calls.
void sound_boot_begin(void) {}
void sound_boot_block(uint16_t spc_dest, uint32_t src, uint16_t size) { (void)spc_dest; (void)src; (void)size; }
void sound_boot_start(uint16_t spc_entry) { (void)spc_entry; }
void sound_clear_ports(void) {}

// Added by the decomp at d0eaf01 (request I): the ROM source of the next unpacked upload.
// Ignored for now: the port recognises those graphics by their content (port/video/assets.c),
// which stays checked; the name may replace the content hash later.
void video_note_source(uint32_t rom, uint16_t vram_word) { (void)rom; (void)vram_word; }
