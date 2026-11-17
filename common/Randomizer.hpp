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
#include "OneiroiControls.hpp"

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
    // Maquina Oneiroi / Oneiroi FX use the module's own randomizer (below,
    // oneiroiRandomize), not this panel. "oosc", "oloop" and "ofx" are its
    // targets (the knobs the module randomizes); the locked groups hold the
    // rest of the sound (SHIFT functions, faders, envelope), so presets
    // (isSoundParam) cover them.
    { "oosc", "OSC", false, 0, 100, { "osc_pitch", "osc_detune" } },
    { "oloop", "LOOPER", false, 0, 100, { "loop_speed", "loop_start", "loop_length" } },
    { "ofx", "EFFECTS", false, 0, 100,
      { "flt_cutoff", "flt_reso", "res_tune", "res_feedback", "echo_repeats", "echo_density", "amb_decay",
        "amb_spacetime" } },
    { "oshift", "SHIFT", true, 0, 100,
      { "osc_octave", "osc_unison", "osc2_mode", "loop_sos", "loop_filter", "flt_mode", "flt_pos",
        "res_dissonance", "echo_filter", "amb_autopan", "mod_level", "mod_speed", "mod_type" } },
    { "omix", "MIX", true, 0, 100,
      { "vol_input", "vol_looper", "vol_sine", "vol_osc2", "mix_filter", "mix_resonator", "mix_echo",
        "mix_ambience" } },
    { "oenv", "ENV", true, 0, 100, { "env_attack", "env_decay", "env_sustain", "env_release", "env_drone" } },
    // Maquina Klang / Klong / Klong FX (Model and Tune are shared by both)
    { "mmodel", "MODEL", false, 0, 100, { "model" } },
    { "ksound", "SOUND", false, 0, 100, { "harmonics", "timbre", "morph" } },
    { "kenv", "ENV MOD", false, 0, 100, { "fm_env", "timbre_env", "morph_env" } },
    { "klpg", "LPG", false, 0, 100, { "lpg_decay", "lpg_colour", "lpg_mode" } },
    { "rres", "RESONATOR", false, 0, 100, { "structure", "brightness", "damping", "position" } },
    { "rpoly", "POLY", true, 0, 100, { "polyphony" } },
    { "mtune", "TUNE", true, 0, 100, { "coarse", "fine", "key_track" } },
    // Maquina Acida (Tune and the engine locked: they change the line's key / character)
    { "afilter", "FILTER", false, 0, 100, { "cutoff", "resonance", "env_mod", "decay" } },
    { "aaccent", "ACCENT", false, 0, 100, { "accent" } },
    { "awave", "WAVE", false, 0, 100, { "waveform" } },
    { "atune", "TUNE", true, 0, 100, { "tune", "engine" } },
    // Maquina Acida FX (the on/off switches locked)
    { "xdist", "DIST", false, 0, 100, { "dist_amount", "dist_shape" } },
    { "xpcf", "PCF", false, 0, 100, { "pcf_freq", "pcf_reso", "pcf_amount", "pcf_wave", "pcf_decay", "pcf_mode" } },
    { "xcomp", "COMP", false, 0, 100, { "comp_amount", "comp_threshold" } },
    { "xdelay", "DELAY", false, 0, 100, { "delay_steps", "delay_mode", "delay_feedback", "delay_pan", "delay_send" } },
    { "xswitch", "SWITCHES", true, 0, 100, { "dist_on", "pcf_on", "comp_on", "delay_on" } },
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
    // Maquina Oneiroi's randomizer switches (OneiroiTarget, OneiroiAmount)
    uint8_t oTarget = 0, oAmount = 0;

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
    // oscOn[0..2], "orand=target:amount:0" Oneiroi's switches (same
    // three-number shape).
    std::string toString() const
    {
        std::string out;
        for (int g = 0; g < kNumRandGroups; ++g) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%s%s=%d:%d:%d", g ? " " : "", kRandGroups[g].key, int(lock[g]),
                          int(min[g]), int(max[g]));
            out += buf;
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, " dosc=%d:%d:%d orand=%d:%d:0", int(oscOn[0]), int(oscOn[1]),
                      int(oscOn[2]), int(oTarget), int(oAmount));
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
            if (!std::strcmp(key, "orand")) {
                oTarget = uint8_t(std::clamp(l, 0, 3));
                oAmount = uint8_t(std::clamp(lo, 0, 2));
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

// ---------------------------------------------------------------------------
// Maquina Oneiroi / Oneiroi FX: the module's randomizer (Ui.h Randomize(),
// ParamController.h LockableParam::Randomize()), on the editor side, so the
// new values are ordinary parameter changes the host sees.
//
//   target  All, Oscillators, Looper, Effects: the knobs of groups "oosc",
//           "oloop", "ofx" (the module never randomizes faders, switches,
//           SHIFT functions or the modulation knobs)
//   amount  the AMOUNT switch: Full = a new value anywhere (10 % of the time
//           near the far end), Mid = up to 10 % of the range towards the
//           centre, Low = up to 2 %. Pitch moves in whole semitones (Mid:
//           2..4, Low: 1..2, up or down).
//
// Works on the module's 0..1 control positions (OneiroiControls.hpp) and
// rounds back to plugin units.

enum OneiroiTarget : uint8_t { kOneiroiAll = 0, kOneiroiOsc, kOneiroiLooper, kOneiroiFx };
enum OneiroiAmount : uint8_t { kOneiroiFull = 0, kOneiroiMid, kOneiroiLow };
static const char* const kOneiroiTargetLabels[] = { "All", "Osc", "Looper", "Effects" };
static const char* const kOneiroiAmountLabels[] = { "Full", "Mid", "Low" };

inline bool oneiroiTargets(const char* symbol, int target)
{
    const int g = randGroupOf(symbol);
    if (g < 0) return false;
    const char* key = kRandGroups[g].key;
    switch (target) {
    case kOneiroiOsc: return !std::strcmp(key, "oosc");
    case kOneiroiLooper: return !std::strcmp(key, "oloop");
    case kOneiroiFx: return !std::strcmp(key, "ofx");
    default: return !std::strcmp(key, "oosc") || !std::strcmp(key, "oloop") || !std::strcmp(key, "ofx");
    }
}

// LockableParam::Randomize() for one control position k (0..1); rf in [0, 1)
inline float oneiroiRandomKnob(float k, int amount, bool semitones, float rf)
{
    const float s1 = k < 0.5f ? 1.f : -1.f; // towards the centre
    const float s2 = rf < 0.5f ? 1.f : -1.f;
    float v;
    if (amount == kOneiroiFull)
        v = rf < 0.1f ? (k < 0.5f ? 0.9f : 0.1f) : rf;
    else if (amount == kOneiroiMid)
        v = semitones ? k + 0.083f * (rf < 0.1f ? 4.f : rf < 0.5f ? 3.f : 2.f) * s2 : k + rf * 0.1f * s1;
    else
        v = semitones ? k + 0.083f * (rf < 0.1f ? 2.f : 1.f) * s2 : k + rf * 0.02f * s1;
    v = std::clamp(v, 0.f, 1.f);
    if (semitones) v = std::rint(v * 12.f) * (1.f / 12.f); // Quantize(v, 12)
    return v;
}

// Randomize values[0 .. numAllParams(d)) in place (plugin units).
inline void oneiroiRandomize(const VoiceDef& d, int target, int amount, float* values, std::mt19937& rng)
{
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    for (uint32_t i = 0; i < d.numParams; ++i) {
        const VoiceParam& p = d.params[i];
        if (!oneiroiTargets(p.symbol, target)) continue;
        const bool semis = oneiroi::isPitch(p);
        const float k = oneiroi::toKnob(p, values[i]);
        values[i] = float(oneiroi::fromKnob(p, oneiroiRandomKnob(k, amount, semis, unit(rng))));
    }
}

} // namespace maquina
