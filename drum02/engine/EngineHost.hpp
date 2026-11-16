// EngineHost.hpp - one plugin instance's view of the LXR firmware engine.
//
// The firmware engine keeps its state in globals, so all instances in a
// process share one engine. Each EngineHost owns a private copy of that state
// (a blob of ~8 KB). Before touching the engine an instance takes the global
// lock; if another instance's state is resident it is saved to that
// instance's blob and ours is loaded. With a single instance nothing is
// copied at all.
//
// Timing: the engine renders 64-frame blocks at 44140 Hz. A note at host frame
// H is applied to the engine block that contains engine frame
// H * ratio + kEventDelay, so it is never late however far the resampler has
// rendered ahead. The resulting constant delay is reported as latency.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "Resampler.hpp"
#include "Timing.h"
#include "VoiceTypes.hpp"

namespace maquina {

class EngineHost {
public:
    EngineHost(const VoiceDef& def, uint32_t seed);
    ~EngineHost();

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    const VoiceDef& def() const { return fDef; }

    void setSampleRate(double hostRate);
    uint32_t latencyFrames() const { return fLatency; }

    // Parameters in plugin units (see VoiceParam). Safe from any thread.
    void setParam(uint32_t index, float value);
    float getParam(uint32_t index) const;
    float defaultParam(uint32_t index) const;
    void resetToDefaults();

    // Key Track: every note triggers the voice; off = at its set pitch,
    // on = transposed by the played note around C4 (the firmware's "Any"
    // trigger-note mode).
    void setNoteFollowsPitch(bool on) { fNotePitch.store(on, std::memory_order_relaxed); }
    bool noteFollowsPitch() const { return fNotePitch.load(std::memory_order_relaxed); }

    // Audio thread. Queue events for the coming process() call (frame offsets
    // within that call, in order), then call process().
    void noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity);
    void noteOff(uint32_t, uint8_t) {} // the LXR voices ignore note-off
    void setTempo(double bpm) { fBpm = bpm; }
    void process(float* outL, float* outR, uint32_t frames);

    // Clear the timing pipeline (e.g. on activate). Voice state is kept.
    void resetTiming();

private:
    struct Event {
        double engineFrame;
        uint8_t trigger, note, velocity;
    };

    void acquire();
    void release();
    uint8_t toFirmware(uint32_t index, float value) const;
    float fromFirmware(uint32_t index, uint8_t fw) const;
    void applyDirtyParams();
    void triggerNote(uint8_t note, uint8_t velocity);
    void renderBlock(int16_t* lr);

    static constexpr int kEventDelay = MAQUINA_EVENT_DELAY;
    static_assert(kEventDelay >= Resampler::kMaxLookahead, "notes would be late");
    static constexpr int kMaxEvents = 512;

    const VoiceDef& fDef;
    std::vector<uint8_t> fBlob;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    std::vector<std::atomic<uint32_t>> fDirty; // bitmask words
    std::atomic<bool> fNotePitch { false };

    Resampler fResampler;
    double fHostRate = 48000.0;
    uint32_t fLatency = 0;
    double fBpm = 0.0;
    uint16_t fEngineBpm = 0;

    Event fEvents[kMaxEvents];
    uint32_t fEventHead = 0, fEventCount = 0;
    double fHostFrame = 0.0;   // absolute host frames processed
    double fEngineFrame = 0.0; // absolute engine frames rendered
};

} // namespace maquina
