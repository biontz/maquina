// La50Host.hpp - one plugin instance of Maquina LA-50: the engine (La50Engine)
// and its effects (La50Fx) at 32 kHz, resampled from / to the host rate
// (BlockStream). The plugin's parameters are the D-50 patch's bytes
// (VoiceParam::fwPar = the byte's index in the 448-byte patch; the value shown
// is the byte plus the parameter's display offset).
//
// MIDI: notes (with their channel: the Separate key modes play the Lower tone
// on channel 2), pitch bend, CC 1 (the lever's modulation), CC 64 hold, CC 65
// portamento, channel aftertouch, CC 120 / 123 all notes off. Events take
// effect at the 32-frame block they fall in (1 ms); a note-off waits for the
// next block, so a note always sounds.
//
// Same interface as the other engine hosts.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "BlockStream.hpp"
#include "VoiceTypes.hpp"

namespace la50 {
class Engine;
class Fx;
}

namespace maquina {

class La50Host {
public:
    static constexpr double kEngineRate = 32000.0;
    static constexpr int kBlock = 32;

    La50Host(const VoiceDef& def, uint32_t seed);
    ~La50Host();
    La50Host(const La50Host&) = delete;
    La50Host& operator=(const La50Host&) = delete;

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

    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity, uint8_t channel = 0);
    void noteOff(uint32_t frameOffset, uint8_t note, uint8_t channel = 0);
    void pitchBend(uint32_t frameOffset, int32_t bend);
    void controlChange(uint32_t frameOffset, uint8_t cc, uint8_t value);
    void aftertouch(uint32_t frameOffset, uint8_t value);
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames);
    void process(float* outL, float* outR, uint32_t frames) { process(nullptr, nullptr, outL, outR, frames); }
    void resetTiming();

    uint64_t underruns() const { return fStream.underruns(); }
    int activeVoices() const;

private:
    enum EventType : uint8_t { kNoteOn, kNoteOff, kBend, kCc, kAfter };
    struct Event {
        double frame;
        EventType type;
        uint8_t a, channel;
        int16_t b;
    };
    void push(uint32_t frameOffset, EventType type, uint8_t a, int16_t b, uint8_t channel);
    void applyParams();
    void runBlock(uint64_t firstFrame, float* outL, float* outR);

    static constexpr int kMaxEvents = 1024;
    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::unique_ptr<la50::Engine> fEngine;
    std::unique_ptr<la50::Fx> fFx;
    BlockStream<kBlock> fStream;
    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;
};

} // namespace maquina
