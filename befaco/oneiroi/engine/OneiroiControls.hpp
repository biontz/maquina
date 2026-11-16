// OneiroiControls.hpp - Maquina Oneiroi's parameters as the module's panel
// controls, shared by the engine host (OneiroiHost.cpp) and the editor's
// randomizer (Randomizer.hpp). Header-only, no DSP dependencies.
//
// The module reads every knob and fader as 0..1. The plugin's knobs are
// 0..127 (k = v / 127), except:
//
//   centred knobs  -64..+63, 0 = the module's centre position: speed 0x
//                  (Speed, at the calibrated centre 2192/4041), unison, no
//                  filter (Loop/Echo Filter), smallest room (Spacetime).
//                  The module's CenterMap() puts the centre at 0.55 and the
//                  end at 0.99, so -64 -> 0, 0 -> centre, +63 -> 0.99.
//   osc_pitch      -60..+60 tenths of a semitone; the PITCH knob spans
//                  +-6 semitones (k = 0.5 + v / 120)
//
// Octave, the selectors and the envelope are not knobs (OneiroiHost.cpp).
// The envelope (a Maquina addition) maps its 0..127 knobs as below; the host
// runs it, the editor shows the times.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

#include "VoiceTypes.hpp"

namespace maquina {
namespace oneiroi {

constexpr float kSpeedZero = 2192.f / 4041.f; // Ui.h: uncalibrated speed knob centre
constexpr float kCentre = 0.55f;              // Commons.h CenterMap() default centre
constexpr float kCentredTop = 0.99f;          // CenterMap() reaches its maximum here

inline bool isCentred(const VoiceParam& p) { return p.kind == ParamKind::Int && p.offset == -64; }
inline bool isPitch(const VoiceParam& p) { return !std::strcmp(p.symbol, "osc_pitch"); }
inline float centreOf(const VoiceParam& p) { return !std::strcmp(p.symbol, "loop_speed") ? kSpeedZero : kCentre; }

// plugin value (as shown) -> module control 0..1
inline float toKnob(const VoiceParam& p, float v)
{
    if (isPitch(p)) return std::clamp(0.5f + v / 120.f, 0.f, 1.f);
    if (isCentred(p)) {
        const float c = centreOf(p);
        return v < 0.f ? c * (1.f + std::max(v, -64.f) / 64.f) : c + (kCentredTop - c) * std::min(v, 63.f) / 63.f;
    }
    const float span = float(p.fwMax - p.fwMin);
    return span > 0.f ? std::clamp((v - float(p.fwMin + p.offset)) / span, 0.f, 1.f) : 0.f;
}

// module control 0..1 -> nearest plugin value
inline int fromKnob(const VoiceParam& p, float k)
{
    k = std::clamp(k, 0.f, 1.f);
    float v;
    if (isPitch(p)) {
        v = (k - 0.5f) * 120.f;
    } else if (isCentred(p)) {
        const float c = centreOf(p);
        v = k < c ? (k / c - 1.f) * 64.f : (k - c) / (kCentredTop - c) * 63.f;
    } else {
        v = float(p.fwMin + p.offset) + k * float(p.fwMax - p.fwMin);
    }
    const int lo = p.fwMin + p.offset, hi = p.fwMax + p.offset;
    return std::clamp(int(std::lround(v)), lo, hi);
}

// Maquina envelope: Attack 0 = instant, else 1 ms .. 10 s (linear rise);
// Decay / Release 5 ms .. 20 s to -60 dB (exponential); Sustain (v/127)^2
inline double attackSeconds(int v) { return v <= 0 ? 0.0 : 0.001 * std::pow(10000.0, v / 127.0); }
inline double fallSeconds(int v) { return 0.005 * std::pow(4000.0, v / 127.0); }
inline double sustainLevel(int v) { const double x = std::clamp(v, 0, 127) / 127.0; return x * x; }

} // namespace oneiroi
} // namespace maquina
