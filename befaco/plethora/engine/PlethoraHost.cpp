// PlethoraHost.cpp - see PlethoraHost.hpp. Compiled together with the
// vendored Noise Plethora port (befaco/plethora/plugins/plethora/Engine.cpp).
// SPDX-License-Identifier: GPL-3.0-only
#include "PlethoraHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "noise-plethora/plugins/Banks.hpp"
#include "noise-plethora/plugins/NoisePlethoraPlugin.hpp"

namespace maquina {

namespace {

constexpr float kFreqC4 = 261.6256f; // Rack dsp::FREQ_C4
// the module's filter type switch -> SVF output (NoisePlethora.cpp typeMappingSVF)
constexpr plethora::FilterMode kTypeMapping[3] = { plethora::LOWPASS, plethora::BANDPASS, plethora::HIGHPASS };
// int16 handed to the Resampler (2x headroom), and the mix level: one channel
// at full level peaks around -6 dBFS like the other Maquina voices
constexpr float kToInt16 = 16384.f;
constexpr float kFromResampler = 2.f;
constexpr float kMixGain = 0.5f;

float knob(int v) { return float(v) / 127.f; }

// Maquina envelope times
float attackStep(int v) // per sample; 0 = instant
{
    if (v <= 0) return 1.f;
    const double t = 0.001 * std::pow(2000.0, v / 127.0); // 1 ms .. 2 s
    return float(1.0 / (t * PlethoraHost::kEngineRate));
}
float decayCoef(int v) // per sample, -60 dB after the decay time; 127 = hold
{
    if (v >= 127) return 1.f;
    const double t = 0.005 * std::pow(2000.0, v / 127.0); // 5 ms .. ~9 s
    return float(std::pow(0.001, 1.0 / (t * PlethoraHost::kEngineRate)));
}

} // namespace

float PlethoraHost::Envelope::process(float step, float coef, bool gate)
{
    switch (stage) {
    case Idle: value = 0.f; break;
    case Attack:
        value += step;
        if (value >= 1.f) {
            value = 1.f;
            stage = gate ? Hold : Decay;
        }
        break;
    case Hold: value = 1.f; break;
    case Decay:
        value *= coef;
        if (value < 1e-4f && coef < 1.f) {
            value = 0.f;
            stage = Idle;
        }
        break;
    }
    return value;
}

PlethoraHost::PlethoraHost(const VoiceDef& def, uint32_t seed)
    : fDef(def),
      fDefaults(def.numParams, 0.f),
      fValues(def.numParams),
      fWhite(new AudioSynthNoiseWhiteFloat()),
      fGrit(new AudioSynthNoiseGritFloat()),
      fRng(new rack::random::Xoshiro128Plus())
{
    fRng->seed(seed);
    rack::random::ScopedRng rngScope(*fRng);

    // parameter indices by symbol: <channel>_<name>
    static const char* const kNames[10] = { "algo", "x", "y", "ftype", "cutoff", "reso", "attack", "decay",
                                            "level", "mute" };
    for (auto& row : fIndex)
        for (int& i : row) i = -1;
    for (uint32_t i = 0; i < def.numParams; ++i) {
        const VoiceParam& p = def.params[i];
        fDefaults[i] = float(p.def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
        const char* s = p.symbol;
        if (!std::strcmp(s, "c_source")) fSourceParam = int(i);
        else if (!std::strcmp(s, "c_grit")) fGritParam = int(i);
        else if (!std::strcmp(s, "env_sync")) fSyncParam = int(i);
        else if (!std::strcmp(s, "trig_mode")) fModeParam = int(i);
        else if (s[0] >= 'a' && s[0] <= 'c' && s[1] == '_')
            for (int k = 0; k < 10; ++k)
                if (!std::strcmp(s + 2, kNames[k])) fIndex[s[0] - 'a'][k] = int(i);
    }

    // every algorithm for A and B, as the VCV module does
    int n = 0;
    for (int b = 0; b < numBanks; ++b) {
        Bank& bank = getBankForIndex(b);
        for (int p = 0; p < bank.getSize() && n < kNumAlgorithms; ++p, ++n) {
            const std::string name(bank.getProgramName(p));
            fAlgoGain[n] = bank.getProgramGain(p);
            for (auto& list : fAlgos) list.push_back(MyFactory::Instance()->Create(name));
        }
    }

    const float dcFc = 22.05f / float(kEngineRate); // ~20 Hz, as the module
    for (auto& dc : fDc) dc.setFrequency(dcFc);

    readParams();
    setSampleRate(fHostRate);
}

PlethoraHost::~PlethoraHost() = default;

void PlethoraHost::setSampleRate(double hostRate)
{
    fHostRate = hostRate > 0.0 ? hostRate : 48000.0;
    fResampler.setup(kEngineRate, fHostRate);
    // events act at the engine sample they fall in (no block quantisation)
    const double meanDelay = kEventDelay - 0.5 + 1.0;
    fLatency = uint32_t(std::lround(meanDelay / fResampler.ratio()));
    resetTiming();
}

void PlethoraHost::resetTiming()
{
    fResampler.reset();
    fEventHead = fEventCount = 0;
    fHostFrame = 0.0;
    fFrame = 0.0;
}

// ---------------------------------------------------------------------------
// parameters

void PlethoraHost::setParam(uint32_t index, float value)
{
    if (index >= fDef.numParams) return;
    const VoiceParam& p = fDef.params[index];
    const float lo = float(p.fwMin + p.offset), hi = float(p.fwMax + p.offset);
    fValues[index].store(std::clamp(std::round(value), lo, hi), std::memory_order_relaxed);
}

float PlethoraHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float PlethoraHost::defaultParam(uint32_t index) const
{
    return index < fDef.numParams ? fDefaults[index] : 0.f;
}

void PlethoraHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int PlethoraHost::paramInt(int index) const
{
    return index < 0 ? 0 : int(std::lround(fValues[uint32_t(index)].load(std::memory_order_relaxed)));
}

void PlethoraHost::readParams()
{
    for (int c = 0; c < kNumChannels; ++c) {
        ChannelParams& p = fCh[c];
        const int* ix = fIndex[c];
        p.algo = std::clamp(paramInt(ix[0]), 0, kNumAlgorithms - 1);
        p.x = paramInt(ix[1]);
        p.y = paramInt(ix[2]);
        p.ftype = std::clamp(paramInt(ix[3]), 0, 2);
        p.cutoff = paramInt(ix[4]);
        p.reso = paramInt(ix[5]);
        p.attack = paramInt(ix[6]);
        p.decay = paramInt(ix[7]);
        p.level = paramInt(ix[8]);
        p.mute = paramInt(ix[9]) != 0;

        // filter: NoisePlethora.cpp, A/B (2nd order) and C (4th order) mappings
        const float pitch = c == C ? rescale(knob(p.cutoff), 0.f, 1.f, -5.f, +6.4f)
                                   : rescale(knob(p.cutoff), 0.f, 1.f, -5.5f, +5.5f);
        const float cutoff = clamp(kFreqC4 * std::pow(2.f, pitch), 1.f, c == C ? 44100.f / 2.f : 20000.f);
        p.fc = clamp(cutoff / float(kEngineRate), 0.f, 0.49f);
        p.q = c == C ? 0.5f + std::pow(knob(p.reso), 2) * 20.f : float(M_SQRT1_2) + std::pow(knob(p.reso), 2) * 10.f;
        p.attackStep = attackStep(p.attack);
        p.decayCoef = decayCoef(p.decay);
        p.gain = knob(p.level) * knob(p.level);
    }
    fWhiteSource = paramInt(fSourceParam) != 0;
    fGrit01 = paramInt(fGritParam);
    fGrit->setDensity(0.1f + std::pow(knob(fGrit01), 2) * 20000.f);
    fSync = paramInt(fSyncParam) != 0;
    const bool gate = paramInt(fModeParam) != 0;
    if (gate != fGateMode) {
        fGateMode = gate;
        if (!gate && fGateHigh) {
            for (auto& e : fEnv) e.release();
            fGateHigh = false;
            fGateNote = -1;
        }
    }
    // algorithm switch: init() as the module does (never allocates)
    for (int c = 0; c < 2; ++c)
        if (fCh[c].algo != fCurrent[c]) {
            fCurrent[c] = fCh[c].algo;
            if (auto& a = fAlgos[c][size_t(fCurrent[c])]) a->init();
        }
}

// ---------------------------------------------------------------------------
// notes

void PlethoraHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (velocity == 0 || fEventCount >= kMaxEvents) return; // the module has no velocity
    Event& e = fEvents[(fEventHead + fEventCount) % kMaxEvents];
    e.frame = (fHostFrame + frameOffset) * fResampler.ratio() + kEventDelay;
    e.note = note;
    e.on = true;
    ++fEventCount;
}

void PlethoraHost::noteOff(uint32_t frameOffset, uint8_t note)
{
    if (fEventCount >= kMaxEvents) return;
    Event& e = fEvents[(fEventHead + fEventCount) % kMaxEvents];
    e.frame = (fHostFrame + frameOffset) * fResampler.ratio() + kEventDelay;
    e.note = note;
    e.on = false;
    ++fEventCount;
}

// ---------------------------------------------------------------------------
// audio

// channel A or B: algorithm -> gain -> filter -> DC blocker -> saturator
// (NoisePlethora.cpp processTopSection, filters on, DC blocking on)
float PlethoraHost::renderTop(int c)
{
    NoisePlethoraPlugin* algo = fAlgos[c][size_t(fCurrent[c])].get();
    if (!algo) return 0.f;
    const ChannelParams& p = fCh[c];
    if (fUntilParamUpdate == 0) algo->process(knob(p.x), knob(p.y));
    float out = algo->processGraph() * fAlgoGain[fCurrent[c]];

    fSvf[c].setParameters(p.fc, p.q);
    fSvf[c].process(out);
    out = fSvf[c].output(kTypeMapping[p.ftype]);
    out = fDc[c].process(out);
    return plethora::Saturator::process(out);
}

// channel C, the module's filtered output (processBottomSection)
float PlethoraHost::renderC()
{
    const ChannelParams& p = fCh[C];
    float src;
    if (fWhiteSource) {
        src = fWhite->process();
    } else {
        src = fGrit->process(float(1.0 / kEngineRate));
    }
    fSvfC.setParameters(p.fc, p.q);
    float out = fSvfC.process(src, kTypeMapping[p.ftype]);
    // asymmetric saturator, to get those lovely even harmonics
    out = plethora::Saturator::process(out + 0.33f);
    return fDc[C].process(out);
}

void PlethoraHost::renderBlock(int16_t* lr)
{
    for (int s = 0; s < Resampler::kBlock; ++s) {
        const double end = fFrame + 1.0;
        while (fEventCount > 0 && fEvents[fEventHead].frame < end) {
            const Event& e = fEvents[fEventHead];
            if (e.on) {
                for (auto& env : fEnv) env.trigger();
                fGateHigh = fGateMode;
                fGateNote = fGateMode ? e.note : -1;
            } else if (int(e.note) == fGateNote) {
                for (auto& env : fEnv) env.release();
                fGateHigh = false;
                fGateNote = -1;
            }
            fEventHead = (fEventHead + 1) % kMaxEvents;
            --fEventCount;
        }

        float mix = 0.f;
        for (int c = 0; c < kNumChannels; ++c) {
            const ChannelParams& p = fCh[c];
            const ChannelParams& e = fSync ? fCh[A] : p; // Env Sync: A's Attack/Decay
            const float env = fEnv[c].process(e.attackStep, e.decayCoef, fGateHigh);
            if (p.level <= 0 || p.mute || fEnv[c].stage == Envelope::Idle) continue; // not computed
            const float out = c == C ? renderC() : renderTop(c);
            mix += out * env * p.gain;
        }
        if (++fUntilParamUpdate >= kParamUpdateFrames) fUntilParamUpdate = 0;

        const int16_t v = int16_t(std::lround(clamp(mix * kMixGain * kToInt16, -32767.f, 32767.f)));
        lr[2 * s] = v;
        lr[2 * s + 1] = v;
        fFrame = end;
    }
}

void PlethoraHost::process(float* outL, float* outR, uint32_t frames)
{
    rack::random::ScopedRng rngScope(*fRng);
    readParams();
    fResampler.process(outL, outR, frames, [this](int16_t* lr) { renderBlock(lr); });
    for (uint32_t i = 0; i < frames; ++i) {
        outL[i] *= kFromResampler;
        outR[i] *= kFromResampler;
    }
    fHostFrame += frames;
}

} // namespace maquina
