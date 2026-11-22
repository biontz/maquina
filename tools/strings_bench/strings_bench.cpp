// strings_bench - Maquina Strings' engine CPU load per scenario (StringsEngine, no plugin
// around it): renders N seconds of 8 held notes at the engine rate and prints the CPU time
// per second of audio. On another machine than the Pi the Pi 4 figure is an estimate:
// the load here times a factor (--pi-factor, default 5: a Cortex-A72 at 1.5 GHz against an
// i7-9750H core on scalar float DSP; 4..7 is the plausible range).
//
//   strings_bench [--seconds S] [--pi-factor F] [--scenario NAME]
//
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "StringsEngine.hpp"

using namespace maquina::strings;

namespace {

struct Scenario {
    const char* name;
    const char* what;
    void (*setup)(Params&);
};

const Scenario kScenarios[] = {
    { "default", "the factory sound: both layers fading, unison 1, LP, ensemble, reverb", [](Params&) {} },
    { "worst", "both layers all the time, saw+PWM, 3 footages, unison 4, comb, ensemble, reverb",
      [](Params& p) {
          p.blend = 0.5f;
          p.blendShift = 0.f;
          p.blendVelo = 0.f;
          p.reg = 0.25f; // Ensemble: all three footages
          p.shape = 2;
          p.unison = 4;
          p.filterMode = 3;
          p.reso = 0.8f;
          p.fxMode = 0;
          p.reverb = 0.6f;
      } },
    { "strings", "strings only (BLEND 0), ensemble, reverb",
      [](Params& p) {
          p.blend = 0.f;
          p.blendShift = 0.f;
          p.blendVelo = 0.f;
          p.reg = 0.25f;
      } },
    { "wave", "wave only (BLEND 1), unison 1, LP",
      [](Params& p) {
          p.blend = 1.f;
          p.blendShift = 0.f;
          p.blendVelo = 0.f;
      } },
    { "wave4", "wave only, unison 4, comb",
      [](Params& p) {
          p.blend = 1.f;
          p.blendShift = 0.f;
          p.blendVelo = 0.f;
          p.unison = 4;
          p.filterMode = 3;
      } },
    { "fx-off", "the worst case without effects",
      [](Params& p) {
          kScenarios[1].setup(p);
          p.fxAmount = 0.f;
          p.reverb = 0.f;
      } },
    { "saw", "strings only, saw", [](Params& p) { kScenarios[2].setup(p); p.shape = 0; } },
    { "pwm", "strings only, PWM", [](Params& p) { kScenarios[2].setup(p); p.shape = 1; } },
    { "cello", "strings only, Cello (2 footages), saw", [](Params& p) { kScenarios[2].setup(p); p.shape = 0; p.reg = 0.f; } },
    { "phaser", "the worst case, PHASER", [](Params& p) { kScenarios[1].setup(p); p.fxMode = 1; } },
    { "chorus", "the worst case, CHORUS", [](Params& p) { kScenarios[1].setup(p); p.fxMode = 2; } },
    { "formant", "the worst case, FORMANT", [](Params& p) { kScenarios[1].setup(p); p.fxMode = 3; } },
};

double cpuSeconds()
{
    timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
}

} // namespace

int main(int argc, char** argv)
{
    double seconds = 10, piFactor = 5;
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--pi-factor") && i + 1 < argc) piFactor = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--scenario") && i + 1 < argc) only = argv[++i];
        else {
            std::fprintf(stderr, "usage: %s [--seconds S] [--pi-factor F] [--scenario NAME]\n", argv[0]);
            return 2;
        }
    }
    std::printf("%-9s %9s %8s %10s  %s\n", "scenario", "ms/s", "load", "Pi 4 est.", "what");
    int bad = 0;
    for (const Scenario& sc : kScenarios) {
        if (!only.empty() && only != sc.name) continue;
        Engine e(1);
        Params p;
        sc.setup(p);
        e.setParams(p);
        // 8 notes: a spread chord over three octaves (the split at C4 sees both sides)
        static const uint8_t kNotes[8] = { 36, 43, 48, 55, 60, 64, 67, 72 };
        for (uint8_t n : kNotes) e.noteOn(n, 100);
        float l[kMaxBlock], r[kMaxBlock];
        const long blocks = long(seconds * kRate / kMaxBlock);
        // warm up: the attack and the blend ramp settle (not timed)
        for (long b = 0; b < long(0.5 * kRate / kMaxBlock); ++b) e.render(l, r, kMaxBlock);
        double peak = 0, sum2 = 0;
        bool finite = true;
        const double t0 = cpuSeconds();
        for (long b = 0; b < blocks; ++b) {
            e.render(l, r, kMaxBlock);
            for (int i = 0; i < kMaxBlock; ++i) {
                finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
                peak = std::fmax(peak, std::fmax(std::fabs(l[i]), std::fabs(r[i])));
                sum2 += double(l[i]) * l[i] + double(r[i]) * r[i];
            }
        }
        const double cpu = cpuSeconds() - t0;
        const double audio = double(blocks) * kMaxBlock / kRate;
        const double load = cpu / audio;
        const double rms = std::sqrt(sum2 / (2.0 * double(blocks) * kMaxBlock));
        std::printf("%-9s %9.1f %7.1f%% %9.0f%%  %s  [voices %d, peak %.2f, rms %.3f%s]\n", sc.name, 1000.0 * load,
                    100.0 * load, 100.0 * load * piFactor, sc.what, e.activeVoices(), peak, rms,
                    finite ? "" : ", NOT FINITE");
        if (!finite || peak == 0.0) ++bad;
    }
    std::printf("(load = CPU time / audio time on one core here; Pi 4 est. = load x %.1f)\n", piFactor);
    return bad ? 1 : 0;
}
