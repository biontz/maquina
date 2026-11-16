// KlongHost.cpp - see KlongHost.hpp. Compiled with the Rings headers in its
// own translation unit (klong/plugins/klong*/Engine.cpp); the Rings code
// itself is libklong.a (mutable-instruments/Makefile).
// SPDX-License-Identifier: GPL-3.0-only
#include "KlongHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"
#include "stmlib/utils/random.h"

namespace maquina {

namespace {

int findSymbol(const VoiceDef& d, const char* symbol)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, symbol)) return int(i);
    return -1;
}

// rings/cv_scaler.cc channel_settings_: the pots' low-pass, per block
constexpr float kLpPot = 0.01f;
// rings/rings.cc FillBuffer()
constexpr float kNoiseGateThreshold = 0.00003f;

} // namespace

// ---------------------------------------------------------------------------
// the module's objects, as rings/rings.cc creates them

struct KlongHost::Engine {
    uint16_t reverbBuffer[32768]; // shared by Part and StringSynthPart
    rings::Part part;
    rings::StringSynthPart stringSynth;
    rings::Strummer strummer;
    uint32_t rng = 0; // stmlib::Random's state for this instance

    // smoothed pot positions (CvScaler::adc_lp_), chord hysteresis state
    float tonic = 0.f, structure = 0.f, brightness = 0.f, damping = 0.f, position = 0.f;
    int32_t chord = 0;
    float inLevel = 0.f; // the input noise gate's follower
    int polyphony = -1, model = -1;
    bool first = true;

    float in[kBlock], out[kBlock], aux[kBlock];

    // zeroed memory, as the module's objects in .bss: not every member is
    // set by Init()
    static void* operator new(size_t size) { return std::calloc(1, size); }
    static void operator delete(void* p) { std::free(p); }

    explicit Engine(uint32_t seed)
    {
        rng = seed;
        stmlib::Random::Seed(rng);
        strummer.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
        part.Init(reverbBuffer);
        stringSynth.Init(reverbBuffer);
        rng = stmlib::Random::state();
    }
};

// ---------------------------------------------------------------------------

KlongHost::KlongHost(const VoiceDef& def, uint32_t seed)
    : fDef(def), fDefaults(def.numParams, 0.f), fValues(def.numParams), fEngine(new Engine(seed))
{
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
    fIxModel = findSymbol(def, "model");
    fIxPoly = findSymbol(def, "polyphony");
    fIxCoarse = findSymbol(def, "coarse");
    fIxFine = findSymbol(def, "fine");
    fIxNotes = findSymbol(def, "note_source");
    fIxStructure = findSymbol(def, "structure");
    fIxBrightness = findSymbol(def, "brightness");
    fIxDamping = findSymbol(def, "damping");
    fIxPosition = findSymbol(def, "position");
    fIxOutput = findSymbol(def, "output");
    setSampleRate(48000.0);
}

KlongHost::~KlongHost() = default;

void KlongHost::setSampleRate(double hostRate)
{
    fStream.setup(hostRate, kEngineRate, isFx());
    resetTiming();
}

void KlongHost::resetTiming()
{
    fStream.reset();
    fEventHead = fEventCount = 0;
}

// ---------------------------------------------------------------------------
// parameters

void KlongHost::setParam(uint32_t index, float value)
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

float KlongHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float KlongHost::defaultParam(uint32_t index) const { return index < fDef.numParams ? fDefaults[index] : 0.f; }

void KlongHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int KlongHost::paramInt(int index) const
{
    return index < 0 ? 0 : int(std::lround(fValues[uint32_t(index)].load(std::memory_order_relaxed)));
}

// ---------------------------------------------------------------------------
// notes: every note-on is a strum with that note; note-offs do nothing (the
// module has no gate: strings ring out, DAMPING sets how long)

void KlongHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (note > 127 || velocity == 0 || fEventCount >= kMaxEvents) return;
    fEvents[(fEventHead + fEventCount) % kMaxEvents] = { fStream.engineFrameAt(frameOffset), note };
    ++fEventCount;
}

void KlongHost::noteOff(uint32_t, uint8_t) {}

// ---------------------------------------------------------------------------
// audio

void KlongHost::runBlock(uint64_t firstFrame, const float* inL, const float* inR, float* outL, float* outR)
{
    Engine& e = *fEngine;
    stmlib::Random::Seed(e.rng);

    // the pots (CvScaler::Read), smoothed per block
    const float tonic = 60.f + float(paramInt(fIxCoarse)) + 0.01f * float(paramInt(fIxFine));
    const float structure = float(paramInt(fIxStructure)) / 127.f;
    const float brightness = float(paramInt(fIxBrightness)) / 127.f;
    const float damping = float(paramInt(fIxDamping)) / 127.f;
    const float position = float(paramInt(fIxPosition)) / 127.f;
    if (e.first) {
        e.tonic = tonic;
        e.structure = structure;
        e.brightness = brightness;
        e.damping = damping;
        e.position = position;
        e.first = false;
    } else {
        e.tonic += kLpPot * (tonic - e.tonic);
        e.structure += kLpPot * (structure - e.structure);
        e.brightness += kLpPot * (brightness - e.brightness);
        e.damping += kLpPot * (damping - e.damping);
        e.position += kLpPot * (position - e.position);
    }
    rings::Patch patch;
    patch.structure = std::clamp(e.structure, 0.f, 0.9995f);
    patch.brightness = std::clamp(e.brightness, 0.f, 1.f);
    patch.damping = std::clamp(e.damping, 0.f, 1.f);
    patch.position = std::clamp(e.position, 0.f, 1.f);

    // polyphony (1..4; 3 is the module's hidden setting) and model
    const int poly = std::clamp(paramInt(fIxPoly), 1, 4);
    if (poly != e.polyphony) {
        e.part.set_polyphony(poly);
        e.stringSynth.set_polyphony(poly);
        e.polyphony = poly;
    }
    const int model = std::clamp(paramInt(fIxModel), 0, 2 * kNumResonators - 1);
    const bool easterEgg = model >= kNumResonators;
    if (model != e.model) {
        if (easterEgg)
            e.stringSynth.set_fx(rings::FxType(model - kNumResonators));
        else
            e.part.set_model(rings::ResonatorModel(model));
        e.model = model;
    }

    // notes, strums
    rings::PerformanceState ps;
    const bool midiNotes = !isFx() || paramInt(fIxNotes) == 1;
    ps.internal_exciter = !isFx();
    ps.internal_note = !midiNotes;
    ps.internal_strum = !midiNotes;
    ps.strum = false;
    if (midiNotes) {
        // a waiting note-on that is due, unless the strummer would still
        // inhibit it (then it waits: strummed 10 ms after the previous one)
        if (fInhibit > 0) {
            --fInhibit;
        } else if (fEventCount > 0 && fEvents[fEventHead].frame < double(firstFrame + kBlock)) {
            fNote = fEvents[fEventHead].note;
            fEventHead = (fEventHead + 1) % kMaxEvents;
            --fEventCount;
            ps.strum = true;
            fInhibit = kStrumInhibit;
        }
    } else {
        fEventHead = fEventCount = 0; // MIDI notes ignored
    }
    ps.note = midiNotes ? float(fNote - 60) : 0.f;
    ps.tonic = e.tonic;
    ps.fm = 0.f;
    // chord: STRUCTURE with the module's hysteresis
    const float chord = e.structure * float(rings::kNumChords - 1);
    const float hysteresis = chord - float(e.chord) > 0.f ? -0.1f : 0.1f;
    e.chord = std::clamp(int32_t(chord + hysteresis + 0.5f), int32_t(0), int32_t(rings::kNumChords - 1));
    ps.chord = e.chord;

    // FillBuffer(): input (noise gate), strummer, part / string synth
    for (int i = 0; i < kBlock; ++i) {
        const float s = isFx() ? 0.5f * (inL[i] + inR[i]) : 0.f;
        if (easterEgg) {
            e.in[i] = s;
        } else {
            const float error = s * s - e.inLevel;
            e.inLevel += error * (error > 0.f ? 0.1f : 0.0001f);
            const float gain = e.inLevel <= kNoiseGateThreshold ? (1.f / kNoiseGateThreshold) * e.inLevel : 1.f;
            e.in[i] = gain * s;
        }
    }
    if (easterEgg) {
        e.strummer.Process(nullptr, kBlock, &ps);
        e.stringSynth.Process(ps, patch, e.in, e.out, e.aux, kBlock);
    } else {
        e.strummer.Process(e.in, kBlock, &ps);
        e.part.Process(ps, patch, e.in, e.out, e.aux, kBlock);
    }
    if (ps.strum) ++fStrums;

    // the codec's 16-bit output (Clip16), ODD left, EVEN right
    const bool mono = paramInt(fIxOutput) == 1;
    constexpr float kMax = 32767.f / 32768.f;
    for (int i = 0; i < kBlock; ++i) {
        const float odd = std::clamp(e.out[i], -1.f, kMax);
        const float even = std::clamp(e.aux[i], -1.f, kMax);
        if (mono) {
            outL[i] = outR[i] = std::clamp(odd + even, -1.f, kMax); // as VCV Rack mixes them
        } else {
            outL[i] = odd;
            outR[i] = even;
        }
    }
    e.rng = stmlib::Random::state();
}

void KlongHost::process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames)
{
    fStream.process(inL, inR, outL, outR, frames,
                    [this](uint64_t first, const float* il, const float* ir, float* l, float* r) {
                        runBlock(first, il, ir, l, r);
                    });
}

} // namespace maquina
