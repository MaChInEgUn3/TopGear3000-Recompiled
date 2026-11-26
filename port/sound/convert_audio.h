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

#endif
