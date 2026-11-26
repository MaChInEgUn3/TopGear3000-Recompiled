// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's sound sets as it hands them over (decomp 2d72646, request 4 step 2;
// docs/proposals/sound.md). Each scene change sends one set: a songs block, an instruments and
// settings block, the samples one by one, then the sample directory. The port keeps the songs,
// instruments and samples by ROLE (never as a copy of the sound CPU's memory); the directory is
// ignored (each sample arrives with its slot and loop). The native player reads this (step 3).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "port.h"
#include "tg3000.h"

// Destinations that name a block's role (measured: docs/proposals/sound.md, notes on the 8 sets).
#define SOUND_DEST_SONGS 0x1600u
#define SOUND_DEST_INSTRUMENTS 0x0400u
// The sample directory arrives at the set's directory address (measured: $4000, $2000, $2500).
static int is_directory_dest(uint16_t d) { return d == 0x4000u || d == 0x2000u || d == 0x2500u; }

typedef struct {
    uint8_t* bytes;
    uint16_t size;
    uint16_t loop_offset;  // loop point as an offset into the sample (the game gives an address)
    int present;
} port_sample;

static uint8_t* g_songs;
static uint16_t g_songs_size;
static uint8_t* g_instruments;
static uint16_t g_instruments_size;
static port_sample g_samples[32];
static int g_in_upload;

static uint8_t* copy_bus(uint32_t src, uint16_t size) {
    uint8_t* p = (uint8_t*)malloc(size ? size : 1u);
    if (p == NULL) port_fatal("sound: out of memory");
    for (uint16_t i = 0; i < size; ++i) p[i] = bus_rd8(src + i);
    return p;
}

void sound_upload_begin(void) {
    if (g_in_upload) port_fatal("sound: upload begun twice");
    g_in_upload = 1;
}

void sound_upload_end(void) {
    if (!g_in_upload) port_fatal("sound: upload ended without a begin");
    g_in_upload = 0;
}

void sound_upload_block(uint16_t spc_dest, uint32_t src, uint16_t size) {
    if (spc_dest == SOUND_DEST_SONGS) {
        // A new set starts with its songs: forget the previous set's samples.
        for (int i = 0; i < 32; ++i) { free(g_samples[i].bytes); memset(&g_samples[i], 0, sizeof g_samples[i]); }
        free(g_songs);
        g_songs = copy_bus(src, size);
        g_songs_size = size;
    } else if (spc_dest == SOUND_DEST_INSTRUMENTS) {
        free(g_instruments);
        g_instruments = copy_bus(src, size);
        g_instruments_size = size;
    } else if (is_directory_dest(spc_dest)) {
        // ignored: every sample already came with its slot and loop
    } else {
        char msg[96];
        snprintf(msg, sizeof msg, "sound: a block for an unknown place $%04X", (unsigned)spc_dest);
        port_fatal(msg);
    }
}

void sound_upload_sample(uint8_t slot, uint16_t spc_dest, uint32_t src, uint16_t size, uint16_t spc_loop) {
    if (slot >= 32u) port_fatal("sound: sample slot out of the measured range");
    if (spc_loop < spc_dest || spc_loop >= (uint32_t)spc_dest + size)
        port_fatal("sound: sample loop point outside the sample");
    port_sample* s = &g_samples[slot];
    free(s->bytes);
    s->bytes = copy_bus(src, size);  // BRR bytes from the cartridge; converted by the player
    s->size = size;
    s->loop_offset = (uint16_t)(spc_loop - spc_dest);
    s->present = 1;
}
