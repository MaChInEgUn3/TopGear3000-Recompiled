// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_VIDEO_ASSETS_H
#define TG3000_VIDEO_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ROLE_NONE = -1,
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
    ASSET_TITLE_M7_TILES_A,
    ASSET_TITLE_M7_TILES_B,
    ASSET_TITLE_M7_MAP,
    ASSET_TITLE_OBJ_0,
    ASSET_TITLE_OBJ_1,
    ASSET_TITLE_OBJ_2,
    ASSET_TITLE_OBJ_3,
    ASSET_CHAMP_OBJ_FONT,       /* $87:8190, 2560 B (80 4bpp tiles: OBJ 256..335) */
    ASSET_CHAMP_OBJ_BAR,        /* $90:C320, 96 B (3 4bpp tiles: OBJ 336..338) */
    ASSET_CHAMP_OBJ_GRAPHICS,   /* $87:86BE, 5120 B (160 4bpp tiles: OBJ 340..499) */
    ASSET_CHAMP_BG_TILES_A,     /* $8F:83CC, 960 B (30 4bpp tiles: BG 0..29) */
    ASSET_CHAMP_BG_OBJ_TILES_B, /* $98:8000, 10208 B (319 4bpp tiles: BG 32..350 and OBJ 0..94) */
    ASSET_COUNT
} asset_id;

/* Decompress and hash all known cartridge blocks after g_rom is set and before the game runs. */
void assets_init(void);

/* The VS panels' arrangement named by the game (request J): built from its 14 WRAM rows. */
void assets_note_vs_panels(uint32_t rows);

/* Note the ROM source address of an unpacked upload (from video_note_source). */
void assets_note_source(uint32_t rom);

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
bool assets_title_objs_loaded(void);

#define CHAMP_HALF_COUNT 2
#define CHAMP_MAP_BYTES 2048u
#define CHAMP_FIRE_BAND_BYTES 512u
#define CHAMP_PANEL_ROWS_BYTES 896u
#define CHAMP_NOISE_TILES_BYTES 512u

typedef struct {
    bool has_bg1_map;
    bool has_bg2_map;
    bool has_fire_band;
    bool has_bg2_panel_rows;
    bool has_noise_tiles;
    uint32_t noise_gen;
    uint8_t bg1_map[CHAMP_MAP_BYTES];
    uint8_t bg2_map[CHAMP_MAP_BYTES];
    uint8_t fire_band[CHAMP_FIRE_BAND_BYTES];
    uint8_t bg2_panel_rows[CHAMP_PANEL_ROWS_BYTES];
    uint8_t noise_tiles[CHAMP_NOISE_TILES_BYTES];
} champ_half_pieces;

bool assets_champ_loaded(void);
const champ_half_pieces* assets_champ_half(int half);

#endif
