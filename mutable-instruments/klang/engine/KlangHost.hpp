// KlangHost.hpp - one plugin instance of Maquina Klang: the Mutable
// Instruments Plaits voice (third_party/eurorack, firmware 1.2, 24 models)
// at the module's 47872 Hz in 12-frame blocks, resampled to the host rate
// (BlockStream).
//
// Per block, as the module's Ui::Poll() + FillBuffer(): the knobs are
// smoothed with the module's pot coefficients and written to plaits::Patch;
// plaits::Modulations get the MIDI note as V/OCT (with the module's pitch
// low-pass), the gate as TRIGGER (patched: notes ping the low-pass gate and
// the internal decay envelope) and, in LPG mode Velocity, the velocity as
// LEVEL while the note is held. The FM / TIMBRE / MORPH CV inputs are
// unpatched, so their attenuverters set the internal envelope's amount (the
// module's normalisation).
//
// Maquina additions: Coarse/Fine instead of the FREQUENCY knob's octave
// ranges, Key Track (off: notes trigger without changing the pitch), and
// the Output choice (OUT, AUX, both, or their mix).
//
// Mono, last-note priority; a note played while another is held retriggers.
// Same interface as the other engine hosts. The Plaits code is compiled in
// its own translation unit (klang/plugins/klang/Engine.cpp + libklang.a);
// this header stays free of its names.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "BlockStream.hpp"
#include "VoiceTypes.hpp"

namespace maquina {

class KlangHost {
public:
    // the module's real sample rate (plaits::kCorrectedSampleRate: its DAC
    // runs at 47872.34 Hz, and the code tunes for that), so pitch and timing
    // match the hardware
    static constexpr double kEngineRate = 47872.34;
    static constexpr int kBlock = 12; // plaits::kBlockSize
    static constexpr int kNumModels = 24;

    KlangHost(const VoiceDef& def, uint32_t seed);
    ~KlangHost();

    KlangHost(const KlangHost&) = delete;
    KlangHost& operator=(const KlangHost&) = delete;

    const VoiceDef& def() const { return fDef; }

    void setSampleRate(double hostRate);
    uint32_t latencyFrames() const { return fStream.latency(); }

    void setParam(uint32_t index, float value);
    float getParam(uint32_t index) const;
    float defaultParam(uint32_t index) const;
    void resetToDefaults();

    void setNoteFollowsPitch(bool) {}
    bool noteFollowsPitch() const { return false; }
    void setTempo(double) {}
    void setTransport(bool, double, double, double) {}

    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity);
    void noteOff(uint32_t frameOffset, uint8_t note);
    void process(float* outL, float* outR, uint32_t frames);
    void resetTiming();

    // test hooks
    uint64_t underruns() const { return fStream.underruns(); }
    int activeModel() const; // the model the engine plays (parameter value)

    // parameter value of the Model parameter <-> plaits engine index: the
    // list starts with the original 16 models (engines 8..23), then the 8
    // of firmware 1.1/1.2 (engines 0..7)
    static int engineOfModel(int model) { return (model + 8) % kNumModels; }
    static int modelOfEngine(int engine) { return (engine + 16) % kNumModels; }

    struct Engine; // the module's objects (KlangHost.cpp)

private:
    enum class EventKind : uint8_t { NoteOn, NoteOff };
    struct Event {
        double frame; // engine frame
        EventKind kind;
        uint8_t note, velocity;
    };

    int paramInt(int index) const;
    void pushEvent(double frame, EventKind kind, uint8_t note, uint8_t velocity);
    void applyEvent(const Event& e);
    void runBlock(uint64_t firstFrame, float* outL, float* outR);

    static constexpr int kMaxEvents = 1024;

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::unique_ptr<Engine> fEngine;
    BlockStream<kBlock> fStream;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;

    // notes: mono, last note priority
    uint8_t fHeld[128] = {};
    uint8_t fStack[128] = {};
    int fStackSize = 0;
    int fNote = 60;
    uint8_t fVelocity = 100;
    bool fGate = false;
    int fRetrigger = 0; // blocks the TRIGGER input stays low before a new note

    // parameter indices
    int fIxModel = -1, fIxCoarse = -1, fIxFine = -1, fIxTrack = -1, fIxHarmonics = -1, fIxTimbre = -1,
        fIxMorph = -1, fIxFmAmt = -1, fIxTimbreAmt = -1, fIxMorphAmt = -1, fIxDecay = -1, fIxColour = -1,
        fIxLpgMode = -1, fIxOutput = -1;
};

} // namespace maquina
