// oneiroi_ref.cpp - fidelity check for Maquina Oneiroi's engine.
//
// Runs the module's engine as vendored (befaco/oneiroi/third_party/oneiroi/Oneiroi.h, class
// Oneiroi) and Maquina's copy (befaco/oneiroi/engine/OneiroiCore.h) side by side on
// the same controls, the same input and the same random stream, and
// requires bit-identical output for every scenario: OneiroiCore without a
// note envelope is the module. Scenarios cover every source, every filter
// mode and position, resonator / echo / ambience extremes, all modulation
// types, recording (PRE and POST), clearing, and external sync.
//
// Also checks that OneiroiCore with a constant envelope of 1 is the module
// too, and (with --mutate) that the comparison notices a 1-ulp change.
//
// Build + run: make oneiroi-check (part of make test).
// SPDX-License-Identifier: GPL-3.0-only
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wreorder"
#endif
#include "oneiroi_unity.cpp"
#include "Oneiroi.h" // the module's engine, unmodified

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr int kBlock = 32;
constexpr int kBlocks = 1500 * 6; // 6 s at the module's block rate

struct Rig {
    OneiroiRng rng;
    PatchCtrls c {};
    PatchCvs v {};
    PatchState s {};
    Clock* clock = nullptr;
    AudioBuffer* buf = nullptr;
    Oneiroi* module = nullptr;
    OneiroiCore* core = nullptr;

    Rig(bool useCore, uint32_t seed)
    {
        rng.seed(seed);
        OneiroiRng::Scope scope(rng);
        s.sampleRate = 48000.f;
        s.blockRate = 1500.f;
        s.blockSize = kBlock;
        s.inputLevel = FloatArray::create(kBlock);
        s.efModLevel = FloatArray::create(kBlock);
        s.outLevel = 1.f;
        s.pitchZero = 2128.f / 4041.f;
        s.speedZero = 2192.f / 4041.f;
        s.startupPhase = StartupPhase::STARTUP_DONE;
        c.filterCutoffModAmount = 0.5f;
        c.looperSpeedCvAmount = c.looperStartCvAmount = c.looperLengthCvAmount = 1.f;
        c.oscPitchCvAmount = c.oscDetuneCvAmount = c.filterCutoffCvAmount = 1.f;
        c.resonatorTuneCvAmount = c.echoDensityCvAmount = c.ambienceSpacetimeCvAmount = 1.f;
        if (useCore) core = OneiroiCore::create(&c, &v, &s);
        else module = Oneiroi::create(&c, &v, &s);
        clock = Clock::create(&c, &s);
        buf = AudioBuffer::create(2, kBlock);
    }
    ~Rig()
    {
        if (core) OneiroiCore::destroy(core);
        if (module) Oneiroi::destroy(module);
        Clock::destroy(clock);
        AudioBuffer::destroy(buf);
    }
};

// a neutral patch: sine at C3, everything dry
void basePatch(PatchCtrls& c)
{
    c.inputVol = 0.f;
    c.looperVol = 0.f;
    c.looperSos = 0.f;
    c.looperFilter = 0.55f;
    c.looperSpeed = 0.766f; // ~1x
    c.looperStart = 0.f;
    c.looperLength = 1.f;
    c.osc1Vol = 0.5f;
    c.osc2Vol = 0.f;
    c.oscOctave = 4;
    c.oscPitch = 130.81f;
    c.oscUnison = 0.f;
    c.oscDetune = 0.2f;
    c.filterVol = 0.f;
    c.filterMode = 0.125f;
    c.filterCutoff = 0.8f;
    c.filterResonance = 0.2f;
    c.filterPosition = 0.125f;
    c.resonatorVol = 0.f;
    c.resonatorTune = 0.5f;
    c.resonatorFeedback = 0.5f;
    c.echoVol = 0.f;
    c.echoDensity = 0.5f;
    c.echoRepeats = 0.5f;
    c.echoFilter = 0.55f;
    c.ambienceVol = 0.f;
    c.ambienceSpacetime = 0.7f;
    c.ambienceDecay = 0.5f;
    c.ambienceAutoPan = 0.f;
    c.modLevel = 0.f;
    c.modSpeed = 0.5f;
    c.modType = 0.f;
}

struct Scenario {
    const char* name;
    std::function<void(PatchCtrls&)> setup;
    // per block: change controls / sync / input (block index, controls, state, input buffer)
    std::function<void(int, PatchCtrls&, PatchState&, AudioBuffer&)> perBlock;
};

void sineInput(int blk, AudioBuffer& b, float freq, float amp)
{
    for (int i = 0; i < kBlock; ++i) {
        const double t = double(blk * kBlock + i) / 48000.0;
        b.getSamples(0)[i] = float(amp * std::sin(2 * M_PI * freq * t));
        b.getSamples(1)[i] = float(amp * std::sin(2 * M_PI * freq * 1.01 * t));
    }
}

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> v;
    auto none = [](int, PatchCtrls&, PatchState&, AudioBuffer&) {};
    v.push_back({ "sine", [](PatchCtrls&) {}, none });
    v.push_back({ "sine unison", [](PatchCtrls& c) { c.oscUnison = 0.7f; }, none });
    v.push_back({ "supersaw", [](PatchCtrls& c) { c.osc2Vol = 0.6f; c.oscDetune = 0.8f; }, none });
    v.push_back({ "wavetable", [](PatchCtrls& c) { c.osc2Vol = 0.6f; c.oscUseWavetable = 1.f; c.oscDetune = 0.4f; }, none });
    v.push_back({ "looper noise 1x", [](PatchCtrls& c) { c.osc1Vol = 0; c.looperVol = 0.8f; }, none });
    v.push_back({ "looper -2x short", [](PatchCtrls& c) {
                     c.osc1Vol = 0; c.looperVol = 0.8f; c.looperSpeed = 0.f; c.looperLength = 0.05f;
                     c.looperStart = 0.3f; }, none });
    for (int m = 0; m < 4; ++m)
        for (int pos = 0; pos < 4; ++pos) {
            static char names[16][32];
            std::snprintf(names[m * 4 + pos], 32, "filter mode %d pos %d", m, pos + 1);
            v.push_back({ names[m * 4 + pos], [m, pos](PatchCtrls& c) {
                             c.osc2Vol = 0.5f; c.filterVol = 1.f; c.filterMode = 0.125f + 0.25f * m;
                             c.filterPosition = 0.125f + 0.25f * pos; c.filterCutoff = 0.4f;
                             c.filterResonance = 0.6f; c.resonatorVol = 0.5f; c.echoVol = 0.4f;
                             c.ambienceVol = 0.4f; }, none });
        }
    v.push_back({ "filter chaos noise", [](PatchCtrls& c) { c.filterVol = 1.f; c.filterResonance = 0.95f; }, none });
    v.push_back({ "resonator self-osc", [](PatchCtrls& c) {
                     c.resonatorVol = 1.f; c.resonatorFeedback = 1.f; c.resonatorDissonance = 0.6f;
                     c.resonatorTune = 0.3f; }, none });
    v.push_back({ "echo long", [](PatchCtrls& c) { c.echoVol = 1.f; c.echoRepeats = 0.95f; c.echoDensity = 0.9f;
                                                   c.echoFilter = 0.2f; }, none });
    v.push_back({ "ambience big fwd", [](PatchCtrls& c) { c.ambienceVol = 1.f; c.ambienceSpacetime = 0.99f;
                                                          c.ambienceDecay = 0.9f; c.ambienceAutoPan = 0.8f; }, none });
    v.push_back({ "ambience reverse", [](PatchCtrls& c) { c.ambienceVol = 1.f; c.ambienceSpacetime = 0.05f;
                                                          c.ambienceDecay = 0.7f; }, none });
    v.push_back({ "ambience extreme (wraps)", [](PatchCtrls& c) { c.ambienceVol = 1.f; c.ambienceSpacetime = 0.f; }, none });
    for (int t = 0; t <= 6; ++t) {
        static char names[7][32];
        std::snprintf(names[t], 32, "modulation type %d", t);
        v.push_back({ names[t], [t](PatchCtrls& c) {
                         c.osc2Vol = 0.5f; c.filterVol = 1.f; c.modLevel = 0.8f; c.modSpeed = 0.8f;
                         c.modType = t / 6.f; c.looperVol = 0.3f; }, none });
    }
    v.push_back({ "knob sweep", [](PatchCtrls& c) { c.osc2Vol = 0.5f; c.filterVol = 1.f; c.echoVol = 0.5f;
                                                    c.ambienceVol = 0.5f; c.resonatorVol = 0.5f; },
                  [](int blk, PatchCtrls& c, PatchState&, AudioBuffer&) {
                      const float x = 0.5f + 0.5f * std::sin(blk * 0.003f);
                      c.filterCutoff = x; c.echoDensity = 1.f - x; c.ambienceSpacetime = x;
                      c.resonatorTune = x; c.oscDetune = x; c.looperSpeed = x;
                      c.oscPitch = 100.f + 300.f * x; } });
    v.push_back({ "record input (PRE)", [](PatchCtrls& c) { c.osc1Vol = 0; c.inputVol = 0.5f; c.looperVol = 0.8f; },
                  [](int blk, PatchCtrls& c, PatchState&, AudioBuffer& in) {
                      sineInput(blk, in, 330.f, 0.5f);
                      c.looperRecording = blk > 300 && blk < 3000 ? 1.f : 0.f; } });
    v.push_back({ "record output (POST) + SOS", [](PatchCtrls& c) {
                     c.looperResampling = 1.f; c.looperVol = 0.6f; c.looperSos = 0.7f; c.osc2Vol = 0.4f; },
                  [](int blk, PatchCtrls& c, PatchState&, AudioBuffer&) {
                      c.looperRecording = blk > 100 && blk < 5000 ? 1.f : 0.f; } });
    v.push_back({ "clear looper", [](PatchCtrls& c) { c.osc1Vol = 0; c.looperVol = 0.8f; },
                  [](int blk, PatchCtrls&, PatchState& s, AudioBuffer&) { if (blk == 2000) s.clearLooperFlag = true; } });
    v.push_back({ "external sync", [](PatchCtrls& c) { c.echoVol = 1.f; c.modLevel = 0.6f; c.looperVol = 0.5f;
                                                       c.ambienceVol = 0.5f; c.ambienceAutoPan = 0.7f; },
                  [](int blk, PatchCtrls&, PatchState& s, AudioBuffer&) {
                      const bool on = blk % 750 < 2; // 2 Hz pulses
                      if (on != s.syncIn) { s.tempo->trigger(on, 0); s.syncIn = on; } } });
    return v;
}

// renders one scenario, returns the output (interleaved)
std::vector<float> render(const Scenario& sc, bool useCore, int coreGain, uint32_t seed)
{
    Rig rig(useCore, seed);
    basePatch(rig.c);
    sc.setup(rig.c);
    std::vector<float> out;
    out.reserve(size_t(kBlocks) * kBlock * 2);
    float ones[kBlock];
    for (float& o : ones) o = 1.f;
    for (int blk = 0; blk < kBlocks; ++blk) {
        OneiroiRng::Scope scope(rig.rng);
        rig.buf->clear();
        sc.perBlock(blk, rig.c, rig.s, *rig.buf);
        rig.clock->Process();
        if (rig.core) rig.core->Process(*rig.buf, coreGain ? ones : nullptr, true);
        else rig.module->Process(*rig.buf);
        for (int i = 0; i < kBlock; ++i) {
            out.push_back(rig.buf->getSamples(0)[i]);
            out.push_back(rig.buf->getSamples(1)[i]);
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    const bool mutate = argc > 1 && !std::strcmp(argv[1], "--mutate");
    int failures = 0;
    for (const Scenario& sc : scenarios()) {
        const std::vector<float> ref = render(sc, false, 0, 0x1234567u);
        double rms = 0.0, peak = 0.0;
        bool finite = true;
        for (float x : ref) {
            rms += double(x) * x;
            peak = std::max(peak, double(std::fabs(x)));
            finite = finite && std::isfinite(x);
        }
        rms = std::sqrt(rms / double(ref.size()));
        for (int gain = 0; gain < 2; ++gain) {
            std::vector<float> got = render(sc, true, gain, 0x1234567u);
            if (mutate) got[got.size() / 2] = std::nextafter(got[got.size() / 2], 1e9f);
            size_t diff = 0, first = 0;
            for (size_t i = 0; i < ref.size(); ++i)
                if (std::memcmp(&ref[i], &got[i], sizeof(float)) != 0) {
                    if (!diff) first = i;
                    ++diff;
                }
            if (diff || !finite) {
                std::printf("FAIL %-28s %s: %zu samples differ (first at %zu)%s\n", sc.name,
                            gain ? "gain 1" : "no gain", diff, first / 2, finite ? "" : ", non-finite output");
                ++failures;
            }
        }
        std::printf("  %-28s rms %.4f peak %.3f%s\n", sc.name, rms, peak, rms < 1e-6 ? "  (silent!)" : "");
    }
    if (mutate) {
        // the mutation must be caught by every comparison
        const int expected = int(scenarios().size()) * 2;
        std::printf("oneiroi-ref --mutate: %d of %d comparisons failed (all must)\n", failures, expected);
        return failures == expected ? 0 : 1;
    }
    std::printf("oneiroi-ref: %s (%zu scenarios x 6 s, OneiroiCore vs Oneiroi.h, bit-exact)\n",
                failures ? "FAILED" : "OK", scenarios().size());
    return failures ? 1 : 0;
}
