// StringsUnits.hpp - Maquina Strings: what a parameter's stored value means.
//
// Parameters are stored as whole steps (tools/gen_params.py strings_params()); this header turns
// them into seconds, Hz, cents and fractions. The editor shows values with it, and the engine
// (to come) will read them the same way, so both always agree.
//
// Log ranges (times, rates, the cutoff) bend at their default: steps 0..knee run from lo to the
// default, knee..127 from the default to hi, both exponential. knee = round(127 * ln(def / lo) /
// ln(hi / lo)) (also computed in gen_params.py), so the default lands exactly on a step and the
// curve is close to one plain exponential.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cmath>

namespace maquina {
namespace strings {

constexpr int kLogSteps = 127;

struct LogRange {
    float lo, def, hi;
};

inline int logKnee(const LogRange& r)
{
    return int(std::lround(kLogSteps * std::log(r.def / r.lo) / std::log(r.hi / r.lo)));
}

inline float logValue(const LogRange& r, int step)
{
    const int k = logKnee(r);
    if (step <= 0) return r.lo;
    if (step >= kLogSteps) return r.hi;
    if (step <= k) return r.lo * std::pow(r.def / r.lo, float(step) / float(k));
    return r.def * std::pow(r.hi / r.def, float(step - k) / float(kLogSteps - k));
}

// the log-mapped parameters (gen_params.py STRINGS_LOG)
constexpr LogRange kWaveLfoRate { 0.05f, 0.8f, 10.f };  // Hz
constexpr LogRange kFilterCutoff { 20.f, 1800.f, 18000.f }; // Hz
constexpr LogRange kBlendTime { 0.05f, 2.4f, 10.f };    // s
constexpr LogRange kAttack { 0.001f, 0.42f, 10.f };     // s
constexpr LogRange kDecay { 0.001f, 1.2f, 20.f };       // s
constexpr LogRange kRelease { 0.001f, 1.8f, 20.f };     // s

// the linear ones
inline float vibratoHz(int v) { return 5.f + 0.1f * float(v); }    // 0..30: 5..8 Hz
inline float vibDelaySeconds(int v) { return 0.1f * float(v); }    // 0..30: 0..3 s
inline float fxSlowHz(int v) { return 0.3f + 0.01f * float(v); }   // 0..60: 0.3..0.9 Hz
inline float fxFastHz(int v) { return 3.f + 0.1f * float(v); }     // 0..60: 3..9 Hz
inline float registerPos(int v) { return 0.01f * float(v); }       // 0..100: 0..1

// REGISTER's five stations, evenly spread over 0..1
constexpr int kNumStations = 5;
constexpr const char* kStationNames[kNumStations] = { "Cello", "Ensemble", "Viola", "Violin", "Organ" };
inline int nearestStation(int registerValue)
{
    const int s = int(std::lround(registerPos(registerValue) * float(kNumStations - 1)));
    return s < 0 ? 0 : s >= kNumStations ? kNumStations - 1 : s;
}
inline int stationValue(int station) { return int(std::lround(100.0 * station / (kNumStations - 1))); }

} // namespace strings
} // namespace maquina
