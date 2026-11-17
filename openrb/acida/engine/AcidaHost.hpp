// AcidaHost.hpp - one plugin instance of Maquina Acida or Acida FX, around
// the C++ port of OpenRB-338's engine (AcidaDsp.hpp, libacida.a) at its
// 44100 Hz in 64-frame blocks, resampled from/to the host rate (BlockStream).
//
//   Maquina Acida (instrument): the TB-303 in OpenRB's note mode. A note
//     plays at its pitch (MIDI 36 = the pattern's C at the Tune centre),
//     velocity >= Accent Vel = accent, a note played while another is held
//     slides to it (no new attack), the gate lasts as long as notes are held
//     (last-note priority). Notes take effect at the engine block they fall
//     in (1.45 ms); a note-off waits for the next block, so a note always
//     sounds. The output is ReBirth's mixer at its defaults (channel level
//     100, centre, master 100) and its int16 range (clamped at +-1).
//   Maquina Acida FX (audio effect): ReBirth's effects on a stereo input:
//     Distortion and PCF per channel (a pair of each, as two mixer channels
//     routed to them), the Compressor linked on both (as on the master), and
//     the Delay as a send: Send feeds it the mono sum, its output is added to
//     the dry signal. The PCF's patterns and the delay time follow the host
//     transport (ReBirth's 768 ticks per bar); stopped, they run on at the
//     host tempo.
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

namespace maquina {

class AcidaHost {
public:
    static constexpr double kEngineRate = 44100.0;
    static constexpr int kBlock = 64;

    AcidaHost(const VoiceDef& def, uint32_t seed);
    ~AcidaHost();

    AcidaHost(const AcidaHost&) = delete;
    AcidaHost& operator=(const AcidaHost&) = delete;

    const VoiceDef& def() const { return fDef; }
    bool isFx() const { return fDef.voice == kAcidaFxVoice; }

    void setSampleRate(double hostRate);
    uint32_t latencyFrames() const { return fStream.latency(); }

    void setParam(uint32_t index, float value);
    float getParam(uint32_t index) const;
    float defaultParam(uint32_t index) const;
    void resetToDefaults();

    void setNoteFollowsPitch(bool) {}
    bool noteFollowsPitch() const { return false; }
    void setTempo(double bpm);
    // the transport at the start of the next process() call: playing, the
    // position in beats, beats per bar, beats per host sample
    void setTransport(bool playing, double beat, double beatsPerBar, double beatsPerSample);

    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity);
    void noteOff(uint32_t frameOffset, uint8_t note);
    // inL / inR may be null (silence): Maquina Acida has no audio input
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames);
    void process(float* outL, float* outR, uint32_t frames) { process(nullptr, nullptr, outL, outR, frames); }
    void resetTiming();

    // test hooks
    uint64_t underruns() const { return fStream.underruns(); }
    int32_t tick() const { return fTickAtBlock; }

    struct Engine; // the DSP objects (AcidaHost.cpp)

private:
    struct Event {
        double frame; // engine frame
        uint8_t note, velocity; // velocity 0 = note-off
    };

    int paramInt(int index) const;
    uint8_t byteOf(int index) const;
    void runBlock(uint64_t firstFrame, const float* inL, const float* inR, float* outL, float* outR);

    static constexpr int kMaxEvents = 1024;

    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::unique_ptr<Engine> fEngine;
    BlockStream<kBlock> fStream;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;

    // transport: host frames processed, the last setTransport()
    uint64_t fHostFrames = 0;
    double fBpm = 120.0;
    bool fPlaying = false;
    double fBeat = 0.0, fBeatsPerSample = 0.0;
    uint64_t fBeatHostFrame = 0;
    double fFreeTick = 0.0; // stopped: the tick, advanced at the tempo
    int32_t fTickAtBlock = 0;

    // parameter indices
    int fIx[32];
};

} // namespace maquina
