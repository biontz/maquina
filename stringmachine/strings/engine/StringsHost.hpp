// StringsHost.hpp - one plugin instance of Maquina Strings.
//
// THE EDITOR-FIRST SKELETON: this host keeps the parameters and the held notes but renders
// silence. The engine (a string machine after jpcima's string-machine + Plaits' wavetable
// engine, blend / split, per-voice filter, effects) replaces process() later; the interface is
// the other engine hosts' (KlangHost, La50Host), so DrumPlugin.cpp needs nothing else then.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include "VoiceTypes.hpp"

namespace maquina {

class StringsHost {
public:
    StringsHost(const VoiceDef& def, uint32_t seed);

    StringsHost(const StringsHost&) = delete;
    StringsHost& operator=(const StringsHost&) = delete;

    const VoiceDef& def() const { return fDef; }

    void setSampleRate(double hostRate) { fRate = hostRate; }
    uint32_t latencyFrames() const { return 0; }

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
    void pitchBend(uint32_t, int32_t) {}
    void controlChange(uint32_t frameOffset, uint8_t controller, uint8_t value);
    void process(float* outL, float* outR, uint32_t frames);
    void resetTiming() {}

    // test hook: notes held now
    int heldNotes() const;

private:
    const VoiceDef& fDef;
    std::vector<float> fDefaults;
    std::vector<std::atomic<float>> fValues;
    double fRate = 48000.0;
    uint8_t fHeld[128] = {};
};

} // namespace maquina
