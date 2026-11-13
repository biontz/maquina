// check_presets - factory presets: every source file in presets/<voice>/
// parses (Presets.hpp) to exactly the compiled table entry, and a preset
// written by writePreset() (the editor's Save) reads back to the same values.
// Run from the repository root.
// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <vector>

#include "FactoryPresets.generated.hpp"
#include "VoiceTables.generated.hpp"

using namespace maquina;

int main()
{
    int bad = 0, files = 0;
    for (uint32_t vi = 0; vi < sizeof(k_voices) / sizeof(k_voices[0]); ++vi) {
        const VoiceDef& d = k_voices[vi];
        const VoicePresets& fp = k_factory_presets[vi];
        const uint32_t n = numAllParams(d);
        const std::string dir = std::string("presets/") + d.id;
        DIR* dp = opendir(dir.c_str());
        if (!dp) { std::printf("no %s\n", dir.c_str()); return 1; }
        uint32_t found = 0;
        while (dirent* e = readdir(dp)) {
            const std::string f = e->d_name;
            if (f.size() <= kPresetExtLen || f.substr(f.size() - kPresetExtLen) != kPresetExt) continue;
            std::ifstream in(dir + "/" + f);
            std::stringstream ss; ss << in.rdbuf();
            const PresetFile pf = parsePreset(d, ss.str());
            ++files;
            if (!pf.error.empty()) { std::printf("%s/%s: %s\n", dir.c_str(), f.c_str(), pf.error.c_str()); ++bad; continue; }
            const FactoryPreset* fac = nullptr;
            for (uint32_t k = 0; k < fp.count; ++k)
                if (pf.name == fp.presets[k].name) fac = &fp.presets[k];
            if (!fac) { std::printf("%s: '%s' not compiled (run tools/gen_presets.py)\n", d.id, pf.name.c_str()); ++bad; continue; }
            ++found;
            std::vector<float> a(n, 0.f), b(n, 0.f);
            applyPreset(d, pf.values.data(), uint32_t(pf.values.size()), a.data());
            applyPreset(d, fac->values, fac->numValues, b.data());
            if (a != b || pf.category != fac->category || joinTags(pf.tags) != fac->tags) {
                std::printf("%s: '%s' differs from the compiled table (run tools/gen_presets.py)\n", d.id, pf.name.c_str()); ++bad;
            }
            // Save -> Load round trip
            const PresetFile back = parsePreset(d, writePreset(d, pf.name, pf.category, pf.tags, a.data()));
            std::vector<float> c(n, 0.f);
            applyPreset(d, back.values.data(), uint32_t(back.values.size()), c.data());
            if (!back.error.empty() || c != a || back.name != pf.name || back.category != pf.category || back.tags != pf.tags) {
                std::printf("%s: '%s' does not survive write/parse %s\n", d.id, pf.name.c_str(), back.error.c_str()); ++bad;
            }
        }
        closedir(dp);
        if (found != fp.count) { std::printf("%s: %u files, %u compiled\n", d.id, found, fp.count); ++bad; }
        // trigger settings are never touched by a preset
        std::vector<float> v(n, 42.f);
        for (uint32_t k = 0; k < fp.count; ++k) {
            applyPreset(d, fp.presets[k].values, fp.presets[k].numValues, v.data());
            for (uint32_t i = 0; i < n; ++i)
                if (!isSoundParam(d, i) && v[i] != 42.f) { std::printf("%s: preset changed %s\n", d.id, paramAt(d, i).symbol); ++bad; }
        }
    }
    std::printf("check_presets: %s (%d files)\n", bad ? "FAILED" : "ok", files);
    return bad ? 1 : 0;
}
