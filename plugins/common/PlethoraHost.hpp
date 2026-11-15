// PlethoraHost.hpp - one plugin instance of Maquina Plethora: the Befaco
// Noise Plethora as ported to VCV Rack (third_party/noise-plethora), three
// channels mixed into one output, resampled to the host rate.
//
//   A, B   algorithm (60: the module's 3 banks + the VCV port's 3) with X/Y,
//          gain per algorithm, multimode filter (2nd-order SVF), saturator,
//          20 Hz DC blocker - exactly the VCV module's chain
//   C      gritty or white noise -> 4th-order SVF -> asymmetric saturator ->
//          DC blocker, as the module's filtered C output
//
// Everything runs at 44100 Hz like the Teensy 4 (the algorithms are written
// for it); the shared Resampler converts to the host rate, so the sound is
// the same at any host rate. Algorithm parameters are updated every 128
// samples (2.9 ms) like the module.
//
// Maquina additions (the module has no envelope): per channel an envelope
// (Attack, Decay; Decay at max holds), Level and Mute; Env Sync makes B and
// C use A's Attack/Decay; Mode Trig/Gate (Gate: hold while the note is down).
// A channel at Level 0, muted or with its envelope finished is not computed,
// so one channel (Machinista's profile) costs one channel of CPU.
//
// Same interface as EngineHost / DredrumHost. Compiled in its own translation
// unit with the vendored port (plugins/plethora/Engine.cpp), which brings
// VCV Rack's names (engine/plethora/compat/rack.hpp); this header stays clean.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Resampler.hpp"
#include "Timing.h"
#include "VoiceTypes.hpp"
#include "plethora/plethora_dsp.hpp"

class NoisePlethoraPlugin;
class AudioSynthNoiseWhiteFloat;
class AudioSynthNoiseGritFloat;
namespace rack { namespace random { struct Xoshiro128Plus; } }

namespace maquina {

class PlethoraHost {
public:
    static constexpr double kEngineRate = 44100.0;
    static constexpr int kNumAlgorithms = 60;
    static constexpr int kNumChannels = 3;

    PlethoraHost(const VoiceDef& def, uint32_t seed);
    ~PlethoraHost();

    PlethoraHost(const PlethoraHost&) = delete;
    PlethoraHost& operator=(const PlethoraHost&) = delete;

    const VoiceDef& def() const { return fDef; }

    void setSampleRate(double hostRate);
    uint32_t latencyFrames() const { return fLatency; }

    void setParam(uint32_t index, float value);
    float getParam(uint32_t index) const;
    float defaultParam(uint32_t index) const;
    void resetToDefaults();

    void setNoteFollowsPitch(bool) {}
    bool noteFollowsPitch() const { return false; }
    void setTempo(double) {}

    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity);
    void noteOff(uint32_t frameOffset, uint8_t note);
    void process(float* outL, float* outR, uint32_t frames);
    void resetTiming();

private:
    struct Event {
        double frame; // engine frame
        uint8_t note;
        bool on;
    };

    // Maquina's envelope: linear attack, exponential decay (-60 dB at Decay)
    struct Envelope {
        enum Stage { Idle, Attack, Hold, Decay } stage = Idle;
        float value = 0.f;
        void trigger() { stage = Attack; }
        void release()
        {
            if (stage == Attack || stage == Hold) stage = Decay;
        }
        float process(float attackStep, float decayCoef, bool gate);
    };

    enum Ch { A = 0, B = 1, C = 2 };
    struct ChannelParams {
        int algo = 0, x = 0, y = 0, ftype = 0, cutoff = 127, reso = 0, attack = 0, decay = 0, level = 0;
        bool mute = false;
        // derived once per host block
        float fc = 0.49f, q = 0.7f, attackStep = 1.f, decayCoef = 0.f, gain = 0.f;
    };

    int paramInt(int index) const;
    void readParams();
    void renderBlock(int16_t* lr);
    float renderTop(int ch);
    float renderC();

    static constexpr int kEventDelay = MAQUINA_EVENT_DELAY;
    static_assert(kEventDelay >= Resampler::kMaxLookahead, "notes would be late");
    static constexpr int kMaxEvents = 512;
    static constexpr int kParamUpdateFrames = 128; // the module: every 2.9 ms

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    int fIndex[kNumChannels][10];
    int fSourceParam = -1, fGritParam = -1, fSyncParam = -1, fModeParam = -1;

    // per channel A/B: every algorithm, created up front (switching never allocates)
    std::vector<std::shared_ptr<NoisePlethoraPlugin>> fAlgos[2];
    float fAlgoGain[kNumAlgorithms] {};
    int fCurrent[2] = { -1, -1 };
    std::unique_ptr<AudioSynthNoiseWhiteFloat> fWhite;
    std::unique_ptr<AudioSynthNoiseGritFloat> fGrit;
    plethora::StateVariableFilter2ndOrder fSvf[2];
    plethora::StateVariableFilter4thOrder fSvfC;
    plethora::DCBlocker fDc[kNumChannels];

    ChannelParams fCh[kNumChannels];
    bool fWhiteSource = false, fSync = false, fGateMode = false;
    int fGrit01 = 64;
    Envelope fEnv[kNumChannels];
    bool fGateHigh = false;
    int fGateNote = -1;
    int fUntilParamUpdate = 0;
    std::unique_ptr<rack::random::Xoshiro128Plus> fRng; // this instance's random stream

    Resampler fResampler;
    double fHostRate = 48000.0;
    uint32_t fLatency = 0;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;
    double fHostFrame = 0.0;
    double fFrame = 0.0;
};

} // namespace maquina
