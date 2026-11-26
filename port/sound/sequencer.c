// SPDX-License-Identifier: GPL-3.0-or-later
//
// The native song player (see sequencer.h). It reads the game's own song format (the set's
// songs block) and instruments (the set's instruments block), keeps named state per channel and
// per voice, and tells the mixer what to play: start a sample at a rate in Hz with left/right
// gains and an envelope in seconds, change pitch or volume, let a note go, cut it. It never
// produces chip register values: the units are converted by convert_audio.c.
//
// Timing follows the original driver, measured on the decomp's bench (notes/sound/SEQUENCER.md,
// local): one step per game message; inside a step, in this order: the message (volumes, the
// music request, an effect), a song start, each voice's output, the per-step pitch tables, the
// pending note starts, then the song's ticks by a tempo accumulator. A note read in a tick cuts
// its voice at once and starts on the next step.
//
// Only what the game's songs and menu effects use is understood; anything else stops the port
// with a message (port_fatal).
#include "sequencer.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mixer.h"
#include "port.h"

#define VOICES 8
#define CHANNELS 8
#define INSTRUMENTS 64
#define TABLE_PAGE 256          /* the pitch offset and pitch sweep tables: 16 tables of 16 bytes */
#define VOLUME_STEPS 32

/* The instruments-and-settings block, by role (offsets from its start). */
#define BLOCK_INSTRUMENT_HEAD 0x000   /* bytes 0-3 of instrument i at 4i */
#define BLOCK_INSTRUMENT_TAIL 0x100   /* bytes 4-7 of instrument i at 4i */
#define BLOCK_PITCH_OFFSETS 0x200     /* per-step pitch offset sequences (arpeggio/vibrato) */
#define BLOCK_PITCH_SWEEPS 0x300      /* pitch sweeps: (steps, delta) pairs */
#define BLOCK_ALWAYS_TICK 0x405       /* a flag: tick every step regardless of tempo (unused) */
#define BLOCK_VOLUME_STEPS 0x406      /* the 32 volume steps of the volume commands */
#define BLOCK_MIN_SIZE (BLOCK_VOLUME_STEPS + VOLUME_STEPS)

/* The driver's tempo divisor at 60 Hz: a song of tempo T ticks T/197 times a step. */
#define TEMPO_DIVISOR_NTSC 197

/* Song events. */
#define EV_REST 0x00
#define EV_NOTE_LAST 0x7F
#define EV_DRUM_LAST 0x9F
#define EV_INSTRUMENT_LAST 0xBF
#define EV_VOLUME_LAST 0xDF
#define EV_LET_GO 0xE8
#define EV_PATTERN_END 0xFF
#define ORDER_END 0xFFFFu
#define DRUM_PITCH 0x0400u
#define EFFECT_NOTE 0x3C
#define HOLD_FOREVER 0xFEu           /* a hold time of $FE/$FF never runs out */
#define SWEEP_OFF 0xFFu

typedef struct {
    uint8_t hold_steps;      /* steps a note is held before it is let go (0: until a let-go) */
    uint16_t tune;           /* added to the note's pitch value (1/16 semitones) */
    bool adsr;               /* an ADSR envelope (else the chip's direct gain: unused) */
    sound_envelope envelope; /* converted at load */
    bool echo;
    uint8_t offsets_table;   /* which per-step pitch offset sequence */
    uint8_t sweep_table;     /* which pitch sweep */
    uint8_t sample;          /* sample slot */
    bool noise;
    uint8_t volume;          /* multiplies the voice's volume (/256) */
} instrument;

typedef struct {
    bool playing;            /* has a song to read */
    bool finished;           /* its order list ended with no loop */
    size_t order_list;       /* song-block offset of its order list */
    unsigned order_index;
    size_t read;             /* song-block offset of the next event */
    bool at_pattern_end;     /* the song's start: the first read loads order entry 0 */
    uint8_t wait;            /* ticks before the next read */
    uint8_t instrument;
    uint8_t volume;          /* the channel's volume, copied into a voice at its notes */
} channel;

typedef struct {
    uint8_t instrument;
    uint16_t pitch;          /* the note's pitch value */
    uint16_t tune;           /* the instrument's tune, moved by its pitch sweep */
    int8_t offset;           /* the current entry of the pitch offset sequence */
    uint8_t hold;            /* steps left before the note is let go (see hold_steps) */
    bool held;
    uint8_t volume;          /* the voice's volume */
    uint8_t inst_volume;     /* its instrument's volume (full once the voice is cleared) */
    bool effect;             /* an effect plays on it */
    bool start_pending;      /* starts at the end of this step */
    bool started;            /* has played a note since the set loaded */
    /* the pitch offset sequence: position, end, loop point (byte indices in its page) */
    uint8_t ofs_at, ofs_end, ofs_loop;
    /* the pitch sweep: position (SWEEP_OFF: none), steps left in the current pair, end, loop */
    uint8_t sweep_at, sweep_left, sweep_end, sweep_loop;
    /* what the mixer was last told */
    double out_hz;
    int out_left, out_right; /* on the driver's volume scale */
    bool out_let_go;
} voice;

static struct {
    const uint8_t* songs;
    size_t songs_size;
    bool have_instruments;
    instrument inst[INSTRUMENTS];
    uint8_t offsets[TABLE_PAGE];
    uint8_t sweeps[TABLE_PAGE];
    uint8_t volume_steps[VOLUME_STEPS];
    bool always_tick;
    bool have_balance;
    uint8_t balance_left, balance_right;  /* the driver's centre: pan table entries 8 and 7 */
} g_set;

static channel g_ch[CHANNELS];
static voice g_v[VOICES];
static unsigned g_channels;
static size_t g_song;            /* song-block offset of the playing song */
static uint8_t g_loop_order;
static uint16_t g_pitch_base;
static uint8_t g_tempo;
static int g_tempo_acc;
static int g_request = -1;       /* the song asked for, or -1 */
static uint8_t g_music_volume;   /* the message's music volume, doubled (8 bits) */
static uint8_t g_effect_volume;  /* the message's effect volume, doubled (8 bits) */
static long g_song_step = -1;    /* steps since the song started (events and tests) */

/* ---- events (tests) ------------------------------------------------------------------------ */

static FILE* g_events;
static unsigned long g_event_count;
static uint64_t g_event_hash = 0xCBF29CE484222325ULL;

static void event_line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

static void event_line(const char* fmt, ...) {
    if (g_events == NULL) return;
    char line[192];
    int n = snprintf(line, sizeof line, "%lu %ld ", port_frame_number(), g_song_step);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    for (const char* c = line; *c; ++c) {
        g_event_hash ^= (uint8_t)*c;
        g_event_hash *= 0x100000001B3ULL;
    }
    ++g_event_count;
    fputs(line, g_events);
    fputc('\n', g_events);
}

static void events_close(void) {
    if (g_events == NULL) return;
    fclose(g_events);
    g_events = NULL;
    printf("sound events: %lu, fingerprint %016llx\n", g_event_count, (unsigned long long)g_event_hash);
    fflush(stdout);
}

void sequencer_open_events(const char* path) {
    g_events = fopen(path, "w");
    if (g_events == NULL) port_fatal("--sound-events: cannot open the file for writing");
    fputs("# frame step voice event ... (the native player's calls to the mixer)\n", g_events);
    atexit(events_close);
}

static void fatal_at(const char* what, unsigned value) {
    char msg[160];
    snprintf(msg, sizeof msg, "sound: %s ($%02X)", what, value);
    port_fatal(msg);
}

/* ---- the set -------------------------------------------------------------------------------- */

void sequencer_new_set(void) {
    mixer_stop_all();
    memset(g_ch, 0, sizeof g_ch);
    memset(g_v, 0, sizeof g_v);
    for (int n = 0; n < VOICES; ++n) {
        g_v[n].sweep_at = SWEEP_OFF;
        g_v[n].inst_volume = 0xFF;
    }
    g_channels = 0;
    g_tempo = 0;
    g_tempo_acc = 0;
    g_request = -1;
    g_song_step = -1;
    g_set.songs = NULL;
    g_set.songs_size = 0;
    g_set.have_instruments = false;
}

void sequencer_songs_loaded(const uint8_t* songs, size_t size) {
    g_set.songs = songs;
    g_set.songs_size = size;
}

void sequencer_instruments_loaded(const uint8_t* block, size_t size) {
    if (size < BLOCK_MIN_SIZE) fatal_at("the instruments block is shorter than measured", (unsigned)size);
    for (unsigned i = 0; i < INSTRUMENTS; ++i) {
        const uint8_t* a = block + BLOCK_INSTRUMENT_HEAD + 4u * i;
        const uint8_t* b = block + BLOCK_INSTRUMENT_TAIL + 4u * i;
        instrument* in = &g_set.inst[i];
        in->hold_steps = a[0];
        in->tune = (uint16_t)((int)(int8_t)a[1] * 4 + (b[1] >> 4));
        in->adsr = convert_envelope(a[2], a[3], &in->envelope);
        in->echo = (b[0] & 0x80u) != 0;
        in->offsets_table = b[0] & 0x0Fu;
        in->sweep_table = b[1] & 0x0Fu;
        in->noise = (b[2] & 0x80u) != 0;
        in->sample = b[2];
        in->volume = b[3];
    }
    memcpy(g_set.offsets, block + BLOCK_PITCH_OFFSETS, TABLE_PAGE);
    memcpy(g_set.sweeps, block + BLOCK_PITCH_SWEEPS, TABLE_PAGE);
    memcpy(g_set.volume_steps, block + BLOCK_VOLUME_STEPS, VOLUME_STEPS);
    g_set.always_tick = block[BLOCK_ALWAYS_TICK] != 0;
    g_set.have_instruments = true;
}

void sequencer_balance_loaded(const uint8_t table[16]) {
    g_set.balance_left = table[8];
    g_set.balance_right = table[7];
    g_set.have_balance = true;
}

static bool set_ready(void) {
    return g_set.songs != NULL && g_set.have_instruments && g_set.have_balance;
}

static uint8_t song_byte(size_t at) {
    if (at >= g_set.songs_size) fatal_at("a song reads past its block", (unsigned)(at & 0xFFFF));
    return g_set.songs[at];
}

static uint16_t song_word(size_t at) {
    return (uint16_t)(song_byte(at) | song_byte(at + 1) << 8);
}

/* ---- voices -------------------------------------------------------------------------------- */

static uint16_t voice_pitch_value(const voice* v) {
    return (uint16_t)(v->pitch + v->tune + v->offset);
}

/* The voice's gain on the driver's volume scale: its volume times the instrument's, split by
   the driver's centre balance (every note of the game plays at the centre). */
static void voice_volume(const voice* v, int* left, int* right) {
    int a = (v->volume * v->inst_volume) >> 8;
    *left = (g_set.balance_left * a) >> 8;
    *right = (g_set.balance_right * a) >> 8;
}

static void voice_cut(int n) {
    mixer_cut(n);
    event_line("v%d cut", n);
}

/* A note (or an effect) on voice n: it is cut now and starts at the end of the step. */
static void voice_note(int n, unsigned inst_index, uint16_t pitch, uint8_t volume, bool effect) {
    if (inst_index >= INSTRUMENTS) return;  /* the driver ignores such an instrument */
    const instrument* in = &g_set.inst[inst_index];
    if (in->noise) fatal_at("an instrument plays noise, which the native player does not have", inst_index);
    if (in->echo) fatal_at("an instrument uses echo, which the native player does not have yet", inst_index);
    if (!in->adsr) fatal_at("an instrument uses the chip's direct gain, not an envelope", inst_index);
    voice* v = &g_v[n];
    v->instrument = (uint8_t)inst_index;
    v->pitch = pitch;
    v->tune = in->tune;
    v->hold = in->hold_steps;
    v->held = true;
    v->offset = 0;
    unsigned p = in->offsets_table * 16u;
    v->ofs_at = (uint8_t)(p + 2u);
    v->ofs_end = (uint8_t)(p + 2u + g_set.offsets[p]);
    v->ofs_loop = (uint8_t)(p + 2u + g_set.offsets[p + 1u]);
    unsigned q = in->sweep_table * 16u;
    v->sweep_at = (uint8_t)q;
    v->sweep_left = 0;
    v->sweep_end = (uint8_t)(q + 2u + 2u * g_set.sweeps[q]);
    v->sweep_loop = (uint8_t)(q + 2u + 2u * g_set.sweeps[q + 1u]);
    v->start_pending = true;
    v->effect = effect;
    v->volume = volume;
    v->inst_volume = in->volume;
    voice_cut(n);
}

/* Each step, every voice: tell the mixer what changed, then count the hold down. */
static void voice_output(int n) {
    voice* v = &g_v[n];
    double hz = convert_pitch_hz(voice_pitch_value(v), 0);
    int l, r;
    voice_volume(v, &l, &r);
    bool let_go = !v->held;
    if (v->started && !v->start_pending) {
        if (hz != v->out_hz) {
            mixer_set_pitch(n, hz);
            event_line("v%d pitch hz=%.4f", n, hz);
        }
        if (l != v->out_left || r != v->out_right) {
            mixer_set_volume(n, convert_voice_gain(l), convert_voice_gain(r));
            event_line("v%d vol l=%.6f r=%.6f", n, convert_voice_gain(l), convert_voice_gain(r));
        }
        if (let_go && !v->out_let_go) {
            mixer_let_go(n);
            event_line("v%d letgo", n);
        }
    }
    v->out_hz = hz;
    v->out_left = l;
    v->out_right = r;
    v->out_let_go = let_go;
    if (v->hold >= 1u && v->hold < HOLD_FOREVER) {
        if (--v->hold == 0u) v->held = false;
    }
}

/* Each step, after the output: the instrument's pitch sweep and pitch offset sequence move on
   (heard from the next step). */
static void voice_pitch_tables(voice* v) {
    if (v->sweep_at != SWEEP_OFF) {
        uint8_t at = v->sweep_at;
        bool moves = true;
        if (v->sweep_left == 0u) {
            at = (uint8_t)(at + 2u);
            if (at == v->sweep_end) {
                if (at < v->sweep_loop) moves = false;               /* ends */
                else if (at > v->sweep_loop) at = v->sweep_loop;     /* loops */
            }
            if (moves && g_set.sweeps[at] == 0u) moves = false;     /* a pair of 0 steps ends it */
            if (moves) {
                v->sweep_left = g_set.sweeps[at];
                v->sweep_at = at;
            }
        }
        if (moves) {
            --v->sweep_left;
            v->tune = (uint16_t)(v->tune + (int8_t)g_set.sweeps[(uint8_t)(at + 1u)]);
        }
    }
    if (v->ofs_at != v->ofs_end) {
        v->offset = (int8_t)g_set.offsets[v->ofs_at];
        v->ofs_at = (uint8_t)(v->ofs_at + 1u);
        if (v->ofs_at == v->ofs_end) v->ofs_at = v->ofs_loop;
    }
}

static void voice_start(int n) {
    voice* v = &g_v[n];
    v->start_pending = false;
    v->started = true;
    const instrument* in = &g_set.inst[v->instrument];
    const converted_sample* s = sound_set_sample(in->sample);
    if (s == NULL) fatal_at("an instrument names a sample slot the set did not send", in->sample);
    mixer_start(n, s, v->out_hz, convert_voice_gain(v->out_left), convert_voice_gain(v->out_right),
                &in->envelope, v->out_let_go);
    event_line("v%d on inst=%u sample=%u hz=%.4f l=%.6f r=%.6f", n, (unsigned)v->instrument,
               (unsigned)in->sample, v->out_hz, convert_voice_gain(v->out_left),
               convert_voice_gain(v->out_right));
}

/* ---- songs --------------------------------------------------------------------------------- */

static void song_start(int number) {
    /* Song n: skip n songs, each starting with its own length; a 0 length means no song. */
    size_t at = 0;
    for (int i = 0; i < number; ++i) {
        uint16_t len = song_word(at);
        if (len == 0u) return;
        at += len;
    }
    if (song_word(at) == 0u) return;
    uint16_t width = song_word(at + 2);
    unsigned count = song_byte(at + 4);
    if (count > CHANNELS) fatal_at("a song has more channels than voices", count);
    g_song = at;
    g_channels = count;
    g_tempo = song_byte(at + 6);
    g_loop_order = song_byte(at + 8);
    g_pitch_base = song_word(at + 10);
    if (g_set.always_tick) port_fatal("sound: the set asks to tick every step, which no song uses");
    size_t stride = (size_t)width * 2u + 2u;
    memset(g_ch, 0, sizeof g_ch);
    for (unsigned c = 0; c < count; ++c) {
        channel* ch = &g_ch[c];
        ch->playing = true;
        ch->order_list = at + 12u + c * stride;
        ch->at_pattern_end = true;
        ch->wait = 1;
    }
    /* Voices: those of the song's channels (all 8 for song 0) are cleared; every voice is cut. */
    unsigned cleared = (number == 0) ? VOICES : count;
    for (unsigned n = 0; n < VOICES; ++n) {
        voice* v = &g_v[n];
        if (n < cleared) {
            v->hold = 0;
            v->volume = 0;
            v->inst_volume = 0xFF;
            v->offset = 0;
            v->ofs_at = v->ofs_end = v->ofs_loop = 0;
            v->sweep_at = SWEEP_OFF;
            v->sweep_left = v->sweep_end = v->sweep_loop = 0;
            v->start_pending = false;
        }
    }
    g_tempo_acc = 0;
    g_song_step = 0;
    for (int n = 0; n < VOICES; ++n) voice_cut(n);
    event_line("song %d channels=%u tempo=%u", number, count, (unsigned)g_tempo);
}

/* A pattern ended (or the song starts): the channel's volume goes back to the music volume and
   the next pattern of its order list starts. False when the list ended with no loop. */
static bool channel_next_pattern(channel* ch) {
    ch->volume = g_music_volume;
    uint16_t entry = song_word(ch->order_list + 2u * ch->order_index);
    if (entry == ORDER_END) {
        if (g_loop_order == 0x7Fu || g_loop_order == 0xFFu) {
            ch->finished = true;
            return false;
        }
        ch->order_index = g_loop_order;
        entry = song_word(ch->order_list + 2u * ch->order_index);
        if (entry == ORDER_END) fatal_at("a song loops to the end of its order list", g_loop_order);
    }
    ch->order_index++;
    ch->read = g_song + entry;
    ch->at_pattern_end = false;
    return true;
}

static void channel_note(unsigned c, unsigned inst_index, uint16_t pitch) {
    voice* v = &g_v[c];
    if (v->effect && v->hold != 0u) return;  /* an effect still holds this voice: skipped */
    voice_note((int)c, inst_index, pitch, g_ch[c].volume, false);
}

static void channel_tick(unsigned c) {
    channel* ch = &g_ch[c];
    if (!ch->playing || ch->finished) return;
    if (--ch->wait != 0u) return;
    for (;;) {
        if (ch->at_pattern_end) {
            if (!channel_next_pattern(ch)) return;
            continue;
        }
        uint8_t e = song_byte(ch->read++);
        unsigned wait;
        if (e == EV_REST) {
            wait = song_byte(ch->read++);
        } else if (e <= EV_NOTE_LAST) {
            channel_note(c, ch->instrument, (uint16_t)(e * 16u + g_pitch_base));
            wait = song_byte(ch->read++);
        } else if (e <= EV_DRUM_LAST) {
            channel_note(c, e & 0x1Fu, DRUM_PITCH);
            wait = song_byte(ch->read++);
        } else if (e <= EV_INSTRUMENT_LAST) {
            ch->instrument = e & 0x1Fu;
            continue;
        } else if (e <= EV_VOLUME_LAST) {
            unsigned t = g_set.volume_steps[e & 0x1Fu];
            unsigned a = (t * 8u + (t != 0u ? 7u : 0u)) & 0xFFu;
            a = (a * g_music_volume) >> 8;
            ch->volume = (uint8_t)a;
            if (!g_v[c].effect) {
                if (a == 0u) voice_cut((int)c);
                else g_v[c].volume = (uint8_t)a;
            }
            continue;
        } else if (e == EV_LET_GO) {
            if (!g_v[c].effect) g_v[c].held = false;
            wait = song_byte(ch->read++);
        } else if (e == EV_PATTERN_END) {
            ch->at_pattern_end = true;
            continue;
        } else {
            fatal_at("a song uses a command the game's songs never use", e);
            return;
        }
        if (wait != 0u) {
            ch->wait = (uint8_t)wait;
            return;
        }
    }
}

/* ---- effects ------------------------------------------------------------------------------- */

/* An effect request: instrument id-1 at the effect note, on the voice of `mask` whose hold runs
   out first (the highest such voice on a tie). */
static void effect_start(uint8_t id, uint8_t volume, uint8_t mask) {
    int best = -1;
    for (int n = 0; n < VOICES; ++n) {
        if (!(mask & (1u << n))) continue;
        if (best < 0 || g_v[n].hold <= g_v[best].hold) best = n;
    }
    if (best < 0) return;
    unsigned inst_index = (unsigned)(id - 1u) & 0x3Fu;
    event_line("effect id=$%02X v%d", (unsigned)id, best);
    voice_note(best, inst_index, (uint16_t)(EFFECT_NOTE * 16u), (uint8_t)((volume * g_effect_volume) >> 8), true);
}

/* ---- one step ------------------------------------------------------------------------------ */

/* The message (decomp src/sound.c, sound_send_message): group 0 master, music volume, music
   request, flags; group 1 engine pitch, effect volume, engine volume and voice, engine pitch;
   groups 2 and 3 the two effect requests (volume, note, voices, id). */
enum {
    MSG_MASTER = 0, MSG_MUSIC_VOLUME = 1, MSG_MUSIC = 2, MSG_FLAGS = 3,
    MSG_EFFECT_VOLUME = 5,
    MSG_A_VOLUME = 8, MSG_A_NOTE = 9, MSG_A_VOICES = 10, MSG_A_ID = 11,
    MSG_B_ID = 15,
};
#define FLAG_PAL 0x02u
#define FLAG_ENGINE 0x20u
#define REQUEST_KEEP 0x80u
#define EFFECT_ID_MASK 0x7Fu
#define EFFECT_NOTE_GIVEN 0x40u

void sequencer_message(const uint8_t msg[16]) {
    /* Without a set (memory started from a saved game) or a booted driver, nothing plays. */
    if (!set_ready()) return;
    if (g_song_step >= 0) ++g_song_step;

    /* 1. The message. */
    uint8_t flags = msg[MSG_FLAGS];
    if (flags & FLAG_PAL) port_fatal("sound: 50 Hz timing is not native yet");
    if (flags & FLAG_ENGINE) port_fatal("sound: the engine sound is not native yet");
    mixer_set_master(convert_voice_gain((uint8_t)(msg[MSG_MASTER] * 2u) >> 1));
    g_music_volume = (uint8_t)(msg[MSG_MUSIC_VOLUME] * 2u);
    g_effect_volume = (uint8_t)(msg[MSG_EFFECT_VOLUME] * 2u);
    if (!(msg[MSG_MUSIC] & REQUEST_KEEP)) g_request = msg[MSG_MUSIC];
    uint8_t id_a = msg[MSG_A_ID] & EFFECT_ID_MASK;
    if (id_a != 0u) {
        if (id_a & EFFECT_NOTE_GIVEN) fatal_at("an effect request gives its own note, which the game never does", id_a);
        effect_start(id_a, msg[MSG_A_VOLUME], msg[MSG_A_VOICES]);
    }
    if ((msg[MSG_B_ID] & EFFECT_ID_MASK) != 0u) fatal_at("the second effect request is used", msg[MSG_B_ID]);

    /* 2. A song start. */
    if (g_request >= 0) {
        int n = g_request;
        g_request = -1;
        song_start(n);
    }

    /* 3. Each voice's output. 5. The pitch tables. 6. The pending starts. */
    for (int n = 0; n < VOICES; ++n) voice_output(n);
    for (int n = VOICES - 1; n >= 0; --n) voice_pitch_tables(&g_v[n]);
    for (int n = 0; n < VOICES; ++n) {
        if (g_v[n].start_pending) voice_start(n);
    }

    /* 7. The song's ticks: T/197 a step on average, by an accumulator. */
    g_tempo_acc -= g_tempo;
    while (g_tempo_acc < 0) {
        for (unsigned c = 0; c < g_channels; ++c) channel_tick(c);
        g_tempo_acc += TEMPO_DIVISOR_NTSC;
    }
}
