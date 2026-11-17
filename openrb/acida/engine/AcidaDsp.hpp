// AcidaDsp.hpp - the C++ port of OpenRB-338's TB-303 voice and the four
// effects (distortion, PCF, compressor, delay) for Maquina Acida / Acida FX.
//
// Source: openrb/acida/third_party/openrb-338/crates/orb-engine/src/
// (tb303.rs, dist.rs, pcf.rs, comp.rs, delay.rs, tables/mod.rs), GPL-3.0-only,
// itself a bit-exact reimplementation of the ReBirth RB-338 2.01 engine.
// The port keeps its numerics rules (orb-engine/src/lib.rs): double
// arithmetic in the original's operand order, a float rounding exactly where
// the Rust code stores an f32, no fused multiply-add (this code is built with
// -ffp-contract=off and without -ffast-math: openrb/Makefile, libacida.a),
// truncating conversions. tools/acida_test checks it sample for sample
// against the Rust engine (tools/acida_ref).
//
// What is left out: the song/sequencer plumbing (pattern mode, song mode,
// tracks, the pattern gate) and the meters (display only; they never touch
// the audio). The 303 is played in OpenRB's note mode (key_on / key_off).
//
// The engine runs at 44100 Hz in 64-frame blocks.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <memory>

namespace maquina::acida {

constexpr int kBlock = 64;
constexpr double kSampleRate = 44100.0;
using Block = float[kBlock];

// ---------------------------------------------------------------------------
// tables (tables/mod.rs), built once per process

struct Smoother {
    float b2, a, g;
};

struct Tables {
    float level[128];
    float pan[128][128][2];
    float tb303Pitch[6144];
    float tb303Filter[8][1024][4];
    float pcfLowpass[8][512][3];
    float pcfBandpass[8][512][3];
    float distShaper[32][256];
    Smoother tb303Smoother, pcfSmoother;

    // the shared instance (built on first use; call once off the audio thread)
    static const Tables& get();
};

// ---------------------------------------------------------------------------
// the TB-303 (tb303.rs)

class Tb303 {
public:
    // live parameter bytes (controllers)
    enum : int { ENABLED = 0, PATTERN, TUNE, CUTOFF, RESONANCE, ENVMOD, DECAY, ACCENT, WAVEFORM };
    static constexpr int kMidiC = 36;          // MIDI note of the pattern note 0 at the Tune centre
    static constexpr uint8_t kAccentVelocity = 100;

    Tb303();

    void setVintage(bool v) { vintage = v; }
    void setParam(uint8_t ctrl, uint8_t value)
    {
        if (ctrl <= WAVEFORM) live[ctrl] = value;
    }
    uint8_t param(uint8_t ctrl) const { return live[ctrl]; }

    // note mode (accentVelocity: OpenRB's threshold is 100; Maquina's
    // Accent Vel parameter changes it)
    void keyOn(uint8_t note, uint8_t velocity, uint8_t accentVelocity = kAccentVelocity);
    void keyOff(uint8_t note);
    void silence(); // slot 6: slide cleared, note-off, held notes forgotten
    void reset();   // slot 5 (+ slot 6)

    void process(Block& bus, const Tables& t);

    bool gateOpen() const { return gate != 0.f; }
    uint8_t heldCount() const { return heldLen; }

private:
    struct Coefs {
        double c0, c1, g, c3;
    };
    struct Voice {
        double ph, s, y1, y2, o;
        float sample(double inc, bool square, const Coefs& k);
    };

    bool keySemis(uint8_t note, int& semis) const;
    void keyPlay(int semis, bool accent, bool legato);
    void noteOff();
    void noteOnSemis(int semis, uint8_t slide, uint8_t accent, uint8_t rest);
    double glide(const Tables& t);
    double smoothCutoff(const Tables& t);
    Voice voice() const;
    void storeVoice(const Voice& v);
    static void interpolate(const Tables& t, float c, double rf, float out[4]);
    void process20(Block& bus, const Tables& t);
    void processVintage(Block& bus, const Tables& t);

    static constexpr int kHeld = 16;

    uint8_t live[12];
    bool retrig = false, slide = false;
    float phase = 0.f, env = 0.f, envDecay = 0.f;
    uint32_t pitchTarget = 0, pitch = 0;
    float accAmount = 0.f, accEnv = 0.f;
    float cs1 = 0.5f, cs2 = 0.5f;
    float ampIn = 0.f, ampFb = 0.f, gate = 0.f, amp = 0.f;
    float y1 = 0.f, y2 = 0.f, s = 0.f, o = 0.f;
    float accCoef = 0.f;
    float coefs[4] = { 0.f, 0.f, 0.f, 0.f };
    bool vintage = false;
    struct Held {
        uint8_t note;
        bool accent;
    };
    Held held[kHeld] = {};
    uint8_t heldLen = 0;
};

// ---------------------------------------------------------------------------
// the effects

// dist.rs: controllers enabled, amount, shape
class Distortion {
public:
    enum : int { ENABLED = 0, AMOUNT, SHAPE };
    void setParam(uint8_t ctrl, uint8_t value);
    void process(Block& bus, const Tables& t);

private:
    void paramUpdate(const Tables& t);
    uint8_t live[3] = { 1, 70, 0 };
    uint16_t counter = 0;
    bool clip = false;
    float gainIn = 0.f, gainOut = 0.f, targetIn = 0.f, targetOut = 0.f;
    uint16_t row = 0;
};

// pcf.rs: controllers enabled, frequency, resonance, amount, wave (0..54
// here: the waves above read past the patterns), decay, mode (1 = LP)
class Pcf {
public:
    enum : int { ENABLED = 0, FREQUENCY, RESONANCE, AMOUNT, WAVE, DECAY, MODE };
    void setParam(uint8_t ctrl, uint8_t value);
    // tick: the song position in ReBirth ticks (768 per 4/4 bar), already
    // advanced for this block
    void process(Block& bus, const Tables& t, int32_t tick);

private:
    uint8_t live[7] = { 1, 70, 64, 40, 24, 90, 1 };
    float lp[2] = { 0.f, 0.f }, bp[2] = { 0.f, 0.f };
    float trigger = 0.f, base = 0.f, env = 0.f;
    int32_t step = -1;
    float attack = 0.f, decay = 0.f;
    float smooth[2] = { 0.f, 0.f };
    int32_t quotient = 0;
};

// comp.rs: controllers enabled, amount, threshold
class Compressor {
public:
    enum : int { ENABLED = 0, AMOUNT, THRESHOLD };
    void setParam(uint8_t ctrl, uint8_t value)
    {
        if (ctrl <= THRESHOLD) live[ctrl] = value;
    }
    void process(Block& bus);              // mono (a channel insert)
    void processStereo(Block& l, Block& r); // linked (the master insert)

private:
    bool begin();
    void run(Block* const* buses, int n);
    uint8_t live[3] = { 1, 70, 64 };
    uint16_t counter = 0;
    float x = 1.f, off = 1.f, env = 1.f, gain = 0.f, floor = 1000.f;
    uint16_t hold = 0;
};

// delay.rs: controllers enabled, steps, mode (0 triplets, else 16ths),
// feedback, pan. The original's process-global line is per instance here.
class Delay {
public:
    enum : int { ENABLED = 0, STEPS, MODE, FEEDBACK, PAN };
    static constexpr uint32_t kLen = 1u << 17;
    static constexpr uint32_t kMask = kLen - 1;
    Delay();
    void setParam(uint8_t ctrl, uint8_t value);
    // send: the input (refilled with 1e-6, as the original's send bus);
    // l / r: the line's output is added; tempo: BPM * 1000
    void process(Block& send, Block& l, Block& r, const Tables& t, uint32_t tempo);
    void clear(); // the line refilled with 1e-6 (slot 6)

private:
    void paramUpdate(const Tables& t, uint32_t tempo);
    std::unique_ptr<float[]> line;
    bool lineGate = false;
    uint8_t live[5] = { 1, 3, 1, 80, 64 };
    uint16_t counter = 0;
    float target[2] = { 0.f, 0.f }, gain[2] = { 0.f, 0.f };
    float feedback = 0.f;
    uint32_t length = 0, write = 0;
};

// the delay length in samples (delay.rs length())
uint32_t delayLength(uint32_t tempo, uint8_t steps, uint8_t mode);

// the mixer's 1e-6 denormal guard (frame/mod.rs GUARD)
constexpr float kGuard = 9.99999997e-07f; // 0x358637BD

} // namespace maquina::acida
