/* scenario.h - the scenario scripts of tools/bl101_ref (the S1Vave firmware engine,
 * C) and tools/bl101_test (the Maquina BL-101 port, C++). Plain C99, shared.
 *
 *   # comment
 *   mode synth | fx          synth: notes etc. into the engine; fx: the test
 *                            signal (sc_input) through the effects
 *   length FRAMES            at 44100 Hz
 *   FRAME CMD [A [B]]        applied before the control block (synth: 16
 *                            frames, fx: 256, as the firmware's ISR) holding FRAME
 *
 *   p P_INDEX VALUE          a patch byte (obv_params.h order)
 *   on NOTE VEL | off NOTE   notes
 *   bend V (-8192..8191) | wheel V (CC 1) | sus 0/1 (CC 64)
 *   draw STEP VALUE          OSC DRAW table, int16
 *   chop SOURCE MASK         OSC CHOP mask, 0..65535 (0 sqr, 1 saw, 2 sub, 3 noise)
 *   tempo BPM                LFO sync / delay note values (integer)
 *   factory N                factory sound N (0..63): patch, draw table, masks
 *   panic                    all notes off
 *
 * SPDX-License-Identifier: GPL-3.0-only */
#ifndef BL101_SCENARIO_H
#define BL101_SCENARIO_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC_MAX_EV 8192

typedef struct { uint32_t frame; char cmd[12]; int32_t a, b; } sc_ev;
typedef struct {
    int fx;
    uint32_t length, block;
    int n;
    sc_ev ev[SC_MAX_EV];
} scenario;

static int sc_load(const char *path, scenario *s)
{
    char line[256];
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "%s: cannot open\n", path); return -1; }
    memset(s, 0, sizeof *s);
    s->length = 44100;
    while (fgets(line, sizeof line, f)) {
        char w0[32] = "", w1[32] = "";
        long a = 0, b = 0;
        char *h = strchr(line, '#');
        if (h) *h = 0;
        int k = sscanf(line, "%31s %31s %ld %ld", w0, w1, &a, &b);
        if (k <= 0) continue;
        if (!strcmp(w0, "mode")) { s->fx = !strcmp(w1, "fx"); continue; }
        if (!strcmp(w0, "length")) { s->length = (uint32_t)strtoul(w1, NULL, 10); continue; }
        if (s->n >= SC_MAX_EV || k < 2) { fprintf(stderr, "%s: bad line: %s", path, line); fclose(f); return -1; }
        sc_ev *e = &s->ev[s->n++];
        e->frame = (uint32_t)strtoul(w0, NULL, 10);
        snprintf(e->cmd, sizeof e->cmd, "%s", w1);
        e->a = (int32_t)a;
        e->b = (int32_t)b;
    }
    fclose(f);
    s->block = s->fx ? 256u : 16u;
    s->length -= s->length % s->block;
    return 0;
}

/* the fx scenarios' input: a detuned saw pair with noise hits, Q15 stereo */
static void sc_input(uint32_t frame, int32_t *l, int32_t *r)
{
    static uint32_t ph1, ph2, rnd = 0x2545F491u;
    static int32_t env;
    if (frame == 0) { ph1 = ph2 = 0; rnd = 0x2545F491u; env = 0; }
    ph1 += 21424615u;                          /* 220 Hz */
    ph2 += 21524615u;
    if (frame % 22050u == 0) env = 30000;      /* a hit every 0.5 s */
    rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5;
    int32_t n = ((int32_t)(rnd >> 16) - 32768) * env >> 15;
    env -= env >> 10;
    int32_t s1 = ((int32_t)(ph1 >> 16) - 32768) >> 2, s2 = ((int32_t)(ph2 >> 16) - 32768) >> 2;
    *l = s1 + (n >> 1);
    *r = s2 - (n >> 1);
}

#endif
