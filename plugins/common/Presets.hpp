// Presets.hpp - preset format and helpers shared by DSP, editor and tools.
//
// A preset file (.maquina, UTF-8 text) - the factory sources in presets/ and
// user presets in ~/.config/maquina/presets/<voice>/ use the same format:
//
//   # Maquina preset
//   voice = kick
//   name = 808 Long Boom
//   category = 808
//   tags = long, boomy, clean
//   osc_coarse = -31          (plugin units, as shown in the editor)
//   osc_wave = Sine           (selectors may use the label)
//
// Parameters not listed take their default. A preset holds the sound only:
// the trigger settings (Trigger Note, Hat, Note > Pitch; the parameters in no
// Randomizer group) are never changed by loading a preset.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ExtraParams.hpp"
#include "Randomizer.hpp"

namespace maquina {

// file extension of preset files (factory sources and user presets)
constexpr char kPresetExt[] = ".maquina";
constexpr size_t kPresetExtLen = sizeof(kPresetExt) - 1;

// ---------------------------------------------------------------------------
// categories and tags

struct VoiceCategories {
    const char* voice;
    const char* names[10];
};
static const VoiceCategories kPresetCategories[] = {
    { "kick", { "808", "909", "Acoustic", "Sub", "FM", "Distorted", "Lo-Fi", "FX" } },
    { "tom", { "808", "909", "Acoustic", "Perc", "FM", "FX" } },
    { "snare", { "808", "909", "Acoustic", "Rim", "Clap", "Noise", "Lo-Fi", "FX" } },
    { "cymbal", { "Analog", "Metallic", "Noise", "Acoustic", "Lo-Fi", "FX" } },
    { "hihat", { "Analog", "Metallic", "Noise", "Acoustic", "Lo-Fi", "FX" } },
};
static const char* const kPresetTags[] = { "short", "long", "punchy", "boomy", "soft", "bright", "dark",
                                           "clean", "dirty", "metallic", "noisy", "pitched", "lo-fi",
                                           "modulated" };
constexpr int kNumPresetTags = int(sizeof(kPresetTags) / sizeof(kPresetTags[0]));

inline const VoiceCategories* categoriesOf(const char* voiceId)
{
    for (const auto& c : kPresetCategories)
        if (!std::strcmp(c.voice, voiceId)) return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// what a preset sets

inline bool isSoundParam(const VoiceDef& d, uint32_t i) { return randGroupOf(paramAt(d, i).symbol) >= 0; }

struct PresetValue { uint16_t param; int16_t value; };

// compiled factory preset (FactoryPresets.generated.hpp)
struct FactoryPreset {
    const char* name;
    const char* category;
    const char* tags; // "long, boomy"
    const PresetValue* values;
    uint16_t numValues;
};

// Sound parameters to their defaults, then the preset's values; trigger
// settings in values[] are left as they are.
inline void applyPreset(const VoiceDef& d, const PresetValue* pv, uint32_t n, float* values)
{
    for (uint32_t i = 0; i < numAllParams(d); ++i)
        if (isSoundParam(d, i)) values[i] = float(paramAt(d, i).def);
    for (uint32_t k = 0; k < n; ++k)
        if (pv[k].param < numAllParams(d) && isSoundParam(d, pv[k].param)) values[pv[k].param] = float(pv[k].value);
}

// ---------------------------------------------------------------------------
// text format

struct PresetFile {
    std::string voice, name, category;
    std::vector<std::string> tags;
    std::vector<PresetValue> values;
    std::string error; // empty if parsed
};

namespace preset_detail {
inline std::string trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
} // namespace preset_detail

inline std::vector<std::string> splitTags(const std::string& s)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= s.size()) {
        const size_t c = s.find(',', p);
        const std::string t = preset_detail::trim(s.substr(p, c == std::string::npos ? std::string::npos : c - p));
        if (!t.empty()) out.push_back(t);
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return out;
}

inline std::string joinTags(const std::vector<std::string>& tags)
{
    std::string out;
    for (size_t i = 0; i < tags.size(); ++i) out += (i ? ", " : "") + tags[i];
    return out;
}

// parse text for voice d; unknown keys or bad values make error non-empty
// (the valid lines are still used)
inline PresetFile parsePreset(const VoiceDef& d, const std::string& text)
{
    using preset_detail::trim;
    PresetFile pf;
    size_t pos = 0;
    int lineNo = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { pf.error = "line " + std::to_string(lineNo) + ": no '='"; continue; }
        std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "voice") { pf.voice = val; continue; }
        if (key == "name") { pf.name = val; continue; }
        if (key == "category") { pf.category = val; continue; }
        if (key == "tags") { pf.tags = splitTags(val); continue; }
        // HiHat presets from before the single Amp Decay: the closed decay is it
        if (key == "amp_decay_open") continue;
        if (key == "amp_decay_closed") key = "amp_decay";
        int idx = -1;
        for (uint32_t i = 0; i < numAllParams(d); ++i)
            if (key == paramAt(d, i).symbol) { idx = int(i); break; }
        if (idx < 0) { pf.error = "unknown parameter '" + key + "'"; continue; }
        const VoiceParam& p = paramAt(d, uint32_t(idx));
        int v = 0;
        bool ok = false;
        if (p.labels && p.kind != ParamKind::Int)
            for (int l = 0; l < p.numLabels; ++l)
                if (val == p.labels[l]) { v = l; ok = true; break; }
        if (!ok) {
            char* end = nullptr;
            const long n = std::strtol(val.c_str(), &end, 10);
            ok = end && *end == '\0' && !val.empty();
            v = int(n);
        }
        const int lo = p.kind == ParamKind::Int ? p.fwMin + p.offset : 0;
        const int hi = p.kind == ParamKind::Int ? p.fwMax + p.offset
                     : (p.kind == ParamKind::Toggle || p.kind == ParamKind::Retrigger) ? 1 : p.numLabels - 1;
        if (!ok || v < lo || v > hi) { pf.error = key + ": bad value '" + val + "'"; continue; }
        pf.values.push_back({ uint16_t(idx), int16_t(v) });
    }
    if (pf.voice.empty()) pf.voice = d.id;
    else if (pf.voice != d.id && pf.error.empty()) pf.error = "preset is for " + pf.voice;
    if (pf.name.empty() && pf.error.empty()) pf.error = "no name";
    return pf;
}

// text of a preset with every sound parameter (selectors by label)
inline std::string writePreset(const VoiceDef& d, const std::string& name, const std::string& category,
                               const std::vector<std::string>& tags, const float* values)
{
    std::string out = "# Maquina preset\n";
    out += "voice = " + std::string(d.id) + "\n";
    out += "name = " + name + "\n";
    out += "category = " + category + "\n";
    out += "tags = " + joinTags(tags) + "\n";
    for (uint32_t i = 0; i < numAllParams(d); ++i) {
        if (!isSoundParam(d, i)) continue;
        const VoiceParam& p = paramAt(d, i);
        const int v = int(values[i] + (values[i] < 0 ? -0.5f : 0.5f));
        out += std::string(p.symbol) + " = ";
        if (p.kind == ParamKind::Enum && p.labels && v >= 0 && v < p.numLabels) out += p.labels[v];
        else out += std::to_string(v);
        out += "\n";
    }
    return out;
}

} // namespace maquina
