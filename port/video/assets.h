// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_VIDEO_ASSETS_H
#define TG3000_VIDEO_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ROLE_BG_DRAWINGS = 0,
    ROLE_BG_ARRANGEMENT,
    ROLE_TEXT_DRAWINGS_128,
    ROLE_TEXT_DRAWINGS_256,
    ROLE_OBJ_DRAWINGS,
    ROLE_OBJ_DRAWINGS_HI, /* object drawings starting at tile 320 */
    ROLE_COUNT
} asset_role;

typedef enum {
    ASSET_MENU_BG_DRAWINGS = 0,
    ASSET_MENU_BG_ARRANGEMENT,
    ASSET_TITLE_TEXT_DRAWINGS,
    ASSET_TITLE_OBJ_DRAWINGS,
    ASSET_PASSWORD_TEXT_DRAWINGS,
    ASSET_MENU_TEXT_A,
    ASSET_MENU_TEXT_B,
    ASSET_MENU_CURSOR_DRAWINGS,
    ASSET_VS_BG_DRAWINGS,
    ASSET_VS_PANELS_ARRANGEMENT,
    ASSET_RESULTS_STARS_DRAWINGS,
    ASSET_COUNT
} asset_id;

/* Decompress and hash all known cartridge blocks after g_rom is set and before the game runs. */
void assets_init(void);

/* Note a graphics upload from WRAM $7F:0000 (matched by size+hash) or direct ROM address. */
void assets_note_upload(uint32_t src, uint32_t size);

/* Append a word written to VRAM; matches a known "word run" block by length and FNV-1a 32 hash. */
void assets_note_word(uint16_t value);

/* Reset the running word-write sequence (called on any other video event). */
void assets_reset_word_run(void);

/* Mark all roles empty (the game cleared all graphics). */
void assets_clear_loaded(void);

int asset_current(asset_role r);
const uint8_t* asset_bytes(int id, size_t* size);
uint32_t asset_generation(int id);

#endif
