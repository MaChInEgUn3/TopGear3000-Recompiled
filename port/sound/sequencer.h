// SPDX-License-Identifier: GPL-3.0-or-later
//
// The native song player (docs/DECISIONS.md, "The native sound player (set 1)"): plays the
// game's songs and sound effects from the set the game loaded, one step per game message, and
// drives the port's mixer (mixer.h). Written against the game's song format with named state;
// it runs no sound-CPU program and models no sound chip. Spec: notes/sound/SEQUENCER.md (local).
#ifndef TG3000_SOUND_SEQUENCER_H
#define TG3000_SOUND_SEQUENCER_H

#include <stddef.h>
#include <stdint.h>

#include "convert_audio.h"

/* A new set is about to replace the current one: silence, forget the song and the set. */
void sequencer_new_set(void);
/* The set's songs block and its instruments-and-settings block, as the game sent them (kept by
   sound_sets.c until the next set). The instruments are converted here, once. */
void sequencer_songs_loaded(const uint8_t* songs, size_t size);
void sequencer_instruments_loaded(const uint8_t* block, size_t size);
/* The driver's left/right balance table (16 entries), read from its start-up upload. */
void sequencer_balance_loaded(const uint8_t table[16]);

/* One step of the player: the game's message for this frame (sound_send_frame). */
void sequencer_message(const uint8_t msg[16]);

/* Test: write every event the player sends to the mixer to `path` and print a fingerprint of
   them at exit. */
void sequencer_open_events(const char* path);

/* The set's sample in `slot`, or NULL (sound_sets.c). */
const converted_sample* sound_set_sample(unsigned slot);

#endif
