// KlangHost.cpp - see KlangHost.hpp. Compiled with the Plaits headers in
// its own translation unit (klang/plugins/klang/Engine.cpp); the Plaits code
// itself is libklang.a (mutable-instruments/Makefile).
// SPDX-License-Identifier: GPL-3.0-only
#include "KlangHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "plaits/dsp/voice.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

namespace maquina {

namespace {

int findSymbol(const VoiceDef& d, const char* symbol)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, symbol)) return int(i);
    return -1;
}

// the module's pot low-pass coefficients (plaits/ui.cc Ui::Init), per block
constexpr float kLpFrequency = 0.005f, kLpHarmonics = 0.005f, kLpTimbre = 0.01f, kLpMorph = 0.01f,
                kLpAttenuverter = 0.005f;

} // namespace

// ---------------------------------------------------------------------------
// the module's objects, as plaits/plaits.cc creates them

struct KlangHost::Engine {
    char sharedBuffer[16384]; // plaits.cc shared_buffer: the engines' RAM
    plaits::Voice voice;
    plaits::Patch patch {};
    plaits::Modulations modulations {};
    plaits::Voice::Frame frames[kBlock];

    // smoothed pot positions (PotController::value_), and Ui's pitch_lp_
    float note = 0.f, harmonics = 0.f, timbre = 0.f, morph = 0.f;
    float fmAmt = 0.f, timbreAmt = 0.f, morphAmt = 0.f;
    float pitchLp = 0.f;
    bool first = true;
    uint32_t rng = 0; // stmlib::Random's state for this instance (one global in the code)

    // zeroed memory, as the module's objects in .bss: not every member is
    // set by Init()
    static void* operator new(size_t size) { return std::calloc(1, size); }
    static void operator delete(void* p) { std::free(p); }

    explicit Engine(uint32_t seed)
    {
        stmlib::Random::Seed(seed);
        stmlib::BufferAllocator allocator(sharedBuffer, sizeof(sharedBuffer));
        voice.Init(&allocator);
        rng = stmlib::Random::state();
    }
};

// ---------------------------------------------------------------------------

KlangHost::KlangHost(const VoiceDef& def, uint32_t seed)
    : fDef(def), fDefaults(def.numParams, 0.f), fValues(def.numParams), fEngine(new Engine(seed))
{
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
    fIxModel = findSymbol(def, "model");
    fIxCoarse = findSymbol(def, "coarse");
    fIxFine = findSymbol(def, "fine");
    fIxTrack = findSymbol(def, "key_track");
    fIxHarmonics = findSymbol(def, "harmonics");
    fIxTimbre = findSymbol(def, "timbre");
    fIxMorph = findSymbol(def, "morph");
    fIxFmAmt = findSymbol(def, "fm_env");
    fIxTimbreAmt = findSymbol(def, "timbre_env");
    fIxMorphAmt = findSymbol(def, "morph_env");
    fIxDecay = findSymbol(def, "lpg_decay");
    fIxColour = findSymbol(def, "lpg_colour");
    fIxLpgMode = findSymbol(def, "lpg_mode");
    fIxOutput = findSymbol(def, "output");
    setSampleRate(kEngineRate);
}

KlangHost::~KlangHost() = default;

void KlangHost::setSampleRate(double hostRate)
{
    fStream.setup(hostRate, kEngineRate, false);
    resetTiming();
}

void KlangHost::resetTiming()
{
    fStream.reset();
    fEventHead = fEventCount = 0;
}

int KlangHost::activeModel() const { return modelOfEngine(fEngine->voice.active_engine()); }

// ---------------------------------------------------------------------------
// parameters

void KlangHost::setParam(uint32_t index, float value)
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

float KlangHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float KlangHost::defaultParam(uint32_t index) const { return index < fDef.numParams ? fDefaults[index] : 0.f; }

void KlangHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int KlangHost::paramInt(int index) const
{
    return index < 0 ? 0 : int(std::lround(fValues[uint32_t(index)].load(std::memory_order_relaxed)));
}

// ---------------------------------------------------------------------------
// notes

void KlangHost::pushEvent(double frame, EventKind kind, uint8_t note, uint8_t velocity)
{
    if (fEventCount >= kMaxEvents) return;
    fEvents[(fEventHead + fEventCount) % kMaxEvents] = { frame, kind, note, velocity };
    ++fEventCount;
}

void KlangHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (note > 127) return;
    if (velocity == 0) return noteOff(frameOffset, note);
    pushEvent(fStream.engineFrameAt(frameOffset), EventKind::NoteOn, note, velocity);
}

void KlangHost::noteOff(uint32_t frameOffset, uint8_t note)
{
    if (note > 127) return;
    pushEvent(fStream.engineFrameAt(frameOffset), EventKind::NoteOff, note, 0);
}

void KlangHost::applyEvent(const Event& ev)
{
    if (ev.kind == EventKind::NoteOn) {
        if (fHeld[ev.note]) { // repeated note-on: move it to the top
            int k = 0;
            while (k < fStackSize && fStack[k] != ev.note) ++k;
            for (; k + 1 < fStackSize; ++k) fStack[k] = fStack[k + 1];
            --fStackSize;
        }
        fHeld[ev.note] = 1;
        fStack[fStackSize++] = ev.note;
        fNote = ev.note;
        fVelocity = ev.velocity;
        // a new note while the gate is high: TRIGGER low for a block first,
        // so the module sees a rising edge
        if (fGate) fRetrigger = 1;
        fGate = true;
        return;
    }
    if (!fHeld[ev.note]) return;
    fHeld[ev.note] = 0;
    for (int k = 0; k < fStackSize; ++k)
        if (fStack[k] == ev.note) {
            for (; k + 1 < fStackSize; ++k) fStack[k] = fStack[k + 1];
            --fStackSize;
            break;
        }
    if (fStackSize > 0)
        fNote = fStack[fStackSize - 1]; // back to the previous note, legato (no new trigger)
    else
        fGate = false;
}

// ---------------------------------------------------------------------------
// audio

void KlangHost::runBlock(uint64_t firstFrame, float* outL, float* outR)
{
    Engine& e = *fEngine;
    stmlib::Random::Seed(e.rng);
    // notes up to the end of this block (block-accurate, 0.25 ms)
    const double end = double(firstFrame + kBlock);
    while (fEventCount > 0 && fEvents[fEventHead].frame < end) {
        applyEvent(fEvents[fEventHead]);
        fEventHead = (fEventHead + 1) % kMaxEvents;
        --fEventCount;
    }

    // Ui::Poll(): the pots, smoothed per block
    const float note = 60.f + float(paramInt(fIxCoarse)) + 0.01f * float(paramInt(fIxFine));
    const float harmonics = float(paramInt(fIxHarmonics)) / 127.f;
    const float timbre = float(paramInt(fIxTimbre)) / 127.f;
    const float morph = float(paramInt(fIxMorph)) / 127.f;
    const float fmAmt = std::clamp(float(paramInt(fIxFmAmt)) / 63.f, -1.f, 1.f);
    const float timbreAmt = std::clamp(float(paramInt(fIxTimbreAmt)) / 63.f, -1.f, 1.f);
    const float morphAmt = std::clamp(float(paramInt(fIxMorphAmt)) / 63.f, -1.f, 1.f);
    if (e.first) {
        e.note = note;
        e.harmonics = harmonics;
        e.timbre = timbre;
        e.morph = morph;
        e.fmAmt = fmAmt;
        e.timbreAmt = timbreAmt;
        e.morphAmt = morphAmt;
        e.first = false;
    } else {
        ONE_POLE(e.note, note, kLpFrequency);
        ONE_POLE(e.harmonics, harmonics, kLpHarmonics);
        ONE_POLE(e.timbre, timbre, kLpTimbre);
        ONE_POLE(e.morph, morph, kLpMorph);
        ONE_POLE(e.fmAmt, fmAmt, kLpAttenuverter);
        ONE_POLE(e.timbreAmt, timbreAmt, kLpAttenuverter);
        ONE_POLE(e.morphAmt, morphAmt, kLpAttenuverter);
    }

    plaits::Patch& p = e.patch;
    p.engine = engineOfModel(std::clamp(paramInt(fIxModel), 0, kNumModels - 1));
    p.note = e.note;
    p.harmonics = e.harmonics;
    p.timbre = e.timbre;
    p.morph = e.morph;
    p.frequency_modulation_amount = e.fmAmt;
    p.timbre_modulation_amount = e.timbreAmt;
    p.morph_modulation_amount = e.morphAmt;
    p.decay = float(paramInt(fIxDecay)) / 127.f;
    p.lpg_colour = float(paramInt(fIxColour)) / 127.f;

    // the CV inputs: V/OCT = the note around C4 (Key Track), TRIGGER = the
    // gate, LEVEL = the velocity (LPG mode Velocity) while the note is held
    plaits::Modulations& m = e.modulations;
    const float vOct = paramInt(fIxTrack) ? float(fNote - 60) : 0.f;
    ONE_POLE(e.pitchLp, vOct, 0.7f);
    m.note = e.pitchLp;
    const bool high = fGate && fRetrigger == 0;
    if (fRetrigger > 0) --fRetrigger;
    m.trigger = high ? 1.f : 0.f;
    m.trigger_patched = true;
    m.level_patched = paramInt(fIxLpgMode) == 1;
    m.level = m.level_patched && fGate ? float(fVelocity) / 127.f : 0.f;

    e.voice.Render(p, m, e.frames, kBlock);

    // the module's outputs (inverting op-amp, as VCV Rack has them)
    const int mode = paramInt(fIxOutput);
    for (int i = 0; i < kBlock; ++i) {
        const float out = -float(e.frames[i].out) / 32768.f;
        const float aux = -float(e.frames[i].aux) / 32768.f;
        switch (mode) {
        case 1: outL[i] = outR[i] = aux; break;                 // Aux
        case 2: outL[i] = out; outR[i] = aux; break;            // Out + Aux
        case 3: outL[i] = outR[i] = 0.5f * (out + aux); break;  // Mix
        default: outL[i] = outR[i] = out; break;                // Out
        }
    }
    e.rng = stmlib::Random::state();
}

void KlangHost::process(float* outL, float* outR, uint32_t frames)
{
    fStream.process(nullptr, nullptr, outL, outR, frames,
                    [this](uint64_t first, const float*, const float*, float* l, float* r) { runBlock(first, l, r); });
}

} // namespace maquina
