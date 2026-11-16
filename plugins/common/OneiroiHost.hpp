// OneiroiHost.hpp - one plugin instance of Maquina Oneiroi or Oneiroi FX:
// the Befaco Oneiroi's engine (third_party/oneiroi, patch 1.2.2) at the
// module's 48 kHz in 32-frame blocks, resampled from/to the host rate.
//
// The module's UI layer (Ui.h, ParamController.h: knobs, SHIFT/MOD/CV pages,
// catch-up, saving, LEDs) is replaced by the plugin parameters. Per block,
// as the module's Ui::Poll(): knob and fader values are smoothed with the
// module's own coefficients, faders get its curves, pitch / octave / unison /
// V/Oct are computed the same way. The modulation goes to the filter cutoff
// at amount 0.5 (the module's default mapping), the CV inputs read 0.
//
// Maquina additions:
//   - MIDI note = V/Oct: the note relative to C4 (60) is added to PITCH and
//     OCTAVE, with the module's V/Oct glide; mono, last note priority.
//   - Maquina Oneiroi: an ADSR on the sources (looper + oscillators) before
//     the effects, opened by notes; DRONE holds it open (= the module).
//   - SYNC from the host transport: a sync pulse every beat / bar / 4 bars
//     while playing (the module's SYNC IN).
//   - Record / Clear as parameters; Oneiroi records its own output (POST),
//     Oneiroi FX the input or the output (PRE/POST).
//
// Same interface as the other engine hosts, plus process() with audio input.
// Compiled with the vendored engine in its own translation unit
// (plugins/oneiroi*/Engine.cpp); this header stays free of OWL names.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "StreamResampler.hpp"
#include "VoiceTypes.hpp"

namespace maquina {

class OneiroiHost {
public:
    static constexpr double kEngineRate = 48000.0;
    static constexpr int kBlock = 32; // the module's block size (block rate 1500 Hz)

    OneiroiHost(const VoiceDef& def, uint32_t seed);
    ~OneiroiHost();

    OneiroiHost(const OneiroiHost&) = delete;
    OneiroiHost& operator=(const OneiroiHost&) = delete;

    const VoiceDef& def() const { return fDef; }
    bool isFx() const { return fDef.voice == kOneiroiFxVoice; }

    void setSampleRate(double hostRate);
    uint32_t latencyFrames() const { return fLatency; }

    void setParam(uint32_t index, float value);
    float getParam(uint32_t index) const;
    float defaultParam(uint32_t index) const;
    void resetToDefaults();

    void setNoteFollowsPitch(bool) {}
    bool noteFollowsPitch() const { return false; }
    void setTempo(double) {}

    // host transport for SYNC, before process(): playing, position in beats
    // at the block's first frame, beats per bar, beats per host frame
    void setTransport(bool playing, double beat, double beatsPerBar, double beatsPerFrame);

    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity);
    void noteOff(uint32_t frameOffset, uint8_t note);
    // inL / inR may be null (silence): Maquina Oneiroi has no audio input
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames);
    void process(float* outL, float* outR, uint32_t frames) { process(nullptr, nullptr, outL, outR, frames); }
    void resetTiming();

    // test hooks: the output FIFO never ran dry (should hold for any block
    // size); the module's clock (external = following SYNC) and its period
    uint64_t underruns() const { return fUnderruns; }
    bool clockExternal() const;
    double clockPeriodSeconds() const;

    struct Engine; // the module's objects (OneiroiHost.cpp)

private:
    enum class EventKind : uint8_t { NoteOn, NoteOff, Sync };
    struct Event {
        double frame; // engine frame
        EventKind kind;
        uint8_t note;
    };

    // Maquina's ADSR, per engine sample
    struct Envelope {
        enum Stage : uint8_t { Idle, Attack, Decay, Sustain, Release } stage = Idle;
        float value = 0.f;
    };

    int paramInt(int index) const;
    void pushEvent(double frame, EventKind kind, uint8_t note);
    void engineFrame(float l, float r);
    void runBlock();
    void poll();
    void applyEvent(const Event& e);

    static constexpr int kMaxEvents = 1024;
    static constexpr uint32_t kFifoFrames = 1 << 15;
    static constexpr uint32_t kChunk = 4096; // host frames per pipeline pass

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::unique_ptr<Engine> fEngine;

    StreamResampler fIn, fOut;
    double fHostRate = 48000.0;
    uint32_t fLatency = 0;

    // engine input block being filled, and its position in engine frames
    float fBlockL[kBlock] = {}, fBlockR[kBlock] = {};
    int fBlockFill = 0;
    uint64_t fEngineFrame = 0; // first frame of the block being filled

    // output FIFO (host rate), primed with fLatency frames of silence
    std::vector<float> fFifoL, fFifoR;
    uint32_t fFifoRead = 0, fFifoCount = 0;
    uint64_t fUnderruns = 0;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;
    uint64_t fHostFrame = 0;

    // notes (mono, last note priority) and the envelope
    uint8_t fHeld[128] = {};
    uint8_t fStack[128] = {};
    int fStackSize = 0;
    int fNote = 60;
    bool fGate = false;
    Envelope fEnv;

    // transport sync
    bool fPlaying = false;
    double fBeat = 0.0, fBeatsPerBar = 4.0, fBeatsPerFrame = 0.0;
    int fSyncHigh = 0; // blocks the SYNC IN pulse stays high
    bool fLastClear = false;

    // parameter indices (-1: this plugin has none)
    int fIxOctave = -1, fIxRecord = -1, fIxSource = -1, fIxClear = -1, fIxDrone = -1;
    int fIxAttack = -1, fIxDecay = -1, fIxSustain = -1, fIxRelease = -1, fIxSync = -1;
};

} // namespace maquina
