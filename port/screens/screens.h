// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_SCREENS_H
#define TG3000_SCREENS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "port.h"

#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 224

/* Decode the game's brightness byte ($019B): 0..15, or 0 when bit 7 (forced blank) is set.
   Stops if unmeasured bits 4..6 are non-zero. */
static inline uint8_t screen_decode_brightness(const char* screen_name, uint8_t b_raw) {
    if ((b_raw & 0x70u) != 0u) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: unexpected brightness byte 0x%02X", screen_name,
                 (unsigned)b_raw);
        port_fatal(msg);
    }
    return ((b_raw & 0x80u) != 0u) ? 0u : (b_raw & 0x0Fu);
}

/* Scale lines y0..y1-1 of the framebuffer by the game's brightness 0..15. Measured on the
   bench's pictures (fade frames of the pre-race screen, the shop and the ticker, brightness
   1..14): each 8-bit channel c becomes (c * k) >> 16 with k = (b << 16) / 15 (integer). */
static inline void screen_apply_brightness_lines(uint32_t* fb, int y0, int y1, uint8_t b) {
    if (b == 0u) {
        for (int i = y0 * SCREEN_WIDTH; i < y1 * SCREEN_WIDTH; ++i) {
            fb[i] = 0xFF000000u;
        }
        return;
    }
    if (b < 15u) {
        const uint32_t k = ((uint32_t)b << 16) / 15u;
        for (int i = y0 * SCREEN_WIDTH; i < y1 * SCREEN_WIDTH; ++i) {
            uint32_t px = fb[i];
            uint32_t r = (((px >> 16) & 0xFFu) * k) >> 16;
            uint32_t g = (((px >> 8) & 0xFFu) * k) >> 16;
            uint32_t bl = ((px & 0xFFu) * k) >> 16;
            fb[i] = 0xFF000000u | (r << 16) | (g << 8) | bl;
        }
    }
}

/* Scale the whole 256x224 framebuffer by the game's brightness 0..15. */
static inline void screen_apply_brightness(uint32_t* fb, uint8_t b) {
    screen_apply_brightness_lines(fb, 0, SCREEN_HEIGHT, b);
}

int menu_screen_active(void);
void menu_screen_draw(int which, uint32_t* fb);
const char* menu_screen_name(int which);

bool results_screen_active(void);
void results_screen_draw(uint32_t* fb);

bool intro_screen_active(void);
void intro_screen_draw(uint32_t* fb);

bool title_screen_active(void);
void title_screen_draw(uint32_t* fb);

bool champ_screen_active(void);
void champ_screen_draw(uint32_t* fb);
/* Each half taken as its interrupt sends it: the top at the NMI, the bottom at the IRQ. */
void champ_screen_note_split(void);
void champ_screen_note_nmi(void);
void champ_screen_note_irq(void);

bool shop_screen_active(void);
void shop_screen_draw(uint32_t* fb);

/* The ticker's view block is taken when the NMI runs (two-task branch of frame.c). */
void ticker_screen_note_nmi(void);
bool ticker_screen_active(void);
void ticker_screen_draw(uint32_t* fb);

/* The pre-race screen is drawn at the start of a frame wait, before the NMI (frame.c). */
void race_start_screen_note_nmi(uint16_t target);
bool race_start_screen_active(void);
/* The bar's first frame, right after its first NMI: drawn then (it has no frame-code pieces). */
bool race_start_screen_first_after_nmi(void);
void race_start_screen_draw(uint32_t* fb);

/* The one-player race's first slice (sky, horizon, HUD), drawn after the NMI; the race NMI's
   target is noted before it runs (frame.c). The mask says which pixels were drawn (1) and
   which were left black for the slices not drawn yet (0): tests compare only the drawn ones. */
void race_screen_note_nmi(uint16_t target);
bool race_screen_active(void);
void race_screen_draw(uint32_t* fb);
const uint8_t* race_screen_drawn_mask(void);

#endif
