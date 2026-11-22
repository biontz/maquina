// StringsHost.cpp - Maquina Strings: the engine at its rate, resampled to the host
// (StringsHost.hpp).
// SPDX-License-Identifier: GPL-3.0-only
#include "StringsHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "StringsUnits.hpp"

namespace maquina {

namespace {

int findSymbol(const VoiceDef& d, const char* symbol)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, symbol)) return int(i);
    return -1;
}

// the parameters StringsHost reads, in Ix order
const char* const kSymbols[] = { "register", "shape", "detune", "tone", "vibrato", "vib_depth", "vib_delay",
                                 "wave_bank", "wave_x", "wave_y", "wave_octave", "wave_lfo_rate", "wave_lfo_depth",
                                 "wave_env_pos", "layer_detune", "wave_unison", "wave_spread", "filter_mode",
                                 "filter_cutoff", "filter_reso", "filter_env", "filter_keytrack", "blend",
                                 "blend_shift", "blend_time", "blend_velo", "keys", "attack", "decay", "sustain",
                                 "release", "velocity", "fx_mode", "fx_amount", "fx_slow", "fx_fast", "reverb",
                                 "strings_on", "strings_level", "wave_on", "wave_level" };

} // namespace

StringsHost::StringsHost(const VoiceDef& def, uint32_t seed)
    : fDef(def), fDefaults(def.numParams, 0.f), fValues(def.numParams), fEngine(new strings::Engine(seed))
{
    static_assert(sizeof kSymbols / sizeof kSymbols[0] == kNumIx, "a symbol per parameter index");
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
    for (int k = 0; k < kNumIx; ++k) fIx[k] = findSymbol(def, kSymbols[k]);
    fEngine->setParams(engineParams());
    setSampleRate(kEngineRate);
}

StringsHost::~StringsHost() = default;

void StringsHost::setSampleRate(double hostRate)
{
    fStream.setup(hostRate, kEngineRate, false);
    resetTiming();
}

void StringsHost::resetTiming()
{
    fStream.reset();
    fEventHead = fEventCount = 0;
}

// ---------------------------------------------------------------------------
// parameters

void StringsHost::setParam(uint32_t index, float value)
{
    if (index >= fDef.numParams) return;
    const VoiceParam& p = fDef.params[index];
    float lo = float(p.fwMin + p.offset), hi = float(p.fwMax + p.offset);
    if (p.kind != ParamKind::Int) {
        lo = 0.f;
        hi = p.kind == ParamKind::Toggle ? 1.f : float(p.numLabels - 1);
    }
    fValues[index].store(std::clamp(std::round(value), lo, hi), std::memory_order_relaxed);
}

float StringsHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float StringsHost::defaultParam(uint32_t index) const { return index < fDef.numParams ? fDefaults[index] : 0.f; }

void StringsHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int StringsHost::paramInt(int index) const
{
    return index < 0 ? 0 : int(std::lround(fValues[uint32_t(index)].load(std::memory_order_relaxed)));
}

strings::Params StringsHost::engineParams() const
{
    namespace su = strings;
    auto v = [this](Ix k) { return paramInt(fIx[k]); };
    auto pct = [&](Ix k) { return float(v(k)) * 0.01f; };
    su::Params p;
    p.reg = su::registerPos(v(kReg));
    p.shape = v(kShape);
    p.detuneCt = float(v(kDetune));
    p.tone = pct(kTone);
    p.vibHz = su::vibratoHz(v(kVib));
    p.vibDepth = pct(kVibDepth);
    p.vibDelay = su::vibDelaySeconds(v(kVibDelay));
    p.bank = v(kBank);
    p.x = pct(kX);
    p.y = pct(kY);
    p.octave = v(kOctave);
    p.lfoHz = su::logValue(su::kWaveLfoRate, v(kLfoRate));
    p.lfoDepth = pct(kLfoDepth);
    p.envPos = pct(kEnvPos);
    p.layerDetuneCt = float(v(kLayerDetune));
    p.unison = std::clamp(v(kUnison), 1, su::kMaxUnison);
    p.spreadCt = float(v(kSpread));
    p.filterMode = v(kFMode);
    p.cutoffHz = su::logValue(su::kFilterCutoff, v(kCutoff));
    p.reso = pct(kReso);
    p.filterEnv = pct(kFEnv);
    p.keytrack = pct(kKeytrack);
    p.blend = pct(kBlend);
    p.blendShift = pct(kShift);
    p.blendTime = su::logValue(su::kBlendTime, v(kBTime));
    p.blendVelo = pct(kBVelo);
    p.keys = v(kKeys);
    p.attack = su::logValue(su::kAttack, v(kAttack));
    p.decay = su::logValue(su::kDecay, v(kDecay));
    p.sustain = pct(kSustain);
    p.release = su::logValue(su::kRelease, v(kRelease));
    p.velocity = pct(kVelocity);
    p.fxMode = v(kFxMode);
    p.fxAmount = pct(kFxAmount);
    p.fxSlowHz = su::fxSlowHz(v(kFxSlow));
    p.fxFastHz = su::fxFastHz(v(kFxFast));
    p.reverb = pct(kReverb);
    p.stringsOn = fIx[kStringsOn] < 0 || v(kStringsOn) != 0;
    p.waveOn = fIx[kWaveOn] < 0 || v(kWaveOn) != 0;
    p.stringsLevel = std::pow(10.f, float(v(kStringsLevel)) / 20.f); // dB
    p.waveLevel = std::pow(10.f, float(v(kWaveLevel)) / 20.f);
    return p;
}

// ---------------------------------------------------------------------------
// events

void StringsHost::pushEvent(const Event& e)
{
    if (fEventCount >= kMaxEvents) return;
    fEvents[(fEventHead + fEventCount) % kMaxEvents] = e;
    ++fEventCount;
}

void StringsHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (note > 127) return;
    if (velocity == 0) return noteOff(frameOffset, note);
    pushEvent({ fStream.engineFrameAt(frameOffset), EventKind::NoteOn, note, velocity, 0 });
}

void StringsHost::noteOff(uint32_t frameOffset, uint8_t note)
{
    if (note > 127) return;
    pushEvent({ fStream.engineFrameAt(frameOffset), EventKind::NoteOff, note, 0, 0 });
}

void StringsHost::pitchBend(uint32_t frameOffset, int32_t value)
{
    pushEvent({ fStream.engineFrameAt(frameOffset), EventKind::Bend, 0, 0, value });
}

void StringsHost::controlChange(uint32_t frameOffset, uint8_t controller, uint8_t value)
{
    pushEvent({ fStream.engineFrameAt(frameOffset), EventKind::Cc, controller, value, 0 });
}

void StringsHost::applyEvent(const Event& e)
{
    switch (e.kind) {
    case EventKind::NoteOn: fEngine->noteOn(e.a, e.b); break;
    case EventKind::NoteOff: fEngine->noteOff(e.a); break;
    case EventKind::Bend: fEngine->pitchBend(2.f * float(std::clamp(e.value, -8192, 8191)) / 8192.f); break;
    case EventKind::Cc:
        if (e.a == 1) fEngine->modWheel(float(e.b) / 127.f);   // mod wheel: vibrato
        else if (e.a == 64) fEngine->sustainPedal(e.b >= 64); // sustain
        else if (e.a == 120) fEngine->allSoundOff();          // all sound off
        else if (e.a == 123) fEngine->allNotesOff();          // all notes off
        break;
    }
}

// ---------------------------------------------------------------------------
// audio

void StringsHost::runBlock(uint64_t firstFrame, float* outL, float* outR)
{
    const double blockEnd = double(firstFrame + kBlock);
    while (fEventCount > 0 && fEvents[fEventHead].frame < blockEnd) {
        applyEvent(fEvents[fEventHead]);
        fEventHead = (fEventHead + 1) % kMaxEvents;
        --fEventCount;
    }
    fEngine->setParams(engineParams());
    fEngine->render(outL, outR, kBlock);
}

void StringsHost::process(float* outL, float* outR, uint32_t frames)
{
    fStream.process(nullptr, nullptr, outL, outR, frames,
                    [this](uint64_t first, const float*, const float*, float* l, float* r) { runBlock(first, l, r); });
}

} // namespace maquina
