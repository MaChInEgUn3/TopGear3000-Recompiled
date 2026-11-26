// SPDX-License-Identifier: GPL-3.0-or-later
//
// Format conversion: SNES BRR (Bit Rate Reduction) -> 16-bit signed PCM.
// Format constants live only in this file (docs/DECISIONS.md, "Sound: the owner's questions").

#include "convert_audio.h"

#include <stdlib.h>

/* BRR block layout: 1 header byte followed by 8 data bytes (16 4-bit signed nibbles, high first). */
#define BRR_BLOCK_BYTES 9u
#define BRR_DATA_BYTES 8u
#define BRR_SAMPLES_PER_BLOCK 16u

/* Header byte fields:
   bits 7..4: shift range (0..12; values 13..15 saturate to -2048 or 0)
   bits 3..2: prediction filter (0..3)
   bit 1:     loop flag (active when end flag is also set)
   bit 0:     end-of-sample flag */
#define BRR_FLAG_END 0x01u
#define BRR_FLAG_LOOP 0x02u
#define BRR_FILTER_SHIFT 2u
#define BRR_FILTER_MASK 0x03u
#define BRR_RANGE_SHIFT 4u
#define BRR_MAX_SHIFT 12u

static int32_t clamp_s16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

bool convert_brr_to_pcm(const uint8_t* brr, size_t brr_size, uint16_t loop_offset,
                        converted_sample* out) {
    if (brr == NULL || out == NULL || brr_size == 0u) return false;
    if ((brr_size % BRR_BLOCK_BYTES) != 0u) return false;
    if (loop_offset > brr_size || (loop_offset % BRR_BLOCK_BYTES) != 0u) return false;

    size_t num_blocks = brr_size / BRR_BLOCK_BYTES;
    size_t num_samples = num_blocks * BRR_SAMPLES_PER_BLOCK;
    int16_t* pcm = (int16_t*)malloc(num_samples * sizeof(int16_t));
    if (pcm == NULL) return false;

    int32_t prev1 = 0;
    int32_t prev2 = 0;
    bool loops = false;
    size_t out_idx = 0;

    for (size_t b = 0; b < num_blocks; ++b) {
        const uint8_t* block = brr + b * BRR_BLOCK_BYTES;
        uint8_t header = block[0];
        unsigned shift = (unsigned)(header >> BRR_RANGE_SHIFT);
        unsigned filter = (unsigned)((header >> BRR_FILTER_SHIFT) & BRR_FILTER_MASK);
        if ((header & BRR_FLAG_END) != 0u && (header & BRR_FLAG_LOOP) != 0u) {
            loops = true;
        }

        for (size_t i = 0; i < BRR_DATA_BYTES; ++i) {
            uint8_t byte = block[1u + i];
            for (int k = 0; k < 2; ++k) {
                int32_t nibble = (k == 0) ? (int32_t)(byte >> 4) : (int32_t)(byte & 0x0Fu);
                if (nibble >= 8) nibble -= 16;

                int32_t s;
                if (shift <= BRR_MAX_SHIFT) {
                    s = (nibble * (1 << shift)) >> 1;
                } else {
                    s = (nibble < 0) ? -2048 : 0;
                }

                switch (filter) {
                    case 0:
                        break;
                    case 1:
                        s += prev1 + ((-prev1) >> 4);
                        break;
                    case 2:
                        s += (prev1 * 2) + ((-(prev1 * 3)) >> 5) - prev2 + (prev2 >> 4);
                        break;
                    case 3:
                        s += (prev1 * 2) + ((-(prev1 * 13)) >> 6) - prev2 + ((prev2 * 3) >> 4);
                        break;
                    default:
                        break;
                }

                s = clamp_s16(s);
                pcm[out_idx++] = (int16_t)s;
                prev2 = prev1;
                prev1 = s;
            }
        }
    }

    out->pcm = pcm;
    out->num_samples = num_samples;
    out->loop_sample = ((size_t)loop_offset / BRR_BLOCK_BYTES) * BRR_SAMPLES_PER_BLOCK;
    out->loops = loops;
    return true;
}
