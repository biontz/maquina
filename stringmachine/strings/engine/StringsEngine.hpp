// StringsEngine.hpp - Maquina Strings' sound engine: 8 voices of two layers, then effects.
//
//   Layer A, strings: per voice three footages (16', 8', 4') of saw (DPW), pulse with PWM
//   (string-machine's PwmOscillator) or both; per footage string-machine's keyed high-pass,
//   asymmetric shaper ("enhance") and keyed low-pass; a high shelf; the keytracked TONE
//   low-pass; a delayed vibrato. REGISTER morphs five stations (Cello, Ensemble, Viola, Violin,
//   Organ): the footages' levels and the filters.
//   Layer B, wave: Plaits' wavetable engine (4 banks of 8 x 8 waves), 1-4 unison voices spread
//   in pitch and stereo, an LFO and the envelope moving the position, then a filter per voice:
//   LP / BP / HP (stmlib's state variable filter) or COMB (a keytracked feedback delay).
//   Blend: per voice, from BLEND, moved by SHIFT over TIME after the note-on and by velocity;
//   equal-power. KEYS LAYER plays both; WAVE LOW / WAVE HIGH split the keyboard at C4.
//   A layer at zero level for a whole block is not computed.
//   Effects: ENSEMBLE (string-machine's Solina chorus, digital delays), PHASER, CHORUS or
//   FORMANT, then a reverb (Rings').
//
// Runs at a fixed rate (kRate: Plaits' tuning) in blocks of up to kMaxBlock frames;
// StringsHost resamples to the host. Header free of Plaits / string-machine names.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <memory>

namespace maquina {
namespace strings {

constexpr double kRate = 47872.34;  // plaits::kCorrectedSampleRate: Plaits' pitch tables assume it
constexpr int kMaxBlock = 24;       // plaits::kMaxBlockSize
constexpr int kNumVoices = 8;
constexpr int kMaxUnison = 4;
constexpr int kSplitKey = 60;       // KEYS WAVE LOW / HIGH: the wave layer below / from C4

// the parameters in their own units (StringsHost converts the stored steps)
struct Params {
    // strings
    float reg = 0.42f;        // REGISTER 0..1 (Cello .. Organ)
    int shape = 2;            // 0 saw, 1 PWM, 2 mix
    float detuneCt = 6;       // footage spread, cents
    float tone = 0.5f;        // 0..1
    float vibHz = 6, vibDepth = 0.3f, vibDelay = 0.6f; // depth 0..1 (1 = +-30 cents), delay s
    // wave
    int bank = 0;             // 0..3
    float x = 0.35f, y = 0.6f;
    int octave = 0;           // -2..+2
    float lfoHz = 0.8f, lfoDepth = 0.25f, envPos = 0.3f; // depth 0..1, env -1..1
    float layerDetuneCt = 7;
    int unison = 1;           // 1..4
    float spreadCt = 12;
    // filter (the wave layer)
    int filterMode = 0;       // 0 LP, 1 BP, 2 HP, 3 comb
    float cutoffHz = 1800, reso = 0.6f, filterEnv = 0.36f, keytrack = 0.5f; // env -1..1, keytrack 0..1
    // blend
    float blend = 0.4f, blendShift = 0.4f, blendTime = 2.4f, blendVelo = 0.3f; // shift -1..1, time s
    int keys = 0;             // 0 layer, 1 wave low, 2 wave high
    // amp
    float attack = 0.42f, decay = 1.2f, sustain = 1, release = 1.8f, velocity = 0; // s, 0..1
    // effects
    int fxMode = 0;           // 0 ensemble, 1 phaser, 2 chorus, 3 formant
    float fxAmount = 0.7f, fxSlowHz = 0.6f, fxFastHz = 6, reverb = 0.4f;
};

class Engine {
public:
    explicit Engine(uint32_t seed);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void setParams(const Params& p); // taken at the next block
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void pitchBend(float semitones);  // -2..+2
    void modWheel(float amount);      // 0..1: adds vibrato (no delay)
    void sustainPedal(bool down);
    void allNotesOff();               // release
    void allSoundOff();               // silence now
    void render(float* outL, float* outR, int frames); // frames <= kMaxBlock

    // test / bench hooks
    int activeVoices() const;
    uint64_t blocksA() const; // voice blocks with the strings layer computed
    uint64_t blocksB() const; // ... the wave layer

    struct Impl;

private:
    std::unique_ptr<Impl> fImpl;
};

} // namespace strings
} // namespace maquina
