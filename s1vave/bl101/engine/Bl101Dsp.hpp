// Bl101Dsp.hpp - the C++ port of S1Vave's sound engine (github.com/AndrewGG88/S1Vave,
// firmware/src/obv_engine.h and obv_fx.h, GPL-3.0-only), for Maquina BL-101 and
// BL-101 FX.
//
// The firmware keeps its state in globals (one synth per chip); here each
// instance owns it. The code is the firmware's, line by line, integers only
// (Q15 audio, Q24 envelopes, Q8 semitones), so the output is bit-exact with
// the firmware built for a computer (tools/bl101_ref, tools/bl101_test). The
// DSP primitives (obv_dsp.h), the parameter table (obv_params.h) and the
// generated tables (obv_tables.h) are the vendored files themselves.
//
// What is not here: the sequencer and its second sound (track B), the
// arpeggiator, the metronome click, the scope and the program-change fade
// (the plugin sets parameters instead of loading programs).
//
//   Synth   the 8 voices, LFO, envelopes, the output stage (patch volume,
//           softclip) and the softclip the effects stage ends with, so its
//           output is the firmware's with every effect off. 16-frame
//           control blocks (CTL), any multiple of them per call.
//   Fx      the effects stage: chorus, ping-pong delay, plate reverb, the
//           rate-reduce / distortion slot, then the softclip.
//
// Parameters are the firmware's patch bytes (obv_params.h P_*), set with
// setByte(P_*, value); the plugin maps its parameters to them
// (tools/gen_params.py bl101_params: VoiceParam::fwPar).
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <memory>

namespace bl101 {

constexpr uint32_t kRate = 44100; // the firmware's audio rate
constexpr uint32_t kCtl = 16;     // frames per control block (obv_tables.h CTL)
constexpr uint32_t kNumBytes = 80; // patch bytes (obv_params.h PATCH_BYTES)

// the firmware's 6 chop masks (s1vave.c MASK_PRESETS), as the plugin's
// selector lists them after "Custom"
constexpr uint16_t kChopMaskPresets[6] = { 0xFFFFu, 0x5555u, 0xAAAAu, 0x1111u, 0x8888u, 0x0000u };

// a patch byte's default (obv_params.h PDEF[p].def), its maximum, the number
// of firmware parameters (NPARAM) and the factory sounds (obv_patches.h)
uint8_t paramDefault(uint32_t p);
uint8_t paramMax(uint32_t p);
uint32_t numFirmwareParams();
// the INIT pattern's OSC DRAW table (a ramp, its last step wrapping to -32768)
void initDrawTable(int16_t* table16);
// factory sound n (0..63): its patch bytes (defaults + its own; transpose
// centred as the firmware loads it), draw table and chop masks. Returns its
// name ("PTN 1-01").
const char* factorySound(uint32_t n, uint8_t* bytes80, int16_t* draw16, uint16_t* chop4);
uint32_t numFactorySounds();

class Synth {
public:
    Synth();
    ~Synth();
    Synth(const Synth&) = delete;
    Synth& operator=(const Synth&) = delete;

    void setByte(uint32_t p, uint8_t value);
    uint8_t byte(uint32_t p) const;
    void setDrawStep(uint32_t step, int16_t value);
    void setChopMask(uint32_t source, uint16_t mask);

    // MIDI: queued, taken at the start of the next control block
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void setBend(int32_t bend);      // -8192..8191
    void setModWheel(uint8_t value); // CC 1
    void setSustain(bool on);        // CC 64
    void setTempo(double bpm);       // the LFO's tempo sync
    void panic();                    // all notes and voices off (next block)
    void silence();                  // everything quiet now (reset)

    // n frames (a multiple of kCtl) of Q15 stereo, L R L R
    void process(int32_t* out, uint32_t frames);
    bool quiet() const;

    struct State; // the firmware's globals (Bl101Dsp.cpp)

private:
    std::unique_ptr<State> s;
};

class Fx {
public:
    Fx();
    ~Fx();
    Fx(const Fx&) = delete;
    Fx& operator=(const Fx&) = delete;

    void setByte(uint32_t p, uint8_t value);
    uint8_t byte(uint32_t p) const;
    void setTempo(double bpm); // the delay's note values
    void silence();

    // in place, n frames of Q15 stereo: fx_block (parameters, once per call)
    // then fx_process, as the firmware's ISR
    void process(int32_t* io, uint32_t frames);

    struct State;

private:
    std::unique_ptr<State> s;
};

} // namespace bl101
