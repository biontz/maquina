// StringsHost.hpp - one plugin instance of Maquina Strings: the engine (StringsEngine.hpp) at its
// fixed rate (Plaits' 47872.34 Hz) in 24-frame blocks, resampled to the host rate (BlockStream).
//
// Notes, the bender, the mod wheel (vibrato), sustain and all-notes-off are queued at their
// frame and applied at the block they fall in (0.5 ms). The parameters are read once a block
// and turned into the engine's units (StringsUnits.hpp). Same interface as the other engine
// hosts (KlangHost, La50Host).
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "BlockStream.hpp"
#include "StringsEngine.hpp"
#include "VoiceTypes.hpp"

namespace maquina {

class StringsHost {
public:
    static constexpr double kEngineRate = strings::kRate;
    static constexpr int kBlock = strings::kMaxBlock;

    StringsHost(const VoiceDef& def, uint32_t seed);
    ~StringsHost();

    StringsHost(const StringsHost&) = delete;
    StringsHost& operator=(const StringsHost&) = delete;

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
    void pitchBend(uint32_t frameOffset, int32_t value); // -8192..8191: +-2 semitones
    void controlChange(uint32_t frameOffset, uint8_t controller, uint8_t value);
    void process(float* outL, float* outR, uint32_t frames);
    void resetTiming();

    // test hooks
    uint64_t underruns() const { return fStream.underruns(); }
    int activeVoices() const { return fEngine->activeVoices(); }
    strings::Params engineParams() const; // the parameters as the engine gets them

private:
    enum class EventKind : uint8_t { NoteOn, NoteOff, Bend, Cc };
    struct Event {
        double frame; // engine frame
        EventKind kind;
        uint8_t a, b;
        int32_t value;
    };

    int paramInt(int index) const;
    void pushEvent(const Event& e);
    void applyEvent(const Event& e);
    void runBlock(uint64_t firstFrame, float* outL, float* outR);

    static constexpr int kMaxEvents = 1024;

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::unique_ptr<strings::Engine> fEngine;
    BlockStream<kBlock> fStream;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;

    // parameter indices (findSymbol), in StringsHost.cpp's order
    enum Ix { kReg, kShape, kDetune, kTone, kVib, kVibDepth, kVibDelay, kBank, kX, kY, kOctave, kLfoRate,
              kLfoDepth, kEnvPos, kLayerDetune, kUnison, kSpread, kFMode, kCutoff, kReso, kFEnv, kKeytrack,
              kBlend, kShift, kBTime, kBVelo, kKeys, kAttack, kDecay, kSustain, kRelease, kVelocity, kFxMode,
              kFxAmount, kFxSlow, kFxFast, kReverb, kStringsOn, kStringsLevel, kWaveOn, kWaveLevel, kNumIx };
    int fIx[kNumIx];
};

} // namespace maquina
