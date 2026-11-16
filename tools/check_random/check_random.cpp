// check_random - the randomizer (plugins/common/Randomizer.hpp) for every
// voice: locked groups and trigger parameters never change, knob values stay
// inside the group's min..max window, every value is an integer in range, the
// filter is never switched off, and the settings string round-trips.
// Maquina Oneiroi's own randomizer: only the target's knobs change, by no
// more than the amount allows, pitch in whole semitones.
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
        // a parameter in no group must be a trigger/extra one (gain: a mix control)
        for (uint32_t i = 0; i < n; ++i) {
            const VoiceParam& p = paramAt(d, i);
            static const char* const kOneiroiPerformance[] = { "loop_record", "loop_source", "loop_clear", "sync" };
            bool oneiroiPerformance = false;
            for (const char* sym : kOneiroiPerformance) oneiroiPerformance |= !std::strcmp(p.symbol, sym);
            const bool trigger = !std::strcmp(p.symbol, "gain") ? true
                                 : maquina::isOneiroi(d.voice) ? oneiroiPerformance
                                 : maquina::hasChannels(d.voice)
                                     ? !std::strcmp(p.symbol, "trig_mode") || std::strstr(p.symbol, "_mute") != nullptr
                                     : p.page == 7;
            if (randGroupOf(p.symbol) < 0 && !trigger) {
                std::printf("%s.%s is in no randomize group\n", d.id, p.symbol); ++bad;
            }
        }
    }

    // Maquina Dredrum / Plethora: ROLL touches only its oscillator/channel; RANDOM skips ones switched off
    for (const VoiceDef& d : k_voices) {
        if (!hasChannels(d.voice)) continue;
        const uint32_t n = numAllParams(d);
        RandSettings rs;
        for (int g = 0; g < kNumRandGroups; ++g) { rs.lock[g] = false; rs.min[g] = 0; rs.max[g] = 100; }
        std::mt19937 rng(5);
        for (int osc = -1; osc < 3; ++osc) {
            bool movedOwn = false;
            for (int round = 0; round < 20; ++round) {
                std::vector<float> v(n), init(n);
                for (uint32_t i = 0; i < n; ++i) init[i] = v[i] = float(paramAt(d, i).def);
                RandSettings r2 = rs;
                if (osc < 0) r2.oscOn[1] = false; // main RANDOM with OSC 2 off
                randomize(d, r2, v.data(), rng, osc);
                for (uint32_t i = 0; i < n; ++i) {
                    const int o = channelOf(d, paramAt(d, i).symbol);
                    const bool allowed = osc >= 0 ? o == osc : o != 1;
                    if (v[i] != init[i] && !allowed) {
                        std::printf("%s: %s changed (roll %d)\n", d.id, paramAt(d, i).symbol, osc); ++bad;
                    }
                    if (v[i] != init[i] && allowed) movedOwn = true;
                }
            }
            if (!movedOwn) { std::printf("%s: roll %d changed nothing\n", d.id, osc); ++bad; }
        }
        RandSettings on;
        on.oscOn[0] = false; on.oscOn[2] = false;
        RandSettings back;
        back.fromString(on.toString().c_str());
        if (back.oscOn[0] || !back.oscOn[1] || back.oscOn[2]) { std::printf("dredrum: oscOn state round trip\n"); ++bad; }
    }

    // Maquina Oneiroi / FX: the module's randomizer
    for (const VoiceDef& d : k_voices) {
        if (!isOneiroi(d.voice)) continue;
        const uint32_t n = numAllParams(d);
        std::mt19937 orng(9);
        for (int target = 0; target < 4; ++target)
            for (int amount = 0; amount < 3; ++amount) {
                bool moved = false;
                for (int round = 0; round < 200; ++round) {
                    std::vector<float> v(n), before(n);
                    for (uint32_t i = 0; i < n; ++i) v[i] = float(paramAt(d, i).def);
                    // start from a random knob position each round
                    for (uint32_t i = 0; i < d.numParams; ++i)
                        if (oneiroiTargets(d.params[i].symbol, target))
                            v[i] = float(oneiroi::fromKnob(d.params[i], float(orng() % 1000) / 999.f));
                    before = v;
                    oneiroiRandomize(d, target, amount, v.data(), orng);
                    for (uint32_t i = 0; i < n; ++i) {
                        const VoiceParam& p = paramAt(d, i);
                        if (v[i] == before[i]) continue;
                        moved = true;
                        if (!oneiroiTargets(p.symbol, target)) {
                            std::printf("%s.%s changed outside target %d\n", d.id, p.symbol, target); ++bad;
                            continue;
                        }
                        if (v[i] != std::round(v[i]) || v[i] < lo(p) || v[i] > hi(p)) {
                            std::printf("%s.%s: %g out of range\n", d.id, p.symbol, v[i]); ++bad;
                        }
                        const bool pitch = oneiroi::isPitch(p);
                        if (pitch && std::fmod(std::fabs(v[i] + 60.f), 10.f) != 0.f) {
                            std::printf("%s.osc_pitch %g: not a whole semitone\n", d.id, v[i]); ++bad;
                        }
                        // knob movement: Mid <= 10 % (pitch 4 semitones), Low <= 2 % (2 semitones), plus rounding
                        const float dk = std::fabs(oneiroi::toKnob(p, v[i]) - oneiroi::toKnob(p, before[i]));
                        const float limit = amount == kOneiroiFull ? 1.f
                                            : pitch ? (amount == kOneiroiMid ? 4.5f : 2.5f) / 12.f + 0.05f
                                                    : (amount == kOneiroiMid ? 0.10f : 0.02f) + 0.02f;
                        if (dk > limit) {
                            std::printf("%s.%s moved %.3f > %.3f (amount %d)\n", d.id, p.symbol, dk, limit, amount);
                            ++bad;
                        }
                    }
                }
                const bool noOsc = d.voice == kOneiroiFxVoice && target == kOneiroiOsc; // FX: no oscillators
                if (!moved && !noOsc) { std::printf("%s: target %d amount %d changed nothing\n", d.id, target, amount); ++bad; }
            }
    }
    {
        RandSettings o;
        o.oTarget = 2;
        o.oAmount = 1;
        RandSettings back;
        back.fromString(o.toString().c_str());
        if (back.oTarget != 2 || back.oAmount != 1) { std::printf("oneiroi: orand state round trip\n"); ++bad; }
    }

    RandSettings a;
    for (int g = 0; g < kNumRandGroups; ++g) {
        a.lock[g] = g % 2;
        a.min[g] = uint8_t((g * 7) % 55);       // < 55
        a.max[g] = uint8_t(60 + (g * 3) % 40); // 60..99, >= min
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
