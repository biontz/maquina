/* bl101_ref - S1Vave's firmware sound engine, unmodified (s1vave/bl101/third_party/s1vave),
 * running a scenario (scenario.h) on this computer. The reference that
 * tools/bl101_test compares Maquina BL-101's C++ port (Bl101Dsp.cpp) against.
 *
 *   bl101_ref SCENARIO OUT.raw     OUT: int32 stereo frames (L R), Q15
 *
 * Only the firmware's sound path is compiled: obv_dsp.h, obv_params.h,
 * obv_patches.h, obv_fx.h, obv_engine.h. The sequencer (obv_seq.h) is replaced
 * by the stubs below (no track B; the OSC DRAW table and the CHOP masks are
 * the scenario's), the arpeggiator stays off.
 *
 * SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS 44100u
#define HALF_FRAMES 256u

#include "obv_dsp.h"
#include "obv_params.h"
#include "obv_patches.h"
#include "obv_fx.h"
#include "obv_engine.h"

#include "scenario.h"

/* the sequencer's side of the engine (obv_seq.h) */
static int16_t ref_draw[16];
static uint16_t ref_chop[4];
static uint32_t seq_b_own(void) { return 0; }
static uint32_t seq_b_dry(void) { return 0; }
static void seq_tick(void) {}
static inline const int16_t *seq_draw_table(void) { return ref_draw; }
static inline const uint16_t *seq_chop_masks(void) { return ref_chop; }
static void apply_program(uint32_t n) { (void)n; }

static scenario sc;
static patch_t fxp;                             /* the fx scenarios' patch */

static void apply(const sc_ev *e)
{
    const char *c = e->cmd;
    patch_t *p = sc.fx ? &fxp : &live;
    if (!strcmp(c, "p")) {
        if (e->a >= 0 && e->a < (int32_t)NPARAM)
            p->v[e->a] = (uint8_t)(e->b < 0 ? 0 : e->b > PDEF[e->a].max ? PDEF[e->a].max : e->b);
    } else if (!strcmp(c, "on")) note_event(1, (uint32_t)e->a & 127u, (uint32_t)e->b & 127u);
    else if (!strcmp(c, "off")) note_event(0, (uint32_t)e->a & 127u, 0);
    else if (!strcmp(c, "bend")) ctl_bend = clampi(e->a, -8192, 8191);
    else if (!strcmp(c, "wheel")) ctl_vib = e->a & 127;
    else if (!strcmp(c, "sus")) ctl_sustain = e->a ? 1u : 0u;
    else if (!strcmp(c, "draw")) { if (e->a >= 0 && e->a < 16) ref_draw[e->a] = (int16_t)e->b; }
    else if (!strcmp(c, "chop")) { if (e->a >= 0 && e->a < 4) ref_chop[e->a] = (uint16_t)e->b; }
    else if (!strcmp(c, "tempo")) ctl_tempo = (uint32_t)e->a;
    else if (!strcmp(c, "panic")) ctl_panic = 1;
    else if (!strcmp(c, "factory")) {
        uint32_t n = (uint32_t)e->a % NFACT, i;
        patch_from_factory(&live, &FACTORY[n]);
        for (i = 0; i < 16u; i++) ref_draw[i] = FACT_WAVE[n].draw[i];
        for (i = 0; i < 4u; i++) ref_chop[i] = FACT_WAVE[n].chop[i];
        /* the effects off: the port's synth has none */
        live.v[P_DLY_LVL] = live.v[P_REV_LVL] = live.v[P_CHO_MODE] = live.v[P_FX_AMT] = 0;
    } else {
        fprintf(stderr, "bl101_ref: unknown command %s\n", c);
        exit(2);
    }
}

int main(int argc, char **argv)
{
    uint32_t f, i;
    int k = 0;
    FILE *out;
    static int32_t buf[2u * HALF_FRAMES];

    if (argc != 3) { fprintf(stderr, "usage: bl101_ref SCENARIO OUT.raw\n"); return 2; }
    if (sc_load(argv[1], &sc)) return 2;
    out = fopen(argv[2], "wb");
    if (!out) { fprintf(stderr, "bl101_ref: cannot write %s\n", argv[2]); return 2; }

    patch_defaults(&live);
    patch_defaults(&fxp);
    for (i = 0; i < 16u; i++) ref_draw[i] = (int16_t)(((int32_t)i - 7) * 4096);
    for (i = 0; i < 4u; i++) ref_chop[i] = 0xFFFFu;
    engine_init();

    for (f = 0; f < sc.length; f += sc.block) {
        while (k < sc.n && sc.ev[k].frame < f + sc.block) apply(&sc.ev[k++]);
        if (sc.fx) {
            for (i = 0; i < sc.block; i++) sc_input(f + i, &buf[2u * i], &buf[2u * i + 1u]);
            fx_block(&fxp, 60u * FS / clampi(ctl_tempo, 40, 240));
            fx_process(buf, sc.block);
        } else {
            memset(buf, 0, sizeof buf);
            engine_audio(buf, sc.block);
        }
        fwrite(buf, sizeof(int32_t), 2u * sc.block, out);
    }
    fclose(out);
    return 0;
}
