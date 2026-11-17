// Bl101Dsp.cpp - see Bl101Dsp.hpp. The firmware's obv_engine.h / obv_fx.h with
// their globals as members of Synth::State / Fx::State; everything else is
// the firmware's text. Comments marked "(firmware)" are the original's.
// Built without -ffast-math and with -fwrapv, as tools/bl101_ref builds the
// firmware (s1vave/Makefile).
// SPDX-License-Identifier: GPL-3.0-only
#include "Bl101Dsp.hpp"

#include <cstring>
#include <new>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wc99-designator"
#endif
#pragma GCC diagnostic ignored "-Wunused-function"

#include "obv_dsp.h"
#include "obv_params.h"

#define FS 44100u
#define HALF_FRAMES 256u
#include "obv_patches.h"

namespace bl101 {

static_assert(kCtl == CTL, "CTL");
static_assert(kNumBytes == PATCH_BYTES, "PATCH_BYTES");

uint8_t paramDefault(uint32_t p) { return p < NPARAM ? PDEF[p].def : 0; }
uint8_t paramMax(uint32_t p) { return p < NPARAM ? PDEF[p].max : 0; }
uint32_t numFirmwareParams() { return NPARAM; }
uint32_t numFactorySounds() { return NFACT; }

void initDrawTable(int16_t* t)
{
    for (int32_t i = 0; i < 16; i++) t[i] = (int16_t)((i - 7) * 4096); // (firmware) obv_seq.h pat_init
}

const char* factorySound(uint32_t n, uint8_t* bytes, int16_t* draw, uint16_t* chop)
{
    if (n >= NFACT) n = 0;
    patch_t p;
    patch_from_factory(&p, &FACTORY[n]);
    std::memcpy(bytes, p.v, PATCH_BYTES);
    for (uint32_t i = 0; i < 16u; i++) draw[i] = FACT_WAVE[n].draw[i];
    for (uint32_t i = 0; i < 4u; i++) chop[i] = FACT_WAVE[n].chop[i];
    return FACTORY[n].name;
}

// ===========================================================================
// the synth (obv_engine.h)

namespace {

constexpr uint32_t NV = 8u;
constexpr uint32_t NHELD = 16u;
constexpr int32_t ENV_ONE = 1 << 24;
constexpr int32_t LEVEL_FLOOR = 2048;
constexpr uint32_t NQ = 64u;

enum { E_IDLE, E_ATT, E_DEC, E_SUS, E_REL };
struct env_t { uint32_t stage; int32_t level; };

struct voice_t {
    osc_t o_sqr, o_saw, o_sub;
    pink_noise_t pn;
    uint32_t noise;
    env_t fe, ae;
    svf_state_t f1, f2;
    svf_coef_t c1, c2;
    uint32_t inc, pw;
    int32_t amp0, amp1;
    int32_t pitch16, target16;
    int32_t det8;
    int32_t gl, gr;
    int32_t drift;
    int32_t noise_lp;
    uint32_t lfo_blk;
    int32_t lfo_fade;
    uint32_t seq;
    uint8_t note, vel, gate;
    uint8_t part;
    uint8_t chord_idx;
};

struct nev_t { uint8_t type, note, vel, part; };

struct part_t {
    const patch_t* p;
    uint32_t lfo_ph, lfo_rnd_a, lfo_rnd_b;
    int32_t lfo_val, lfo_noi_sm;
    int32_t last_pitch16;
    int32_t fenv_amt, keytrk, lfo_pd, lfo_fd, wheel_q, bend8, flt_bend8;
    int32_t lvl_sqr, lvl_saw, lvl_sub, lvl_noi, reso_k1, reso_k2, vint, sus_f, sus_a, gain_q12;
    int32_t cut_sm, reso_sm;
    uint32_t pw_base, pwm_src, sub_type, noise_mode, draw_sw, draw_mult_q8;
    uint32_t chop_ovtn, chop_comb_q8;
    int32_t g_chop_masked;
    uint32_t poly_mode, porta_mode, glide_k;
    int32_t fine_tune8, range_semi8, transp_semi8;
};

// (firmware) Vintage warmth card offsets (Oct 19 Midnight Tone)
const int16_t VINT_TUNE8_WARM[NV] = {-12, 10, -8, 12, -10, 8, -6, 10};
const int8_t VINT_ENV_WARM[NV] = {4, -3, 5, -4, 3, -2, 4, -3};
const int32_t VINT_PW8_WARM[NV] = {-0x01800000, 0x01200000, -0x00D00000, 0x01A00000,
                                   -0x01000000, 0x01400000, -0x00A00000, 0x01600000};
const int8_t RANGE_SEMITONES[6] = {-36, -24, -12, 0, 12, 24};

inline void env_tick(env_t* e, uint32_t a, uint32_t d, int32_t sus, uint32_t r, int32_t gate, int32_t vint)
{
    int32_t k, target;
    if (gate && (e->stage == E_IDLE || e->stage >= E_REL)) e->stage = E_ATT;
    if (!gate && e->stage && e->stage < E_REL) e->stage = E_REL;
    switch (e->stage) {
    case E_ATT:
        k = (int32_t)ENV_K_ATT[a & 127u];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        target = ENV_ONE + (ENV_ONE * 3 >> 3);
        e->level += (int32_t)(((int64_t)(target - e->level) * k) >> 24);
        if (e->level >= ENV_ONE) { e->level = ENV_ONE; e->stage = E_DEC; }
        break;
    case E_DEC:
        k = (int32_t)ENV_K_DEC[d & 127u];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        e->level += (int32_t)(((int64_t)(sus - e->level) * k) >> 24) - 16;
        if (e->level <= sus + 64) { e->level = sus; e->stage = E_SUS; }
        break;
    case E_SUS:
        e->level = sus;
        break;
    case E_REL:
        k = (int32_t)ENV_K_REL[r & 127u];
        k += (int32_t)(((int64_t)k * vint) >> 7);
        e->level += (int32_t)(((int64_t)(0 - e->level) * k) >> 24) - 32;
        if (e->level < LEVEL_FLOOR) { e->level = 0; e->stage = E_IDLE; }
        break;
    default:
        e->level = 0;
    }
}

} // namespace

// The globals of obv_engine.h that a single sound (part 0, no sequencer,
// no arpeggiator) uses. Names as in the firmware.
struct Synth::State {
    patch_t live;
    int16_t draw_table[16];
    uint16_t chop_mask[4];

    voice_t vc[NV];
    uint32_t vseq = 0, rr = 0;

    nev_t nq[NQ];
    uint32_t nq_w = 0, nq_r = 0;

    int32_t ctl_bend = 0;
    uint32_t ctl_sustain = 0, ctl_cc7 = 127;
    int32_t ctl_vib = 0;
    int32_t ctl_tune8 = 0;
    double ctl_tempo = 120.0; // (firmware: uint32_t; the host's tempo may have a fraction)
    uint32_t ctl_panic = 0;
    uint32_t ctl_note_pri = 0; // 0 = last

    int32_t ui_vlevel[NV];

    struct { uint8_t note, vel, phys; } held[NHELD];
    uint32_t nheld = 0;
    uint32_t rnd_state = 0x9E3779B9u;

    part_t pt;
    uint32_t sus_was = 0;
    int32_t vol_q12 = 0;

    State()
    {
        std::memset(&live, 0, sizeof live);
        patch_defaults(&live);
        initDrawTable(draw_table);
        for (auto& m : chop_mask) m = 0xFFFFu;
        std::memset(vc, 0, sizeof vc);
        std::memset(nq, 0, sizeof nq);
        std::memset(ui_vlevel, 0, sizeof ui_vlevel);
        std::memset(held, 0, sizeof held);
        std::memset(&pt, 0, sizeof pt);
        pt.p = &live;
        // (firmware) pt[2] = {{&live, 0, 0, 0, 0, 60 << 16}, ...}: the sixth
        // field is lfo_noi_sm (meant for last_pitch16, which stays 0)
        pt.lfo_noi_sm = 60 << 16;
        engine_init();
    }

    // ---- note events
    void note_event(uint32_t on, uint32_t note, uint32_t vel)
    {
        if (nq_w - nq_r >= NQ) return;
        nq[nq_w % NQ].part = 0;
        nq[nq_w % NQ].type = (uint8_t)on;
        nq[nq_w % NQ].note = (uint8_t)note;
        nq[nq_w % NQ].vel = (uint8_t)vel;
        nq_w++;
    }

    void held_remove(uint32_t note)
    {
        uint32_t i, j;
        for (i = j = 0; i < nheld; i++)
            if (held[i].note != note) held[j++] = held[i];
        nheld = j;
    }

    // (firmware) ADV stereo: applied at note-on only
    int32_t SPREAD64() const { return (int32_t)live.v[P_SPREAD] * 64 / 100; }
    int32_t DETUNE8() const { return (int32_t)live.v[P_DETUNE] * 256 / 100; }

    void voice_pan(voice_t* v, int32_t pan)
    {
        uint32_t pos = (uint32_t)(clampi(pan, -64, 64) + 64);
        v->gl = PAN_Q12[128u - pos];
        v->gr = PAN_Q12[pos];
    }

    void voice_start(voice_t* v, uint32_t part, uint32_t note, uint32_t vel, int32_t det8, int32_t pan, uint8_t c_idx)
    {
        const uint8_t* pv = pt.p->v;
        v->part = (uint8_t)part;
        v->note = (uint8_t)note;
        v->vel = (uint8_t)vel;
        v->gate = 1;
        v->seq = ++vseq;
        v->det8 = det8;
        v->chord_idx = c_idx;
        v->target16 = (int32_t)note << 16;
        v->pitch16 = (pv[P_PORTA_MODE] == PORTA_ON && pv[P_PORTA_TIME]) ? pt.last_pitch16 : v->target16;
        v->fe.stage = E_ATT;
        v->ae.stage = E_ATT;
        v->lfo_blk = 0;
        v->lfo_fade = 32767;
        uint32_t jitter = xorshift(&v->noise) & 0x1FFFFFFFu;
        v->o_sqr.ph += jitter;
        v->o_saw.ph = v->o_sqr.ph;
        v->o_sub.ph = (pv[P_SUB_TYPE] % 3u == SUB_1OC) ? (v->o_sqr.ph >> 1) : (v->o_sqr.ph >> 2);
        if (pv[P_RESO] > 110) {
            v->f2.ic1 += (int32_t)pv[P_RESO] * 64;
        }
        voice_pan(v, pan);
    }

    uint32_t voice_alloc()
    {
        uint32_t i, v, pick = 0, best = 0xFFFFFFFFu;
        for (i = 0; i < NV; i++) {
            v = (rr + i) % NV;
            if (vc[v].ae.stage == E_IDLE) { rr = (v + 1u) % NV; return v; }
        }
        for (v = 0; v < NV; v++) {
            if (!vc[v].gate && vc[v].seq < best) { best = vc[v].seq; pick = v; }
        }
        if (best == 0xFFFFFFFFu) {
            for (v = 0; v < NV; v++) {
                if (vc[v].seq < best) { best = vc[v].seq; pick = v; }
            }
        }
        rr = (pick + 1u) % NV;
        return pick;
    }

    void voice_trigger(uint32_t note, uint32_t vel)
    {
        const uint32_t part = 0;
        const uint8_t* pv = pt.p->v;
        uint32_t mode = pv[P_POLY_MODE] % NPOLY_MODE;

        if (mode == POLY_POLY) {
            uint32_t v = voice_alloc();
            voice_start(&vc[v], part, note, vel,
                        (vseq & 1) ? DETUNE8() : -DETUNE8(),
                        (vseq & 1) ? SPREAD64() : -SPREAD64(), 0);
        } else if (mode == POLY_MONO) {
            voice_t* v = &vc[0];
            int legato = v->gate;
            if (legato) {
                v->note = (uint8_t)note;
                v->vel = (uint8_t)vel;
                v->target16 = (int32_t)note << 16;
                if (pv[P_PORTA_MODE] == PORTA_OFF) v->pitch16 = v->target16;
                if (pv[P_ENV_TRG] == TRIG_GATE_TRIG) {
                    v->fe.stage = E_ATT;
                    v->ae.stage = E_ATT;
                }
            } else {
                voice_start(v, part, note, vel, 0, 0, 0);
            }
        } else if (mode == POLY_UNISON) {
            int32_t det = 22 + DETUNE8();
            int32_t det_offsets[4] = {-det, -det / 3, det / 3, det};
            int32_t sp = SPREAD64();
            int32_t pan_offsets[4] = {-sp, -sp / 3, sp / 3, sp};
            int legato = vc[0].gate;
            if (legato) {
                for (uint32_t i = 0; i < 4; i++) {
                    vc[i].note = (uint8_t)note;
                    vc[i].vel = (uint8_t)vel;
                    vc[i].target16 = (int32_t)note << 16;
                    if (pv[P_PORTA_MODE] == PORTA_OFF) vc[i].pitch16 = vc[i].target16;
                    if (pv[P_ENV_TRG] == TRIG_GATE_TRIG) {
                        vc[i].fe.stage = E_ATT;
                        vc[i].ae.stage = E_ATT;
                    }
                }
            } else {
                uint32_t base_ph = vc[0].o_sqr.ph;
                for (uint32_t i = 0; i < 4; i++) {
                    voice_start(&vc[i], part, note, vel, det_offsets[i], pan_offsets[i], (uint8_t)i);
                    uint32_t u_ph = base_ph + i * 0x40000000u;
                    vc[i].o_sqr.ph = u_ph;
                    vc[i].o_saw.ph = u_ph;
                    uint32_t u_sub = (pv[P_SUB_TYPE] % 3u == SUB_1OC) ? (u_ph >> 1) : (u_ph >> 2);
                    vc[i].o_sub.ph = u_sub + i * 0x40000000u;
                }
                for (uint32_t i = 4; i < NV; i++) {
                    vc[i].gate = 0;
                }
            }
        } else if (mode == POLY_CHORD) {
            int legato = vc[0].gate;
            int32_t shifts[4] = {
                0,
                pv[P_CHORD_SW2] ? (int32_t)pv[P_CHORD_KS2] - 12 : 0,
                pv[P_CHORD_SW3] ? (int32_t)pv[P_CHORD_KS3] - 12 : 0,
                pv[P_CHORD_SW4] ? (int32_t)pv[P_CHORD_KS4] - 12 : 0
            };
            if (legato) {
                for (uint32_t i = 0; i < 4; i++) {
                    int active = (i == 0) || (i == 1 && pv[P_CHORD_SW2]) || (i == 2 && pv[P_CHORD_SW3]) || (i == 3 && pv[P_CHORD_SW4]);
                    if (active) {
                        uint32_t cn = (uint32_t)clampi((int32_t)note + shifts[i], 0, 127);
                        vc[i].note = (uint8_t)cn;
                        vc[i].vel = (uint8_t)vel;
                        vc[i].target16 = (int32_t)cn << 16;
                        if (pv[P_PORTA_MODE] == PORTA_OFF) vc[i].pitch16 = vc[i].target16;
                        if (pv[P_ENV_TRG] == TRIG_GATE_TRIG) {
                            vc[i].fe.stage = E_ATT;
                            vc[i].ae.stage = E_ATT;
                        }
                    } else {
                        vc[i].gate = 0;
                    }
                }
            } else {
                int32_t sp = SPREAD64();
                int32_t pan_offsets[4] = {-sp, -sp / 3, sp / 3, sp};
                voice_start(&vc[0], part, note, vel, 0, pan_offsets[0], 0);
                for (uint32_t i = 1; i < 4; i++) {
                    int active = (i == 1 && pv[P_CHORD_SW2]) || (i == 2 && pv[P_CHORD_SW3]) || (i == 3 && pv[P_CHORD_SW4]);
                    if (active) {
                        uint32_t cn = (uint32_t)clampi((int32_t)note + shifts[i], 0, 127);
                        voice_start(&vc[i], part, cn, vel, 0, pan_offsets[i], (uint8_t)i);
                    } else {
                        vc[i].gate = 0;
                    }
                }
                for (uint32_t i = 4; i < NV; i++) {
                    vc[i].gate = 0;
                }
            }
        }
        pt.last_pitch16 = (int32_t)note << 16;
        if (pv[P_LFO_TRIG]) {
            pt.lfo_ph = 0;
            pt.lfo_val = lfo_shape(&pt, 0, pv[P_LFO_WAVE]);
        }
    }

    void voice_release(uint32_t note)
    {
        const uint8_t* pv = pt.p->v;
        uint32_t v, mode = pv[P_POLY_MODE];

        if (mode == POLY_MONO || mode == POLY_UNISON || mode == POLY_CHORD) {
            if (nheld) {
                uint8_t fallback = held[0].note;
                if (ctl_note_pri == 1) {
                    for (uint32_t i = 1; i < nheld; i++) {
                        if (held[i].note < fallback) fallback = held[i].note;
                    }
                } else {
                    fallback = held[nheld - 1u].note;
                }
                if (mode == POLY_MONO) {
                    vc[0].note = fallback;
                    vc[0].target16 = (int32_t)fallback << 16;
                    if (pv[P_PORTA_MODE] == PORTA_OFF) vc[0].pitch16 = vc[0].target16;
                } else if (mode == POLY_UNISON) {
                    for (uint32_t i = 0; i < 4; i++) {
                        vc[i].note = fallback;
                        vc[i].target16 = (int32_t)fallback << 16;
                        if (pv[P_PORTA_MODE] == PORTA_OFF) vc[i].pitch16 = vc[i].target16;
                    }
                } else if (mode == POLY_CHORD) {
                    int32_t shifts[4] = {
                        0,
                        pv[P_CHORD_SW2] ? (int32_t)pv[P_CHORD_KS2] - 12 : 0,
                        pv[P_CHORD_SW3] ? (int32_t)pv[P_CHORD_KS3] - 12 : 0,
                        pv[P_CHORD_SW4] ? (int32_t)pv[P_CHORD_KS4] - 12 : 0
                    };
                    for (uint32_t i = 0; i < 4; i++) {
                        int active = (i == 0) || (i == 1 && pv[P_CHORD_SW2]) || (i == 2 && pv[P_CHORD_SW3]) || (i == 3 && pv[P_CHORD_SW4]);
                        if (active) {
                            uint32_t cn = (uint32_t)clampi((int32_t)fallback + shifts[i], 0, 127);
                            vc[i].note = (uint8_t)cn;
                            vc[i].target16 = (int32_t)cn << 16;
                            if (pv[P_PORTA_MODE] == PORTA_OFF) vc[i].pitch16 = vc[i].target16;
                        }
                    }
                }
                pt.last_pitch16 = (int32_t)fallback << 16;
            } else {
                if (mode == POLY_MONO) {
                    vc[0].gate = 0;
                } else {
                    for (v = 0; v < NV; v++) vc[v].gate = 0;
                }
            }
            return;
        }
        for (v = 0; v < NV; v++) {
            if (vc[v].gate && vc[v].note == note && vc[v].part == 0) vc[v].gate = 0;
        }
    }

    void voices_release_all()
    {
        for (uint32_t v = 0; v < NV; v++) vc[v].gate = 0;
    }

    uint32_t part_sus() const { return ctl_sustain; }

    void ev_note_on(uint32_t note, uint32_t vel)
    {
        if (part_sus() && nheld > 0) {
            uint32_t any_phys = 0;
            for (uint32_t i = 0; i < nheld; i++) {
                if (held[i].phys) { any_phys = 1; break; }
            }
            if (!any_phys) nheld = 0;
        }
        held_remove(note);
        if (nheld == NHELD) {
            for (uint32_t i = 1; i < NHELD; i++) held[i - 1u] = held[i];
            nheld--;
        }
        held[nheld].note = (uint8_t)note;
        held[nheld].vel = (uint8_t)vel;
        held[nheld].phys = 1;
        nheld++;
        uint32_t pn = note;
        if (ctl_note_pri == 1) {
            for (uint32_t i = 0; i < nheld; i++)
                if (held[i].note < pn) pn = held[i].note;
        }
        voice_trigger(pn, vel);
    }

    void ev_note_off(uint32_t note)
    {
        for (uint32_t i = 0; i < nheld; i++)
            if (held[i].note == note) held[i].phys = 0;
        if (part_sus()) return;
        held_remove(note);
        voice_release(note);
    }

    void ev_sustain_up()
    {
        uint8_t gone[NHELD];
        uint32_t ng = 0, j = 0;
        for (uint32_t i = 0; i < nheld; i++) {
            if (held[i].phys) held[j++] = held[i];
            else gone[ng++] = held[i].note;
        }
        nheld = j;
        for (uint32_t i = 0; i < ng; i++) voice_release(gone[i]);
    }

    void engine_events()
    {
        uint32_t sus = part_sus();
        if (ctl_panic) { ctl_panic = 0; nheld = 0; voices_release_all(); }
        if (sus_was & ~sus & 1u) ev_sustain_up();
        sus_was = sus;
        while (nq_r != nq_w) {
            nev_t e = nq[nq_r % NQ];
            nq_r++;
            if (e.type) ev_note_on(e.note, e.vel);
            else ev_note_off(e.note);
        }
    }

    // ---- LFO
    int32_t lfo_shape(const part_t* g, uint32_t ph, uint32_t shape)
    {
        switch (shape) {
        case LFO_TRI: {
            int32_t p = (int32_t)((ph + 0x40000000u) >> 15);
            return p < 65536 ? p - 32768 : 98303 - p;
        }
        case LFO_SAW:     return (int32_t)(ph >> 16) - 32768;
        case LFO_INV_SAW: return 32767 - (int32_t)(ph >> 16);
        case LFO_SQR:     return ph < 0x80000000u ? 32767 : -32767;
        case LFO_RND:     return (int32_t)(g->lfo_rnd_b >> 16) - 32768;
        default:
            return (int32_t)(xorshift(&rnd_state) >> 16) - 32768;
        }
    }

    void lfo_tick(part_t* g)
    {
        uint32_t inc;
        if (g->p->v[P_LFO_SYNC]) {
            uint32_t slot = g->p->v[P_LFO_RATE];
            if (slot >= 31u) slot = (slot * 30u) / 127u;
            uint32_t num = LFO_SYNC_RATIO[slot][0];
            uint32_t den = LFO_SYNC_RATIO[slot][1];
            // (firmware) inc = (ctl_tempo * (25971u * den)) / num, the tempo an integer
            inc = (uint32_t)(ctl_tempo * double(25971u * den) / double(num));
        } else {
            uint32_t rate = g->p->v[P_LFO_RATE] & 127u;
            inc = LFO_INC[rate];
            if (g->p->v[P_LFO_MODE]) inc = (inc * 11u) >> 2;
        }
        uint32_t old = g->lfo_ph;
        g->lfo_ph += inc;
        if (g->lfo_ph < old) {
            g->lfo_rnd_a = g->lfo_rnd_b;
            g->lfo_rnd_b = xorshift(&rnd_state);
            if (g->p->v[P_ENV_TRG] == TRIG_LFO) {
                for (uint32_t v = 0; v < NV; v++) {
                    if (vc[v].gate && vc[v].part == 0) {
                        vc[v].fe.stage = E_ATT;
                        vc[v].ae.stage = E_ATT;
                    }
                }
            }
        }

        g->lfo_val = lfo_shape(g, g->lfo_ph, g->p->v[P_LFO_WAVE]);
        if (g->p->v[P_LFO_WAVE] == LFO_NOISE) {
            g->lfo_noi_sm += (g->lfo_val - g->lfo_noi_sm) >> 4;
        } else {
            g->lfo_noi_sm = g->lfo_val;
        }
    }

    // ---- per block
    void block_globals(part_t* g)
    {
        const uint8_t* pv = g->p->v;
        int32_t d;

        g->range_semi8 = (int32_t)RANGE_SEMITONES[pv[P_RANGE] % 6u] << 8;
        g->fine_tune8 = ((int32_t)pv[P_FINE_TUNE] - 64) * 48;
        g->transp_semi8 = ((int32_t)pv[P_TRANSPOSE] - 64) << 8;

        g->lvl_sqr = pv[P_PULSE_LVL];
        g->lvl_saw = pv[P_SAW_LVL];
        g->lvl_sub = ((uint32_t)pv[P_SUB_LVL] * 7u) >> 2;
        g->lvl_noi = (uint32_t)pv[P_NOISE_LVL] * 6u;

        g->sub_type = pv[P_SUB_TYPE] % 3u;
        g->noise_mode = pv[P_NOISE_MODE] % 2u;

        g->pwm_src = pv[P_PWM_SRC];
        g->pw_base = ((uint32_t)pv[P_PWM_DEPTH] << 25) + (1u << 24);

        g->draw_sw = pv[P_DRAW_SW];
        g->draw_mult_q8 = ((uint32_t)pv[P_DRAW_MULT] + 1u) << 8;

        g->chop_ovtn = pv[P_CHOP_OVTN];
        g->chop_comb_q8 = ((uint32_t)pv[P_CHOP_COMB] + 1u) << 8;
        g->g_chop_masked = chop_calc_masked_gain(g->chop_ovtn);

        int32_t fa = (int32_t)pv[P_FENV_AMT];
        uint32_t cut_val = pv[P_CUTOFF] & 127u;
        if (cut_val < 32u) {
            int32_t scale = 75 + (int32_t)((52u * cut_val) >> 5);
            fa = (fa * scale) / 127;
        }
        if (fa > 80) fa = 80 + ((fa - 80) * 6) / 8;
        g->fenv_amt = fa;
        g->keytrk = ((int32_t)pv[P_KEYTRK] << 8) / 127;

        uint32_t pd_idx = pv[P_LFO_PITCH] & 127u;
        g->lfo_pd = (int32_t)LFO_PITCH_DEPTH[pd_idx];
        d = pv[P_FLFO_AMT];  g->lfo_fd = (d * 38) + ((d * d * 70) >> 7);
        g->wheel_q = pv[P_MOD_DEPTH];

        g->bend8 = (ctl_bend * pv[P_OSC_BEND] * 256) >> 13;
        g->flt_bend8 = (ctl_bend * pv[P_FLT_BEND] * 256) >> 13;

        int32_t target_cut = (int32_t)CUTOFF_TAPER_WARM[pv[P_CUTOFF] & 127u];
        g->cut_sm += (target_cut - g->cut_sm) >> 6;
        int32_t reso_scaled = (int32_t)pv[P_RESO] * 2;
        if (reso_scaled > 127) reso_scaled = 127;
        g->reso_sm += ((reso_scaled << 8) - g->reso_sm) >> 6;
        g->reso_k1 = 8192 - g->reso_sm * 8400 / (127 * 256);
        g->reso_k2 = 8192;

        int32_t sus_raw = pv[P_SUSTAIN];
        int32_t sus_adj = sus_raw + ((sus_raw * (127 - sus_raw) * 29) >> 12);
        g->sus_f = sus_adj * (ENV_ONE / 127);
        g->sus_a = sus_adj * (ENV_ONE / 127);

        d = pv[P_VOLUME];
        g->gain_q12 = (int32_t)((d * d) >> 2);

        g->poly_mode = pv[P_POLY_MODE] % NPOLY_MODE;
        g->porta_mode = pv[P_PORTA_MODE] % 3u;
        g->glide_k = GLIDE_K[pv[P_PORTA_TIME] & 127u];
    }

    void voice_block(voice_t* v, uint32_t card)
    {
        const part_t* g = &pt;
        const uint8_t* pv = g->p->v;
        int32_t lfo = g->lfo_val, p8, cut, fenv, a;
        uint32_t glide_k = g->glide_k;

        if (v->pitch16 != v->target16) {
            if (!glide_k || g->porta_mode == PORTA_OFF) {
                v->pitch16 = v->target16;
            } else {
                int32_t diff = v->target16 - v->pitch16;
                v->pitch16 += (int32_t)(((int64_t)diff * glide_k) >> 16);
                if (diff > -64 && diff < 64) v->pitch16 = v->target16;
            }
        }

        int32_t v_env = VINT_ENV_WARM[card];
        int32_t dec_scaled = ((int32_t)pv[P_DECAY] * 115) / 106;
        if (dec_scaled > 127) dec_scaled = 127;
        int32_t rel_scaled = ((int32_t)pv[P_RELEASE] * 5) / 4;
        if (rel_scaled > 127) rel_scaled = 127;
        env_tick(&v->fe, pv[P_ATTACK], dec_scaled, g->sus_f, rel_scaled, v->gate, v_env);
        env_tick(&v->ae, pv[P_ATTACK], dec_scaled, g->sus_a, rel_scaled, v->gate, v_env);
        fenv = v->fe.level >> 9;

        v->drift += (((int32_t)(xorshift(&v->noise) >> 16) - 32768) - v->drift) >> 8;

        int32_t v_tune = VINT_TUNE8_WARM[card];
        p8 = (v->pitch16 >> 8) + g->range_semi8 + g->fine_tune8 + g->transp_semi8 + g->bend8 + ctl_tune8
            + v->det8 + v_tune + (v->drift >> 11);
        int32_t lfo_pd = g->lfo_pd;
        if (pv[P_LFO_WAVE] == LFO_NOISE) {
            lfo_pd = 0;
        }
        p8 += (lfo * lfo_pd) >> 15;
        if (ctl_vib) {
            p8 += (lfo * ctl_vib) >> 14;
        }
        v->inc = pitch_inc(p8);

        if (g->pwm_src == PWM_LFO) {
            int32_t pwm_lfo = lfo_shape(g, g->lfo_ph, LFO_TRI);
            int64_t p = (int64_t)0x80000000u + (((int64_t)pwm_lfo * (int32_t)pv[P_PWM_DEPTH]) * 464);
            if (p < (1 << 26)) p = 1 << 26;
            if (p > (int64_t)0xFFFFFFFFu - (1 << 26)) p = (int64_t)0xFFFFFFFFu - (1 << 26);
            v->pw = (uint32_t)p;
        } else if (g->pwm_src == PWM_ENV) {
            int64_t p = (int64_t)0x80000000u - (((int64_t)fenv * (int32_t)pv[P_PWM_DEPTH]) * 464);
            if (p < (1 << 26)) p = 1 << 26;
            if (p > (int64_t)0xFFFFFFFFu - (1 << 26)) p = (int64_t)0xFFFFFFFFu - (1 << 26);
            v->pw = (uint32_t)p;
        } else {
            uint32_t delta = (uint32_t)pv[P_PWM_DEPTH] * 0x00E80000u;
            v->pw = 0x80000000u - delta;
        }
        int32_t v_pw = VINT_PW8_WARM[card];
        v->pw += (uint32_t)v_pw;

        int32_t env_mult = 244;
        int32_t env_sweep = (fenv * g->fenv_amt * env_mult) >> 15;
        cut = g->cut_sm + env_sweep
            + ((int32_t)v->note - 60) * (int32_t)g->keytrk + ((lfo * g->lfo_fd) >> 15)
            + g->flt_bend8;
        cut = clampi(cut, 0, 136 << 8);
        svf_coef(&v->c1, cut, g->reso_k1);
        svf_coef(&v->c2, cut, 8192);

        v->amp0 = v->amp1;
        if (pv[P_AMP_MODE] == AMP_GATE) {
            a = v->gate ? 32767 : 0;
        } else {
            a = v->ae.level >> 9;
        }
        int32_t vel = v->vel ? (int32_t)v->vel : 100;
        int32_t vel_q15 = 4096 + (vel * 225);
        a = (a * vel_q15) >> 15;
        v->amp1 = (a * g->gain_q12) >> 12;
        ui_vlevel[card] = a;
    }

    // (firmware's blk_fade_f0 = 32768, blk_fade_df = 0: no program change fade here)
    void voice_render(voice_t* v, int32_t* out)
    {
        uint32_t i, inc = v->inc, pw = v->pw, d = inc >> 15;
        int32_t amp0 = v->amp0, da = v->amp1 - v->amp0, gl = v->gl, gr = v->gr;
        const part_t* g = &pt;
        int32_t l_sqr = g->lvl_sqr, l_saw = g->lvl_saw, l_sub = g->lvl_sub, l_noi = g->lvl_noi;
        uint32_t sub_t = g->sub_type, draw_sw = g->draw_sw, draw_mult = g->draw_mult_q8;
        uint32_t comb_q8 = g->chop_comb_q8;

        int32_t noise_pd = 0;
        int32_t noise_val = 0;
        if (g->p->v[P_LFO_WAVE] == LFO_NOISE) {
            noise_pd = (g->lfo_pd * 255) >> 8;
            noise_val = g->lfo_noi_sm;
        }

        int chop_active = (chop_mask[0] != 0xFFFFu || chop_mask[1] != 0xFFFFu ||
                           chop_mask[2] != 0xFFFFu || chop_mask[3] != 0xFFFFu);
        int32_t g_masked = (chop_active && g->chop_ovtn > 0) ? g->g_chop_masked : 32767;
        if (g_masked != 32767 && inc > 87652000u) {
            if (inc >= 107130000u) {
                g_masked = 32767;
            } else {
                uint32_t fade = (uint32_t)(((uint64_t)(inc - 87652000u) * 32767u) / (107130000u - 87652000u));
                g_masked += (int32_t)(((int64_t)(32767 - g_masked) * fade) >> 15);
            }
        }

        for (i = 0; i < CTL; i++) {
            int32_t s_sqr, s_saw, s_sub, s_noi, mix, lp, bp, hp, y, amp;

            uint32_t inc_i = inc, d_i = d;
            if (noise_pd) {
                int32_t delta_p8 = (noise_val * noise_pd) >> 15;
                int32_t delta_inc = (int32_t)(((int64_t)inc * delta_p8 * 3783) >> 24);
                int32_t inc_signed = (int32_t)inc + delta_inc;
                inc_i = (uint32_t)inc_signed;
                d_i = inc_i >> 15;
            }

            if (draw_sw != DSW_OFF) {
                v->o_sqr.ph += inc_i;
                s_sqr = osc_draw_sample(draw_table, v->o_sqr.ph, draw_mult, draw_sw);
            } else {
                s_sqr = osc_step(&v->o_sqr, inc_i, d_i, pw, OSC_PLS, -65536, -1);
            }

            s_saw = osc_step(&v->o_saw, inc_i, d_i, 0, OSC_SAW, -65536, -1);

            uint32_t inc_sub = (sub_t == SUB_1OC) ? (inc_i >> 1) : (inc_i >> 2);
            uint32_t d_sub = inc_sub >> 15;
            uint32_t pw_sub = (sub_t == SUB_2OA) ? 0x40000000u : 0x80000000u;
            s_sub = osc_step(&v->o_sub, inc_sub, d_sub, pw_sub, OSC_PLS, -65536, -1);

            if (g->noise_mode == NOISE_PINK) {
                s_noi = pink_noise_step(&v->pn, (int32_t)(xorshift(&v->noise) >> 16) - 32768);
            } else {
                int32_t w = (int32_t)(xorshift(&v->noise) >> 16) - 32768;
                v->noise_lp += (w - v->noise_lp) >> 1;
                s_noi = (v->noise_lp * 180) >> 8;
            }

            if (g_masked != 32767) {
                s_sqr = osc_chop_sample(s_sqr, v->o_sqr.ph, chop_mask[0], comb_q8, g_masked);
                s_saw = osc_chop_sample(s_saw, v->o_saw.ph, chop_mask[1], comb_q8, g_masked);
                s_sub = osc_chop_sample(s_sub, v->o_saw.ph, chop_mask[2], comb_q8, g_masked);
                s_noi = osc_chop_sample(s_noi, v->o_saw.ph, chop_mask[3], comb_q8, g_masked);
                s_sqr = (s_sqr * 21294) >> 15;
                s_saw = (s_saw * 21294) >> 15;
                s_sub = (s_sub * 21294) >> 15;
                s_noi = (s_noi * 21294) >> 15;
            }

            mix = (s_sqr * l_sqr + s_saw * l_saw + s_sub * l_sub + s_noi * l_noi) >> 7;
            mix = clampi(mix, -65535, 65535);

            svf_step(&v->c1, &v->f1, mix, &lp, &bp, &hp);
            y = svf_lp(&v->c2, &v->f2, lp);

            amp = amp0 + ((da * (int32_t)i) >> 4);
            y = (y * amp) >> 16;
            out[2u * i]      += (y * gl) >> 12;
            out[2u * i + 1u] += (y * gr) >> 12;
        }
    }

    void engine_audio(int32_t* out, uint32_t n)
    {
        uint32_t b, v, i;
        vol_q12 = (int32_t)((ctl_cc7 * ctl_cc7) >> 2);

        for (b = 0; b < n; b += CTL) {
            engine_events();
            lfo_tick(&pt);
            block_globals(&pt);

            for (v = 0; v < NV; v++) {
                if (vc[v].ae.stage == E_IDLE && !vc[v].gate) {
                    if (ui_vlevel[v]) { ui_vlevel[v] = 0; vc[v].amp0 = vc[v].amp1 = 0; }
                    uint32_t step = vc[v].inc * CTL;
                    vc[v].o_sqr.ph += step;
                    vc[v].o_saw.ph = vc[v].o_sqr.ph;
                    uint32_t step_sub = (pt.sub_type == SUB_1OC) ? (step >> 1) : (step >> 2);
                    vc[v].o_sub.ph += step_sub;
                    continue;
                }
                voice_block(&vc[v], v);
                voice_render(&vc[v], out + 2u * b);
            }
        }

        // (firmware) the output stage with soft saturation, then fx_process
        // with every effect off: its softclip
        for (i = 0; i < n; i++) {
            int32_t l = softclip((clampi(out[2u * i], -262144, 262143) >> 2) * vol_q12 >> 10);
            int32_t r = softclip((clampi(out[2u * i + 1u], -262144, 262143) >> 2) * vol_q12 >> 10);
            out[2u * i] = softclip(l);
            out[2u * i + 1u] = softclip(r);
        }
    }

    bool engine_quiet() const
    {
        for (uint32_t v = 0; v < NV; v++)
            if (vc[v].ae.stage != E_IDLE || vc[v].gate) return false;
        return nq_r == nq_w;
    }

    void engine_init()
    {
        for (uint32_t v = 0; v < NV; v++) {
            vc[v].noise = 0x12345678u * (v + 1u) + 0x9E3779B9u;
            uint32_t ph = xorshift(&vc[v].noise) + v * 0x40000000u;
            vc[v].o_sqr.ph = ph;
            vc[v].o_saw.ph = ph;
            vc[v].o_sub.ph = ph >> 1;
            vc[v].o_sub.prev = 0;
            vc[v].inc = pitch_inc((60 + (int32_t)v * 3) << 8);
            vc[v].pn.b0 = vc[v].pn.b1 = vc[v].pn.b2 = 0;
            vc[v].gl = vc[v].gr = 2896;
        }
        pt.lfo_rnd_a = xorshift(&rnd_state);
        pt.lfo_rnd_b = xorshift(&rnd_state);
    }
};

Synth::Synth() : s(new State()) {}
Synth::~Synth() = default;

void Synth::setByte(uint32_t p, uint8_t value)
{
    if (p < NPARAM) s->live.v[p] = value > PDEF[p].max ? PDEF[p].max : value;
}
uint8_t Synth::byte(uint32_t p) const { return p < PATCH_BYTES ? s->live.v[p] : 0; }
void Synth::setDrawStep(uint32_t step, int16_t value)
{
    if (step < 16) s->draw_table[step] = value;
}
void Synth::setChopMask(uint32_t source, uint16_t mask)
{
    if (source < 4) s->chop_mask[source] = mask;
}

void Synth::noteOn(uint8_t note, uint8_t velocity) { s->note_event(1, note & 127u, velocity & 127u); }
void Synth::noteOff(uint8_t note) { s->note_event(0, note & 127u, 0); }
void Synth::setBend(int32_t bend) { s->ctl_bend = clampi(bend, -8192, 8191); }
void Synth::setModWheel(uint8_t value) { s->ctl_vib = value & 127; }
void Synth::setSustain(bool on) { s->ctl_sustain = on ? 1u : 0u; }
void Synth::setTempo(double bpm)
{
    if (bpm > 0.0) s->ctl_tempo = bpm < 20.0 ? 20.0 : bpm > 999.0 ? 999.0 : bpm;
}
void Synth::panic() { s->ctl_panic = 1; }

void Synth::silence()
{
    // a new engine with this one's parameters
    patch_t live = s->live;
    int16_t draw[16];
    uint16_t chop[4];
    std::memcpy(draw, s->draw_table, sizeof draw);
    std::memcpy(chop, s->chop_mask, sizeof chop);
    const int32_t bend = s->ctl_bend, vib = s->ctl_vib;
    const double tempo = s->ctl_tempo;
    s.reset(new State());
    s->live = live;
    std::memcpy(s->draw_table, draw, sizeof draw);
    std::memcpy(s->chop_mask, chop, sizeof chop);
    s->ctl_bend = bend;
    s->ctl_vib = vib;
    s->ctl_tempo = tempo;
}

void Synth::process(int32_t* out, uint32_t frames)
{
    frames -= frames % CTL;
    std::memset(out, 0, sizeof(int32_t) * 2u * frames);
    s->engine_audio(out, frames);
}

bool Synth::quiet() const { return s->engine_quiet(); }

// ===========================================================================
// the effects (obv_fx.h)

namespace {

constexpr uint32_t REV_DIV = 1u;
constexpr uint32_t RVL(uint32_t x) { return (uint32_t)(x * 44100u / 29761u / REV_DIV); }
constexpr uint32_t DLY_MAX = 32768u;
constexpr uint32_t CHO_N = 1024u;
constexpr uint32_t REV_PRE_N = 8192u / REV_DIV;
constexpr uint32_t RV_IN1 = RVL(142), RV_IN2 = RVL(107), RV_IN3 = RVL(379), RV_IN4 = RVL(277);
constexpr uint32_t RV_AP1 = RVL(672), RV_D1 = RVL(4453), RV_AP2 = RVL(1800), RV_D2 = RVL(3720);
constexpr uint32_t RV_AP3 = RVL(908), RV_D3 = RVL(4217), RV_AP4 = RVL(2656), RV_D4 = RVL(3163);
constexpr uint32_t REV_N = REV_PRE_N + RV_IN1 + RV_IN2 + RV_IN3 + RV_IN4 + RV_AP1 + 16u + RV_D1 + RV_AP2 + RV_D2 +
                           RV_AP3 + 16u + RV_D3 + RV_AP4 + RV_D4;
constexpr uint32_t FX_MEM_N = CHO_N + 2u * DLY_MAX + REV_N;

struct line_t { int16_t* b; uint32_t len, w; };

inline int32_t sat16(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }
inline int32_t line_read(const line_t* l, uint32_t back)
{
    if (back > l->len) back = l->len;
    if (back < 1u) back = 1u;
    uint32_t i = l->w + l->len - back;
    if (i >= l->len) i -= l->len;
    return l->b[i];
}
inline void line_write(line_t* l, int32_t v)
{
    l->b[l->w] = (int16_t)sat16(v);
    if (++l->w >= l->len) l->w = 0;
}
inline int32_t ap_step(line_t* l, int32_t in, int32_t g)
{
    int32_t d = line_read(l, l->len), v = in - ((d * g) >> 15);
    line_write(l, v);
    return d + ((v * g) >> 15);
}
inline int32_t ap_mod(line_t* l, int32_t in, int32_t g, uint32_t base, int32_t e)
{
    uint32_t back = (uint32_t)((int32_t)base + (e >> 8));
    int32_t f = e & 255, a = line_read(l, back), b = line_read(l, back + 1u), d = a + (((b - a) * f) >> 8), v;
    v = in - ((d * g) >> 15);
    line_write(l, v);
    return d + ((v * g) >> 15);
}
inline int32_t tri_q(uint32_t ph)
{
    int32_t t = (int32_t)(ph >> 19);
    return t < 4096 ? t - 2048 : 6143 - t;
}
inline int32_t line_read_q16(const line_t* l, int32_t back_q16)
{
    if (back_q16 < 0) back_q16 = 0;
    uint32_t b = (uint32_t)back_q16 >> 16;
    int32_t f = (back_q16 >> 8) & 255, a = line_read(l, b), c = line_read(l, b + 1u);
    return a + (((c - a) * f) >> 8);
}
inline int32_t line_read_q16_h(const line_t* l, int32_t back_q16)
{
    if (back_q16 < (1 << 16)) back_q16 = 1 << 16;
    uint32_t b = (uint32_t)back_q16 >> 16;
    if (b + 2u >= l->len) b = l->len - 3u;
    int32_t f = (back_q16 >> 8) & 255, p0 = line_read(l, b - 1u), p1 = line_read(l, b), p2 = line_read(l, b + 1u),
            p3 = line_read(l, b + 2u);
    int32_t c1 = (p2 - p0) >> 1, c2 = p0 - ((5 * p1) >> 1) + 2 * p2 - (p3 >> 1),
            c3 = ((p3 - p0) >> 1) + ((3 * (p1 - p2)) >> 1);
    return p1 + ((f * (c1 + ((f * (c2 + ((f * c3) >> 8))) >> 8))) >> 8);
}

} // namespace

struct Fx::State {
    patch_t p;
    double tempo = 120.0;

    int16_t fx_mem[FX_MEM_N];
    uint32_t fx_used = 0;

    line_t cho, dl, dr, rv_pre, rv_in[4], rv_ap1, rv_d1, rv_ap2, rv_d2, rv_ap3, rv_d3, rv_ap4, rv_d4;
    uint32_t cho_ph = 0, cho_inc = 0, rv_ph1 = 0, rv_ph2 = 0, rv_ph_inc1 = 97391u * REV_DIV,
             rv_ph_inc2 = 68174u * REV_DIV;
    int32_t cho_depth_k = 0, cho_mix = 0, dly_mix = 0, rev_mix = 0, dly_fb = 0, dly_tone_k = 0, rev_decay = 0,
            rev_damp_k = 0, rev_bw = 16700, rev_mod_scale = 256;
    int32_t dly_time = 0, dly_cur = 0;
    int32_t dly_lpl = 0, dly_lpr = 0, rv_hp_in = 0, rv_lp_in = 0, rv_lpl = 0, rv_lpr = 0, rv_yl = 0, rv_yr = 0;
    int32_t rv_hold = 0;
    uint32_t rev_pre_n = 1, dly_active = 0, rev_active = 0;
    uint32_t rr_acc = 0, rr_inc = 0;
    int32_t rr_hold_l = 0, rr_hold_r = 0, rr_shift = 0;
    int32_t fx_drive = 0, fx_makeup = 0, fx_wet = 0;
    uint32_t fx_is_dist = 0, fx_amt = 0;

    State()
    {
        patch_defaults(&p);
        std::memset(fx_mem, 0, sizeof fx_mem);
        fx_init();
    }

    void line_init(line_t* l, uint32_t len) { l->b = fx_mem + fx_used; l->len = len; l->w = 0; fx_used += len; }

    void fx_init()
    {
        line_init(&cho, CHO_N);
        line_init(&dl, DLY_MAX);
        line_init(&dr, DLY_MAX);
        line_init(&rv_pre, REV_PRE_N);
        line_init(&rv_in[0], RV_IN1); line_init(&rv_in[1], RV_IN2); line_init(&rv_in[2], RV_IN3); line_init(&rv_in[3], RV_IN4);
        line_init(&rv_ap1, RV_AP1 + 16u); line_init(&rv_d1, RV_D1); line_init(&rv_ap2, RV_AP2); line_init(&rv_d2, RV_D2);
        line_init(&rv_ap3, RV_AP3 + 16u); line_init(&rv_d3, RV_D3); line_init(&rv_ap4, RV_AP4); line_init(&rv_d4, RV_D4);
        dly_cur = dly_time = 11025 << 16;
    }

    // (firmware) the patch's numbers, once per ISR call; quarter: frames per
    // quarter note. n: the frames of this call (the firmware's HALF_FRAMES)
    void fx_block(const patch_t* pp, uint32_t quarter, uint32_t n)
    {
        int32_t d;
        uint32_t samples;
        uint32_t cho_m = pp->v[P_CHO_MODE];

        switch (cho_m) {
        case CHO_TYPE1: cho_mix = 70; cho_inc = 50u * 974u; cho_depth_k = 35 * 67; break;
        case CHO_TYPE2: cho_mix = 85; cho_inc = 120u * 974u; cho_depth_k = 50 * 67; break;
        case CHO_TYPE3: cho_mix = 95; cho_inc = 400u * 974u; cho_depth_k = 25 * 67; break;
        case CHO_TYPE4: cho_mix = 80; cho_inc = 30u * 974u; cho_depth_k = 65 * 67; break;
        default: cho_mix = 0; cho_inc = 0; cho_depth_k = 0; break;
        }

        fx_is_dist = pp->v[P_FX_MODE] != 0;
        {
            int32_t amt = pp->v[P_FX_AMT];
            rr_inc = 65536u / (1u + (uint32_t)(amt * 31) / 100u);
            rr_shift = (uint32_t)amt / 25u;
            fx_drive = 256 + amt * 16;
            fx_makeup = (int32_t)(65536u / (uint32_t)fx_drive);
            fx_wet = (pp->v[P_FX_MIX] * 256) / 100;
            fx_amt = (amt != 0);
        }

        dly_mix = (pp->v[P_DLY_LVL] * 200) >> 7;
        rev_mix = (pp->v[P_REV_LVL] * 160) >> 7;
        dly_fb = pp->v[P_DLY_FB] * 250;
        dly_tone_k = 180;

        {
            uint32_t rev_m = pp->v[P_REV_TYPE];
            int32_t bw, damp, dec_base, dec_range, mod_scale;
            uint32_t inc1, inc2;

            switch (rev_m) {
            case REV_AMB:
                bw = 16000; damp = 65; dec_base = 8000; dec_range = 11000; mod_scale = 140;
                inc1 = 70000u * REV_DIV; inc2 = 50000u * REV_DIV;
                break;
            case REV_ROOM:
                bw = 19000; damp = 45; dec_base = 10000; dec_range = 12500; mod_scale = 180;
                inc1 = 90000u * REV_DIV; inc2 = 64000u * REV_DIV;
                break;
            case REV_HALL1:
            default:
                bw = 22000; damp = 32; dec_base = 14000; dec_range = 14500; mod_scale = 220;
                inc1 = 97391u * REV_DIV; inc2 = 68174u * REV_DIV;
                break;
            case REV_HALL2:
                bw = 20000; damp = 42; dec_base = 14000; dec_range = 15000; mod_scale = 240;
                inc1 = 97391u * REV_DIV; inc2 = 68174u * REV_DIV;
                break;
            case REV_PLATE:
                bw = 28000; damp = 16; dec_base = 12000; dec_range = 16800; mod_scale = 180;
                inc1 = 120000u * REV_DIV; inc2 = 85000u * REV_DIV;
                break;
            case REV_SPRING:
                bw = 15000; damp = 55; dec_base = 12000; dec_range = 14000; mod_scale = 260;
                inc1 = 280000u * REV_DIV; inc2 = 205000u * REV_DIV;
                break;
            case REV_MOD:
                bw = 24000; damp = 24; dec_base = 16000; dec_range = 14000; mod_scale = 360;
                inc1 = 55000u * REV_DIV; inc2 = 38000u * REV_DIV;
                break;
            }

            rev_bw = bw;
            rev_mod_scale = mod_scale;
            rv_ph_inc1 = inc1;
            rv_ph_inc2 = inc2;

            d = pp->v[P_REV_TIME];
            int32_t t_norm = (d * 115 + ((d * d * 25) >> 7));
            rev_decay = dec_base + (int32_t)(((int64_t)dec_range * t_norm) >> 15);
            if (rev_decay > 31000) rev_decay = 31000;
            rev_damp_k = 32768 - damp * damp * 2;
            {
                uint32_t x = (uint32_t)(32768 - rev_damp_k) << 15, r = 0, bit = 1u << 15;
                for (; bit; bit >>= 1)
                    if ((r + bit) * (r + bit) <= x) r += bit;
                rev_damp_k = 32768 - (int32_t)r;
            }
        }
        rev_pre_n = 1u + (uint32_t)pp->v[P_REV_PRE] * (41u / REV_DIV);
        if (rev_pre_n >= REV_PRE_N) rev_pre_n = REV_PRE_N - 1u;
        if (pp->v[P_DLY_SYNC]) {
            uint32_t dt = pp->v[P_DLY_TIME];
            uint32_t div = (dt * 16u) / 128u;
            if (div >= NDDIV) div = NDDIV - 1u;
            samples = (quarter * DDIV_NUM[div]) / DDIV_DEN[div];
        } else {
            static const uint8_t ANCHOR_RAW[8] = {0, 11, 26, 55, 106, 179, 245, 255};
            static const uint16_t ANCHOR_MS[8] = {1, 14, 44, 103, 273, 517, 737, 740};
            uint32_t raw = (uint32_t)pp->v[P_DLY_TIME] * 255u / 127u;
            uint32_t ms = 740;
            for (uint32_t k = 0; k < 7u; k++) {
                if (raw <= ANCHOR_RAW[k + 1]) {
                    uint32_t num = raw - ANCHOR_RAW[k];
                    uint32_t den = ANCHOR_RAW[k + 1] - ANCHOR_RAW[k];
                    ms = ANCHOR_MS[k] + (num * (ANCHOR_MS[k + 1] - ANCHOR_MS[k])) / den;
                    break;
                }
            }
            samples = (ms * 44100u) / 1000u;
        }
        if (samples > DLY_MAX - 4u) samples = DLY_MAX - 4u;
        if (samples < 64u) samples = 64u;
        dly_time = (int32_t)(samples << 16);
        // (firmware: HALF_FRAMES, the frames of every ISR call)
        if (dly_mix) dly_active = 12u * FS; else if (dly_active > n) dly_active -= n; else dly_active = 0;
        if (rev_mix) rev_active = 24u * FS; else if (rev_active > n) rev_active -= n; else rev_active = 0;
    }

    void rev_tick(int32_t in)
    {
        int32_t x, a, b;
        int32_t e1 = (tri_q(rv_ph1) * rev_mod_scale) >> 8;
        int32_t e2 = (tri_q(rv_ph2) * rev_mod_scale) >> 8;
        int32_t dec = rev_decay, dk = rev_damp_k;
        rv_ph1 += rv_ph_inc1;
        rv_ph2 += rv_ph_inc2;
        line_write(&rv_pre, in);
        x = line_read(&rv_pre, rev_pre_n);
        rv_hp_in += ((x - rv_hp_in) * 1870) >> 15;
        x -= rv_hp_in;
        rv_lp_in += ((x - rv_lp_in) * rev_bw) >> 15;
        x = ap_step(&rv_in[0], rv_lp_in, 24576);
        x = ap_step(&rv_in[1], x, 24576);
        x = ap_step(&rv_in[2], x, 20480);
        x = ap_step(&rv_in[3], x, 20480);
        a = x + ((line_read(&rv_d4, RV_D4) * dec) >> 15);
        a = ap_mod(&rv_ap1, a, -22938, RV_AP1, e1);
        line_write(&rv_d1, a);
        b = line_read(&rv_d1, RV_D1);
        rv_lpl += ((b - rv_lpl) * dk) >> 15;
        b = ap_step(&rv_ap2, (rv_lpl * dec) >> 15, 16384);
        line_write(&rv_d2, b);
        a = x + ((line_read(&rv_d2, RV_D2) * dec) >> 15);
        a = ap_mod(&rv_ap3, a, -22938, RV_AP3, e2);
        line_write(&rv_d3, a);
        b = line_read(&rv_d3, RV_D3);
        rv_lpr += ((b - rv_lpr) * dk) >> 15;
        b = ap_step(&rv_ap4, (rv_lpr * dec) >> 15, 16384);
        line_write(&rv_d4, b);
        a = line_read(&rv_d3, RVL(266)) + line_read(&rv_d3, RVL(2974)) - line_read(&rv_ap4, RVL(1913)) + line_read(&rv_d4, RVL(1996))
          - line_read(&rv_d1, RVL(1990)) - line_read(&rv_ap2, RVL(187)) - line_read(&rv_d2, RVL(1066));
        b = line_read(&rv_d1, RVL(353)) + line_read(&rv_d1, RVL(3627)) - line_read(&rv_ap2, RVL(1228)) + line_read(&rv_d2, RVL(2673))
          - line_read(&rv_d3, RVL(2111)) - line_read(&rv_ap4, RVL(335)) - line_read(&rv_d4, RVL(121));
        rv_yl = (a * 21299) >> 15;
        rv_yr = (b * 21299) >> 15;
    }

    // (firmware) fx_process, the reverb at the full rate (no ECO)
    void fx_process(int32_t* out, uint32_t n)
    {
        uint32_t i, do_dly = dly_active != 0, do_rev = rev_active != 0;
        int32_t cm = cho_mix, dm = dly_mix, rm = rev_mix;
        for (i = 0; i < n; i++) {
            int32_t l = out[2u * i], r = out[2u * i + 1u];
            if (cm) {
                int32_t bl = (529 << 16) + tri_q(cho_ph) * cho_depth_k, br = (529 << 16) + tri_q(cho_ph + 0x40000000u) * cho_depth_k;
                cho_ph += cho_inc;
                line_write(&cho, (l + r) >> 1);
                l += ((line_read_q16_h(&cho, bl) - l) * cm) >> 8;
                r += ((line_read_q16_h(&cho, br) - r) * cm) >> 8;
            }
            if (do_dly) {
                int32_t w;
                if (dly_cur < dly_time) {
                    int32_t step = (dly_time - dly_cur) >> 5;
                    if (step < (1 << 16)) step = 1 << 16;
                    dly_cur += step;
                    if (dly_cur > dly_time) dly_cur = dly_time;
                } else if (dly_cur > dly_time) {
                    int32_t step = (dly_cur - dly_time) >> 5;
                    if (step < (1 << 16)) step = 1 << 16;
                    dly_cur -= step;
                    if (dly_cur < dly_time) dly_cur = dly_time;
                }
                w = line_read_q16(&dl, dly_cur);
                dly_lpl += ((w - dly_lpl) * dly_tone_k) >> 8;
                int32_t din = (l + r) >> 1;
                line_write(&dl, din + ((dly_lpl * dly_fb) >> 15));
                int32_t wet = (w * dm) >> 8;
                l += wet;
                r += wet;
            }
            if (do_rev) {
                rev_tick((l + r) >> 1);
                l += (rv_yl * rm) >> 8;
                r += (rv_yr * rm) >> 8;
            }
            if (fx_amt) {
                int32_t l1 = l, r1 = r;
                if (fx_is_dist) {
                    l1 = (softclip((l * fx_drive) >> 8) * fx_makeup) >> 8;
                    r1 = (softclip((r * fx_drive) >> 8) * fx_makeup) >> 8;
                } else if (rr_inc < 65536u) {
                    rr_acc += rr_inc;
                    if (rr_acc >= 65536u) { rr_acc -= 65536u; rr_hold_l = l; rr_hold_r = r; }
                    l1 = rr_hold_l; r1 = rr_hold_r;
                    if (rr_shift) { l1 = (l1 >> rr_shift) << rr_shift; r1 = (r1 >> rr_shift) << rr_shift; }
                }
                l = l + (((l1 - l) * fx_wet) >> 8);
                r = r + (((r1 - r) * fx_wet) >> 8);
            }
            out[2u * i] = softclip(l);
            out[2u * i + 1u] = softclip(r);
        }
    }
};

Fx::Fx() : s(new State()) {}
Fx::~Fx() = default;

void Fx::setByte(uint32_t p, uint8_t value)
{
    if (p < NPARAM) s->p.v[p] = value > PDEF[p].max ? PDEF[p].max : value;
}
uint8_t Fx::byte(uint32_t p) const { return p < PATCH_BYTES ? s->p.v[p] : 0; }
void Fx::setTempo(double bpm)
{
    if (bpm > 0.0) s->tempo = bpm;
}

void Fx::silence()
{
    const patch_t p = s->p;
    const double tempo = s->tempo;
    s.reset(new State());
    s->p = p;
    s->tempo = tempo;
}

void Fx::process(int32_t* io, uint32_t frames)
{
    // (firmware) quarter = 60 * FS / tempo, the tempo 40..240
    const double bpm = s->tempo < 40.0 ? 40.0 : s->tempo > 240.0 ? 240.0 : s->tempo;
    const uint32_t quarter = (uint32_t)(60.0 * double(FS) / bpm);
    s->fx_block(&s->p, quarter, frames);
    s->fx_process(io, frames);
}

} // namespace bl101
