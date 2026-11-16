/*
 * render_ref - fidelity and isolation checks for the engine host layer.
 *
 *  1. Single-voice path vs firmware mixer: for every voice and a set of
 *     patches, lxr_render_voice() must match the DAC1 output of the firmware's
 *     own mixer_calcNextSampleBlock() sample for sample.
 *  2. Instance isolation: two engines rendered interleaved (state swapped in
 *     and out every block) must match each engine rendered alone.
 *  3. Optional: write a WAV of each voice at the engine rate (-w dir).
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lxr_host.h"
#include "../../common/Timing.h"
#include "Parameters.h"

#define BLOCKS 700 /* ~1 s at 44140 Hz */

typedef struct { uint16_t par; uint8_t val; } Edit;

/* Patch edits per voice, on top of the init kit: the init kit plus a few
 * variations exercising FM/mix mode, filter types, transients, LFO + velocity
 * modulation, decimation and drive. */
typedef struct { const char* name; Edit edits[16]; } Patch;

static const Patch patches[] = {
    { "initkit", { {0, 0} } },
    { "fm_lp_lfo", {
        { PAR_MIX_MOD_1, 0 }, { PAR_MIX_MOD_2, 0 }, { PAR_MIX_MOD_3, 0 },
        { PAR_FMAMNT1, 90 }, { PAR_FMAMNT2, 90 }, { PAR_FMAMNT3, 90 },
        { PAR_FILTER_TYPE_1, 1 }, { PAR_FILTER_TYPE_4, 2 }, { PAR_FILTER_TYPE_6, 3 },
        { PAR_RESO_1, 100 }, { PAR_RESO_4, 90 }, { PAR_RESO_6, 80 },
        { PAR_AMOUNT_LFO1, 100 }, { PAR_AMOUNT_LFO4, 100 }, { PAR_FREQ_LFO1, 90 }, { 0, 0 } } },
    { "drive_decim_trans", {
        { PAR_DRIVE1, 100 }, { PAR_SNARE_DISTORTION, 100 }, { PAR_HAT_DISTORTION, 90 },
        { PAR_VOICE_DECIMATION1, 60 }, { PAR_VOICE_DECIMATION4, 50 }, { PAR_VOICE_DECIMATION6, 70 },
        { PAR_TRANS1_VOL, 120 }, { PAR_TRANS4_VOL, 120 }, { PAR_TRANS5_VOL, 120 },
        { PAR_TRANS1_WAVE, 3 }, { PAR_TRANS4_WAVE, 5 }, { PAR_PAN1, 20 }, { PAR_PAN4, 110 },
        { PAR_VELOD1, 110 }, { PAR_VELOD6_OPEN, 120 }, { 0, 0 } } },
    { "lfo_velo_mod", { { PAR_FILTER_TYPE_1, 0 }, { PAR_FILTER_FREQ_1, 60 }, { PAR_FILTER_FREQ_4, 60 },
        { PAR_FILTER_FREQ_6, 60 }, { 0, 0 } } },
};
#define NUM_PATCHES (sizeof(patches) / sizeof(patches[0]))

/* modTargets[] index of firmware parameter `par` within voice v's window */
static uint8_t mod_target_for(uint8_t v, uint16_t par)
{
    uint8_t start, end;
    lxr_mod_target_range(v, &start, &end);
    for (uint8_t i = start; i <= end; i++)
        if (lxr_mod_target_param(i) == par) return i;
    return 0;
}

static void setup_patch(const Patch* p, uint32_t seed)
{
    lxr_state_reset();
    lxr_load_init_kit();
    for (int i = 0; i < 16 && p->edits[i].par; i++)
        lxr_set_param(p->edits[i].par, p->edits[i].val);
    if (!strcmp(p->name, "lfo_velo_mod")) {
        /* every voice: LFO -> filter cutoff, velocity -> amp decay */
        static const uint16_t filt[6] = { PAR_FILTER_FREQ_1, PAR_FILTER_FREQ_2, PAR_FILTER_FREQ_3,
                                          PAR_FILTER_FREQ_4, PAR_FILTER_FREQ_5, PAR_FILTER_FREQ_6 };
        static const uint16_t dec[6] = { PAR_VELOD1, PAR_VELOD2, PAR_VELOD3, PAR_VELOD4, PAR_VELOD5,
                                         PAR_VELOD6_CLOSED };
        for (uint8_t v = 0; v < 6; v++) {
            lxr_set_param(PAR_TARGET_LFO1 + v, mod_target_for(v, filt[v]));
            lxr_set_param(PAR_AMOUNT_LFO1 + v, 110);
            lxr_set_param(PAR_FREQ_LFO1 + v, 70);
            lxr_set_param(PAR_WAVE_LFO1 + v, (uint8_t)(v % 8));
            lxr_set_param(PAR_RETRIGGER_LFO1 + v, (uint8_t)(v % 2 ? v + 1 : 0));
            lxr_set_param(PAR_VEL_DEST_1 + v, mod_target_for(v, dec[v]));
            lxr_set_param(PAR_VELO_MOD_AMT_1 + v, 120);
            if (!mod_target_for(v, filt[v]) || !mod_target_for(v, dec[v]))
                printf("warning: voice %u has no mod target for filter/decay\n", v);
        }
    }
    lxr_seed_rng(seed);
}

/* trigger pattern: hits at fixed blocks with varying velocity and note */
static int trigger_at(int block, uint8_t* note, uint8_t* vel)
{
    static const struct { int block; uint8_t note, vel; } hits[] = {
        { 0, 60, 127 }, { 90, 60, 64 }, { 200, 67, 100 }, { 330, 48, 127 }, { 500, 60, 30 },
    };
    for (size_t i = 0; i < sizeof(hits) / sizeof(hits[0]); i++)
        if (hits[i].block == block) { *note = hits[i].note; *vel = hits[i].vel; return 1; }
    return 0;
}

static uint8_t trigger_slot(uint8_t voice, int block)
{
    /* hihat alternates closed/open */
    if (voice == LXR_VOICE_HIHAT && (block / 90) % 2)
        return LXR_TRIGGER_HIHAT_OPEN;
    return voice;
}

static void write_wav(const char* path, const int16_t* lr, size_t frames)
{
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    const uint32_t rate = 44140, bytes = (uint32_t)(frames * 4);
    uint8_t h[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\0\0\0\0\0\0\0\x04\0\x10\0data";
    uint32_t v;
    v = 36 + bytes; memcpy(h + 4, &v, 4);
    memcpy(h + 24, &rate, 4);
    v = rate * 4; memcpy(h + 28, &v, 4);
    memcpy(h + 40, &bytes, 4);
    fwrite(h, 1, 44, f);
    fwrite(lr, 4, frames, f);
    fclose(f);
}

/* --plugin-ref VOICE SECONDS OUT.wav: what the plugin must output at
 * 44140 Hz for lv2_render's --aligned note pattern (defaults, Key Track off,
 * seed 7). Every note (60 and 46) plays the voice at its set pitch; the
 * HiHat plays the closed-hat trigger. */
static int plugin_reference(uint8_t voice, double seconds, const char* path)
{
    static const uint8_t vels[] = { 127, 64, 100, 127, 30, 90, 110, 50 };
    const double rate = 44140.0;
    const size_t total = (size_t)(seconds * rate);
    const size_t blocks = (total + LXR_BLOCK_SIZE - 1) / LXR_BLOCK_SIZE;
    int16_t* out = calloc(blocks * LXR_BLOCK_SIZE * 2, sizeof(int16_t));
    size_t hit_block[64];
    uint8_t hit_note[64], hit_vel[64];
    int nhits = 0;
    double t = 0.0;
    for (int i = 0; t < seconds - 0.4 && nhits < 64; i++, t += 0.37) {
        uint64_t f = (uint64_t)(t * rate) + (uint64_t)(0.05 * rate);
        f = ((f + MAQUINA_EVENT_DELAY + 63) / 64) * 64 - MAQUINA_EVENT_DELAY;
        hit_block[nhits] = (size_t)((f + MAQUINA_EVENT_DELAY) / 64);
        hit_note[nhits] = (i % 2) ? 46 : 60;
        hit_vel[nhits] = vels[i % 8];
        nhits++;
    }

    lxr_state_reset();
    lxr_load_init_kit();
    lxr_seed_rng(7);
    for (size_t b = 0; b < blocks; b++) {
        for (int h = 0; h < nhits; h++) {
            if (hit_block[h] != b) continue;
            lxr_note_on(voice, 60, hit_vel[h]);
        }
        lxr_render_voice(voice, out + b * LXR_BLOCK_SIZE * 2);
    }
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); return 1; }
    fwrite(out, 4, total, f);
    fclose(f);
    free(out);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 5 && !strcmp(argv[1], "--plugin-ref")) {
        lxr_global_init();
        return plugin_reference((uint8_t)atoi(argv[2]), atof(argv[3]), argv[4]);
    }
    const char* wavdir = (argc > 2 && !strcmp(argv[1], "-w")) ? argv[2] : NULL;
    lxr_global_init();
    printf("engine state blob: %zu bytes\n", lxr_state_size());

    int failures = 0;
    static int16_t a[BLOCKS * LXR_BLOCK_SIZE * 2], b[BLOCKS * LXR_BLOCK_SIZE * 2];
    static int16_t o1[LXR_BLOCK_SIZE * 2], o2[LXR_BLOCK_SIZE * 2];
    uint8_t* blob = malloc(lxr_state_size());
    uint8_t* blobA = malloc(lxr_state_size());
    uint8_t* blobB = malloc(lxr_state_size());

    /* ---- 1. single voice vs firmware mixer --------------------------- */
    for (size_t pi = 0; pi < NUM_PATCHES; pi++) {
        for (uint8_t v = 0; v < LXR_NUM_VOICES; v++) {
            setup_patch(&patches[pi], 12345);
            lxr_test_freeze_rng(1);
            lxr_state_save(blob);
            long peak = 0;

            for (int pass = 0; pass < 2; pass++) {
                lxr_state_load(blob);
                int16_t* dst = pass ? b : a;
                for (int blk = 0; blk < BLOCKS; blk++) {
                    uint8_t note, vel;
                    if (trigger_at(blk, &note, &vel))
                        lxr_note_on(trigger_slot(v, blk), note, vel);
                    if (pass == 0) {
                        lxr_render_voice(v, dst + blk * LXR_BLOCK_SIZE * 2);
                    } else {
                        lxr_render_mixer_reference(o1, o2);
                        memcpy(dst + blk * LXR_BLOCK_SIZE * 2, o2, sizeof(o2));
                    }
                }
            }
            lxr_test_freeze_rng(0);

            size_t n = BLOCKS * LXR_BLOCK_SIZE * 2, diff = 0, first = 0;
            for (size_t i = 0; i < n; i++) {
                if (abs(a[i]) > peak) peak = abs(a[i]);
                if (a[i] != b[i]) { if (!diff) first = i; diff++; }
            }
            printf("[mixer-match] %-18s voice %u: peak %6ld  %s", patches[pi].name, v, peak,
                   diff ? "MISMATCH" : "ok");
            if (diff) printf(" (%zu samples differ, first at frame %zu: %d vs %d)", diff, first / 2, a[first], b[first]);
            if (peak == 0) printf("  [silent!]");
            printf("\n");
            if (diff || peak == 0) failures++;
        }
    }

    /* ---- 2. isolation: two instances interleaved --------------------- */
    {
        /* reference runs, alone */
        static int16_t refA[BLOCKS * LXR_BLOCK_SIZE * 2], refB[BLOCKS * LXR_BLOCK_SIZE * 2];
        setup_patch(&patches[1], 1); lxr_state_save(blobA);
        setup_patch(&patches[2], 2); lxr_state_save(blobB);
        for (int which = 0; which < 2; which++) {
            lxr_state_load(which ? blobB : blobA);
            for (int blk = 0; blk < BLOCKS; blk++) {
                uint8_t note, vel;
                if (trigger_at(blk, &note, &vel)) lxr_note_on(which ? LXR_VOICE_SNARE : LXR_VOICE_DRUM1, note, vel);
                lxr_render_voice(which ? LXR_VOICE_SNARE : LXR_VOICE_DRUM1,
                                 (which ? refB : refA) + blk * LXR_BLOCK_SIZE * 2);
            }
        }
        /* interleaved */
        setup_patch(&patches[1], 1); lxr_state_save(blobA);
        setup_patch(&patches[2], 2); lxr_state_save(blobB);
        for (int blk = 0; blk < BLOCKS; blk++) {
            for (int which = 0; which < 2; which++) {
                lxr_state_load(which ? blobB : blobA);
                uint8_t note, vel;
                if (trigger_at(blk, &note, &vel)) lxr_note_on(which ? LXR_VOICE_SNARE : LXR_VOICE_DRUM1, note, vel);
                lxr_render_voice(which ? LXR_VOICE_SNARE : LXR_VOICE_DRUM1,
                                 (which ? b : a) + blk * LXR_BLOCK_SIZE * 2);
                lxr_state_save(which ? blobB : blobA);
            }
        }
        size_t n = BLOCKS * LXR_BLOCK_SIZE * 2;
        int okA = !memcmp(a, refA, n * 2), okB = !memcmp(b, refB, n * 2);
        printf("[isolation] instance A %s, instance B %s\n", okA ? "ok" : "LEAK", okB ? "ok" : "LEAK");
        if (!okA || !okB) failures++;
    }

    /* ---- 3. WAVs ------------------------------------------------------ */
    if (wavdir) {
        static const char* names[] = { "drum1", "drum2", "drum3", "snare", "cymbal", "hihat" };
        for (size_t pi = 0; pi < NUM_PATCHES; pi++)
            for (uint8_t v = 0; v < LXR_NUM_VOICES; v++) {
                setup_patch(&patches[pi], 777);
                for (int blk = 0; blk < BLOCKS; blk++) {
                    uint8_t note, vel;
                    if (trigger_at(blk, &note, &vel)) lxr_note_on(trigger_slot(v, blk), note, vel);
                    lxr_render_voice(v, a + blk * LXR_BLOCK_SIZE * 2);
                }
                char path[512];
                snprintf(path, sizeof(path), "%s/%s_%s.wav", wavdir, patches[pi].name, names[v]);
                write_wav(path, a, BLOCKS * LXR_BLOCK_SIZE);
            }
        printf("wrote WAVs to %s\n", wavdir);
    }

    free(blob); free(blobA); free(blobB);
    printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
