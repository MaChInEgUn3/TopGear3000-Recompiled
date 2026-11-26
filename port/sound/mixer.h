// SPDX-License-Identifier: GPL-3.0-or-later
//
// The port's own mixer (docs/DECISIONS.md, "The native sound player (set 1)"): a few voices,
// each playing one converted sample at a rate in Hz with a left/right gain and an envelope in
// seconds. Its interpolation and envelope curves are its own; it knows nothing of the sound
// chip's registers. Audio goes to an SDL3 audio stream and/or a WAV file, one game frame at a
// time, from the main thread.
#ifndef TG3000_SOUND_MIXER_H
#define TG3000_SOUND_MIXER_H

#include <stdbool.h>

#include "convert_audio.h"

#define MIXER_VOICES 8

/* Start a note on `voice`: `sample` from its start, at `hz`, gains 0..1, `env` from silence.
   `let_go` starts it already let go (its envelope falls at the sustain rate after the decay).
   `echo`: the note also feeds the echo. */
void mixer_start(int voice, const converted_sample* sample, double hz, float left, float right,
                 const sound_envelope* env, bool let_go, bool echo);
void mixer_set_pitch(int voice, double hz);
void mixer_set_volume(int voice, float left, float right);
/* Let the note go: once its decay reaches the sustain level, it falls at the sustain rate. */
void mixer_let_go(int voice);
/* Cut the note: a short fade to silence (the envelope's release time). */
void mixer_cut(int voice);
/* Silence every voice at once and forget their samples (a new set is about to replace them). */
void mixer_stop_all(void);
void mixer_set_master(float gain);
/* The echo: the voices that feed it, delayed by `delay_s`, come back at `volume` (not scaled
   by the master gain); `feedback` of the delayed sound goes round again. A new delay starts
   from silence. */
void mixer_set_echo(float delay_s, float feedback, float volume);

/* Output. The device is opened only when asked (not in headless runs); a WAV file may be written
   at the same time. Each call to mixer_output_frame renders one game frame of audio. */
void mixer_open_device(void);
void mixer_open_wav(const char* path);
void mixer_output_frame(void);

#endif
