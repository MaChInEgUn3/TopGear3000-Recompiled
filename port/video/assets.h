// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_VIDEO_ASSETS_H
#define TG3000_VIDEO_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ROLE_BG_DRAWINGS = 0,
    ROLE_BG_ARRANGEMENT,
    ROLE_TEXT_DRAWINGS,
    ROLE_OBJ_DRAWINGS,
    ROLE_COUNT
} asset_role;

typedef enum {
    ASSET_MENU_BG_DRAWINGS = 0,
    ASSET_MENU_BG_ARRANGEMENT,
    ASSET_TITLE_TEXT_DRAWINGS,
    ASSET_TITLE_OBJ_DRAWINGS,
    ASSET_COUNT
} asset_id;

/* Decompress and hash all known cartridge blocks after g_rom is set and before the game runs. */
void assets_init(void);

/* Note a graphics upload from WRAM $7F:0000 (matched by size+hash) or direct ROM address. */
void assets_note_upload(uint32_t src, uint32_t size);

/* Mark all roles empty (the game cleared all graphics). */
void assets_clear_loaded(void);

int asset_current(asset_role r);
const uint8_t* asset_bytes(int id, size_t* size);

#endif
