// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_VIDEO_ASSETS_H
#define TG3000_VIDEO_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ASSET_BG_TITLE_DRAWINGS = 0,
    ASSET_BG_TITLE_ARRANGEMENT,
    ASSET_TEXT_TITLE_DRAWINGS,
    ASSET_OBJ_TITLE_DRAWINGS,
    ASSET_COUNT
} asset_id;

/* Decompress and hash all known cartridge blocks after g_rom is set and before the game runs. */
void assets_init(void);

/* Note a graphics upload from WRAM $7F:0000 (matched by size+hash) or direct ROM address. */
void assets_note_upload(uint32_t src, uint32_t size);

/* Mark all blocks unloaded (the game cleared all graphics). */
void assets_clear_loaded(void);

bool asset_loaded(int id);
const uint8_t* asset_bytes(int id, size_t* size);

#endif
