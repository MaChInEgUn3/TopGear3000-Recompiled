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
    ASSET_INTRO_TEXT_DRAWINGS,
    ASSET_INTRO_M7_DRAWINGS,
    ASSET_INTRO_M7_MAP,
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

typedef enum {
    INTRO_PIECE_BG1_A = 0,   /* $7E:2000, 1184 bytes (BG1 tiles 16..52) */
    INTRO_PIECE_BG1_B,       /* $7E:24A0, 1184 bytes (BG1 tiles 53..89) */
    INTRO_PIECE_SLICE_0,     /* $7E:3040 + s * 0x0614, 1556 bytes (s = 0..15) */
    INTRO_PIECE_SLICE_1,
    INTRO_PIECE_SLICE_2,
    INTRO_PIECE_SLICE_3,
    INTRO_PIECE_SLICE_4,
    INTRO_PIECE_SLICE_5,
    INTRO_PIECE_SLICE_6,
    INTRO_PIECE_SLICE_7,
    INTRO_PIECE_SLICE_8,
    INTRO_PIECE_SLICE_9,
    INTRO_PIECE_SLICE_10,
    INTRO_PIECE_SLICE_11,
    INTRO_PIECE_SLICE_12,
    INTRO_PIECE_SLICE_13,
    INTRO_PIECE_SLICE_14,
    INTRO_PIECE_SLICE_15,
    INTRO_PIECE_FRAME_0,     /* $7E:9180, 1556 bytes */
    INTRO_PIECE_FRAME_1,     /* $7E:9768, 1556 bytes */
    INTRO_PIECE_FRAME_2,     /* $7E:9D50, 1556 bytes */
    INTRO_PIECE_FRAME_3,     /* $7EA338, 1556 bytes */
    INTRO_PIECE_COUNT
} intro_piece_id;

bool assets_intro_piece_sent(intro_piece_id id);

#endif
