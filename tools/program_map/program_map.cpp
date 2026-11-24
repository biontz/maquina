// program_map - the MIDI Bank Select / Program Change map of a voice
// (common/ProgramMap.hpp), as the plugins build it from the factory presets
// and the user library in ~/.config/maquina/presets/<voice>/.
//
//   program_map <voice>          JSON: banks (CC 0) with their presets in program
//                                order, each with every sound parameter's value
//   program_map --check          self test: a temporary user library (more than
//                                128 presets in one category, renamed and hidden
//                                factory presets, "#all" categories) and the
//                                factory-only map of every voice
//
// tools/export_user_presets.py reads the JSON, so the LV2 presets (Zynthian),
// the Machinista banks and the plugins all number the presets the same way.
// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "ProgramMap.hpp"
#include "VoiceTables.generated.hpp"

using namespace maquina;

static int voiceIndex(const char* id)
{
    for (uint32_t i = 0; i < sizeof(k_voices) / sizeof(k_voices[0]); ++i)
        if (!std::strcmp(k_voices[i].id, id)) return int(i);
    return -1;
}

static std::string json(const std::string& s)
{
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (uint8_t(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; continue; }
        o += c;
    }
    return o + "\"";
}

static int dump(int vi)
{
    const VoiceDef& d = k_voices[vi];
    const ProgramMap m = buildProgramMap(d, k_factory_presets[vi]);
    const uint32_t n = numAllParams(d);
    std::printf("{\n \"voice\": %s,\n \"banks\": [\n", json(d.id).c_str());
    for (size_t b = 0; b < m.banks.size(); ++b) {
        std::printf("  {\"msb\": %zu, \"name\": %s, \"presets\": [", b, json(m.bankNames[b]).c_str());
        for (size_t k = 0; k < m.banks[b].size(); ++k) {
            const ProgramEntry& e = m.banks[b][k];
            std::vector<float> v(n, 0.f);
            applyPreset(d, e.values.data(), uint32_t(e.values.size()), v.data());
            std::printf("%s\n   {\"lsb\": %zu, \"program\": %zu, \"name\": %s, \"user\": %s, \"values\": {",
                        k ? "," : "", k / 128, k % 128, json(e.name).c_str(), e.user ? "true" : "false");
            bool first = true;
            for (uint32_t i = 0; i < n; ++i) {
                if (!isSoundParam(d, i)) continue;
                std::printf("%s%s: %d", first ? "" : ", ", json(paramAt(d, i).symbol).c_str(), int(v[i]));
                first = false;
            }
            std::printf("}}");
        }
        std::printf("]}%s\n", b + 1 < m.banks.size() ? "," : "");
    }
    std::printf(" ]\n}\n");
    return 0;
}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void writeFile(const std::string& path, const std::string& text)
{
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); std::exit(2); }
    std::fputs(text.c_str(), f);
    std::fclose(f);
}

static int check()
{
    // 1. no user library: bank 0 = Init + factory, then the plugin's categories, then User
    char tmpl[] = "/tmp/maquina_program_map_XXXXXX";
    const char* tmp = mkdtemp(tmpl);
    if (!tmp) { std::perror("mkdtemp"); return 2; }
    setenv("XDG_CONFIG_HOME", tmp, 1);
    for (uint32_t vi = 0; vi < sizeof(k_voices) / sizeof(k_voices[0]); ++vi) {
        const VoiceDef& d = k_voices[vi];
        const VoicePresets& fp = k_factory_presets[vi];
        const ProgramMap m = buildProgramMap(d, fp);
        CHECK(m.banks[0].size() == 1 + fp.count);
        CHECK(m.banks[0][0].init && m.banks[0][0].name == "Init");
        for (uint32_t k = 0; k < fp.count; ++k) CHECK(m.banks[0][1 + k].name == fp.presets[k].name);
        const VoiceCategories* vc = categoriesOf(d.id);
        int nc = 0;
        while (vc && nc < 10 && vc->names[nc]) ++nc;
        CHECK(int(m.banks.size()) == nc + 2);
        CHECK(m.bankNames.back() == "User" && m.banks.back().empty());
        // each factory preset sits in its category's bank, in compiled order
        size_t placed = 0;
        for (int c = 0; c < nc; ++c) {
            CHECK(m.bankNames[size_t(c) + 1] == vc->names[c]);
            uint32_t last = 0;
            for (const ProgramEntry& e : m.banks[size_t(c) + 1]) {
                uint32_t k = 0;
                while (k < fp.count && e.name != fp.presets[k].name) ++k;
                CHECK(k < fp.count && !std::strcmp(fp.presets[k].category, vc->names[c]));
                CHECK(k >= last);
                last = k;
                ++placed;
            }
        }
        CHECK(placed == fp.count);
    }

    // 2. a user library for acida: 300 presets in one user category, a renamed
    //    factory category, a hidden factory preset, a user preset in a plugin category
    const int vi = voiceIndex("acida");
    CHECK(vi >= 0);
    const VoiceDef& d = k_voices[vi];
    const VoicePresets& fp = k_factory_presets[vi];
    const std::string dir = std::string(tmp) + "/maquina/presets/acida";
    mkdir((std::string(tmp) + "/maquina").c_str(), 0755);
    mkdir((std::string(tmp) + "/maquina/presets").c_str(), 0755);
    mkdir(dir.c_str(), 0755);
    writeFile(dir + "/categories.txt", "Big\n");
    const std::string renamedFrom = fp.presets[0].category, hiddenName = fp.presets[1].name;
    writeFile(dir + "/labelmap.txt", "cat\t" + renamedFrom + "\tRenamed\n");
    writeFile(dir + "/hidden.txt", hiddenName + "\n");
    for (int i = 0; i < 300; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "big %03d", i);
        writeFile(dir + "/" + name + ".maquina",
                  std::string("voice = acida\nname = ") + name + "\ncategory = Big\ncutoff = " + std::to_string(i % 128) + "\n");
    }
    writeFile(dir + "/zz.maquina", "voice = acida\nname = Zz Bass\ncategory = Bass\n");
    writeFile(dir + "/other.maquina", "voice = kick\nname = Not Acida\ncategory = Bass\n");
    {
        const ProgramMap m = buildProgramMap(d, fp);
        const int big = m.bankOf("Big");
        CHECK(big == int(m.bankNames.size()) - 2); // after the plugin's categories, before User
        CHECK(m.banks[size_t(big)].size() == 300);
        CHECK(m.at(uint32_t(big), 0, 0) && m.at(uint32_t(big), 0, 0)->name == "big 000");
        CHECK(m.at(uint32_t(big), 0, 127) && m.at(uint32_t(big), 0, 127)->name == "big 127");
        CHECK(m.at(uint32_t(big), 1, 0) && m.at(uint32_t(big), 1, 0)->name == "big 128");   // CC 32 = 1
        CHECK(m.at(uint32_t(big), 2, 43) && m.at(uint32_t(big), 2, 43)->name == "big 299");
        CHECK(m.at(uint32_t(big), 2, 44) == nullptr);
        CHECK(m.at(uint32_t(m.banks.size()), 0, 0) == nullptr);
        CHECK(m.banks.back().size() == 301); // User: every acida user preset, not the kick one
        // the renamed category: its factory presets moved out of the old name
        CHECK(m.bankOf(renamedFrom) > 0 && m.bankOf("Renamed") < 0); // not listed in categories.txt
        for (const ProgramEntry& e : m.banks[size_t(m.bankOf(renamedFrom))])
            CHECK(std::string(e.name) != fp.presets[0].name);
        // hidden: out of its category bank, still in bank 0
        bool inBank0 = false, inCat = false;
        for (const ProgramEntry& e : m.banks[0]) inBank0 |= e.name == hiddenName;
        for (size_t b = 1; b < m.banks.size(); ++b)
            for (const ProgramEntry& e : m.banks[b]) inCat |= e.name == hiddenName;
        CHECK(inBank0 && !inCat);
        // a user preset in a plugin category comes after its factory presets
        const auto& bass = m.banks[size_t(m.bankOf("Bass"))];
        CHECK(!bass.empty() && bass.back().name == "Zz Bass");
    }
    // 3. "#all": the full list replaces the plugin's categories
    writeFile(dir + "/categories.txt", "#all\nBig\nBass\n");
    {
        const ProgramMap m = buildProgramMap(d, fp);
        CHECK(m.bankNames.size() == 4 && m.bankNames[1] == "Big" && m.bankNames[2] == "Bass" && m.bankNames[3] == "User");
    }
    // 4. the signature changes with the library
    const uint64_t s1 = librarySignature(d);
    writeFile(dir + "/new.maquina", "voice = acida\nname = New\ncategory = Big\n");
    CHECK(librarySignature(d) != s1);
    std::string rm = std::string("rm -rf '") + tmp + "'";
    if (std::system(rm.c_str()) != 0) std::fprintf(stderr, "could not remove %s\n", tmp);
    std::printf("program_map --check: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--check")) return check();
    if (argc != 2) {
        std::fprintf(stderr, "usage: program_map <voice> | --check\n");
        return 2;
    }
    const int vi = voiceIndex(argv[1]);
    if (vi < 0) {
        std::fprintf(stderr, "unknown voice %s\n", argv[1]);
        return 2;
    }
    return dump(vi);
}
