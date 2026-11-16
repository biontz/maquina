// KlongHost.hpp - one plugin instance of Maquina Klong or Klong FX: the
// Mutable Instruments Rings resonator (third_party/eurorack) at the module's
// 48 kHz in 24-frame blocks, resampled from/to the host rate (BlockStream).
//
// Per block, as the module's FillBuffer() + CvScaler::Read(): the knobs are
// smoothed with the module's pot coefficients into rings::Patch, the chord
// comes from STRUCTURE with the module's hysteresis, the strummer and then
// rings::Part (or the string synth: the "Disastrous Peace" easter egg) run.
// ODD goes to the left output, EVEN to the right (Output Mono: both mixed,
// as the module does with one jack).
//
//   Maquina Klong (instrument): the internal exciter; a MIDI note sets the
//     pitch (V/OCT around C4) and strums. Notes closer than the strummer's
//     10 ms inhibit time are strummed one after the other, 10 ms apart, so
//     chords fill the polyphony instead of being dropped.
//   Maquina Klong FX (audio effect): the input (L+R, the module's mono IN,
//     with its noise gate) excites the resonator. Notes Internal: as the
//     module with nothing in V/OCT and STRUM, the input's onsets strum and
//     Coarse/Fine set the pitch; Notes MIDI: MIDI notes set pitch and strum.
//
// Same interface as the other engine hosts, plus process() with audio input.
// The Rings code is compiled in its own translation unit
// (klong/plugins/klong*/Engine.cpp + libklong.a); this header stays free of
// its names.
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

class KlongHost {
public:
    static constexpr double kEngineRate = 48000.0;
    static constexpr int kBlock = 24;      // rings::kMaxBlockSize
    static constexpr int kNumResonators = 6; // rings::RESONATOR_MODEL_LAST; then 6 string-synth FX

    KlongHost(const VoiceDef& def, uint32_t seed);
    ~KlongHost();

    KlongHost(const KlongHost&) = delete;
    KlongHost& operator=(const KlongHost&) = delete;

    const VoiceDef& def() const { return fDef; }
    bool isFx() const { return fDef.voice == kKlongFxVoice; }

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
    // inL / inR may be null (silence): Maquina Klong has no audio input
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames);
    void process(float* outL, float* outR, uint32_t frames) { process(nullptr, nullptr, outL, outR, frames); }
    void resetTiming();

    // test hooks
    uint64_t underruns() const { return fStream.underruns(); }
    uint32_t strums() const { return fStrums; }

    struct Engine; // the module's objects (KlongHost.cpp)

private:
    struct Event {
        double frame; // engine frame
        uint8_t note;
    };

    int paramInt(int index) const;
    void runBlock(uint64_t firstFrame, const float* inL, const float* inR, float* outL, float* outR);

    static constexpr int kMaxEvents = 1024;
    static constexpr int kStrumInhibit = 20; // Strummer: 0.01 s at the block rate

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::unique_ptr<Engine> fEngine;
    BlockStream<kBlock> fStream;

    // note-ons waiting to be strummed (in time order)
    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;
    int fNote = 60;
    int fInhibit = 0;
    uint32_t fStrums = 0;

    // parameter indices
    int fIxModel = -1, fIxPoly = -1, fIxCoarse = -1, fIxFine = -1, fIxNotes = -1, fIxStructure = -1,
        fIxBrightness = -1, fIxDamping = -1, fIxPosition = -1, fIxOutput = -1;
};

} // namespace maquina
