// SPDX-License-Identifier: GPL-3.0-or-later
//
// Format conversion: SNES BRR (Bit Rate Reduction) -> 16-bit signed PCM; the instruments'
// envelope codes -> seconds and levels; the driver's pitch values -> hertz; the chip's volume
// scale -> gains. Format constants live only in this file (docs/DECISIONS.md, "Sound: the
// owner's questions" and "The native sound player (set 1)").

#include "convert_audio.h"

#include <math.h>
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

/* ---- Envelopes, pitch and volume: the chip's units -> ordinary units ------------------------
   The instruments name their envelope with the chip's rate codes, the driver names pitch with
   its own pitch value and loudness on the chip's volume scale. These functions turn them into
   seconds, levels, hertz and gains; nothing else in the port knows those units. */

/* The rate the chip plays a sample at when its pitch is 1.0 ($1000, 14 bits kept). */
#define CHIP_SAMPLE_RATE_HZ 32000.0
#define CHIP_PITCH_ONE 4096.0
#define CHIP_PITCH_MASK 0x3FFFu

/* Envelope rate codes 0..31: samples (at 32 kHz) between two envelope steps; code 0 never steps. */
static const uint16_t k_rate_period[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};
#define ADSR_ENABLE 0x80u
#define ATTACK_STEPS 64.0          /* a full attack climbs in 64 equal steps */
#define ATTACK_TOP_SAMPLES 2.0     /* the top attack code: 2 steps of 1 sample */
#define EXP_STEP_KEEP (255.0 / 256.0) /* a decay step keeps about 255/256 of the level */
#define SUSTAIN_LEVEL_STEPS 8.0    /* the sustain level code counts eighths of full */
#define KEY_OFF_SAMPLES 256.0      /* a cut voice fades from full to 0 in 256 samples */

static double rate_period_s(unsigned code) {
    return (double)k_rate_period[code & 31u] / CHIP_SAMPLE_RATE_HZ;
}

bool convert_envelope(uint8_t adsr1, uint8_t adsr2, sound_envelope* out) {
    if (out == NULL || (adsr1 & ADSR_ENABLE) == 0u) return false;
    unsigned attack = adsr1 & 0x0Fu;
    unsigned decay = (adsr1 >> 4) & 0x07u;
    unsigned level = (adsr2 >> 5) & 0x07u;
    unsigned sustain = adsr2 & 0x1Fu;
    out->attack_s = (attack == 15u) ? (float)(ATTACK_TOP_SAMPLES / CHIP_SAMPLE_RATE_HZ)
                                    : (float)(ATTACK_STEPS * rate_period_s(attack * 2u + 1u));
    /* The exponential phases as time constants: steps per e-fold times the step period. */
    const double steps_per_e = -1.0 / log(EXP_STEP_KEEP);
    out->decay_tau_s = (float)(steps_per_e * rate_period_s(decay * 2u + 16u));
    out->sustain_level = (float)((level + 1u) / SUSTAIN_LEVEL_STEPS);
    out->sustain_tau_s = (sustain == 0u) ? 0.0f : (float)(steps_per_e * rate_period_s(sustain));
    out->release_s = (float)(KEY_OFF_SAMPLES / CHIP_SAMPLE_RATE_HZ);
    return true;
}

/* The driver's pitch value: 192 steps an octave, through its table of one octave (computed:
   it equals round(16384 * 2^(r/192)), measured on the cartridge), shifted by the octave. */
#define PITCH_STEPS_PER_OCTAVE 192u
#define PITCH_OCTAVES 8u
static uint16_t g_octave[PITCH_STEPS_PER_OCTAVE];
static bool g_octave_ready;

double convert_pitch_hz(uint16_t pitch_value, uint16_t engine_offset) {
    if (!g_octave_ready) {
        for (unsigned r = 0; r < PITCH_STEPS_PER_OCTAVE; ++r) {
            g_octave[r] = (uint16_t)lround(16384.0 * pow(2.0, (double)r / PITCH_STEPS_PER_OCTAVE));
        }
        g_octave_ready = true;
    }
    unsigned octave = pitch_value / PITCH_STEPS_PER_OCTAVE;
    unsigned step = pitch_value % PITCH_STEPS_PER_OCTAVE;
    unsigned rate = (octave >= PITCH_OCTAVES) ? 0u : (unsigned)(g_octave[step] >> (PITCH_OCTAVES - octave));
    rate = (rate + engine_offset) & CHIP_PITCH_MASK;
    return (double)rate * (CHIP_SAMPLE_RATE_HZ / CHIP_PITCH_ONE);
}

float convert_voice_gain(int chip_volume) {
    return (float)chip_volume / 128.0f;
}

/* The decoded BRR samples above are 15-bit (full scale 16384, half the 16-bit range). */
float convert_pcm_gain(void) {
    return 1.0f / 16384.0f;
}
