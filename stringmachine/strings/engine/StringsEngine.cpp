// StringsEngine.cpp - Maquina Strings' sound engine (StringsEngine.hpp).
// SPDX-License-Identifier: GPL-3.0-only
#include "StringsEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio> // plaits' headers under TEST use printf
#include <cstring>
#include <random>

// string-machine (BSL-1.0): Faust-generated blocks (gen/dsp), envelope, filters, ensemble
#include "SolinaChorusStereo.h"
#include "dsp/AHDSREnvelope.h"
#include "dsp/AsymWaveshaper.hpp"
#include "dsp/StringFiltersHighshelf.hpp"

// Mutable Instruments (MIT): Plaits' wavetable engine (libklang.a), stmlib's filter, Rings' reverb
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "plaits/dsp/oscillator/wavetable_oscillator.h"
#include "plaits/resources.h"
#include "rings/dsp/fx/reverb.h"
#include "stmlib/dsp/filter.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace maquina {
namespace strings {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kFs = float(kRate);

inline float clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
inline float semis(float s) { return std::exp2(s * (1.f / 12.f)); }
inline float noteHz(float note) { return 440.f * semis(note - 69.f); }
inline float limitHz(float f) { return std::min(std::max(f, 10.f), 0.45f * kFs); }

// REGISTER's stations: footage levels (16', 8', 4'), the footages' keyed high-pass and low-pass
// (semitones above each footage's pitch, as string-machine's filters), the shaper's "enhance",
// the high shelf (dB, semitones above the 8' pitch)
struct Station {
    float g16, g8, g4, hpSt, lpSt, enhance, shelfDb, shelfSt;
};
constexpr Station kStations[5] = {
    { 1.00f, 0.55f, 0.00f, -2.f, 14.f, 0.15f, 2.f, 24.f },  // Cello: 16' + 8', dark
    { 0.55f, 1.00f, 0.45f, 4.f, 20.f, 0.10f, 4.f, 24.f },   // Ensemble: all three, 8' led
    { 0.25f, 1.00f, 0.35f, 8.f, 22.f, 0.20f, 5.f, 24.8f },  // Viola: 8', a little 16' and 4'
    { 0.00f, 0.70f, 1.00f, 12.f, 26.f, 0.30f, 6.f, 24.8f }, // Violin: 4' led, bright
    { 0.80f, 0.80f, 0.80f, -12.f, 36.f, 0.00f, 1.f, 30.f }, // Organ: 16' 8' 4' equal, open
};
Station stationAt(float reg)
{
    const float p = clamp01(reg) * 4.f;
    const int i = std::min(3, int(p));
    const float t = p - float(i);
    const Station& a = kStations[i];
    const Station& b = kStations[i + 1];
    auto lerp = [t](float u, float v) { return u + (v - u) * t; };
    return { lerp(a.g16, b.g16), lerp(a.g8, b.g8), lerp(a.g4, b.g4), lerp(a.hpSt, b.hpSt), lerp(a.lpSt, b.lpSt),
             lerp(a.enhance, b.enhance), lerp(a.shelfDb, b.shelfDb), lerp(a.shelfSt, b.shelfSt) };
}
constexpr float kFootRatio[3] = { 0.5f, 1.f, 2.f }; // 16', 8', 4'

// a feedback comb, keytracked by its delay; one-pole damping and a soft limit in the loop
struct Comb {
    static constexpr int kSize = 4096; // > kFs / 20 Hz
    float buf[kSize] = {};
    int w = 0;
    float damp = 0;
    float process(float x, float delay, float fb)
    {
        float rp = float(w) - delay;
        if (rp < 0) rp += float(kSize);
        const int i0 = int(rp);
        const float fr = rp - float(i0);
        const float a = buf[i0 & (kSize - 1)], b = buf[(i0 + 1) & (kSize - 1)];
        const float d = a + (b - a) * fr;
        damp += 0.6f * (d - damp);
        float y = x + fb * damp;
        y = y / (1.f + 0.25f * std::fabs(y)); // keeps high feedback bounded
        buf[w] = y;
        w = (w + 1) & (kSize - 1);
        return y;
    }
    void clear()
    {
        std::memset(buf, 0, sizeof buf);
        damp = 0;
    }
};

// a footage: saw and / or pulse with PWM, keyed high-pass and low-pass (string-machine's
// StringOsc + StringFilters, made cheaper: float one-poles with coefficients per block). The saw
// is DPW (the squared ramp, differentiated); the pulse is the same saw minus itself shifted by
// the duty, as string-machine's PwmOscillator (Faust's os.pulsetrain) builds it, the duty swept
// by a triangle LFO as there (0.5 down to 0.26)
struct Footage {
    float phase = 0, prevSq = 0, prevSq2 = 0;
    float pwmPhase = 0, pwmHz = 0.25f;
    float hpX = 0, hpY = 0, lpY = 0;
    bool fresh = true;
};

// Plaits' wavetable engine (plaits/dsp/engine/wavetable_engine.cc, MIT), ported for a whole
// bank: the same tables (wav_integrated_waves) and bank map, Hermite reads, the X / Y glides and
// the differentiator, but Z (the bank) stays on a whole number, so 4 waves are read per sample
// instead of 8 (Plaits' own engine at a whole Z reads the next bank with weight 0)
struct WaveOsc {
    float phase = 0, xPre = 0, yPre = 0, xLp = 0, yLp = 0, prevX = 0, prevY = 0, prevF0 = 0.01f;
    float diffLp = 0, diffPrev = 0;
    void render(const int16_t* const* map, float note, float timbre, float morph, float* out, int n)
    {
        constexpr float kTable = 128.f;
        const float f0 = std::min(noteHz(note) / kFs, 0.45f);
        ONE_POLE(xPre, timbre * 6.9999f, 0.2f);
        ONE_POLE(yPre, morph * 6.9999f, 0.2f);
        const float lpCoef = std::min(std::max(8.f * f0, 0.01f), 0.1f);
        const float dx = (xPre - prevX) / float(n), dy = (yPre - prevY) / float(n), df = (f0 - prevF0) / float(n);
        // the level and the differentiator's cutoff follow f0: per block, the gain ramped
        // (Plaits computes both per sample; f0 moves little within 24 frames)
        auto gainOf = [](float f) { return (1.f / (f * 131072.f)) * (0.95f - f); };
        const float g0 = gainOf(prevF0), dg = (gainOf(f0) - g0) / float(n);
        const float cutoff = std::min(kTable * f0, 1.f);
        float xm = prevX, ym = prevY, fm = prevF0, gain = g0;
        prevX = xPre;
        prevY = yPre;
        prevF0 = f0;
        for (int i = 0; i < n; ++i) {
            xm += dx;
            ym += dy;
            fm += df;
            gain += dg;
            ONE_POLE(xLp, xm, lpCoef);
            ONE_POLE(yLp, ym, lpCoef);
            const int xi = std::min(int(xLp), 6), yi = std::min(int(yLp), 6);
            const float xf = xLp - float(xi), yf = yLp - float(yi);
            phase += fm;
            if (phase >= 1.f) phase -= 1.f;
            const float pp = phase * kTable;
            const int pi = int(pp);
            const float pf = pp - float(pi);
            const float a = plaits::InterpolateWaveHermite(map[xi + yi * 8], pi, pf);
            const float b = plaits::InterpolateWaveHermite(map[xi + 1 + yi * 8], pi, pf);
            const float c = plaits::InterpolateWaveHermite(map[xi + (yi + 1) * 8], pi, pf);
            const float d = plaits::InterpolateWaveHermite(map[xi + 1 + (yi + 1) * 8], pi, pf);
            const float ab = a + (b - a) * xf, cd = c + (d - c) * xf;
            const float mix = ab + (cd - ab) * yf;
            ONE_POLE(diffLp, mix - diffPrev, cutoff); // the differentiator
            diffPrev = mix;
            out[i] = diffLp * gain;
        }
    }
};

// the banks' wave maps, as WavetableEngine::LoadUserData(nullptr) builds them
struct WaveMaps {
    const int16_t* map[4][64];
    WaveMaps()
    {
        for (int bank = 0; bank < 4; ++bank)
            for (int wave = 0; wave < 64; ++wave) {
                const int i = bank * 64 + wave;
                const int w = bank == 3 ? i * 101 % 192 : i;
                map[bank][wave] = plaits::wav_integrated_waves + size_t(w) * (128 + 4);
            }
    }
};
const WaveMaps& waveMaps()
{
    static const WaveMaps m;
    return m;
}

struct Voice {
    bool active = false, gate = false, sustained = false;
    uint8_t note = 60, velocity = 100;
    uint64_t age = 0;
    float t = 0;        // seconds since the note-on
    AHDSREnvelope env;
    Footage foot[3];
    AsymWaveshaper shaper; // string-machine's "enhance", once on the footages' sum
    StringFiltersHighshelf shelf;
    stmlib::Svf tone;
    float vibPhase = 0, vibRate = 1;  // the vibrato's phase, this voice's rate factor
    float footDetune[3] = {};         // this voice's spread per footage (-1..1)
    WaveOsc wave[kMaxUnison];
    float lfoPhase = 0;
    stmlib::Svf svf[2];
    Comb comb[2];
    float gA = -1, gB = -1;           // the layers' gains at the last block's end (-1: none yet)
};

// ---- effects
// a cheap sine of a phase 0..1 (parabola, max error ~0.06: fine for LFOs)
inline float lfoSin(float ph)
{
    const float x = ph - std::floor(ph);
    const float y = x < 0.5f ? 16.f * x * (0.5f - x) : -16.f * (x - 0.5f) * (1.f - x);
    return y;
}

struct Phaser { // 6 first-order allpasses per side, swept by the slow LFO, a little by the fast
    float z[2][6] = {}, fb[2] = {};
    float ph = 0, ph2 = 0;
    void process(float* l, float* r, int n, float slowHz, float fastHz, float amount)
    {
        // the sweep moves per block (24 frames: 0.5 ms)
        ph += slowHz * float(n) / kFs;
        ph -= std::floor(ph);
        ph2 += fastHz * float(n) / kFs;
        ph2 -= std::floor(ph2);
        const float wob = 0.06f * lfoSin(ph2);
        for (int c = 0; c < 2; ++c) {
            const float lfo = 0.5f + 0.5f * lfoSin(ph + 0.25f * float(c)) + wob;
            const float fc = 180.f * std::exp2(4.f * clamp01(lfo)); // 180 Hz .. 2.9 kHz
            const float t = std::tan(kPi * fc / kFs), a = (t - 1.f) / (t + 1.f);
            float* io = c ? r : l;
            for (int i = 0; i < n; ++i) {
                float x = io[i] + 0.55f * fb[c];
                for (float& s : z[c]) { // y = a x + s; s = x - a y
                    const float y = a * x + s;
                    s = x - a * y;
                    x = y;
                }
                fb[c] = x;
                io[i] = io[i] * (1.f - 0.5f * amount) + 0.5f * amount * x; // dry + allpassed: the notches
            }
        }
    }
};

struct Chorus { // two modulated taps per side: the slow LFO sweeps, the fast one shimmers
    static constexpr int kSize = 4096;
    float buf[kSize] = {};
    int w = 0;
    float ph = 0, ph2 = 0;
    float tap(float delay) const
    {
        float rp = float(w) - delay;
        if (rp < 0) rp += float(kSize);
        const int i0 = int(rp);
        const float fr = rp - float(i0);
        return buf[i0 & (kSize - 1)] + (buf[(i0 + 1) & (kSize - 1)] - buf[i0 & (kSize - 1)]) * fr;
    }
    void process(float* l, float* r, int n, float slowHz, float fastHz, float amount)
    {
        const float ms = kFs * 0.001f;
        for (int i = 0; i < n; ++i) {
            buf[w] = 0.5f * (l[i] + r[i]);
            ph += slowHz / kFs;
            if (ph >= 1.f) ph -= 1.f;
            ph2 += fastHz / kFs;
            if (ph2 >= 1.f) ph2 -= 1.f;
            const float s = lfoSin(ph), c = lfoSin(ph + 0.25f), f = 0.04f * lfoSin(ph2);
            const float wl = 0.5f * (tap(ms * (12.f + 4.f * s + f)) + tap(ms * (19.f - 3.f * c + f)));
            const float wr = 0.5f * (tap(ms * (12.f + 4.f * c - f)) + tap(ms * (19.f + 3.f * s - f)));
            w = (w + 1) & (kSize - 1);
            l[i] += amount * (wl - 0.5f * l[i]);
            r[i] += amount * (wr - 0.5f * r[i]);
        }
    }
};

struct Formant { // three band-passes per side on a vowel morph (a e i o u), swept by the slow LFO
    stmlib::Svf bp[2][3];
    float ph = 0, ph2 = 0;
    void init()
    {
        for (auto& side : bp)
            for (auto& f : side) f.Init();
    }
    void process(float* l, float* r, int n, float slowHz, float fastHz, float amount)
    {
        static const float kVowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 },
                                             { 450, 800, 2830 }, { 325, 700, 2700 } };
        static const float kGain[3] = { 1.f, 0.6f, 0.35f };
        ph += slowHz * float(n) / kFs;
        if (ph >= 1.f) ph -= 1.f;
        ph2 += fastHz * float(n) / kFs;
        if (ph2 >= 1.f) ph2 -= 1.f;
        for (int c = 0; c < 2; ++c) {
            const float pos = (0.5f - 0.5f * std::cos(2.f * kPi * (ph + 0.2f * float(c)))) * 4.f;
            const int v = std::min(3, int(pos));
            const float t = pos - float(v);
            const float vib = 1.f + 0.03f * std::sin(2.f * kPi * ph2);
            for (int k = 0; k < 3; ++k) {
                const float f = (kVowels[v][k] + (kVowels[v + 1][k] - kVowels[v][k]) * t) * vib;
                bp[c][k].set_f_q<stmlib::FREQUENCY_FAST>(std::min(f / kFs, 0.45f), 8.f);
            }
            float* io = c ? r : l;
            for (int i = 0; i < n; ++i) {
                float y = 0;
                for (int k = 0; k < 3; ++k)
                    y += kGain[k] * bp[c][k].Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(io[i]);
                io[i] += amount * (2.2f * y - io[i]);
            }
        }
    }
};

} // namespace

// ---------------------------------------------------------------------------

struct Engine::Impl {
    Params p, pNext;
    bool pPending = false;
    AHDSREnvelope::Settings envSet;
    Voice voices[kNumVoices];
    uint64_t ages = 0;
    float bend = 0, mod = 0;
    bool pedal = false;
    std::minstd_rand rng;
    uint64_t blocksA = 0, blocksB = 0;

    SolinaChorusStereo ensemble;
    Phaser phaser;
    Chorus chorus;
    Formant formant;
    rings::Reverb reverb;
    uint16_t reverbMem[32768];

    explicit Impl(uint32_t seed) : rng(seed ? seed : 1)
    {
        applyEnvSettings();
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        for (Voice& v : voices) {
            v.env.init(&envSet, kRate);
            for (int k = 0; k < 3; ++k) {
                Footage& f = v.foot[k];
                f.pwmHz = 0.21f + 0.09f * float(k) + 0.02f * u(rng); // string-machine's default is 0.25 Hz
                f.pwmPhase = 0.5f + 0.5f * u(rng);
                v.footDetune[k] = u(rng);
            }
            v.shaper.init(float(kRate));
            v.shelf.init(float(kRate));
            v.tone.Init();
            v.vibRate = 1.f + 0.06f * u(rng);
            v.vibPhase = 0.5f + 0.5f * u(rng);
            v.lfoPhase = 0.5f + 0.5f * u(rng);
            for (WaveOsc& w : v.wave) w.phase = 0.5f + 0.5f * u(rng);
            for (auto& s : v.svf) s.Init();
        }
        ensemble.init(kRate);
        ensemble.setEnabled(true);
        ensemble.setAnalogMode(false); // the digital delays (light enough for a Pi)
        ensemble.getLfo().set_depth1(30.71f); // string-machine's defaults
        ensemble.getLfo().set_depth2(90.55f);
        ensemble.getLfo().set_globaldepth(100.f);
        formant.init();
        std::memset(reverbMem, 0, sizeof reverbMem);
        reverb.Init(reverbMem);
        reverb.set_diffusion(0.625f);
        reverb.set_lp(0.7f);
        reverb.set_input_gain(0.2f);
    }

    void applyEnvSettings()
    {
        envSet.attack = std::max(0.001f, p.attack);
        envSet.hold = 0.f;
        envSet.decay = std::max(0.001f, p.decay);
        envSet.sustain = 20.f * std::log10(std::max(p.sustain, 0.001f)); // dB
        envSet.release = std::max(0.001f, p.release);
    }

    Voice* findVoice(uint8_t note)
    {
        for (Voice& v : voices)
            if (v.active && v.gate && v.note == note) return &v;
        return nullptr;
    }
    Voice& allocate(uint8_t note)
    {
        if (Voice* v = findVoice(note)) return *v; // the same key again: retrigger it
        for (Voice& v : voices)
            if (!v.active) return v;
        Voice* best = nullptr; // steal: the quietest released voice, else the oldest
        for (Voice& v : voices)
            if (!v.gate && (!best || v.env.getCurrentLevel() < best->env.getCurrentLevel())) best = &v;
        if (best) return *best;
        best = &voices[0];
        for (Voice& v : voices)
            if (v.age < best->age) best = &v;
        return *best;
    }

    void noteOn(uint8_t note, uint8_t vel)
    {
        Voice& v = allocate(note);
        const bool fresh = !v.active;
        v.active = v.gate = true;
        v.sustained = false;
        v.note = note;
        v.velocity = vel;
        v.age = ++ages;
        v.t = 0;
        if (fresh) {
            v.gA = v.gB = -1;
            for (Footage& f : v.foot) {
                f.fresh = true;
                f.hpX = f.hpY = f.lpY = 0;
            }
            v.tone.Reset();
            for (auto& s : v.svf) s.Reset();
            for (auto& c : v.comb) c.clear();
        }
        v.env.trigger();
    }
    void noteOff(uint8_t note)
    {
        for (Voice& v : voices)
            if (v.active && v.gate && v.note == note) {
                if (pedal) v.sustained = true;
                else {
                    v.gate = false;
                    v.env.release();
                }
            }
    }
    void setPedal(bool down)
    {
        pedal = down;
        if (down) return;
        for (Voice& v : voices)
            if (v.sustained) {
                v.sustained = false;
                v.gate = false;
                v.env.release();
            }
    }

    // the layers' gains of a voice now: the blend (or the split) times the mixer's level / on
    void layerGains(const Voice& v, float& gA, float& gB) const
    {
        blendGains(v, gA, gB);
        gA = p.stringsOn ? gA * p.stringsLevel : 0.f;
        gB = p.waveOn ? gB * p.waveLevel : 0.f;
    }
    void blendGains(const Voice& v, float& gA, float& gB) const
    {
        if (p.keys != 0) { // a split: each key plays one layer
            const bool waveKey = p.keys == 1 ? v.note < kSplitKey : v.note >= kSplitKey;
            gA = waveKey ? 0.f : 1.f;
            gB = waveKey ? 1.f : 0.f;
            return;
        }
        const float ramp = 1.f - std::exp(-3.f * v.t / std::max(0.01f, p.blendTime));
        const float w = clamp01(p.blend + p.blendShift * ramp + p.blendVelo * (float(v.velocity) / 127.f - 0.5f));
        gA = std::cos(0.5f * kPi * w);
        gB = std::sin(0.5f * kPi * w);
        if (gA < 1e-4f) gA = 0.f;
        if (gB < 1e-4f) gB = 0.f;
    }

    void renderVoice(Voice& v, float* outL, float* outR, int n)
    {
        float env[kMaxBlock], a[kMaxBlock], bl[kMaxBlock], br[kMaxBlock], tmp[kMaxBlock];
        v.env.process(env, uint32_t(n));
        const float dt = float(n) / kFs;
        v.t += dt;
        const float velGain = 1.f - p.velocity * (1.f - float(v.velocity) / 127.f);
        float gA, gB;
        layerGains(v, gA, gB);
        const float gA0 = v.gA < 0 ? gA : v.gA, gB0 = v.gB < 0 ? gB : v.gB;
        v.gA = gA;
        v.gB = gB;
        const float envLevel = env[n - 1];

        // ---- layer A: the strings
        if (gA0 > 0.f || gA > 0.f) {
            ++blocksA;
            const Station st = stationAt(p.reg);
            const float levels[3] = { st.g16, st.g8, st.g4 };
            const float norm = 1.f / std::sqrt(std::max(0.05f, st.g16 * st.g16 + st.g8 * st.g8 + st.g4 * st.g4));
            // the delayed vibrato: after VIB DELAY it fades in over half a second; the mod wheel adds
            v.vibPhase += p.vibHz * v.vibRate * dt;
            v.vibPhase -= std::floor(v.vibPhase);
            const float fadeIn = clamp01((v.t - p.vibDelay) * 2.f);
            const float vibSt = 0.3f * std::min(1.f, p.vibDepth * fadeIn + mod) * std::sin(2.f * kPi * v.vibPhase);
            const float f8 = noteHz(float(v.note) + bend + vibSt);
            std::fill(a, a + n, 0.f);
            for (int k = 0; k < 3; ++k) {
                if (levels[k] < 1e-3f) continue;
                Footage& ft = v.foot[k];
                const float fk = f8 * kFootRatio[k] * semis(v.footDetune[k] * p.detuneCt * 0.01f);
                const float inc = std::min(fk / kFs, 0.45f), scale = 0.25f / inc;
                const bool saw = p.shape != 1, pulse = p.shape != 0;
                ft.pwmPhase += ft.pwmHz * dt;
                ft.pwmPhase -= std::floor(ft.pwmPhase);
                const float duty = 0.5f - 0.24f * (1.f - std::fabs(2.f * ft.pwmPhase - 1.f));
                if (ft.fresh) {
                    const float x0 = 2.f * ft.phase - 1.f;
                    float p2 = ft.phase + duty;
                    if (p2 >= 1.f) p2 -= 1.f;
                    const float x2 = 2.f * p2 - 1.f;
                    ft.prevSq = x0 * x0;
                    ft.prevSq2 = x2 * x2;
                    ft.fresh = false;
                }
                const float ph = 0.5f * (1.f + std::exp(-2.f * kPi * limitHz(fk * semis(st.hpSt)) / kFs)); // HP gain
                const float hp = 2.f * ph - 1.f;                                                              // HP pole
                const float lc = 1.f - std::exp(-2.f * kPi * limitHz(fk * semis(st.lpSt)) / kFs);            // LP coef
                const float g = levels[k] * norm;
                for (int i = 0; i < n; ++i) {
                    ft.phase += inc;
                    if (ft.phase >= 1.f) ft.phase -= 1.f;
                    float y = 0;
                    if (saw) {
                        const float x = 2.f * ft.phase - 1.f, sq = x * x;
                        y = (sq - ft.prevSq) * scale;
                        ft.prevSq = sq;
                    }
                    if (pulse) {
                        float p2 = ft.phase + duty;
                        if (p2 >= 1.f) p2 -= 1.f;
                        const float x2 = 2.f * p2 - 1.f, sq2 = x2 * x2;
                        const float s2 = (sq2 - ft.prevSq2) * scale;
                        ft.prevSq2 = sq2;
                        const float x = 2.f * ft.phase - 1.f;
                        const float s1 = saw ? y : (x * x - ft.prevSq) * scale;
                        if (!saw) ft.prevSq = x * x;
                        const float pul = 0.5f * (s1 - s2);
                        y = saw ? 0.5f * (y + pul) : pul;
                    }
                    ft.hpY = ph * (y - ft.hpX) + hp * ft.hpY; // string-machine's one-pole high-pass
                    ft.hpX = y;
                    ft.lpY += lc * (ft.hpY - ft.lpY);
                    a[i] += g * ft.lpY;
                }
            }
            v.shaper.set_amount(1.f - st.enhance);
            v.shaper.process(a, a, unsigned(n));
            v.shelf.set_cutoff(limitHz(f8 * semis(st.shelfSt)));
            v.shelf.set_gain(st.shelfDb);
            v.shelf.process(a, a, unsigned(n));
            v.tone.set_f_q<stmlib::FREQUENCY_FAST>(limitHz(f8 * std::exp2(1.f + 7.f * p.tone)) / kFs, 0.707f);
            const float step = (gA - gA0) / float(n);
            for (int i = 0; i < n; ++i) {
                const float y = v.tone.Process<stmlib::FILTER_MODE_LOW_PASS>(a[i]);
                const float s = y * env[i] * velGain * (gA0 + step * float(i + 1)) * 0.5f;
                outL[i] += s;
                outR[i] += s;
            }
        }

        // ---- layer B: the wavetables, then the filter
        if (gB0 > 0.f || gB > 0.f) {
            ++blocksB;
            v.lfoPhase += p.lfoHz * dt;
            v.lfoPhase -= std::floor(v.lfoPhase);
            const float lfo = 1.f - 4.f * std::fabs(v.lfoPhase - 0.5f); // triangle -1..1
            const float timbre = clamp01(p.x + 0.5f * p.lfoDepth * lfo + 0.5f * p.envPos * envLevel);
            const float morph = clamp01(p.y);
            const int uni = std::clamp(p.unison, 1, kMaxUnison);
            const int16_t* const* maps = waveMaps().map[std::clamp(p.bank, 0, 3)];
            const float base = float(v.note) + 12.f * float(p.octave) + 0.01f * p.layerDetuneCt + bend;
            const float uGain = 0.28f / std::sqrt(float(uni)); // level with the strings layer (measured)
            std::fill(bl, bl + n, 0.f);
            std::fill(br, br + n, 0.f);
            for (int u = 0; u < uni; ++u) {
                const float pos = uni > 1 ? float(u) / float(uni - 1) : 0.5f; // 0 left .. 1 right
                v.wave[u].render(maps, base + (pos - 0.5f) * 0.01f * p.spreadCt, timbre, morph, tmp, n);
                if (uni == 1) { // the middle: equal gains (the copy to the right comes after the filter)
                    for (int i = 0; i < n; ++i) bl[i] += uGain * tmp[i];
                    continue;
                }
                const float pan = 0.5f + (pos - 0.5f) * 0.8f;
                const float gl = uGain * std::cos(0.5f * kPi * pan) * 1.4142f, gr = uGain * std::sin(0.5f * kPi * pan) * 1.4142f;
                for (int i = 0; i < n; ++i) {
                    bl[i] += gl * tmp[i];
                    br[i] += gr * tmp[i];
                }
            }
            // the filter: cutoff (or the comb's pitch) keytracked from C4, moved by the envelope
            const float fc = limitHz(p.cutoffHz * semis(float(v.note - 60) * p.keytrack) *
                                     std::exp2(5.f * p.filterEnv * envLevel));
            float* io[2] = { bl, br };
            const int chans = uni == 1 ? 1 : 2; // one voice sits in the middle: filter it once
            if (p.filterMode == 3) {
                const float delay = std::clamp(kFs / fc, 2.f, float(Comb::kSize - 2));
                const float fb = 0.97f * p.reso;
                for (int c = 0; c < chans; ++c)
                    for (int i = 0; i < n; ++i) io[c][i] = (1.f - 0.6f * fb) * v.comb[c].process(io[c][i], delay, fb);
            } else {
                const float q = 0.5f + 11.5f * p.reso * p.reso;
                for (int c = 0; c < chans; ++c) {
                    stmlib::Svf& f = v.svf[c];
                    f.set_f_q<stmlib::FREQUENCY_ACCURATE>(fc / kFs, q);
                    for (int i = 0; i < n; ++i) {
                        const float x = io[c][i];
                        io[c][i] = p.filterMode == 0   ? f.Process<stmlib::FILTER_MODE_LOW_PASS>(x)
                                   : p.filterMode == 1 ? f.Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(x)
                                                       : f.Process<stmlib::FILTER_MODE_HIGH_PASS>(x);
                    }
                }
            }
            if (chans == 1) std::copy(bl, bl + n, br);
            const float step = (gB - gB0) / float(n);
            for (int i = 0; i < n; ++i) {
                const float g = env[i] * velGain * (gB0 + step * float(i + 1));
                outL[i] += g * bl[i];
                outR[i] += g * br[i];
            }
        }

        if (!v.env.isTriggered() && v.env.getCurrentLevel() < 1e-4f) { // finished
            v.active = v.gate = v.sustained = false;
            v.env.clear();
        }
    }

    void render(float* outL, float* outR, int n)
    {
        if (pPending) {
            p = pNext;
            pPending = false;
            applyEnvSettings();
        }
        std::fill(outL, outL + n, 0.f);
        std::fill(outR, outR + n, 0.f);
        for (Voice& v : voices)
            if (v.active) renderVoice(v, outL, outR, n);
        for (int i = 0; i < n; ++i) { // 8 voices' headroom (a full chord peaks near -3 dBFS)
            outL[i] *= 0.55f;
            outR[i] *= 0.55f;
        }
        // the effect slot
        const float amt = clamp01(p.fxAmount);
        if (amt > 0.f) {
            switch (p.fxMode) {
            case 0: {
                ensemble.getLfo().set_rate1(p.fxFastHz);
                ensemble.getLfo().set_rate2(p.fxSlowHz);
                float wl[kMaxBlock], wr[kMaxBlock];
                const float* in[2] = { outL, outR };
                float* out[2] = { wl, wr };
                ensemble.process(in, out, unsigned(n));
                for (int i = 0; i < n; ++i) {
                    outL[i] += amt * (wl[i] - outL[i]);
                    outR[i] += amt * (wr[i] - outR[i]);
                }
                break;
            }
            case 1: phaser.process(outL, outR, n, p.fxSlowHz, p.fxFastHz, amt); break;
            case 2: chorus.process(outL, outR, n, p.fxSlowHz, p.fxFastHz, amt); break;
            default: formant.process(outL, outR, n, p.fxSlowHz, p.fxFastHz, amt); break;
            }
        }
        if (p.reverb > 0.f) {
            reverb.set_amount(0.55f * clamp01(p.reverb));
            reverb.set_time(0.5f + 0.4f * clamp01(p.reverb));
            reverb.Process(outL, outR, size_t(n));
        }
        // the output stage: untouched up to full scale, then bent towards 2 (2 - 1/|x|: the same
        // slope at 1, never above 2), so a hot resonance cannot run away
        for (float* o : { outL, outR })
            for (int i = 0; i < n; ++i) {
                const float a = std::fabs(o[i]);
                if (a > 1.f) o[i] = std::copysign(2.f - 1.f / a, o[i]);
            }
    }
};

// ---------------------------------------------------------------------------

Engine::Engine(uint32_t seed) : fImpl(new Impl(seed)) {}
Engine::~Engine() = default;

void Engine::setParams(const Params& p)
{
    fImpl->pNext = p;
    fImpl->pPending = true;
}
void Engine::noteOn(uint8_t note, uint8_t velocity)
{
    if (velocity == 0) return noteOff(note);
    fImpl->noteOn(note & 127, velocity & 127);
}
void Engine::noteOff(uint8_t note) { fImpl->noteOff(note & 127); }
void Engine::pitchBend(float semitones) { fImpl->bend = semitones; }
void Engine::modWheel(float amount) { fImpl->mod = clamp01(amount); }
void Engine::sustainPedal(bool down) { fImpl->setPedal(down); }
void Engine::allNotesOff()
{
    fImpl->pedal = false;
    for (Voice& v : fImpl->voices)
        if (v.active) {
            v.gate = v.sustained = false;
            v.env.release();
        }
}
void Engine::allSoundOff()
{
    fImpl->pedal = false;
    for (Voice& v : fImpl->voices) {
        v.active = v.gate = v.sustained = false;
        v.env.clear();
    }
}
void Engine::render(float* outL, float* outR, int frames)
{
    fImpl->render(outL, outR, std::clamp(frames, 0, kMaxBlock));
}
int Engine::activeVoices() const
{
    int n = 0;
    for (const Voice& v : fImpl->voices) n += v.active;
    return n;
}
uint64_t Engine::blocksA() const { return fImpl->blocksA; }
uint64_t Engine::blocksB() const { return fImpl->blocksB; }

} // namespace strings
} // namespace maquina
