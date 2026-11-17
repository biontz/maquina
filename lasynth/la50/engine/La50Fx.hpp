// La50Fx.hpp - Maquina LA-50's effects after the tones (our own algorithms, after
// the D-50's: not sample-accurate copies):
//   per tone   2-band EQ (low shelf; high peak with Q), then chorus (8 types:
//              Chorus 1 / 2, Flanger 1 / 2, Feedback Chorus, Tremolo,
//              Chorus + Tremolo, Dimension; rate, depth, balance), stereo
//   per patch  tone balance, the output mode (1: both tones mixed into the
//              reverb; 2: both through the reverb, Upper left / Lower right;
//              3: only Upper reverberated; 4: only Lower), the reverb (32
//              types: halls / rooms, delays, gates, reverse gates, slap backs,
//              space) with its balance, the total volume.
// At the engine's 32 kHz.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <memory>

#include "D50Sysex.hpp"

namespace la50 {

// the chorus modulators as the D-50 sets them from Rate / Depth (see La50Fx.cpp);
// lfo 0: tremolo (0..1), 1 / 2: delay sweeps (ms); -1 when the type doesn't use it
float chorusRateHz(int type, int lfo, int rate);
float chorusDepth(int type, int lfo, int depth);

class Fx {
public:
    Fx();
    ~Fx();
    Fx(const Fx&) = delete;
    Fx& operator=(const Fx&) = delete;

    void reset();
    // the tones' outputs (mono) -> stereo
    void process(const PatchData& patch, const float* upper, const float* lower, float* outL, float* outR, int frames);

    struct State;

private:
    std::unique_ptr<State> s;
};

} // namespace la50
