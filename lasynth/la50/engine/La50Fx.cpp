// La50Fx.cpp - see La50Fx.hpp.
// SPDX-License-Identifier: GPL-3.0-only
#include "La50Fx.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "La50Engine.hpp"

namespace la50 {

namespace {

constexpr float kFs = float(kEngineRate);
constexpr float kPi = 3.14159265358979f;

const float kLowEqHz[16] = { 63, 75, 88, 105, 125, 150, 175, 210, 250, 300, 350, 420, 500, 600, 700, 840 };
const float kHighEqHz[22] = { 250, 300, 350, 420, 500, 600, 700, 840, 1000, 1200, 1400, 1700, 2000, 2400, 2800,
                              3400, 4000, 4800, 5700, 6700, 8000, 9500 };
const float kHighEqQ[9] = { 0.3f, 0.5f, 0.7f, 1.0f, 1.4f, 2.0f, 3.0f, 4.2f, 6.0f };

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    int key = -1; // the settings these coefficients are for
    float run(float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    // RBJ cookbook
    void lowShelf(float f, float db)
    {
        const float A = std::pow(10.f, db / 40.f), w = 2 * kPi * f / kFs, cw = std::cos(w), sw = std::sin(w);
        const float alpha = sw / 2 * std::sqrt(2.f), sa = 2 * std::sqrt(A) * alpha;
        const float a0 = (A + 1) + (A - 1) * cw + sa;
        b0 = A * ((A + 1) - (A - 1) * cw + sa) / a0;
        b1 = 2 * A * ((A - 1) - (A + 1) * cw) / a0;
        b2 = A * ((A + 1) - (A - 1) * cw - sa) / a0;
        a1 = -2 * ((A - 1) + (A + 1) * cw) / a0;
        a2 = ((A + 1) + (A - 1) * cw - sa) / a0;
    }
    void peak(float f, float q, float db)
    {
        const float A = std::pow(10.f, db / 40.f), w = 2 * kPi * f / kFs, alpha = std::sin(w) / (2 * q);
        const float a0 = 1 + alpha / A;
        b0 = (1 + alpha * A) / a0;
        b1 = -2 * std::cos(w) / a0;
        b2 = (1 - alpha * A) / a0;
        a1 = b1;
        a2 = (1 - alpha / A) / a0;
    }
};

struct Delay {
    std::vector<float> buf;
    size_t w = 0;
    explicit Delay(size_t n = 1) : buf(n, 0.f) {}
    void push(float x)
    {
        buf[w] = x;
        if (++w >= buf.size()) w = 0;
    }
    float tap(float samples) const // fractional, 1 .. size-2
    {
        samples = std::clamp(samples, 1.f, float(buf.size() - 2));
        const float pos = float(w) - samples + float(buf.size());
        const size_t i = size_t(pos);
        const float f = pos - float(i);
        const float a = buf[i % buf.size()], b = buf[(i + 1) % buf.size()];
        return a + (b - a) * f;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
};

// The chorus modulation as the D-50 sets it (read from its v2.22 program ROM): each type
// has a centre value per modulator, and Rate / Depth add to it linearly, (value - 50),
// clamped - so a low Depth leaves most types nearly still, and Rate is linear, not
// exponential. Modulators: a tremolo (types 6 and 7 only) and two delay sweeps. The ROM
// gives the values in the chorus chip's units; ours assume 0.05 Hz and 0.06 ms per unit.
struct ChorusMod { float tremHz, trem, hz1, hz2, ms1, ms2; }; // at Rate / Depth 50; < 0: unused
const ChorusMod kChorusMod[8] = {
    { -1, -1, 0.70f, 0.25f, 0.06f, 0.12f },   // Chorus 1
    { -1, -1, 1.35f, 0.90f, 0.66f, 0.18f },   // Chorus 2
    { -1, -1, 0.30f, 0.20f, 0.30f, 1.26f },   // Flanger 1
    { -1, -1, 0.60f, 0.30f, 0.60f, 1.02f },   // Flanger 2
    { -1, -1, 1.25f, 0.70f, 0.18f, 0.12f },   // Feedback Chorus
    { 1.45f, 0.80f, -1, -1, -1, -1 },         // Tremolo
    { 1.50f, 0.80f, 0.70f, 0.25f, 0.24f, 0.36f }, // Chorus + Tremolo
    { -1, -1, 0.20f, 0.15f, 0.24f, 0.30f },   // Dimension
};

} // namespace

// lfo 0: tremolo, 1 / 2: delay sweeps; < 0 when the type doesn't use it
float chorusRateHz(int type, int lfo, int rate)
{
    const ChorusMod& m = kChorusMod[std::clamp(type, 0, 7)];
    const float c = lfo == 0 ? m.tremHz : lfo == 1 ? m.hz1 : m.hz2;
    if (c < 0) return -1;
    return std::clamp(c + 0.05f * float(rate - 50), 0.05f, 10.f);
}
// tremolo: 0..1; sweeps: ms
float chorusDepth(int type, int lfo, int depth)
{
    const ChorusMod& m = kChorusMod[std::clamp(type, 0, 7)];
    if (lfo == 0) return m.trem < 0 ? -1 : std::clamp(m.trem + float(depth - 50) / 127.f, 0.f, 1.f);
    const float c = lfo == 1 ? m.ms1 : m.ms2;
    if (c < 0) return -1;
    return std::clamp(c + 0.06f * float(depth - 50), 0.f, 7.62f);
}

namespace {

// one tone's chorus: a delay swept by two LFOs (L / R in quadrature), a tremolo
struct Chorus {
    Delay line { size_t(kFs * 0.05f) };
    double phase[3] = { 0, 0, 0 };
    float fb = 0;
    void process(int type, int rate, int depth, int balance, float x, float& l, float& r)
    {
        static const float kBase[8] = { 7, 12, 1.5f, 3, 8, 0, 7, 5 }; // base delay ms
        static const float kFb[8] = { 0, 0, 0.6f, 0.75f, 0.5f, 0, 0, 0 };
        float sn[3], cs[3];
        for (int k = 0; k < 3; ++k) {
            const float hz = chorusRateHz(type, k, rate);
            if (hz > 0) {
                phase[k] += hz / kFs;
                if (phase[k] >= 1) phase[k] -= 1;
            }
            sn[k] = std::sin(2 * kPi * float(phase[k]));
            cs[k] = std::cos(2 * kPi * float(phase[k]));
        }
        const float wet = balance / 100.f, dry = 1.f - 0.5f * wet;
        const float trem = chorusDepth(type, 0, depth);
        if (type == 5) { // tremolo, opposite on the sides
            l = x * (1 - 0.5f * trem * wet * (1 + sn[0]));
            r = x * (1 - 0.5f * trem * wet * (1 - sn[0]));
            return;
        }
        const float d1 = std::max(0.f, chorusDepth(type, 1, depth)), d2 = std::max(0.f, chorusDepth(type, 2, depth));
        line.push(x + fb * kFb[type]);
        const float ms = kFs / 1000.f;
        const float base = kBase[type] + 0.5f * (d1 + d2);
        const float a = line.tap((base + 0.5f * (d1 * sn[1] + d2 * sn[2])) * ms);
        const float b = line.tap((base + (type == 7 ? 2.f : 0.f) + 0.5f * (d1 * cs[1] + d2 * cs[2])) * ms);
        fb = 0.5f * (a + b);
        l = dry * x + wet * a;
        r = dry * x + wet * b;
        if (trem > 0) { // + tremolo
            l *= 1 - 0.3f * trem * (1 + sn[0]);
            r *= 1 - 0.3f * trem * (1 - sn[0]);
        }
    }
};

// the patch reverb: a stereo comb / allpass reverb, delays, gates
struct RevType {
    int kind;     // 0 reverb, 1 delay, 2 gate, 3 reverse gate
    float size;   // comb feedback (decay)
    float damp;   // high damping
    float preMs;  // pre-delay
    float gateMs; // gate / reverse length
    float dlMs, drMs, dfb; // delays: left / right time, feedback (cross when drMs < 0)
};
const RevType kRev[32] = {
    { 0, 0.80f, 0.35f, 10, 0, 0, 0, 0 },   { 0, 0.84f, 0.35f, 15, 0, 0, 0, 0 },     // Small / Medium Hall
    { 0, 0.88f, 0.30f, 25, 0, 0, 0, 0 },   { 0, 0.86f, 0.15f, 30, 0, 0, 0, 0 },     // Large Hall, Chapel
    { 0, 0.55f, 0.60f, 2, 0, 0, 0, 0 },    { 0, 0.72f, 0.05f, 4, 0, 0, 0, 0 },      // Box, Small Metal Room
    { 0, 0.66f, 0.50f, 4, 0, 0, 0, 0 },    { 0, 0.72f, 0.45f, 6, 0, 0, 0, 0 },      // Small / Medium Room
    { 0, 0.76f, 0.45f, 8, 0, 0, 0, 0 },    { 0, 0.80f, 0.40f, 12, 0, 0, 0, 0 },     // Medium Large / Large Room
    { 1, 0, 0.3f, 0, 0, 102, 102, 0.30f }, { 1, 0, 0.3f, 0, 0, 180, -180, 0.45f },  // Single 102, Cross 180
    { 1, 0, 0.3f, 0, 0, 224, -224, 0.45f }, { 1, 0, 0.3f, 0, 0, 148, -296, 0.45f }, // Cross 224, 148-296
    { 2, 0.86f, 0.30f, 5, 200, 0, 0, 0 },  { 2, 0.88f, 0.30f, 5, 480, 0, 0, 0 },    // Short / Long Gate
    { 0, 0.86f, 0.10f, 20, 0, 0, 0, 0 },   { 0, 0.93f, 0.40f, 40, 0, 0, 0, 0 },     // Bright Hall, Large Cave
    { 0, 0.78f, 0.02f, 2, 0, 0, 0, 0 },    { 1, 0, 0.3f, 0, 0, 248, 248, 0.40f },   // Steel Pan, Delay 248
    { 1, 0, 0.3f, 0, 0, 338, 338, 0.40f }, { 1, 0, 0.3f, 0, 0, 157, -157, 0.50f },  // Delay 338, Cross 157
    { 1, 0, 0.3f, 0, 0, 252, -252, 0.50f }, { 1, 0, 0.3f, 0, 0, 274, -137, 0.50f }, // Cross 252, 274-137
    { 2, 0.84f, 0.35f, 5, 300, 0, 0, 0 },  { 3, 0.86f, 0.30f, 0, 360, 0, 0, 0 },    // Gate Reverb, Reverse 360
    { 3, 0.88f, 0.30f, 0, 480, 0, 0, 0 },  { 1, 0, 0.4f, 0, 0, 60, 60, 0.0f },      // Reverse 480, Short Slap
    { 1, 0, 0.4f, 0, 0, 95, 95, 0.0f },    { 1, 0, 0.4f, 0, 0, 140, 140, 0.0f },    // Medium / Long Slap Back
    { 0, 0.90f, 0.20f, 60, 0, 0, 0, 0 },   { 0, 0.94f, 0.25f, 80, 0, 0, 0, 0 },     // Twisted Space, Space
};

struct Comb {
    Delay d;
    float store = 0;
    explicit Comb(size_t n) : d(n) {}
    float run(float x, float fb, float damp)
    {
        const float y = d.buf[d.w];
        store = y * (1 - damp) + store * damp;
        d.push(x + store * fb);
        return y;
    }
};
struct Allpass {
    Delay d;
    explicit Allpass(size_t n) : d(n) {}
    float run(float x)
    {
        const float b = d.buf[d.w];
        d.push(x + b * 0.5f);
        return b - x;
    }
};

struct Reverb {
    std::vector<Comb> combL, combR;
    std::vector<Allpass> apL, apR;
    Delay pre { size_t(kFs * 0.1f) }, dl { size_t(kFs * 0.4f) }, dr { size_t(kFs * 0.4f) };
    int lastType = -1;
    float gateEnv = 0, inEnv = 0;
    int sinceOnset = 1 << 30;
    Reverb()
    {
        // Freeverb's tunings, scaled from 44.1 to 32 kHz
        const int c[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        const int a[4] = { 556, 441, 341, 225 };
        for (int k : c) {
            combL.emplace_back(size_t(k * kFs / 44100.f));
            combR.emplace_back(size_t((k + 23) * kFs / 44100.f));
        }
        for (int k : a) {
            apL.emplace_back(size_t(k * kFs / 44100.f));
            apR.emplace_back(size_t((k + 23) * kFs / 44100.f));
        }
    }
    void clear()
    {
        for (auto& c : combL) { c.d.clear(); c.store = 0; }
        for (auto& c : combR) { c.d.clear(); c.store = 0; }
        for (auto& a : apL) a.d.clear();
        for (auto& a : apR) a.d.clear();
        pre.clear(); dl.clear(); dr.clear();
        gateEnv = inEnv = 0;
    }
    void process(int type, float inL, float inR, float& outL, float& outR)
    {
        const RevType& t = kRev[std::clamp(type, 0, 31)];
        if (type != lastType) { clear(); lastType = type; }
        const float ms = kFs / 1000.f;
        if (t.kind == 1) { // delays
            const float l = dl.tap(t.dlMs * ms), r = dr.tap(std::fabs(t.drMs) * ms);
            const bool cross = t.drMs < 0;
            dl.push(inL + (cross ? r : l) * t.dfb);
            dr.push(inR + (cross ? l : r) * t.dfb);
            outL = l;
            outR = r;
            return;
        }
        pre.push(0.5f * (inL + inR));
        const float x = pre.tap(std::max(1.f, t.preMs * ms)) * 0.15f;
        float l = 0, r = 0;
        for (auto& c : combL) l += c.run(x, t.size, t.damp);
        for (auto& c : combR) r += c.run(x, t.size, t.damp);
        for (auto& a : apL) l = a.run(l);
        for (auto& a : apR) r = a.run(r);
        if (t.kind >= 2) { // gates: open with the input, close (or swell then cut) after the gate time
            const float lev = std::fabs(inL) + std::fabs(inR);
            inEnv = std::max(lev, inEnv * 0.9995f);
            if (lev > 0.02f && inEnv <= lev * 1.01f) sinceOnset = 0;
            ++sinceOnset;
            const float gate = t.gateMs * ms;
            float g = sinceOnset < gate ? 1.f : 0.f;
            if (t.kind == 3) g *= std::min(1.f, float(sinceOnset) / gate) * 1.5f; // reverse: swells
            gateEnv += (g - gateEnv) * 0.01f;
            l *= gateEnv;
            r *= gateEnv;
        }
        outL = l;
        outR = r;
    }
};

} // namespace

struct Fx::State {
    Biquad low[2], high[2];
    Chorus chorus[2];
    Reverb reverb;
};

Fx::Fx() : s(new State()) {}
Fx::~Fx() = default;

void Fx::reset()
{
    s.reset(new State());
}

void Fx::process(const PatchData& pd, const float* upper, const float* lower, float* outL, float* outR, int frames)
{
    const uint8_t* patch = pd.data() + kPatch;
    // EQ coefficients when the settings change
    for (int t = 0; t < 2; ++t) {
        const uint8_t* com = pd.data() + (t ? kLowerCommon : kUpperCommon);
        const int lk = com[C_EQ_LOW_FREQ] << 8 | com[C_EQ_LOW_GAIN];
        if (lk != s->low[t].key) {
            s->low[t].lowShelf(kLowEqHz[std::min<int>(com[C_EQ_LOW_FREQ], 15)], float(int(com[C_EQ_LOW_GAIN]) - 12));
            s->low[t].key = lk;
        }
        const int hk = com[C_EQ_HIGH_FREQ] << 16 | com[C_EQ_HIGH_Q] << 8 | com[C_EQ_HIGH_GAIN];
        if (hk != s->high[t].key) {
            s->high[t].peak(kHighEqHz[std::min<int>(com[C_EQ_HIGH_FREQ], 21)], kHighEqQ[std::min<int>(com[C_EQ_HIGH_Q], 8)],
                            float(int(com[C_EQ_HIGH_GAIN]) - 12));
            s->high[t].key = hk;
        }
    }
    const int bal = patch[K_TONE_BALANCE]; // 0 = Lower only .. 100 = Upper only
    const float gU = bal >= 50 ? 1.f : bal / 50.f, gL = bal <= 50 ? 1.f : (100 - bal) / 50.f;
    const float vol = float(patch[K_TOTAL_VOLUME]) / 100.f;
    const float volume = vol * vol;
    const int mode = std::min<int>(patch[K_OUTPUT_MODE], 3);
    const int revType = patch[K_REVERB_TYPE];
    const float revWet = patch[K_REVERB_BALANCE] / 100.f;
    for (int i = 0; i < frames; ++i) {
        float tl[2], tr[2];
        for (int t = 0; t < 2; ++t) {
            const uint8_t* com = pd.data() + (t ? kLowerCommon : kUpperCommon);
            float x = (t ? lower[i] * gL : upper[i] * gU);
            x = s->high[t].run(s->low[t].run(x));
            s->chorus[t].process(std::min<int>(com[C_CHORUS_TYPE], 7), com[C_CHORUS_RATE], com[C_CHORUS_DEPTH],
                                 com[C_CHORUS_BALANCE], x, tl[t], tr[t]);
        }
        float l, r;
        switch (mode) {
        case 1: // both reverberated, Upper left, Lower right
            l = 0.5f * (tl[0] + tr[0]);
            r = 0.5f * (tl[1] + tr[1]);
            break;
        default:
            l = tl[0] + tl[1];
            r = tr[0] + tr[1];
            break;
        }
        float rl = 0, rr = 0;
        if (mode == 2) s->reverb.process(revType, tl[0], tr[0], rl, rr);      // Upper only
        else if (mode == 3) s->reverb.process(revType, tl[1], tr[1], rl, rr); // Lower only
        else s->reverb.process(revType, l, r, rl, rr);
        outL[i] = volume * ((1.f - 0.5f * revWet) * l + revWet * rl);
        outR[i] = volume * ((1.f - 0.5f * revWet) * r + revWet * rr);
    }
}

} // namespace la50
