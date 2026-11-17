// Bl101Host.hpp - one plugin instance of Maquina BL-101 or BL-101 FX, around the
// C++ port of S1Vave's engine (Bl101Dsp.hpp, libbl101.a) at its 44100 Hz,
// resampled from/to the host rate (BlockStream).
//
//   Maquina BL-101 (instrument): the 8-voice synth, in 16-frame control
//     blocks (the firmware's CTL). Notes, pitch bend, the mod wheel (CC 1,
//     the firmware's vibrato) and the sustain pedal (CC 64) take effect at
//     the block they fall in (0.36 ms); a note-off waits for the next block,
//     so a note always sounds. CC 120 / 123: all notes off. The LFO's tempo
//     sync follows the host tempo. The output is the firmware's with its
//     effects off.
//   Maquina BL-101 FX (audio effect): the firmware's effects on a stereo
//     input: chorus, ping-pong delay (note values follow the host tempo),
//     plate reverb, the rate-reduce / distortion slot, and the firmware's
//     final softclip. In 64-frame blocks.
//
// Parameters map to the firmware's patch bytes through VoiceParam::fwPar
// (tools/gen_params.py bl101_params): a P_* index, or kDrawBase + step (the
// OSC DRAW table, shown -128..127 = the int16 / 256), kMaskBase + source
// (the CHOP mask: Custom, or one of the firmware's 6), kBitsLo/HiBase +
// source (the Custom mask's bytes).
//
// Same interface as the other engine hosts.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "BlockStream.hpp"
#include "VoiceTypes.hpp"

namespace bl101 {
class Synth;
class Fx;
}

namespace maquina {

class Bl101Host {
public:
    static constexpr double kEngineRate = 44100.0;
    static constexpr int kBlock = 16;    // the synth: one control block
    static constexpr int kFxBlock = 64;  // the effects

    // VoiceParam::fwPar of the parameters that are not patch bytes
    static constexpr uint16_t kDrawBase = 100;
    static constexpr uint16_t kMaskBase = 200;
    static constexpr uint16_t kBitsLoBase = 210;
    static constexpr uint16_t kBitsHiBase = 220;

    Bl101Host(const VoiceDef& def, uint32_t seed);
    ~Bl101Host();

    Bl101Host(const Bl101Host&) = delete;
    Bl101Host& operator=(const Bl101Host&) = delete;

    const VoiceDef& def() const { return fDef; }
    bool isFx() const { return fDef.voice == kBl101FxVoice; }

    void setSampleRate(double hostRate);
    uint32_t latencyFrames() const { return isFx() ? fFxStream.latency() : fStream.latency(); }

    void setParam(uint32_t index, float value);
    float getParam(uint32_t index) const;
    float defaultParam(uint32_t index) const;
    void resetToDefaults();

    void setNoteFollowsPitch(bool) {}
    bool noteFollowsPitch() const { return false; }
    void setTempo(double bpm);
    void setTransport(bool, double, double, double) {}

    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity);
    void noteOff(uint32_t frameOffset, uint8_t note);
    void pitchBend(uint32_t frameOffset, int32_t bend); // -8192..8191
    void controlChange(uint32_t frameOffset, uint8_t cc, uint8_t value);
    // inL / inR may be null (silence): Maquina BL-101 has no audio input
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames);
    void process(float* outL, float* outR, uint32_t frames) { process(nullptr, nullptr, outL, outR, frames); }
    void resetTiming();

    // test hooks
    uint64_t underruns() const { return isFx() ? fFxStream.underruns() : fStream.underruns(); }
    // the CHOP mask a source plays (the selector, or the Custom bits)
    uint16_t chopMask(uint32_t source) const;

private:
    enum EventType : uint8_t { kNoteOn, kNoteOff, kBend, kCc };
    struct Event {
        double frame; // engine frame
        EventType type;
        uint8_t a;
        int16_t b;
    };

    void push(uint32_t frameOffset, EventType type, uint8_t a, int16_t b);
    int paramInt(uint32_t index) const;
    void applyParams();
    void runBlock(uint64_t firstFrame, float* outL, float* outR);
    void runFxBlock(const float* inL, const float* inR, float* outL, float* outR);

    static constexpr int kMaxEvents = 1024;

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::unique_ptr<bl101::Synth> fSynth;
    std::unique_ptr<bl101::Fx> fFx;
    BlockStream<kBlock> fStream;
    BlockStream<kFxBlock> fFxStream;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;
    double fBpm = 120.0;

    // parameter index per kind (-1: none)
    int fMask[4] = { -1, -1, -1, -1 }, fBitsLo[4] = { -1, -1, -1, -1 }, fBitsHi[4] = { -1, -1, -1, -1 };
};

} // namespace maquina
