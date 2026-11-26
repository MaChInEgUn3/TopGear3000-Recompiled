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

/* Scale the 256x224 framebuffer by the game's brightness 0..15 (linear: b * 17 runs 0..255). */
static inline void screen_apply_brightness(uint32_t* fb, uint8_t b) {
    if (b == 0u) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            fb[i] = 0xFF000000u;
        }
        return;
    }
    if (b < 15u) {
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; ++i) {
            uint32_t px = fb[i];
            /* linear in the game's brightness 0-15: b * 17 runs 0..255 */
            uint32_t r = (((px >> 16) & 0xFFu) * b * 17u) >> 8;
            uint32_t g = (((px >> 8) & 0xFFu) * b * 17u) >> 8;
            uint32_t bl = ((px & 0xFFu) * b * 17u) >> 8;
            fb[i] = 0xFF000000u | (r << 16) | (g << 8) | bl;
        }
    }
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

bool shop_screen_active(void);
void shop_screen_draw(uint32_t* fb);

/* The pre-race screen is drawn at the start of a frame wait, before the NMI (frame.c). */
void race_start_screen_note_nmi(uint16_t target);
bool race_start_screen_active(void);
/* The bar's first frame, right after its first NMI: drawn then (it has no frame-code pieces). */
bool race_start_screen_first_after_nmi(void);
void race_start_screen_draw(uint32_t* fb);

#endif
