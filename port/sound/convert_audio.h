// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TG3000_SOUND_CONVERT_AUDIO_H
#define TG3000_SOUND_CONVERT_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t* pcm;
    size_t num_samples;
    size_t loop_sample;
    bool loops;
} converted_sample;

/* Convert one BRR-encoded sample (`brr_size` bytes, `loop_offset` byte offset into `brr`)
   into 16-bit signed PCM. Caller owns `out->pcm` (free with `free`). Returns false if the
   input size or loop offset is invalid or allocation fails. */
bool convert_brr_to_pcm(const uint8_t* brr, size_t brr_size, uint16_t loop_offset,
                        converted_sample* out);

/* An instrument's envelope in ordinary units (converted once, when the set loads). The mixer
   draws its own curves through these times and levels. */
typedef struct {
    float attack_s;       /* rise from silence to full */
    float decay_tau_s;    /* time constant of the fall from full to the sustain level */
    float sustain_level;  /* 0..1: where that fall stops while the note is held */
    float sustain_tau_s;  /* time constant of the fall once the note is let go; 0 = it stays */
    float release_s;      /* fade from full to silence when the voice is cut */
} sound_envelope;

/* The chip's ADSR settings -> sound_envelope. False if they are not an ADSR envelope. */
bool convert_envelope(uint8_t adsr1, uint8_t adsr2, sound_envelope* out);

/* The driver's pitch value (1/16 semitones, 192 an octave) plus the engine's linear offset
   -> the sample's playback rate in Hz. */
double convert_pitch_hz(uint16_t pitch_value, uint16_t engine_offset);

/* A voice or master volume on the chip's scale (127 = full) -> a gain (about 0..1). */
float convert_voice_gain(int chip_volume);

/* The echo's delay in the chip's units (the set's settings block) -> seconds. False beyond the
   chip's range. */
bool convert_echo_delay_s(unsigned chip_delay, float* out);

/* The gain that brings converted PCM (convert_brr_to_pcm) to a full scale of 1.0. */
float convert_pcm_gain(void);

#endif
