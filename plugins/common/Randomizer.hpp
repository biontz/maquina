// Randomizer.hpp - sound randomizer shared by all voice plugins (the editor's
// RANDOM panel).
//
// Parameters are grouped (OSC, AMP EG, PITCH EG, ...). Each group has
//   lock      the group is never touched
//   min, max  0..100 %: knobs/values are picked only inside that window of
//             each control's own range (e.g. FILTER 40..100)
// Selectors (waves, filter type, ...) pick any choice when unlocked; the
// filter is never switched Off and modulation targets are kept. Key Track
// is never randomized.
//
// Maquina Dredrum / Plethora also randomize per oscillator / channel: each
// OSC / CHANNEL section has a ROLL button (only that one) and an on/off
// switch for whether the main RANDOM button includes it (RandSettings::oscOn). Groups' locks and ranges
// apply either way.
//
// Header-only and independent of DPF so tools/check_random can test it.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

#include "ExtraParams.hpp"

namespace maquina {

struct RandGroup {
    const char* key;   // state key, stable
    const char* title; // label in the editor
    bool defLock;
    uint8_t defMin, defMax; // %
    const char* symbols[16];
};

static const RandGroup kRandGroups[] = {
    { "osc", "OSC", false, 0, 100,
      { "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "noise_freq", "noise_mix", "fm_amount", "fm_freq",
        "fm_wave", "fm_mode", "mod1_freq", "mod1_gain", "mod1_wave", "mod2_freq", "mod2_gain", "mod2_wave" } },
    { "env", "AMP EG", false, 0, 60,
      { "amp_attack", "amp_decay", "amp_slope", "amp_repeat" } },
    { "pitch", "PITCH EG", false, 0, 100, { "pitch_amount", "pitch_decay", "pitch_slope" } },
    { "transient", "TRANS", false, 0, 100, { "trans_wave", "trans_volume", "trans_freq" } },
    { "filter", "FILTER", false, 40, 100, { "filter_type", "filter_freq", "filter_reso", "filter_drive" } },
    { "lfo", "LFO", false, 0, 100,
      { "lfo_wave", "lfo_rate", "lfo_sync", "lfo_amount", "lfo_offset", "lfo_target", "lfo_retrigger" } },
    { "velocity", "VELO", true, 0, 100, { "velo_dest", "velo_amount", "velo_volume" } },
    { "mix", "PAN/RATE", true, 0, 100, { "pan", "decimation" } },
    { "drive", "DRIVE", true, 0, 100, { "drive" } },
    { "volume", "VOLUME", true, 60, 100, { "volume" } },
    // Maquina Dredrum: by function across its 3 oscillators. Decay stops
    // short of 248..255, where the firmware's decay never ends (drone).
    { "dwave", "WAVE", false, 0, 100, { "osc1_wave", "osc2_wave", "osc3_wave", "click" } },
    { "dpitch", "PITCH", false, 0, 100,
      { "osc1_pitch", "osc2_pitch", "osc3_pitch", "osc1_drop", "osc2_drop", "osc3_drop" } },
    { "ddecay", "DECAY", false, 0, 90, { "osc1_decay", "osc2_decay", "osc3_decay" } },
    { "dlevel", "LEVEL", false, 0, 100, { "osc1_level", "osc2_level", "osc3_level" } },
    // Maquina Plethora
    { "palgo", "ALGO", false, 0, 100, { "a_algo", "b_algo", "c_source" } },
    { "pxy", "X / Y", false, 0, 100, { "a_x", "a_y", "b_x", "b_y", "c_grit" } },
    { "pfilter", "FILTER", false, 20, 100,
      { "a_ftype", "a_cutoff", "a_reso", "b_ftype", "b_cutoff", "b_reso", "c_ftype", "c_cutoff", "c_reso" } },
    { "penv", "ENV", false, 0, 85, { "a_attack", "a_decay", "b_attack", "b_decay", "c_attack", "c_decay", "env_sync" } },
    { "plevel", "LEVEL", true, 0, 100, { "a_level", "b_level", "c_level" } },
    { "dtune", "TUNE", true, 0, 100,
      { "osc1_track", "osc2_track", "osc3_track", "osc1_octave", "osc2_octave", "osc3_octave", "osc1_semi",
        "osc2_semi", "osc3_semi" } },
};
constexpr int kNumRandGroups = int(sizeof(kRandGroups) / sizeof(kRandGroups[0]));

// does the voice have any parameter of group g (cymbal/hihat: no pitch EG)
inline bool randGroupUsed(const VoiceDef& d, int g)
{
    for (const char* sym : kRandGroups[g].symbols)
        for (uint32_t i = 0; sym && i < d.numParams; ++i)
            if (!std::strcmp(d.params[i].symbol, sym)) return true;
    return false;
}

// Maquina Dredrum / Plethora: the oscillator or channel (0..2) a parameter
// belongs to, -1 for none
inline int channelOf(const VoiceDef& d, const char* symbol)
{
    if (d.voice == kDredrumVoice) {
        if (!std::strcmp(symbol, "click")) return 2; // oscillator 3's CLICK
        if (!std::strncmp(symbol, "osc", 3) && symbol[3] >= '1' && symbol[3] <= '3' && symbol[4] == '_')
            return symbol[3] - '1';
    } else if (d.voice == kPlethoraVoice) {
        if (symbol[0] >= 'a' && symbol[0] <= 'c' && symbol[1] == '_') return symbol[0] - 'a';
    }
    return -1;
}

inline int randGroupOf(const char* symbol)
{
    for (int g = 0; g < kNumRandGroups; ++g)
        for (const char* sym : kRandGroups[g].symbols)
            if (sym && !std::strcmp(sym, symbol)) return g;
    return -1;
}

constexpr int kNumRandOsc = 3; // Maquina Dredrum oscillators / Plethora channels

struct RandSettings {
    bool lock[kNumRandGroups];
    uint8_t min[kNumRandGroups], max[kNumRandGroups]; // %, min <= max
    bool oscOn[kNumRandOsc] = { true, true, true };  // main RANDOM includes the oscillator

    RandSettings()
    {
        for (int g = 0; g < kNumRandGroups; ++g) {
            lock[g] = kRandGroups[g].defLock;
            min[g] = kRandGroups[g].defMin;
            max[g] = kRandGroups[g].defMax;
        }
    }

    // state string: "osc=0:0:100 env=0:0:60 ..." (lock:min:max); unknown keys
    // are ignored, missing ones keep their default. "dosc=1:1:0" holds
    // oscOn[0..2] (same three-number shape).
    std::string toString() const
    {
        std::string out;
        for (int g = 0; g < kNumRandGroups; ++g) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%s%s=%d:%d:%d", g ? " " : "", kRandGroups[g].key, int(lock[g]),
                          int(min[g]), int(max[g]));
            out += buf;
        }
        char buf[32];
        std::snprintf(buf, sizeof buf, " dosc=%d:%d:%d", int(oscOn[0]), int(oscOn[1]), int(oscOn[2]));
        return out + buf;
    }
    void fromString(const char* str)
    {
        *this = RandSettings();
        if (!str) return;
        char key[32];
        int l, lo, hi, n;
        while (std::sscanf(str, " %31[^= ]=%d:%d:%d%n", key, &l, &lo, &hi, &n) == 4) {
            str += n;
            if (!std::strcmp(key, "dosc")) {
                oscOn[0] = l != 0;
                oscOn[1] = lo != 0;
                oscOn[2] = hi != 0;
                continue;
            }
            for (int g = 0; g < kNumRandGroups; ++g)
                if (!std::strcmp(kRandGroups[g].key, key)) {
                    lo = std::clamp(lo, 0, 100);
                    hi = std::clamp(hi, lo, 100);
                    lock[g] = l != 0;
                    min[g] = uint8_t(lo);
                    max[g] = uint8_t(hi);
                }
        }
    }
};

namespace rand_detail {

inline int uniform(std::mt19937& rng, int lo, int hi)
{
    return lo >= hi ? lo : std::uniform_int_distribution<int>(lo, hi)(rng);
}

inline int labelIndex(const VoiceParam& p, const char* label)
{
    for (int i = 0; i < p.numLabels; ++i)
        if (p.labels && !std::strcmp(p.labels[i], label)) return i;
    return -1;
}

} // namespace rand_detail

// New value for one parameter (plugin units) inside min..max % of its range.
inline int randomValue(const VoiceParam& p, int minPct, int maxPct, int current, std::mt19937& rng)
{
    using namespace rand_detail;
    switch (p.kind) {
    case ParamKind::Int: {
        const int lo = p.fwMin + p.offset, span = p.fwMax - p.fwMin;
        const int a = lo + (span * minPct + 50) / 100;
        const int b = lo + (span * maxPct + 50) / 100;
        return uniform(rng, a, std::max(a, b));
    }
    case ParamKind::Toggle:
    case ParamKind::Retrigger:
        return uniform(rng, 0, 1);
    case ParamKind::ModTarget: // keep what the target is routed to
    case ParamKind::Note:
        return current;
    default: { // Enum: any choice, but keep the filter on
        const int off = !std::strcmp(p.symbol, "filter_type") ? labelIndex(p, "Off") : -1;
        for (int tries = 0; tries < 16; ++tries) {
            const int v = uniform(rng, 0, p.numLabels - 1);
            if (v != off) return v;
        }
        return current;
    }
    }
}

// Randomize values[0 .. numAllParams(d)) in place (plugin units). Locked
// groups and parameters outside every group are left alone. onlyOsc >= 0
// (Maquina Dredrum's per-oscillator ROLL): only that oscillator's parameters;
// otherwise oscillators switched off in rs.oscOn are left alone.
inline void randomize(const VoiceDef& d, const RandSettings& rs, float* values, std::mt19937& rng,
                      int onlyOsc = -1)
{
    for (uint32_t i = 0; i < numAllParams(d); ++i) {
        const VoiceParam& p = paramAt(d, i);
        const int g = randGroupOf(p.symbol);
        if (g < 0 || rs.lock[g]) continue;
        const int osc = channelOf(d, p.symbol);
        if (onlyOsc >= 0 ? osc != onlyOsc : (osc >= 0 && !rs.oscOn[osc])) continue;
        const int cur = int(values[i] + (values[i] < 0 ? -0.5f : 0.5f));
        values[i] = float(randomValue(p, rs.min[g], rs.max[g], cur, rng));
    }
}

} // namespace maquina
