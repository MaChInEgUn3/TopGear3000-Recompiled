// SPDX-License-Identifier: GPL-3.0-or-later

/* TEST ONLY: the DSP-4 tape (--dsp4-tape FILE [--dsp4-tape-nmis]; docs/DECISIONS.md, "The DSP-4
   tape (tests only)").

   A RECORDING, not a DSP-4 model, and never the shipped game's behaviour: the values the race's C
   sent and read through dsp4_port_write16/dsp4_port_read16 (the sprite commands $09 and $06, not
   native yet: docs/decomp-requests.md, A) on one run of the decomp's bench, made by
   tools/make_reference_memory.sh (notes/race/race_tape.txt, local, never in git). Replayed in
   order, nothing computed: a read answers the next recorded read; a write must equal the next
   recorded write, and any difference stops the port (the first one is the finding). It lets the
   one-player race run in tests while the decomp finishes the native commands, like --movie and
   --race-refresh-replay. Without --dsp4-tape both calls stop the game, as in the shipped game.

   --dsp4-tape-nmis (a second recording, listed apart for the owner's veto): the tape's "N" lines
   are the race NMIs the bench ran between two of those calls. Each is run there as a whole port
   refresh before the next call is served, and each race-loop wait takes the next pending one, so
   the race's NMIs fall where the console's did relative to the sprite streams (the frame counter
   they read animates objects). A "P" NMI came after its step's last call and before its rest: it
   runs as that call returns (at the next call it would be counted for the next step). Off, the "N" lines are ignored and the port paces as usual.

   Delete this file when the race's C calls the native $09/$06. */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "platform.h"
#include "port.h"

typedef struct {
    uint32_t step;  /* the bench step of the call or NMI (for the messages) */
    uint16_t value;
    char kind;      /* 'W' = dsp4_port_write16, 'R' = dsp4_port_read16, 'N'/'P' = a race NMI */
} TapeItem;

static TapeItem* g_tape;
static size_t g_tape_len;
static size_t g_tape_at;
static size_t g_calls;  /* calls served (W and R) */
static bool g_nmis;     /* --dsp4-tape-nmis */
static long g_nmi_delta;
static bool g_nmi_delta_set;

void port_set_dsp4_tape_nmis(bool on) {
    g_nmis = on;
}

bool port_set_dsp4_tape(const char* path) {
    FILE* f = fopen(path, "r");
    if (f == NULL) return false;
    char line[256];  /* the comment lines are under 120 characters */
    size_t cap = 0;
    unsigned long step = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        unsigned v = 0;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if ((line[0] == 'N' || line[0] == 'P') && sscanf(line + 1, "%lu", &step) == 1) {
            if (!g_nmis) continue;
        } else if ((line[0] != 'W' && line[0] != 'R') || sscanf(line + 1, "%x", &v) != 1 || v > 0xFFFF) {
            fclose(f);
            port_fatal("--dsp4-tape: a line is not 'W hhhh', 'R hhhh', 'N step' or 'P step'");
        }
        if (g_tape_len == cap) {
            cap = cap ? cap * 2 : 65536;
            TapeItem* next = realloc(g_tape, cap * sizeof *next);
            if (next == NULL) port_fatal("--dsp4-tape: out of memory");
            g_tape = next;
        }
        g_tape[g_tape_len++] = (TapeItem){(uint32_t)step, (uint16_t)v, line[0]};
    }
    fclose(f);
    if (g_tape_len == 0) port_fatal("--dsp4-tape: nothing in the file");
    return true;
}

/* Take the next recorded NMI: check that the port's frames still map one to one onto the bench's
   steps (a drift would also shift the --movie input), then move past it. */
static void take_nmi(void) {
    const TapeItem* n = &g_tape[g_tape_at++];
    long delta = (long)n->step - (long)port_frame_number();
    if (!g_nmi_delta_set) {
        g_nmi_delta = delta;
        g_nmi_delta_set = true;
    } else if (delta != g_nmi_delta) {
        printf("dsp4 tape: the NMI of bench step %u runs at port frame %lu (bench step - port frame "
               "was %ld, now %ld)\n", (unsigned)n->step, port_frame_number(), g_nmi_delta, delta);
        fflush(stdout);
        g_nmi_delta = delta;
    }
}

static void tape_stop(const char* what, char kind, uint16_t got) {
    char msg[200];
    if (g_tape_at >= g_tape_len) {
        snprintf(msg, sizeof msg, "DSP-4 tape: call %zu (%c %04X) is past the tape's end", g_calls,
                 kind, (unsigned)got);
    } else {
        const TapeItem* c = &g_tape[g_tape_at];
        snprintf(msg, sizeof msg, "DSP-4 tape: call %zu (bench step %u): %s: expected %c %04X, got %c %04X",
                 g_calls, (unsigned)c->step, what, c->kind, (unsigned)c->value, kind, (unsigned)got);
    }
    port_fatal(msg);
}

static const TapeItem* next_call(char kind, uint16_t value) {
    if (g_tape == NULL) port_fatal("the race's DSP-4 commands are not native yet");
    while (g_tape_at < g_tape_len && g_tape[g_tape_at].kind == 'N') {  /* the NMIs before it */
        take_nmi();
        port_refresh_once();
    }
    if (g_tape_at >= g_tape_len) tape_stop("ended", kind, value);
    return &g_tape[g_tape_at];
}

/* After a call: the NMIs the bench ran after its step's last call and before its rest ("P"). */
static void served(void) {
    ++g_tape_at;
    ++g_calls;
    while (g_tape_at < g_tape_len && g_tape[g_tape_at].kind == 'P') {
        take_nmi();
        port_refresh_once();
    }
}

void dsp4_port_write16(uint16_t value) {
    const TapeItem* c = next_call('W', value);
    if (c->kind != 'W') tape_stop("a write where a read was recorded", 'W', value);
    if (c->value != value) tape_stop("another value", 'W', value);
    served();
}

uint16_t dsp4_port_read16(void) {
    const TapeItem* c = next_call('R', 0);
    if (c->kind != 'R') tape_stop("a read where a write was recorded", 'R', 0);
    const uint16_t value = c->value;
    served();
    return value;
}

bool port_dsp4_tape_race_wait(void) {
    if (g_tape == NULL || !g_nmis) return false;
    if (g_tape_at >= g_tape_len || g_tape[g_tape_at].kind != 'N') {
        char msg[160];
        snprintf(msg, sizeof msg, "DSP-4 tape: the race's wait before call %zu has no recorded NMI", g_calls);
        port_fatal(msg);
    }
    take_nmi();
    return true;
}

void port_dsp4_tape_summary(void) {
    if (g_tape == NULL) return;
    size_t total = 0;
    for (size_t i = 0; i < g_tape_len; ++i) total += g_tape[i].kind == 'W' || g_tape[i].kind == 'R';
    printf("dsp4 tape: %zu of %zu calls matched (up to bench step %u)\n", g_calls, total,
           g_tape_at ? (unsigned)g_tape[g_tape_at - 1].step : 0u);
    fflush(stdout);
}
