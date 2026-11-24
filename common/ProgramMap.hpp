// ProgramMap.hpp - MIDI Bank Select (CC 0 / CC 32) + Program Change -> preset.
// Shared by the plugin (DrumPlugin.cpp, which loads the presets) and the
// editor (DrumUI.cpp, which shows the numbers), so both use one mapping.
//
//   bank (CC 0, MSB) 0         Init + the factory presets: the host program list
//   bank n = 1 .. C            the n-th preset category, in the editor browser's
//                              order: the plugin's categories (kPresetCategories),
//                              then the user's (categories.txt; a "#all" file is
//                              the whole list). The Machinista bank files
//                              (NN-<category>.json) and the LV2 pset:Bank list use
//                              the same category order.
//   bank C + 1                 "User": every user preset
//   program (PC) 0 .. 127      the preset at that place in the bank: the factory
//                              presets in their compiled order, then the user
//                              presets by name - the browser's order
//   CC 32 (LSB) k              presets 128 k .. 128 k + 127 of the bank, for
//                              banks with more than 128 presets
//
// The user library lives in userPresetDir(): *.maquina, categories.txt,
// tags.txt, labelmap.txt (renamed / deleted factory categories and tags) and
// hidden.txt (deleted factory presets). Hidden factory presets are left out of
// the category banks (as in the browser) but stay in bank 0 (the host's
// program list does not change).
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#endif

#include "FactoryPresets.generated.hpp" // VoicePresets (includes Presets.hpp)

namespace maquina {

// ---------------------------------------------------------------------------
// where the user library is

// ~/.config/maquina ($XDG_CONFIG_HOME/maquina; macOS: ~/Library/Application
// Support/Maquina; Windows: %APPDATA%\Maquina), no trailing slash; empty if unknown
inline std::string configDir()
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    return base ? std::string(base) + "\\Maquina" : std::string();
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/Library/Application Support/Maquina";
    return std::string();
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) return std::string(x) + "/maquina";
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/.config/maquina";
    return std::string();
#endif
}

inline std::string userPresetDir(const VoiceDef& d)
{
    const std::string base = configDir();
    if (base.empty()) return base;
#ifdef _WIN32
    return base + "\\presets\\" + d.id;
#else
    return base + "/presets/" + d.id;
#endif
}

inline std::string libFile(const VoiceDef& d, const char* name)
{
    const std::string dir = userPresetDir(d);
#ifdef _WIN32
    return dir.empty() ? dir : dir + "\\" + name;
#else
    return dir.empty() ? dir : dir + "/" + name;
#endif
}

// the non-empty lines of a text file, trailing '\r' and spaces removed
inline std::vector<std::string> readLines(const std::string& path)
{
    std::vector<std::string> v;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) v.push_back(line);
    }
    return v;
}

// ---------------------------------------------------------------------------
// the user library's lists

struct LabelMap { std::string kind, from, to; }; // kind "cat" or "tag"; to "" = deleted

struct PresetLibrary {
    std::vector<std::string> cats;   // the browser's categories, in order (without All / User / Rated)
    std::vector<std::string> tags;   // the tag chips (at most 32)
    std::vector<LabelMap> labelMap;  // applied to the factory presets
    std::vector<std::string> hidden; // deleted factory presets, by name
};

inline PresetLibrary loadLibrary(const VoiceDef& d)
{
    PresetLibrary lib;
    // categories.txt: "#all" then the full list (older files: the user's own, after the plugin's)
    const std::vector<std::string> lines = readLines(libFile(d, "categories.txt"));
    const bool full = !lines.empty() && lines[0] == "#all";
    const VoiceCategories* vc = categoriesOf(d.id);
    if (!full)
        for (int i = 0; vc && i < 10 && vc->names[i]; ++i) lib.cats.push_back(vc->names[i]);
    for (size_t k = full ? 1 : 0; k < lines.size(); ++k)
        if (std::find(lib.cats.begin(), lib.cats.end(), lines[k]) == lib.cats.end()) lib.cats.push_back(lines[k]);
    // tags.txt: the full tag list ("#all" first) or the user's own after the plugin's
    const std::vector<std::string> tl = readLines(libFile(d, "tags.txt"));
    const bool fullTags = !tl.empty() && tl[0] == "#all";
    if (!fullTags)
        for (int t = 0; t < kNumPresetTags; ++t) lib.tags.push_back(kPresetTags[t]);
    for (size_t k = fullTags ? 1 : 0; k < tl.size() && lib.tags.size() < 32; ++k)
        if (std::find(lib.tags.begin(), lib.tags.end(), tl[k]) == lib.tags.end()) lib.tags.push_back(tl[k]);
    // labelmap.txt: "cat|tag <TAB> old <TAB> new"
    for (const std::string& l : readLines(libFile(d, "labelmap.txt"))) {
        const size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        lib.labelMap.push_back({ l.substr(0, t1), l.substr(t1 + 1, t2 - t1 - 1), l.substr(t2 + 1) });
    }
    lib.hidden = readLines(libFile(d, "hidden.txt"));
    return lib;
}

// a factory preset's category or tag after the user's renames ("" = deleted)
inline std::string mapLabel(const std::vector<LabelMap>& map, const char* kind, std::string v)
{
    for (int hop = 0; hop < 16; ++hop) {
        bool moved = false;
        for (const auto& m : map)
            if (m.kind == kind && m.from == v) { v = m.to; moved = true; break; }
        if (!moved || v.empty()) break;
    }
    return v;
}

struct UserPreset {
    std::string name, category, path;
    std::vector<std::string> tags;
    std::vector<PresetValue> values;
};

// the user presets of a voice (*.maquina in userPresetDir()), by name
inline std::vector<UserPreset> loadUserPresets(const VoiceDef& d)
{
    std::vector<UserPreset> user;
#ifndef _WIN32
    const std::string dir = userPresetDir(d);
    if (DIR* dp = dir.empty() ? nullptr : opendir(dir.c_str())) {
        while (dirent* de = readdir(dp)) {
            const std::string f = de->d_name;
            if (f.size() <= kPresetExtLen || f.compare(f.size() - kPresetExtLen, kPresetExtLen, kPresetExt) != 0) continue;
            std::ifstream in(dir + "/" + f);
            std::stringstream ss;
            ss << in.rdbuf();
            PresetFile pf = parsePreset(d, ss.str());
            if (pf.name.empty() || pf.voice != d.id) continue;
            user.push_back({ pf.name, pf.category, dir + "/" + f, pf.tags, pf.values });
        }
        closedir(dp);
    }
    // by name; the file decides between equal names, so the order never depends on readdir
    std::sort(user.begin(), user.end(), [](const UserPreset& a, const UserPreset& b) {
        return a.name != b.name ? a.name < b.name : a.path < b.path;
    });
#else
    (void)d;
#endif
    return user;
}

// changes whenever a user preset or a library file is added, removed or
// rewritten (count, names, sizes, modification times); 0 if there is no folder
inline uint64_t librarySignature(const VoiceDef& d)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) { h ^= (v >> (8 * i)) & 0xff; h *= 1099511628211ull; }
    };
    auto mixFile = [&](const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) { mix(0); return; }
        mix(uint64_t(st.st_mtime));
        mix(uint64_t(st.st_size));
    };
    for (const char* f : { "categories.txt", "tags.txt", "labelmap.txt", "hidden.txt" })
        mixFile(libFile(d, f));
#ifndef _WIN32
    const std::string dir = userPresetDir(d);
    if (DIR* dp = dir.empty() ? nullptr : opendir(dir.c_str())) {
        std::vector<std::string> names;
        while (dirent* de = readdir(dp)) {
            const std::string f = de->d_name;
            if (f.size() > kPresetExtLen && f.compare(f.size() - kPresetExtLen, kPresetExtLen, kPresetExt) == 0)
                names.push_back(f);
        }
        closedir(dp);
        std::sort(names.begin(), names.end());
        mix(names.size());
        for (const std::string& f : names) {
            for (char c : f) mix(uint8_t(c));
            mixFile(dir + "/" + f);
        }
    }
#endif
    return h;
}

// ---------------------------------------------------------------------------
// the map

struct ProgramEntry {
    std::string name;
    std::vector<PresetValue> values; // empty + init = the Init patch (every sound parameter at its default)
    bool init = false;
    bool user = false;               // a user preset (else Init or a factory preset)
};

struct ProgramMap {
    std::vector<std::string> bankNames;            // [0] = "Factory", then the categories, then "User"
    std::vector<std::vector<ProgramEntry>> banks;  // presets per bank (any number; CC 32 pages them)

    // the preset for CC 0 = msb, CC 32 = lsb, PC = program; nullptr if none
    const ProgramEntry* at(uint32_t msb, uint32_t lsb, uint32_t program) const
    {
        if (msb >= banks.size()) return nullptr;
        const size_t k = size_t(lsb) * 128 + program;
        return k < banks[msb].size() ? &banks[msb][k] : nullptr;
    }
    // the bank of a category ("User" included), -1 if none
    int bankOf(const std::string& category) const
    {
        for (size_t b = 1; b < bankNames.size(); ++b)
            if (bankNames[b] == category) return int(b);
        return -1;
    }
};

inline ProgramMap buildProgramMap(const VoiceDef& d, const VoicePresets& factory, const PresetLibrary& lib,
                                  const std::vector<UserPreset>& user)
{
    ProgramMap m;
    // bank 0: Init + every factory preset (the host program list)
    m.bankNames.push_back("Factory");
    m.banks.emplace_back();
    {
        ProgramEntry init;
        init.name = "Init";
        init.init = true;
        m.banks[0].push_back(std::move(init));
    }
    for (uint32_t k = 0; k < factory.count; ++k) {
        const FactoryPreset& fp = factory.presets[k];
        m.banks[0].push_back({ fp.name, std::vector<PresetValue>(fp.values, fp.values + fp.numValues), false, false });
    }
    // banks 1..C: the categories; factory presets (not deleted, categories renamed) then user presets
    for (const std::string& cat : lib.cats) {
        m.bankNames.push_back(cat);
        std::vector<ProgramEntry> bank;
        for (uint32_t k = 0; k < factory.count; ++k) {
            const FactoryPreset& fp = factory.presets[k];
            if (std::find(lib.hidden.begin(), lib.hidden.end(), fp.name) != lib.hidden.end()) continue;
            if (mapLabel(lib.labelMap, "cat", fp.category) != cat) continue;
            bank.push_back({ fp.name, std::vector<PresetValue>(fp.values, fp.values + fp.numValues), false, false });
        }
        for (const UserPreset& u : user)
            if (u.category == cat) bank.push_back({ u.name, u.values, false, true });
        m.banks.push_back(std::move(bank));
    }
    // bank C + 1: every user preset
    m.bankNames.push_back("User");
    m.banks.emplace_back();
    for (const UserPreset& u : user) m.banks.back().push_back({ u.name, u.values, false, true });
    (void)d;
    return m;
}

inline ProgramMap buildProgramMap(const VoiceDef& d, const VoicePresets& factory)
{
    return buildProgramMap(d, factory, loadLibrary(d), loadUserPresets(d));
}

} // namespace maquina
