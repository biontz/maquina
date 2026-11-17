// la50_test - Maquina LA-50's checks (there is no D-50 reference to compare
// the sound with bit for bit; these check the behaviour).
//
//   la50_test --dump FILE.syx    every patch of a file: "NN name" + hex bytes
//                                (compared with tools/d50_syx.py's reading by `make la50-check`)
//   la50_test                    SysEx round trip / errors, engine behaviour
//                                (pitch, structures, key modes, solo,
//                                polyphony, hold, release, no NaN), host
//                                (rates, random blocks, underruns)
//
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "D50Sysex.hpp"
#include "La50Engine.hpp"
#include "La50Fx.hpp"
#include "La50Host.hpp"
#include "VoiceTables.generated.hpp"

using namespace la50;

namespace {

int gFails = 0;
void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++gFails;
}

void initPatch(Engine& e) { std::memcpy(e.patch().data(), kInitPatch, kPatchBytes); }

struct Render {
    std::vector<float> u, l;
    bool finite = true;
};

// render n frames, calling `events(frame)` before each 32-frame block
template <class F>
Render render(Engine& e, int frames, F events)
{
    Render r;
    r.u.resize(size_t(frames));
    r.l.resize(size_t(frames));
    for (int f = 0; f < frames; f += 32) {
        events(f);
        e.process(&r.u[size_t(f)], &r.l[size_t(f)], std::min(32, frames - f));
    }
    for (int i = 0; i < frames; ++i) r.finite &= std::isfinite(r.u[size_t(i)]) && std::isfinite(r.l[size_t(i)]);
    return r;
}

double rms(const std::vector<float>& x, size_t a, size_t b)
{
    double e = 0;
    for (size_t i = a; i < b && i < x.size(); ++i) e += double(x[i]) * x[i];
    return b > a ? std::sqrt(e / double(b - a)) : 0.0;
}

double pitchHz(const std::vector<float>& x, size_t a, size_t b) // zero crossings
{
    int zc = 0;
    for (size_t i = a + 1; i < b; ++i) zc += (x[i - 1] < 0) != (x[i] < 0);
    return zc / 2.0 / (double(b - a) / kEngineRate);
}

void sysexChecks()
{
    std::printf("SysEx:\n");
    PatchData p;
    std::memcpy(p.data(), kInitPatch, kPatchBytes);
    p[kUpperP1 + P_TVF_CUTOFF] = 77;
    p[kPatch + K_KEY_MODE] = 2;
    const std::vector<uint8_t> syx = patchToSysex(p, 3);
    SysexResult r = parseSysex(syx.data(), syx.size());
    check(r.patches.size() == 1 && r.patches[0].number == 0 && r.patches[0].data == p && r.patches[0].name == "INIT",
          "a patch -> DT1 messages -> the same patch (temporary area, name)");
    std::vector<uint8_t> bad = syx;
    bad[20] ^= 1; // a data byte: the first message's checksum no longer adds up
    r = parseSysex(bad.data(), bad.size());
    check(r.badChecksums == 1 && r.patches.empty(), "a wrong checksum: the message is ignored, no patch");
    const uint8_t junk[] = { 0xF0, 0x43, 0x10, 0x00, 0xF7, 0x01, 0x02, 0xF0, 0x41 };
    r = parseSysex(junk, sizeof junk);
    check(r.patches.empty() && r.messages == 0, "other devices' messages and truncated data: nothing");
}

// the time from note-off until the Upper output falls 40 dB below its held level
double releaseSeconds(int t5, int key = 60, int tkf = 0)
{
    Engine e;
    initPatch(e);
    for (int part : { kUpperP1, kUpperP2 }) {
        e.patch()[part + P_TVA_T5] = uint8_t(t5);
        e.patch()[part + P_TVA_TKF] = uint8_t(tkf);
    }
    const int hold = kEngineRate, total = kEngineRate * 40;
    Render r = render(e, hold + total, [&](int f) { if (f == 0) e.noteOn(0, key, 100); if (f == hold) e.noteOff(0, key); });
    const double held = rms(r.u, size_t(hold - 3200), size_t(hold));
    const size_t win = 320;
    for (size_t a = size_t(hold); a + win < r.u.size(); a += win)
        if (rms(r.u, a, a + win) < held * 0.01) return double(a - size_t(hold)) / kEngineRate;
    return 40.0;
}

// D-50 envelope timing (v2.22 program ROM): the release scales T5 by 1.25, TVA time key
// follow is (key - 60) >> (4 - TKF)
void envelopeChecks()
{
    std::printf("Envelopes:\n");
    const double r0 = releaseSeconds(0), r50 = releaseSeconds(50), r80 = releaseSeconds(80), r100 = releaseSeconds(100);
    char w[128];
    std::snprintf(w, sizeof w, "release grows with T5 (0: %.3f s, 50: %.2f s, 80: %.2f s, 100: %.2f s)", r0, r50, r80, r100);
    check(r0 < 0.05 && r0 < r50 && r50 < r80 && r80 < r100, w);
    const double lo = releaseSeconds(80, 36, 4), hi = releaseSeconds(80, 84, 4);
    std::snprintf(w, sizeof w, "time key follow 4: high keys release sooner (C2 %.2f s, C6 %.2f s)", lo, hi);
    check(hi < lo * 0.5, w);
}

// the chorus modulators as the D-50 sets them: linear in Rate and Depth around the
// type's centre (value - 50), clamped; the tremolo only in types 6 and 7
void chorusChecks()
{
    std::printf("Chorus:\n");
    char w[128];
    const float r0 = chorusRateHz(1, 1, 30), r50 = chorusRateHz(1, 1, 65), r100 = chorusRateHz(1, 1, 100);
    std::snprintf(w, sizeof w, "Chorus 2 rate is linear (30: %.2f, 65: %.2f, 100: %.2f Hz)", r0, r50, r100);
    check(std::fabs((r100 - r50) - (r50 - r0)) < 1e-3 && r100 > r50, w);
    const float d30 = chorusDepth(0, 1, 30), d100 = chorusDepth(0, 1, 100);
    std::snprintf(w, sizeof w, "Chorus 1: depth 30 leaves the sweep still (%.2f ms), 100 sweeps (%.2f ms)", d30, d100);
    check(d30 == 0.f && d100 > 2.f, w);
    bool trem = true;
    for (int t = 0; t < 8; ++t) trem &= (chorusDepth(t, 0, 50) >= 0) == (t == 5 || t == 6);
    check(trem && chorusRateHz(5, 1, 50) < 0, "tremolo only in Tremolo and Chorus + Tremolo; Tremolo has no sweep");
    check(chorusRateHz(2, 1, 50) < chorusRateHz(1, 1, 50) && chorusRateHz(7, 1, 50) < chorusRateHz(0, 1, 50),
          "flangers and Dimension sweep slower than the choruses");
}

void engineChecks()
{
    std::printf("Engine:\n");
    {
        Engine e;
        initPatch(e);
        Render r = render(e, kEngineRate * 2, [&](int f) {
            if (f == 0) e.noteOn(0, 60, 100);
            if (f == kEngineRate) e.noteOff(0, 60);
        });
        const double hz = pitchHz(r.u, 6400, 22400);
        char w[96];
        std::snprintf(w, sizeof w, "INIT: C4 at %.1f Hz, sound %.3f, silent after the release", hz, rms(r.u, 3200, 16000));
        check(r.finite && std::fabs(hz / 261.63 - 1) < 0.02 && rms(r.u, 3200, 16000) > 0.01 &&
                  rms(r.u, size_t(kEngineRate * 1.9), size_t(kEngineRate * 2)) < 1e-4 && e.activeVoices() == 0,
              w);
    }
    for (int st = 0; st < 2; ++st) { // the synth-only structures
        Engine e;
        initPatch(e);
        e.patch()[kUpperCommon + C_STRUCTURE] = uint8_t(st);
        Render r = render(e, 16000, [&](int f) { if (f == 0) e.noteOn(0, 57, 100); });
        char w[64];
        std::snprintf(w, sizeof w, "structure %d sounds (%.3f)", st + 1, rms(r.u, 1600, 16000));
        check(r.finite && rms(r.u, 1600, 16000) > 0.005, w);
    }
    for (int st = 2; st < 7; ++st) { // with PCM partials (stand-ins): sound at the onset
        Engine e;
        initPatch(e);
        e.patch()[kUpperCommon + C_STRUCTURE] = uint8_t(st);
        e.patch()[kUpperP1 + P_WG_PCM_WAVE] = 49; // a loop
        e.patch()[kUpperP2 + P_WG_PCM_WAVE] = 67;
        Render r = render(e, 16000, [&](int f) { if (f == 0) e.noteOn(0, 57, 100); });
        char w[64];
        std::snprintf(w, sizeof w, "structure %d (PCM loops) sounds (%.3f)", st + 1, rms(r.u, 1600, 16000));
        check(r.finite && rms(r.u, 1600, 16000) > 0.002, w);
    }
    // key modes: which tone a note plays
    struct Km { int mode, channel, key; bool upper, lower; const char* what; };
    const Km kms[] = { { 0, 0, 60, true, false, "Whole: Upper" }, { 1, 0, 60, true, true, "Dual: both" },
                       { 2, 0, 50, false, true, "Split: below C4 Lower" }, { 2, 0, 70, true, false, "Split: above Upper" },
                       { 3, 1, 60, false, true, "Separate: channel 2 Lower" },
                       { 3, 0, 60, true, false, "Separate: channel 1 Upper" } };
    for (const Km& k : kms) {
        Engine e;
        initPatch(e);
        e.patch()[kLowerCommon + C_PARTIAL_MUTE] = 3;
        e.patch()[kPatch + K_KEY_MODE] = uint8_t(k.mode);
        e.patch()[kPatch + K_SPLIT_POINT] = 24; // C4
        Render r = render(e, 9600, [&](int f) { if (f == 0) e.noteOn(k.channel, k.key, 100); });
        const bool up = rms(r.u, 1600, 9600) > 0.005, lo = rms(r.l, 1600, 9600) > 0.005;
        char w[64];
        std::snprintf(w, sizeof w, "key mode %s", k.what);
        check(up == k.upper && lo == k.lower, w);
    }
    {
        Engine e;
        initPatch(e);
        e.patch()[kPatch + K_KEY_MODE] = 4; // Whole-S
        render(e, 3200, [&](int f) { if (f == 0) { e.noteOn(0, 60, 100); e.noteOn(0, 64, 100); } });
        int gated = 0;
        Render r = render(e, 3200, [&](int f) { if (f == 0) gated = e.activeVoices(); });
        // the first note is released (still sounding its release) - only one note remains held
        e.noteOff(0, 64);
        Render r2 = render(e, 6400, [](int) {});
        const double hz = pitchHz(r2.u, 1600, 6400);
        char w[96];
        std::snprintf(w, sizeof w, "solo: last note wins, releasing it returns to the held one (%.0f Hz)", hz);
        check(gated >= 1 && std::fabs(hz / 261.63 - 1) < 0.03, w);
    }
    {
        Engine e;
        initPatch(e);
        Render r = render(e, 6400, [&](int f) {
            if (f == 0)
                for (int n = 0; n < 24; ++n) e.noteOn(0, 40 + n, 100);
        });
        char w[64];
        std::snprintf(w, sizeof w, "24 notes: %d voices (16 at most), finite", e.activeVoices());
        check(e.activeVoices() == kVoices && r.finite, w);
    }
    {
        Engine e;
        initPatch(e);
        e.sustain(true);
        render(e, 3200, [&](int f) { if (f == 0) { e.noteOn(0, 60, 100); } if (f == 640) e.noteOff(0, 60); });
        Render held = render(e, 9600, [](int) {});
        e.sustain(false);
        Render rel = render(e, kEngineRate * 2, [](int) {});
        check(rms(held.u, 6400, 9600) > 0.01 && rms(rel.u, size_t(kEngineRate * 1.9), size_t(kEngineRate * 2)) < 1e-4,
              "hold pedal: sustains past the key release, then releases");
    }
    {
        Engine e;
        initPatch(e);
        e.patch()[kPatch + K_PORTA_TIME] = 60;
        e.portamento(true);
        Render r = render(e, 32000, [&](int f) { if (f == 0) e.noteOn(0, 48, 100); if (f == 16000) e.noteOn(0, 72, 100); });
        const double early = pitchHz(r.u, 16100, 16900), late = pitchHz(r.u, 30000, 32000);
        char w[96];
        std::snprintf(w, sizeof w, "portamento: the new note glides (%.0f Hz early, %.0f Hz later)", early, late);
        check(early < late * 0.8, w);
    }
}

void hostChecks()
{
    std::printf("Host:\n");
    const maquina::VoiceDef* d = nullptr;
    for (const auto& v : maquina::k_voices)
        if (v.voice == maquina::kLa50Voice) d = &v;
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        maquina::La50Host h(*d, 1);
        h.setSampleRate(rate);
        const uint32_t frames = uint32_t(rate * 2.5);
        std::vector<float> l(frames), r(frames);
        uint32_t seed = 5;
        for (uint32_t f = 0; f < frames;) {
            const uint32_t n = std::min<uint32_t>(1 + ((seed = seed * 1664525u + 1013904223u) >> 16) % 600, frames - f);
            if (f == 0) h.noteOn(0, 60, 100);
            if (f <= uint32_t(rate * 0.5) && f + n > uint32_t(rate * 0.5)) h.noteOff(uint32_t(rate * 0.5) - f, 60);
            h.process(l.data() + f, r.data() + f, n);
            f += n;
        }
        const double head = rms(l, size_t(rate * 0.05), size_t(rate * 0.4));
        const double tail = rms(l, size_t(rate * 2.3), frames);
        char w[96];
        std::snprintf(w, sizeof w, "%.0f Hz: no underruns, sound %.3f, quiet at the end %.1e, latency %u", rate, head, tail,
                      h.latencyFrames());
        // (the reverb's tail may still ring faintly)
        check(h.underruns() == 0 && head > 0.01 && tail < 2e-3, w);
    }
}

int dump(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const SysexResult r = parseSysex(data.data(), data.size());
    for (const SysexPatch& p : r.patches) {
        std::printf("%02d %s ", p.number, p.name.c_str());
        for (uint8_t b : p.data) std::printf("%02x", b);
        std::printf("\n");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && !std::strcmp(argv[1], "--dump")) return dump(argv[2]);
    sysexChecks();
    engineChecks();
    envelopeChecks();
    chorusChecks();
    hostChecks();
    std::printf(gFails ? "la50_test: %d FAILED\n" : "la50_test: all ok\n", gFails);
    return gFails ? 1 : 0;
}
