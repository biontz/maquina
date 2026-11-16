// DredrumHost.cpp - see DredrumHost.hpp.
// SPDX-License-Identifier: GPL-3.0-only
#include "DredrumHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "DredrumFlash.generated.hpp"

namespace maquina {

namespace {

// output stage parts (third_party/dredrum KiCad netlist)
constexpr double kR10 = 16e3, kC7 = 1e-9;                          // RC lowpass
constexpr double kR11 = 100e3, kR12 = 33e3, kC5 = 680e-12, kC8 = 100e-12; // Sallen-Key
// op-amp rails (0 V / 5 V) in engine units: OCR1A 127 -> 0, duty (OCR + 1) / 256
constexpr double kRailLo = -128.0 / 127.0, kRailHi = 128.0 / 127.0;
// int16 handed to the Resampler: 2x headroom for the filter's overshoot.
// Output level: the op-amp rails (the module's full 5 V swing) at -6 dBFS,
// like the LXR voices' peaks, so one oscillator at full level does not clip.
constexpr double kToInt16 = 16384.0;
constexpr float kFromResampler = 1.0f; // rails -> +-0.504

// e^M for a small dense matrix (scaling and squaring + Taylor)
template <int N>
void expm(const double (&m)[N][N], double (&out)[N][N])
{
    double norm = 0.0;
    for (int i = 0; i < N; ++i) {
        double row = 0.0;
        for (int j = 0; j < N; ++j) row += std::fabs(m[i][j]);
        norm = std::max(norm, row);
    }
    int squarings = 0;
    while (norm > 0.25) { norm *= 0.5; ++squarings; }
    const double scale = std::ldexp(1.0, -squarings);

    double a[N][N], term[N][N], tmp[N][N];
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            a[i][j] = m[i][j] * scale;
            out[i][j] = term[i][j] = i == j ? 1.0 : 0.0;
        }
    for (int k = 1; k <= 20; ++k) {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                double s = 0.0;
                for (int l = 0; l < N; ++l) s += term[i][l] * a[l][j];
                tmp[i][j] = s / k;
            }
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) { term[i][j] = tmp[i][j]; out[i][j] += tmp[i][j]; }
    }
    for (int s = 0; s < squarings; ++s) {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                double v = 0.0;
                for (int l = 0; l < N; ++l) v += out[i][l] * out[l][j];
                tmp[i][j] = v;
            }
        std::memcpy(out, tmp, sizeof(tmp));
    }
}

} // namespace

DredrumHost::DredrumHost(const VoiceDef& def, uint32_t)
    : fDef(def),
      fDefaults(def.numParams, 0.f),
      fValues(def.numParams)
{
    static const char* const kNames[DREDRUM_NUM_PARAMS] = { "level", "decay", "drop", "pitch", "wave" };
    for (auto& row : fParamIndex)
        for (int& i : row) i = -1;
    for (int n = 0; n < DREDRUM_NUM_OSC; ++n) fTrackParam[n] = fOctaveParam[n] = fSemiParam[n] = fMuteParam[n] = -1;
    for (uint32_t i = 0; i < def.numParams; ++i) {
        const VoiceParam& p = def.params[i];
        fDefaults[i] = float(p.def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
        if (!std::strcmp(p.symbol, "click")) fClickParam = int(i);
        else if (!std::strcmp(p.symbol, "trig_mode")) fModeParam = int(i);
        else if (std::strncmp(p.symbol, "osc", 3) == 0 && p.symbol[3] >= '1' && p.symbol[3] <= '3' && p.symbol[4] == '_')
        {
            const int n = p.symbol[3] - '1';
            const char* what = p.symbol + 5;
            for (int k = 0; k < DREDRUM_NUM_PARAMS; ++k)
                if (!std::strcmp(what, kNames[k])) fParamIndex[n][k] = int(i);
            if (!std::strcmp(what, "track")) fTrackParam[n] = int(i);
            else if (!std::strcmp(what, "octave")) fOctaveParam[n] = int(i);
            else if (!std::strcmp(what, "semi")) fSemiParam[n] = int(i);
            else if (!std::strcmp(what, "mute")) fMuteParam[n] = int(i);
        }
    }

    std::memset(&fState, 0, sizeof(fState));
    dredrum_reset(&fState);
    applyParams();
    setupFilter();
    setSampleRate(fHostRate);
}

void DredrumHost::setupFilter()
{
    // states: x0 = C7 node, x1 = Sallen-Key middle node, x2 = output (C8 node,
    // followed by the op-amp); u = held engine sample
    const double a = 1.0 / (kR10 * kC7), b = 1.0 / (kR11 * kC7);
    const double c = 1.0 / (kR11 * kC5), d = 1.0 / (kR12 * kC5), e = 1.0 / (kR12 * kC8);
    const double h = 1.0 / kFilterRate;
    // augmented [[A, B], [0, 0]] * h: its exponential holds Ad and Bd (exact
    // for an input held over the step)
    const double m[4][4] = {
        { -(a + b) * h, b * h, 0.0, a * h },
        { c * h, (-c - d + e) * h, (d - e) * h, 0.0 },
        { 0.0, e * h, -e * h, 0.0 },
        { 0.0, 0.0, 0.0, 0.0 },
    };
    double em[4][4];
    expm<4>(m, em);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) fAd[i][j] = em[i][j];
        fBd[i] = em[i][3];
    }
}

void DredrumHost::setSampleRate(double hostRate)
{
    fHostRate = hostRate > 0.0 ? hostRate : 48000.0;
    fResampler.setup(kFilterRate, fHostRate);
    // An event at host frame H is applied to the engine sample containing
    // filter frame F = H * ratio + kEventDelay, which starts at F - q
    // (q in [0, kOversample)); the resampler shows input position k*ratio - 1
    // at output frame k.
    const double meanDelay = kEventDelay - (kOversample - 1) / 2.0 + 1.0;
    fLatency = uint32_t(std::lround(meanDelay / fResampler.ratio()));
    resetTiming();
}

void DredrumHost::resetTiming()
{
    fResampler.reset();
    fEventHead = fEventCount = 0;
    fHostFrame = 0.0;
    fFrame = 0.0;
}

// ---------------------------------------------------------------------------
// parameters

void DredrumHost::setParam(uint32_t index, float value)
{
    if (index >= fDef.numParams) return;
    const VoiceParam& p = fDef.params[index];
    const float lo = float(p.fwMin + p.offset), hi = float(p.fwMax + p.offset);
    fValues[index].store(std::clamp(std::round(value), lo, hi), std::memory_order_relaxed);
}

float DredrumHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float DredrumHost::defaultParam(uint32_t index) const
{
    return index < fDef.numParams ? fDefaults[index] : 0.f;
}

void DredrumHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i)
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int DredrumHost::paramInt(int index) const
{
    return index < 0 ? 0 : int(std::lround(fValues[uint32_t(index)].load(std::memory_order_relaxed)));
}

void DredrumHost::updateRatios()
{
    for (int n = 0; n < DREDRUM_NUM_OSC; ++n) {
        const int semis = paramInt(fOctaveParam[n]) * 12 + paramInt(fSemiParam[n]) +
                          (paramInt(fTrackParam[n]) ? fNote - 60 : 0);
        // exactly 0x10000 at 0 semitones: the firmware's own code path
        const double r = semis == 0 ? 65536.0 : std::round(65536.0 * std::exp2(semis / 12.0));
        dredrum_set_ratio(&fState, n, uint32_t(std::clamp(r, 1.0, 1073741824.0)));
    }
}

void DredrumHost::applyParams()
{
    auto get = [this](int i) { return paramInt(i); };
    for (int n = 0; n < DREDRUM_NUM_OSC; ++n)
        for (int k = 0; k < DREDRUM_NUM_PARAMS; ++k)
            fState.params[n][k] = uint8_t(std::clamp(get(fParamIndex[n][k]), 0, 255));
    for (int n = 0; n < DREDRUM_NUM_OSC; ++n)
        if (get(fMuteParam[n])) fState.params[n][DREDRUM_LEVEL] = 0; // contributes exactly 0
    fState.click_mode = get(fClickParam) != 0;
    const uint8_t gate = get(fModeParam) != 0;
    if (gate != fState.gate_mode) {
        fState.gate_mode = gate;
        dredrum_gate_off(&fState);
        fGateNote = -1;
    }
    fState.noise = gate ? kDredrumFlashGate : kDredrumFlashTrig;
    updateRatios();
}

// ---------------------------------------------------------------------------
// notes

void DredrumHost::queue(uint32_t frameOffset, uint8_t note, bool on)
{
    if (fEventCount >= kMaxEvents) return;
    Event& e = fEvents[(fEventHead + fEventCount) % kMaxEvents];
    e.frame = (fHostFrame + frameOffset) * fResampler.ratio() + kEventDelay;
    e.note = note;
    e.on = on;
    ++fEventCount;
}

void DredrumHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (velocity != 0) queue(frameOffset, note, true); // the module has no velocity
}

void DredrumHost::noteOff(uint32_t frameOffset, uint8_t note) { queue(frameOffset, note, false); }

void DredrumHost::renderBlock(int16_t* lr)
{
    for (int s = 0; s < Resampler::kBlock / kOversample; ++s) {
        // events inside this engine sample (or overdue) act before it
        const double end = fFrame + kOversample;
        while (fEventCount > 0 && fEvents[fEventHead].frame < end) {
            const Event& e = fEvents[fEventHead];
            if (e.on) {
                fNote = e.note;
                updateRatios();
                dredrum_trigger(&fState);
                fGateNote = fState.gate_mode ? e.note : -1;
            } else if (int(e.note) == fGateNote) {
                dredrum_gate_off(&fState);
                fGateNote = -1;
            }
            fEventHead = (fEventHead + 1) % kMaxEvents;
            --fEventCount;
        }

        const double u = (int(dredrum_render_sample(&fState)) - DREDRUM_SILENCE) / 127.0;
        for (int k = 0; k < kOversample; ++k) {
            const double x0 = fAd[0][0] * fX[0] + fAd[0][1] * fX[1] + fAd[0][2] * fX[2] + fBd[0] * u;
            const double x1 = fAd[1][0] * fX[0] + fAd[1][1] * fX[1] + fAd[1][2] * fX[2] + fBd[1] * u;
            const double x2 = fAd[2][0] * fX[0] + fAd[2][1] * fX[1] + fAd[2][2] * fX[2] + fBd[2] * u;
            fX[0] = x0; fX[1] = x1; fX[2] = x2;
            const double y = std::clamp(x2, kRailLo, kRailHi);
            const int16_t v = int16_t(std::lround(y * kToInt16));
            lr[2 * (s * kOversample + k)] = v;
            lr[2 * (s * kOversample + k) + 1] = v;
        }
        fFrame = end;
    }
    // no denormals in the decaying filter state
    for (double& x : fX)
        if (std::fabs(x) < 1e-12) x = 0.0;
}

void DredrumHost::process(float* outL, float* outR, uint32_t frames)
{
    applyParams();
    fResampler.process(outL, outR, frames, [this](int16_t* lr) { renderBlock(lr); });
    for (uint32_t i = 0; i < frames; ++i) {
        outL[i] *= kFromResampler;
        outR[i] *= kFromResampler;
    }
    fHostFrame += frames;
}

} // namespace maquina
