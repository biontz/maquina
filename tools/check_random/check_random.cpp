// check_random - the randomizer (plugins/common/Randomizer.hpp) for every
// voice: locked groups and trigger parameters never change, knob values stay
// inside the group's min..max window, every value is an integer in range, the
// filter is never switched off, and the settings string round-trips.
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstring>
#include <string>
#include <cstdio>
#include <vector>

#include "Randomizer.hpp"
#include "VoiceTables.generated.hpp"

using namespace maquina;

static float lo(const VoiceParam& p) { return p.kind == ParamKind::Int ? float(p.fwMin + p.offset) : 0.f; }
static float hi(const VoiceParam& p)
{
    switch (p.kind) {
    case ParamKind::Int: return float(p.fwMax + p.offset);
    case ParamKind::Toggle:
    case ParamKind::Retrigger: return 1.f;
    default: return float(p.numLabels - 1);
    }
}

int main()
{
    int bad = 0;
    std::mt19937 rng(1234);
    static const int kWindows[][2] = { { 0, 100 }, { 40, 100 }, { 0, 30 }, { 50, 50 }, { 20, 80 } };
    for (const VoiceDef& d : k_voices) {
        const uint32_t n = numAllParams(d);
        std::vector<float> init(n);
        for (uint32_t i = 0; i < n; ++i) init[i] = float(paramAt(d, i).def);

        for (const auto& w : kWindows) {
            for (int locked = 0; locked < kNumRandGroups; ++locked) {
                RandSettings rs;
                for (int g = 0; g < kNumRandGroups; ++g) {
                    rs.lock[g] = g == locked;
                    rs.min[g] = uint8_t(w[0]);
                    rs.max[g] = uint8_t(w[1]);
                }
                std::vector<float> v = init;
                bool moved = false;
                for (int round = 0; round < 150; ++round) {
                    randomize(d, rs, v.data(), rng);
                    for (uint32_t i = 0; i < n; ++i) {
                        const VoiceParam& p = paramAt(d, i);
                        const int g = randGroupOf(p.symbol);
                        if (v[i] != std::round(v[i]) || v[i] < lo(p) || v[i] > hi(p)) {
                            std::printf("%s.%s: %g out of range\n", d.id, p.symbol, v[i]); ++bad;
                        }
                        if ((g < 0 || g == locked) && v[i] != init[i]) {
                            std::printf("%s.%s changed although %s\n", d.id, p.symbol, g < 0 ? "never randomized" : "locked"); ++bad;
                        }
                        if (g >= 0 && g != locked && p.kind == ParamKind::Int) {
                            const float span = hi(p) - lo(p);
                            const float a = lo(p) + std::round(span * w[0] / 100.f), b = lo(p) + std::round(span * w[1] / 100.f);
                            if (v[i] < a - 0.5f || v[i] > b + 0.5f) {
                                std::printf("%s.%s: %g outside %d..%d %%\n", d.id, p.symbol, v[i], w[0], w[1]); ++bad;
                            }
                        }
                        if (!std::strcmp(p.symbol, "filter_type") && g != locked && p.labels[int(v[i])] == std::string("Off")) {
                            std::printf("%s: filter switched off\n", d.id); ++bad;
                        }
                        if (v[i] != init[i]) moved = true;
                    }
                }
                if (!moved) { std::printf("%s: nothing changed\n", d.id); ++bad; }
            }
        }
        // a parameter in no group must be a trigger/extra one
        for (uint32_t i = 0; i < n; ++i) {
            const VoiceParam& p = paramAt(d, i);
            if (randGroupOf(p.symbol) < 0 && p.page != 7) {
                std::printf("%s.%s is in no randomize group\n", d.id, p.symbol); ++bad;
            }
        }
    }

    RandSettings a;
    for (int g = 0; g < kNumRandGroups; ++g) {
        a.lock[g] = g % 2;
        a.min[g] = uint8_t(g * 7);
        a.max[g] = uint8_t(60 + g * 4);
    }
    RandSettings b;
    b.fromString(a.toString().c_str());
    for (int g = 0; g < kNumRandGroups; ++g)
        if (a.lock[g] != b.lock[g] || a.min[g] != b.min[g] || a.max[g] != b.max[g]) {
            std::printf("state round trip: %s\n", kRandGroups[g].key); ++bad;
        }
    RandSettings c;
    c.fromString("bogus=1:2:3 filter=1:90:10 osc=0:5:300");
    int fi = -1, oi = -1;
    for (int g = 0; g < kNumRandGroups; ++g) {
        if (!std::strcmp(kRandGroups[g].key, "filter")) fi = g;
        if (!std::strcmp(kRandGroups[g].key, "osc")) oi = g;
    }
    if (!c.lock[fi] || c.min[fi] != 90 || c.max[fi] != 90 || c.lock[oi] || c.min[oi] != 5 || c.max[oi] != 100) {
        std::printf("state parse/clamp failed\n"); ++bad;
    }

    std::printf("check_random: %s\n", bad ? "FAILED" : "ok");
    return bad ? 1 : 0;
}
