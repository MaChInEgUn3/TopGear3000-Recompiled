// SPDX-License-Identifier: GPL-3.0-or-later
//
// The port's own mixer (see mixer.h). Choices (docs/DECISIONS.md, "The native sound player
// (set 1)"):
// - interpolation: 4-point cubic Hermite (Catmull-Rom) between the converted PCM samples;
// - envelope: a linear rise over the attack time, then smooth exponential falls with the
//   instrument's time constants (to the sustain level, then, once let go, towards silence),
//   and a linear fade when a voice is cut;
// - echo: one stereo delay line fed by the voices whose notes ask for it, with the game's
//   delay, feedback and volume converted to seconds and gains; no filter (the driver sets the
//   chip's filter to pass the sound through unchanged); its own, not the chip's;
// - output: 48 kHz stereo float, one game frame at a time, pushed to an SDL3 audio stream
//   from the main thread (no audio callback touches the game's data) and/or to a WAV file.
#include "mixer.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port.h"

#define OUT_RATE 48000
#define FRAME_SECONDS (16639267.0 / 1e9)  /* one game frame, as the port paces it */
#define MAX_FRAME_SAMPLES 1024
#define SILENT_LEVEL (1.0f / 2048.0f)     /* an envelope below this has ended */
#define DEVICE_QUEUE_LIMIT_FRAMES 6       /* drop a frame of audio rather than lag behind */
#define ECHO_MAX_SAMPLES 12288            /* past the longest delay the game can ask (240 ms) */
/* The output's headroom: one fixed gain on the whole mix (the voices' relative levels are
   untouched). Measured before it (docs/DECISIONS.md, "The mixer's output level"): the loudest
   sample of the menu song with its effects is 1.405 of full scale, of the bench's five race
   replays (music, engine, effects, echo) 1.345-1.763 (set 5). Halved, the loudest is 0.881:
   nothing reaches the clamp below, which stays only as a guard. */
#define OUTPUT_HEADROOM 0.5f

typedef enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_CUT } env_phase;

typedef struct {
    const converted_sample* sample;
    double pos;
    double step;
    float left, right;
    sound_envelope env;
    env_phase phase;
    bool let_go;
    bool echo;
    float level;
    float attack_add;   /* per output sample */
    float decay_keep;   /* per output sample */
    float sustain_keep; /* per output sample; 1 = stays */
    float cut_sub;      /* per output sample */
} mix_voice;

static mix_voice g_voice[MIXER_VOICES];
static float g_master;
static SDL_AudioStream* g_stream;
static FILE* g_wav;
static uint32_t g_wav_frames;
static float g_peak;          /* the loudest sample before the clamp, full scale = 1 */
static uint32_t g_clipped;    /* samples the clamp cut */
static double g_frame_carry;

/* The echo's delay line (stereo, interleaved), its length in output samples and gains. */
static float g_echo_line[2 * ECHO_MAX_SAMPLES];
static int g_echo_len = 1;
static int g_echo_at;
static float g_echo_feedback;
static float g_echo_volume;

static float per_sample_keep(float tau_s) {
    if (tau_s <= 0.0f) return 1.0f;
    return (float)exp(-1.0 / ((double)tau_s * OUT_RATE));
}

static bool voice_ok(int v) { return v >= 0 && v < MIXER_VOICES; }

void mixer_start(int voice, const converted_sample* sample, double hz, float left, float right,
                 const sound_envelope* env, bool let_go, bool echo) {
    if (!voice_ok(voice) || sample == NULL || env == NULL || sample->num_samples == 0) return;
    mix_voice* m = &g_voice[voice];
    m->sample = sample;
    m->echo = echo;
    m->pos = 0.0;
    m->step = hz / OUT_RATE;
    m->left = left;
    m->right = right;
    m->env = *env;
    m->let_go = let_go;
    m->level = 0.0f;
    m->attack_add = (env->attack_s > 0.0f) ? (float)(1.0 / ((double)env->attack_s * OUT_RATE)) : 1.0f;
    m->decay_keep = per_sample_keep(env->decay_tau_s);
    m->sustain_keep = per_sample_keep(env->sustain_tau_s);
    m->cut_sub = (env->release_s > 0.0f) ? (float)(1.0 / ((double)env->release_s * OUT_RATE)) : 1.0f;
    m->phase = ENV_ATTACK;
}

void mixer_set_pitch(int voice, double hz) {
    if (voice_ok(voice)) g_voice[voice].step = hz / OUT_RATE;
}

void mixer_set_volume(int voice, float left, float right) {
    if (!voice_ok(voice)) return;
    g_voice[voice].left = left;
    g_voice[voice].right = right;
}

void mixer_let_go(int voice) {
    if (voice_ok(voice)) g_voice[voice].let_go = true;
}

void mixer_cut(int voice) {
    if (voice_ok(voice) && g_voice[voice].phase != ENV_OFF) g_voice[voice].phase = ENV_CUT;
}

void mixer_stop_all(void) {
    memset(g_voice, 0, sizeof g_voice);
    memset(g_echo_line, 0, sizeof g_echo_line);
    g_echo_at = 0;
}

void mixer_set_master(float gain) { g_master = gain; }

void mixer_set_echo(float delay_s, float feedback, float volume) {
    int len = (int)lrint((double)delay_s * OUT_RATE);
    if (len < 1) len = 1;
    if (len > ECHO_MAX_SAMPLES) len = ECHO_MAX_SAMPLES;
    if (len != g_echo_len) {
        memset(g_echo_line, 0, sizeof g_echo_line);
        g_echo_at = 0;
        g_echo_len = len;
    }
    g_echo_feedback = feedback;
    g_echo_volume = volume;
}

/* The sample at index i, past the end wrapped into the loop (or silence without one). */
static float sample_at(const converted_sample* s, long i) {
    if (i < 0) return 0.0f;
    if ((size_t)i >= s->num_samples) {
        if (!s->loops || s->loop_sample >= s->num_samples) return 0.0f;
        size_t len = s->num_samples - s->loop_sample;
        i = (long)(s->loop_sample + ((size_t)i - s->num_samples) % len);
    }
    return (float)s->pcm[i];
}

static float envelope_next(mix_voice* m) {
    switch (m->phase) {
        case ENV_ATTACK:
            m->level += m->attack_add;
            if (m->level >= 1.0f) {
                m->level = 1.0f;
                m->phase = ENV_DECAY;
            }
            break;
        case ENV_DECAY:
            m->level *= m->decay_keep;
            if (m->level <= m->env.sustain_level) {
                m->level = m->env.sustain_level;
                m->phase = ENV_SUSTAIN;
            }
            break;
        case ENV_SUSTAIN:
            if (m->let_go) {
                m->level *= m->sustain_keep;
                if (m->level < SILENT_LEVEL) m->phase = ENV_OFF;
            }
            break;
        case ENV_CUT:
            m->level -= m->cut_sub;
            if (m->level <= 0.0f) m->phase = ENV_OFF;
            break;
        case ENV_OFF:
        default:
            break;
    }
    if (m->phase == ENV_OFF) {
        m->level = 0.0f;
        m->sample = NULL;
    }
    return m->level;
}

static void render(float* out, int frames) {
    static float send[2 * MAX_FRAME_SAMPLES];
    memset(out, 0, sizeof(float) * 2u * (size_t)frames);
    memset(send, 0, sizeof(float) * 2u * (size_t)frames);
    for (int v = 0; v < MIXER_VOICES; ++v) {
        mix_voice* m = &g_voice[v];
        if (m->phase == ENV_OFF || m->sample == NULL) continue;
        const converted_sample* s = m->sample;
        for (int i = 0; i < frames; ++i) {
            long n = (long)m->pos;
            float t = (float)(m->pos - (double)n);
            float p0 = sample_at(s, n - 1), p1 = sample_at(s, n), p2 = sample_at(s, n + 1),
                  p3 = sample_at(s, n + 2);
            float y = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 +
                                                       t * (3.0f * (p1 - p2) + p3 - p0)));
            float e = envelope_next(m);
            float l = y * e * m->left, r = y * e * m->right;
            out[2 * i] += l;
            out[2 * i + 1] += r;
            if (m->echo) {
                send[2 * i] += l;
                send[2 * i + 1] += r;
            }
            if (m->phase == ENV_OFF) break;
            m->pos += m->step;
            if (m->pos >= (double)s->num_samples) {
                if (!s->loops || s->loop_sample >= s->num_samples) {
                    m->phase = ENV_OFF;
                    m->sample = NULL;
                    break;
                }
                double len = (double)(s->num_samples - s->loop_sample);
                while (m->pos >= (double)s->num_samples) m->pos -= len;
            }
        }
    }
    /* The echo: what the line gives back now was sent `g_echo_len` samples ago. */
    for (int i = 0; i < frames; ++i) {
        float* at = &g_echo_line[2 * g_echo_at];
        for (int c = 0; c < 2; ++c) {
            float wet = at[c];
            at[c] = send[2 * i + c] + wet * g_echo_feedback;
            out[2 * i + c] = out[2 * i + c] * g_master + wet * g_echo_volume;
        }
        if (++g_echo_at >= g_echo_len) g_echo_at = 0;
    }
    const float gain = convert_pcm_gain() * OUTPUT_HEADROOM;
    for (int i = 0; i < 2 * frames; ++i) {
        float x = out[i] * gain;
        float a = fabsf(x);
        if (a > g_peak) g_peak = a;
        if (a > 1.0f) ++g_clipped;
        out[i] = x > 1.0f ? 1.0f : (x < -1.0f ? -1.0f : x);
    }
}

static void wav_put_u32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void wav_close(void) {
    if (g_wav == NULL) return;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    wav_put_u32(h + 4, 36u + g_wav_frames * 4u);
    memcpy(h + 8, "WAVEfmt ", 8);
    wav_put_u32(h + 16, 16u);
    h[20] = 1; h[21] = 0;  /* PCM */
    h[22] = 2; h[23] = 0;  /* stereo */
    wav_put_u32(h + 24, OUT_RATE);
    wav_put_u32(h + 28, OUT_RATE * 4u);
    h[32] = 4; h[33] = 0;
    h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    wav_put_u32(h + 40, g_wav_frames * 4u);
    fseek(g_wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, g_wav);
    fclose(g_wav);
    g_wav = NULL;
    printf("sound wav: %u frames at %d Hz written, peak %.4f, clipped %u\n",
           (unsigned)g_wav_frames, OUT_RATE, (double)g_peak, (unsigned)g_clipped);
    fflush(stdout);
}

void mixer_open_wav(const char* path) {
    g_wav = fopen(path, "wb");
    if (g_wav == NULL) port_fatal("--sound-wav: cannot open the file for writing");
    uint8_t blank[44] = {0};
    fwrite(blank, 1, sizeof blank, g_wav);
    atexit(wav_close);
}

void mixer_open_device(void) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "sound: no audio (%s)\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = OUT_RATE;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (g_stream == NULL) {
        fprintf(stderr, "sound: no audio device (%s)\n", SDL_GetError());
        return;
    }
    /* A cushion of silence (3 frames) so that frame-to-frame jitter does not starve the device. */
    static const float silence[2 * 3 * 800];
    SDL_PutAudioStreamData(g_stream, silence, (int)sizeof silence);
    SDL_ResumeAudioStreamDevice(g_stream);
}

void mixer_output_frame(void) {
    if (g_stream == NULL && g_wav == NULL) return;
    g_frame_carry += FRAME_SECONDS * OUT_RATE;
    int frames = (int)g_frame_carry;
    g_frame_carry -= frames;
    if (frames > MAX_FRAME_SAMPLES) frames = MAX_FRAME_SAMPLES;
    float buf[2 * MAX_FRAME_SAMPLES];
    render(buf, frames);
    if (g_stream != NULL) {
        int limit = (int)(DEVICE_QUEUE_LIMIT_FRAMES * FRAME_SECONDS * OUT_RATE) * 2 * (int)sizeof(float);
        /* Fast runs (and hitches) would queue without end: past the limit the frame is dropped. */
        if (SDL_GetAudioStreamQueued(g_stream) < limit) {
            SDL_PutAudioStreamData(g_stream, buf, frames * 2 * (int)sizeof(float));
        }
    }
    if (g_wav != NULL) {
        int16_t pcm[2 * MAX_FRAME_SAMPLES];
        for (int i = 0; i < 2 * frames; ++i) pcm[i] = (int16_t)lrintf(buf[i] * 32767.0f);
        fwrite(pcm, sizeof(int16_t), (size_t)(2 * frames), g_wav);
        g_wav_frames += (uint32_t)frames;
    }
}
