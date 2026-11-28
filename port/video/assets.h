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
    ASSET_CHAMP_NAME_BG_TILES,  /* $8C:8000, 5536 B (173 4bpp tiles: NAME entry BG 32..204) */
    ASSET_CHAMP_CTRL_BG_TILES,  /* $9F:8000, 6624 B (207 4bpp tiles: CONTROL BG 32..238) */
    ASSET_CHAMP_GEARS_BG_TILES, /* $8C:8B54, 5312 B (166 4bpp tiles: SPEED/GEARS BG 32..197) */
    ASSET_CHAMP_GEARS_OBJ_TILES,/* $87:898E, 1536 B (48 4bpp tiles: SPEED/GEARS OBJ 208..255) */
    ASSET_RACESTART_SHUTTER_MAP_A, /* $9C:8FC3[0x0000..0x07FF], 2048 B (BG2 shutter map top-left) */
    ASSET_RACESTART_SHUTTER_MAP_B, /* $9C:8FC3[0x0800..0x0FFF], 2048 B (BG2 shutter map bottom-left) */
    ASSET_RACESTART_BG_TILES,      /* $92:80B2[0x0000..0x24DF], 9440 B (295 4bpp tiles: BG1/BG2 64..358) */
    ASSET_RACESTART_OBJ_TILES,     /* $92:80B2[0x24E0..0x3F1F], 6720 B (210 4bpp tiles: OBJ 256..465) */
    ASSET_RACESTART_BG3_TILES,     /* $92:80B2[0x3F20..0x3F3F], 32 B (2 2bpp tiles: BG3 0..1) */
    ASSET_RACESTART_SUN_FRAMES,    /* $92:80B2[0x3F40..0x873F], 18432 B (16 frames x 36 4bpp tiles: BG1 476..511) */
    ASSET_SHOP_BG_TILES_A,         /* $8D:8000, 9728 B (304 4bpp tiles: shop BG 1..304) */
    ASSET_SHOP_BG_TILES_B,         /* $88:8000, 4320 B (135 4bpp tiles: shop BG 305..439) */
    ASSET_SHOP_OBJ_ICONS,          /* $8D:895F, 2688 B (84 4bpp tiles: shop OBJ 80..163) */
    ASSET_SHOP_BG2_MAP,            /* $87:8000, 896 B (the shop's BG2 map, 14 rows of 32) */
    ASSET_SHOP_LIST_PAINT,         /* $8F:8000, 1664 B (list 0's pictures, unpacked to $7F:0900) */
    ASSET_SHOP_LIST_ENGINE,        /* $99:8000, 15616 B (list 1's) */
    ASSET_SHOP_LIST_TIRES,         /* $9D:8000, 9248 B (list 2's) */
    ASSET_SHOP_LIST_GEARBOX,       /* $9C:8040, 12992 B (list 3's) */
    ASSET_SHOP_LIST_ARMOR,         /* $9E:8000, 10976 B (list 4's) */
    ASSET_SHOP_LIST_BOOST,         /* $9D:8F55, 13568 B (list 5's) */
    ASSET_SHOP_LIST_SPECIAL,       /* $97:8000 (lists 6-10's, the specials) */
    ASSET_TICKER_M7_TILES,         /* $88:8C7D, 6464 B (the ticker's Mode 7 drawings: 101 8bpp tiles) */
    ASSET_TICKER_M7_MAP,           /* $95:8000 (the ticker's Mode 7 map: 7 rows of 17 tiles) */
    ASSET_RACE_HUD_FONT,           /* $8F:85F5, 2432 B (the race HUD's 152 2bpp tiles: BG3 $158..$1EF) */
    ASSET_RACE_HUD_MAP,            /* $87:8B3D, 1792 B (the race HUD's BG3 map, rows 0-27) */
    ASSET_RACE_TRACK_SET_0,        /* the 9 packed track tile sets ($83:8585/$8597 by $83:8447) */
    ASSET_RACE_TRACK_SET_1,
    ASSET_RACE_TRACK_SET_2,
    ASSET_RACE_TRACK_SET_3,
    ASSET_RACE_TRACK_SET_4,
    ASSET_RACE_TRACK_SET_5,
    ASSET_RACE_TRACK_SET_6,
    ASSET_RACE_TRACK_SET_7,
    ASSET_RACE_TRACK_SET_8,
    ASSET_RACE_BACKDROP,           /* $98:9FEB, 52672 B unpacked (track_backdrop_load: the road's
                                      tiles and map rows 0-23 of every track, slices by track) */
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

/* A streamed upload (dma_stream_setup/size/kick joined: WRAM source and size only) and the
   sprite-table upload: recognised for the pre-race screen by source and size. */
void assets_note_stream(uint32_t src, uint32_t size);
void assets_note_oam(uint32_t src, uint32_t size);

/* The colours the frame code sends (video_upload_cgram): kept for the shop by source and size. */
void assets_note_cgram(uint8_t index, uint32_t src, uint32_t size);

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
#define CHAMP_SUBMENU_PATCH_BYTES 512u
#define CHAMP_NOISE_TILES_BYTES 512u

typedef struct {
    bool has_bg1_map;
    bool has_bg2_map;
    bool has_fire_band;
    bool has_bg2_panel_rows;
    bool has_noise_tiles;
    bool has_gears_obj_tiles;
    int bg_b_asset;
    uint32_t noise_gen;
    uint8_t bg1_map[CHAMP_MAP_BYTES];
    uint8_t bg2_map[CHAMP_MAP_BYTES];
    uint8_t fire_band[CHAMP_FIRE_BAND_BYTES];
    uint8_t bg2_panel_rows[CHAMP_PANEL_ROWS_BYTES];
    uint8_t noise_tiles[CHAMP_NOISE_TILES_BYTES];
} champ_half_pieces;

bool assets_champ_loaded(void);
const champ_half_pieces* assets_champ_half(int half);

#define RACESTART_MAP_BYTES 0x0700u /* 32x28 words = 1792 bytes */
#define RACESTART_OAM_BYTES 0x0220u
#define RACESTART_MAX_PLANETS 6u
#define RACESTART_SHADOW_BYTES 0x0080u
#define RACESTART_METER_WORDS 10u

typedef struct {
    bool has_shutter_a;
    bool has_shutter_b;
    bool has_obj_font;
    bool has_bg_tiles;
    bool has_obj_tiles;
    bool has_bg3_tiles;
    bool has_bg1_left;
    bool has_bg1_right;
    bool has_bg2_tl;
    bool has_bg2_tr;
    bool has_bg3_left;
    bool has_bg3_right;
    uint8_t bg1_left_map[RACESTART_MAP_BYTES];
    uint8_t bg1_right_map[RACESTART_MAP_BYTES];
    uint8_t bg2_tl_map[RACESTART_MAP_BYTES];
    uint8_t bg2_tr_map[RACESTART_MAP_BYTES];
    uint8_t bg3_left_map[RACESTART_MAP_BYTES];
    uint8_t bg3_right_map[RACESTART_MAP_BYTES];
    /* Handed over each frame (not computed): */
    bool has_oam;               /* the sprite table the NMI sent ($81:0900, $220 bytes) */
    uint8_t oam[RACESTART_OAM_BYTES];
    uint8_t shadows_sent;       /* bit n: planet n's shadow frame was streamed */
    uint8_t shadow[RACESTART_MAX_PLANETS][RACESTART_SHADOW_BYTES]; /* from $7E:8000+n*$800+f*$80 */
    bool has_meter;             /* the meter's 10 words as draw_meter wrote them */
    uint16_t meter[RACESTART_METER_WORDS];
    bool has_sun;               /* the animated sun's frame was streamed ($7F:6640 + k*$480) */
    uint8_t sun_frame;          /* k: the frame of $92:80B2's 16 sun frames the source names */
} racestart_pieces;

/* The shop ($80:8BC5, notes/probe/SHOP.md). Per half (task 1: $7F:0000.., task 2: $7F:8000..):
   pieces captured when the game sends them, by WRAM source and size. */
#define SHOP_HALF_COUNT 2
#define SHOP_BG1_MAP_BYTES 0x03C0u     /* 15 rows of 32 words, from $7F:base */
#define SHOP_NOISE_TILES_BYTES 0x0200u /* 16 4bpp tiles, from $7F:base+$0700 */
#define SHOP_OAM_BYTES 0x0220u
#define SHOP_COLOUR_BYTES 0x0200u
#define SHOP_MAX_PIECES 16

/* A part of an item picture in place: `count` drawings from tile `first_tile` on, the slice's
   drawings from `slice_tile` on (row `slice` of the measured slice table). */
typedef struct {
    int slice;
    uint16_t first_tile;
    uint16_t count;
    uint16_t slice_tile;
} shop_picture_piece;

typedef struct {
    bool bg2_map_noted;     /* $87:8000 unpacked for this half (video_note_source) */
    bool bg2_map_sent;      /* ...and sent from $7F:base (896 bytes, the block's content) */
    bool has_bg1_map;
    bool has_noise_tiles;
    bool has_oam;
    bool has_colours;
    uint32_t noise_gen;
    int piece_count;
    shop_picture_piece piece[SHOP_MAX_PIECES]; /* the item pictures' drawings now in place */
    uint8_t bg1_map[SHOP_BG1_MAP_BYTES];
    uint8_t noise_tiles[SHOP_NOISE_TILES_BYTES];
    uint8_t oam[SHOP_OAM_BYTES];           /* the sprite table as the frame code sent it */
    uint8_t colours[SHOP_COLOUR_BYTES];    /* the 256 colours as the frame code sent them */
} shop_half_pieces;

/* An item picture: a slice of a list's unpacked block, sent from $7F:base+$0900+offset (whole
   or in parts, as the upload budget allows), that serves the object drawings from first_tile
   on (measured, notes/probe/SHOP.md). */
typedef struct {
    int block;          /* asset id of the list block */
    uint16_t offset;    /* in the block */
    uint16_t size;      /* bytes */
    uint16_t first_tile;
} shop_picture_slice;

bool assets_shop_loaded(void);
const shop_half_pieces* assets_shop_half(int half);
const shop_picture_slice* assets_shop_picture(int row);

/* The ticker ($80:E4E6, notes/probe/TICKER.md). Its load: the font $88:8601 noted and sent
   (the top half's object drawings), then three streams: the Mode 7 drawings ($7F:0000, the
   unpacked $88:8C7D), the map cleared (the fixed byte at $81:CBF8, $4000 bytes), and the map's
   7 rows of 17 tiles ($7F:0000 + 17 * row, the unpacked $95:8000). Then, each frame, the top
   half's sprite table and colours as the NMI sends them. */
#define TICKER_MAP_ROWS 7u
#define TICKER_MAP_COLS 17u
#define TICKER_OAM_BYTES 0x0220u
#define TICKER_COLOUR_BYTES 0x0200u

typedef struct {
    bool font_noted;     /* video_note_source($88:8601) */
    bool font_sent;      /* ...and its block sent from $7F:0000 */
    bool tiles_sent;     /* the Mode 7 drawings streamed from $7F:0000 */
    bool map_cleared;    /* the fixed-byte stream from $81:CBF8, $4000 bytes */
    uint8_t rows_sent;   /* bit r: the map's row r streamed */
    bool has_oam;
    bool has_colours;
    uint8_t oam[TICKER_OAM_BYTES];          /* the top half's sprite table as the NMI sent it */
    uint8_t colours[TICKER_COLOUR_BYTES];   /* the top half's 256 colours as the NMI sent them */
} ticker_pieces;

bool assets_ticker_loaded(void);
const ticker_pieces* assets_ticker(void);

/* The one-player race's first slice (the sky, the horizon, the HUD; notes/probe/RACE.md §12-21,
   docs/DECISIONS.md "The race's sky and HUD drawn"). Recognised while the race NMI ($85:8377) is
   the vector, by source, in the load's order (race_load, track_tiles_load): the track's tile set
   made planar (BG1/BG2 tiles $2FE on, its bytes against one of the 9 unpacked sets at that
   offset), its four map slices (BG1 map rows 24-31 left and right, then BG2's), the HUD font
   ($8F:85F5 noted and sent), the HUD map (the unpacked $87:8B3D as sent; all but its 26 patched
   words checked against the block), the markers' 4 words (hud_markers_put). Then, as the NMIs
   send them: the HUD's 52 words (race_hud_draw, captured by call pattern: request T) and its two
   strips, the minimap's tiles ($7E:230C), colours $71-$EF ($7E:6A35), the sprite table.
   The road (second slice): track_backdrop_load's five uploads, in its order, right after the
   HUD map: the road's tiles (BG1/BG2 tiles $080 on; their bytes against the unpacked $98:9FEB
   at that offset), BG1's map rows 0-23 left and right (the block's bytes at their offsets), then
   BG2's (the same words with priority cleared and palette + 1, as the routine writes them:
   checked against the block, kept as sent; BG2 shows inside the road's windows). */
#define RACE_MAP_SLICE_BYTES 0x0200u /* 8 map rows of 32 words */
#define RACE_ROAD_MAP_BYTES 0x0600u  /* 24 map rows of 32 words */
#define RACE_ROAD_FIRST_TILE 0x080u  /* BG tile of the road's first tile (VRAM $3800) */
#define RACE_HUD_MAP_BYTES 0x0700u   /* BG3 map rows 0-27 */
#define RACE_HUD_WORDS 52u
#define RACE_STRIP_BYTES 0x001Au     /* 13 words */
#define RACE_MINIMAP_BYTES 0x0190u   /* 25 2bpp tiles, BG3 $0F4..$10C */
#define RACE_COLOUR_BYTES 0x00FEu    /* colours $71..$EF */
#define RACE_OAM_BYTES 0x0220u
#define RACE_MARKER_WORDS 4u

typedef struct {
    uint32_t generation;   /* +1 at each race load seen */
    int tile_set;          /* asset id of the track tile set the BG1/BG2 tiles came from */
    uint16_t tile_offset;  /* their offset in that unpacked set */
    uint16_t tile_count;   /* 4bpp tiles from BG tile $2FE on */
    int maps_sent;         /* 0..4: BG1 rows 24-31 left, right; BG2's left, right */
    uint8_t map[4][RACE_MAP_SLICE_BYTES];
    bool font_noted;
    bool has_font;
    bool has_hud_map;
    uint8_t hud_map[RACE_HUD_MAP_BYTES];
    bool has_markers;      /* the 4 words $0157 at $7C57.. (all markers unset: measured) */
    int road_sent;         /* 0..5: track_backdrop_load's uploads seen (tiles, 2 maps, BG2's 2) */
    uint16_t road_tile_offset; /* the road's tiles: their offset in the unpacked $98:9FEB */
    uint16_t road_tile_count;  /* 4bpp tiles from BG tile $080 on */
    uint16_t road_map_offset[2];
    uint8_t road_map[4][RACE_ROAD_MAP_BYTES]; /* map rows 0-23: BG1's left, right; BG2's */
    /* Handed over by the NMIs, not computed: */
    bool hud_pending;      /* a 52-word run ended; taken when its first strip follows */
    uint16_t hud_pending_words[RACE_HUD_WORDS];
    bool has_hud;
    uint16_t hud[RACE_HUD_WORDS];
    int strips_sent;       /* 0..2 of the last HUD draw */
    bool has_strips;
    uint8_t strip[2][RACE_STRIP_BYTES];
    bool has_minimap;
    uint8_t minimap[RACE_MINIMAP_BYTES];
    bool has_colours;
    uint8_t colours[RACE_COLOUR_BYTES];
    bool has_oam;
    uint8_t oam[RACE_OAM_BYTES];
} race_pieces;

/* The race NMI is the vector ($0000 = JML $85:8377). */
bool assets_race_vector(void);
bool assets_race_loaded(void);
const race_pieces* assets_race(void);

bool assets_racestart_shutter_loaded(void);
bool assets_racestart_loaded(void);
const racestart_pieces* assets_racestart(void);

#endif
