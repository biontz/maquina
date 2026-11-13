// DredrumHost.hpp - one plugin instance of Maquina Dredrum: the Dredrum
// firmware engine (engine/dredrum, bit-exact) plus a model of the module's
// output stage, resampled to the host rate.
//
// Signal path, as on the module:
//   engine byte (OCR1A) at 19084 Hz, held for a whole sample period
//   -> 16k/1n RC lowpass -> Sallen-Key lowpass (100k, 33k, 680p, 100p;
//      ~10.6 kHz, Q ~1.1) into an MCP6002 follower -> output
// The held samples keep their images above the engine's Nyquist; the analog
// filter only softens them, which is a large part of the 8-bit sound. The
// path runs at 8x the engine rate (exact discretisation of the filter for a
// held input) and the shared Resampler converts to the host rate.
//
// Same interface as EngineHost, so DrumPlugin can use either; the LXR-only
// controls (Key Track, Hat) are no-ops. Per oscillator Track / Octave / Semi
// (Maquina additions) set its pitch ratio: 2^((oct*12 + semi + (track ?
// note - 60 : 0)) / 12); at 1.0 the engine runs the firmware's arithmetic.
// Note-off matters here: in Gate mode the decay holds while the note that
// started it is down.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "Resampler.hpp"
#include "Timing.h"
#include "VoiceTypes.hpp"
#include "dredrum/dredrum_core.h"

namespace maquina {

class DredrumHost {
public:
    static constexpr int kOversample = 8; // filter rate = 8 x DREDRUM_RATE
    static constexpr double kFilterRate = DREDRUM_RATE * kOversample;

    DredrumHost(const VoiceDef& def, uint32_t seed);

    DredrumHost(const DredrumHost&) = delete;
    DredrumHost& operator=(const DredrumHost&) = delete;

    const VoiceDef& def() const { return fDef; }

    void setSampleRate(double hostRate);
    uint32_t latencyFrames() const { return fLatency; }

    void setParam(uint32_t index, float value);
    float getParam(uint32_t index) const;
    float defaultParam(uint32_t index) const;
    void resetToDefaults();

    // LXR-only controls (no parameters for them on this voice)
    void setNoteFollowsPitch(bool) {}
    bool noteFollowsPitch() const { return false; }
    void setHatOpen(bool) {}
    bool hatOpen() const { return false; }
    void setTempo(double) {}

    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity);
    void noteOff(uint32_t frameOffset, uint8_t note);
    void process(float* outL, float* outR, uint32_t frames);
    void resetTiming();

    // Tests: the bare engine (bytes as the firmware writes them to OCR1A).
    dredrum_state& engineState() { return fState; }
    void applyParams();

private:
    struct Event {
        double frame; // filter-rate frame
        uint8_t note;
        bool on;
    };

    void queue(uint32_t frameOffset, uint8_t note, bool on);
    void renderBlock(int16_t* lr);
    void setupFilter();

    static constexpr int kEventDelay = MAQUINA_EVENT_DELAY; // filter-rate frames
    static_assert(kEventDelay >= Resampler::kMaxLookahead, "notes would be late");
    static_assert(Resampler::kBlock % kOversample == 0, "blocks must hold whole engine samples");
    static constexpr int kMaxEvents = 512;

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    int fClickParam = -1, fModeParam = -1;
    int fParamIndex[DREDRUM_NUM_OSC][DREDRUM_NUM_PARAMS];
    int fTrackParam[DREDRUM_NUM_OSC], fOctaveParam[DREDRUM_NUM_OSC], fSemiParam[DREDRUM_NUM_OSC];
    int fMuteParam[DREDRUM_NUM_OSC]; // mute = level 0 in the engine (the oscillator keeps running)
    int fNote = 60; // last played note (key tracking)

    int paramInt(int index) const;
    void updateRatios();

    dredrum_state fState {};
    int fGateNote = -1; // note holding the gate, -1 none

    // output stage: x' = Ad x + Bd u per filter-rate step, y = x[2]
    double fAd[3][3] {}, fBd[3] {};
    double fX[3] {};

    Resampler fResampler;
    double fHostRate = 48000.0;
    uint32_t fLatency = 0;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;
    double fHostFrame = 0.0;
    double fFrame = 0.0; // filter-rate frames rendered
};

} // namespace maquina
