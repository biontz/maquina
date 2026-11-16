// oneiroi_test.cpp - behaviour tests of OneiroiHost (Maquina Oneiroi / FX)
// beyond the engine's bit-exactness (tools/oneiroi_ref):
//
//   pipeline   no output underrun at any host rate / block size; the
//              reported latency is the real one (FX dry path, correlation)
//   pitch      note 69 = 440 Hz with Pitch / Octave at 0; Octave, Pitch,
//              notes transpose as on the module (V/Oct)
//   gate       silent without notes; a note opens the envelope, release
//              closes it; Drone sounds without notes
//   sync       SYNC = Beat at 120 BPM: the module's clock follows (0.5 s)
//   looper     FX records its input (PRE) and plays it back; Clear empties it
//
// oneiroi_test --measure FILE.maquina [-w OUT.wav]: what a preset sounds like
// in numbers (presets are designed without ears). Oneiroi: note 48 held
// 0.2..4 s; Oneiroi FX: the lv2_render plucks as input for 4 s. Reports peak,
// RMS while playing and of the tail, brightness (zero crossings), stereo
// correlation.
//
// SPDX-License-Identifier: GPL-3.0-only
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wreorder"
#endif
#include "oneiroi_unity.cpp"
#include "OneiroiHost.cpp"
#include "VoiceTables.generated.hpp"
#include "Presets.hpp"

#include <fstream>
#include <sstream>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using maquina::OneiroiHost;

namespace {

int failures = 0;
void check(bool ok, const char* what, const char* detail)
{
    std::printf("  %-44s %s  %s\n", what, detail, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

const maquina::VoiceDef& def(bool fx)
{
    for (const auto& v : maquina::k_voices)
        if (v.voice == (fx ? maquina::kOneiroiFxVoice : maquina::kOneiroiVoice)) return v;
    std::abort();
}

void set(OneiroiHost& h, const char* sym, float v)
{
    const auto& d = h.def();
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, sym)) { h.setParam(i, v); return; }
    std::printf("no parameter %s\n", sym);
    std::abort();
}

// plain sound: sine only, everything dry, no modulation
void plain(OneiroiHost& h)
{
    set(h, "flt_cutoff", 127);
    set(h, "mod_level", 0);
}

struct Out { std::vector<float> l, r; };

Out run(OneiroiHost& h, double rate, double seconds, uint32_t block,
        const std::vector<float>* in = nullptr, double bpm = 0.0)
{
    const size_t n = size_t(seconds * rate);
    Out o { std::vector<float>(n), std::vector<float>(n) };
    std::vector<float> zl(block), zr(block);
    for (size_t pos = 0; pos < n; pos += block) {
        const uint32_t k = uint32_t(std::min<size_t>(block, n - pos));
        if (bpm > 0.0) h.setTransport(true, double(pos) * bpm / 60.0 / rate, 4.0, bpm / 60.0 / rate);
        const float* il = in ? in->data() + pos : nullptr;
        h.process(il, il, o.l.data() + pos, o.r.data() + pos, k);
    }
    return o;
}

double rms(const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b && i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(std::max<size_t>(1, std::min(b, x.size()) - a)));
}

// frequency from zero crossings (rising) over [a, b)
double freq(const std::vector<float>& x, size_t a, size_t b, double rate)
{
    long first = -1, last = -1, count = 0;
    for (size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.f && x[i] >= 0.f) {
            if (first < 0) first = long(i);
            last = long(i);
            ++count;
        }
    return count > 1 ? (count - 1) * rate / double(last - first) : 0.0;
}

void pipelineTests()
{
    std::printf("== pipeline\n");
    const double rates[] = { 22050, 44100, 48000, 88200, 96000, 192000 };
    const uint32_t blocks[] = { 1, 7, 32, 64, 333, 1024, 8192 };
    uint64_t under = 0;
    int runs = 0;
    for (double rate : rates)
        for (uint32_t blk : blocks) {
            OneiroiHost h(def(true), 1);
            h.setSampleRate(rate);
            std::vector<float> in(size_t(rate * 0.5), 0.1f);
            run(h, rate, 0.5, blk, &in);
            under += h.underruns();
            ++runs;
        }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d rate/block combinations, %llu underruns", runs,
                  (unsigned long long)under);
    check(under == 0, "output FIFO never runs dry", buf);

    // latency: the FX's dry path (all effects dry, input fader up) delays an
    // input burst by exactly the reported latency
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        OneiroiHost h(def(true), 1);
        h.setSampleRate(rate);
        const size_t n = size_t(rate);
        std::vector<float> in(n, 0.f);
        for (size_t i = size_t(0.3 * rate); i < size_t(0.32 * rate); ++i) // 2 kHz burst, Hann window
            in[i] = float(0.3 * std::sin(2 * M_PI * 2000.0 * i / rate) *
                          std::sin(M_PI * (i - 0.3 * rate) / (0.02 * rate)) *
                          std::sin(M_PI * (i - 0.3 * rate) / (0.02 * rate)));
        const Out o = run(h, rate, 1.0, 256, &in);
        long best = 0;
        double bestC = -1e30;
        for (long lag = 0; lag < 400; ++lag) {
            double c = 0.0;
            for (size_t i = size_t(0.29 * rate); i < size_t(0.34 * rate); ++i) c += double(in[i]) * o.l[i + lag];
            if (c > bestC) { bestC = c; best = lag; }
        }
        char d[96];
        std::snprintf(d, sizeof d, "@%.0f: measured %ld, reported %u frames", rate, best, h.latencyFrames());
        check(std::labs(best - long(h.latencyFrames())) <= 1, "reported latency = real delay", d);
    }
}

void pitchTests()
{
    std::printf("== pitch (sine, note on for 2 s)\n");
    struct Case { int note, octave, pitch; double want; };
    const Case cases[] = { { 69, 0, 0, 440.0 }, { 57, 0, 0, 220.0 }, { 69, -1, 0, 220.0 },
                           { 69, 1, 0, 880.0 }, { 69, 0, 10, 466.164 }, { 69, 0, -25, 440.0 * std::pow(2.0, -2.5 / 12) } };
    for (const Case& c : cases) {
        OneiroiHost h(def(false), 1);
        h.setSampleRate(48000.0);
        plain(h);
        set(h, "osc_octave", float(c.octave));
        set(h, "osc_pitch", float(c.pitch));
        h.noteOn(0, uint8_t(c.note), 100);
        const Out o = run(h, 48000.0, 2.0, 256);
        const double f = freq(o.l, 48000, 96000, 48000.0);
        char d[96];
        std::snprintf(d, sizeof d, "note %d oct %+d pitch %+d: %.2f Hz (want %.2f)", c.note, c.octave, c.pitch, f,
                      c.want);
        check(std::fabs(f / c.want - 1.0) < 0.003, "frequency", d);
    }
}

void gateTests()
{
    std::printf("== gate / drone\n");
    {
        OneiroiHost h(def(false), 1);
        h.setSampleRate(48000.0);
        plain(h);
        const Out o = run(h, 48000.0, 1.0, 256);
        char d[64];
        std::snprintf(d, sizeof d, "rms %.2g", rms(o.l, 0, o.l.size()));
        check(rms(o.l, 0, o.l.size()) < 1e-6, "no note: silent", d);
    }
    {
        OneiroiHost h(def(false), 1);
        h.setSampleRate(48000.0);
        plain(h);
        set(h, "env_release", 0);
        h.noteOn(4800, 60, 100);
        Out a = run(h, 48000.0, 0.5, 256); // note on 0.1 .. 0.5 s
        h.noteOff(0, 60);
        Out b = run(h, 48000.0, 0.5, 256);
        const double before = rms(a.l, 0, 4000), during = rms(a.l, 12000, 24000), after = rms(b.l, 6000, 24000);
        char d[96];
        std::snprintf(d, sizeof d, "rms before %.2g, held %.3f, after release %.2g", before, during, after);
        check(before < 1e-6 && during > 0.05 && after < 1e-4, "note opens and closes the envelope", d);
    }
    {
        OneiroiHost h(def(false), 1);
        h.setSampleRate(48000.0);
        plain(h);
        set(h, "env_drone", 1);
        const Out o = run(h, 48000.0, 1.0, 256);
        char d[64];
        std::snprintf(d, sizeof d, "rms %.3f", rms(o.l, 24000, 48000));
        check(rms(o.l, 24000, 48000) > 0.05, "drone: sounds without notes", d);
    }
}

void syncTests()
{
    std::printf("== sync\n");
    OneiroiHost h(def(false), 1);
    h.setSampleRate(48000.0);
    set(h, "sync", 1); // every beat
    run(h, 48000.0, 4.0, 256, nullptr, 120.0);
    char d[96];
    std::snprintf(d, sizeof d, "external %d, period %.3f s (want 0.500)", int(h.clockExternal()),
                  h.clockPeriodSeconds());
    check(h.clockExternal() && std::fabs(h.clockPeriodSeconds() - 0.5) < 0.01, "SYNC Beat @120: clock follows", d);

    OneiroiHost off(def(false), 1);
    off.setSampleRate(48000.0);
    run(off, 48000.0, 4.0, 256, nullptr, 120.0);
    std::snprintf(d, sizeof d, "external %d", int(off.clockExternal()));
    check(!off.clockExternal(), "SYNC Off: internal clock", d);
}

void looperTests()
{
    std::printf("== looper (FX, PRE)\n");
    const double rate = 48000.0;
    OneiroiHost h(def(true), 1);
    h.setSampleRate(rate);
    set(h, "vol_input", 0);    // only the looper is heard
    set(h, "vol_looper", 127);
    std::vector<float> tone(size_t(rate * 6), 0.f);
    for (size_t i = 0; i < size_t(rate * 6); ++i) tone[i] = float(0.4 * std::sin(2 * M_PI * 330.0 * i / rate));
    set(h, "loop_record", 1);
    run(h, rate, 6.0, 256, &tone); // fill the 5.46 s buffer with the tone
    set(h, "loop_record", 0);
    const Out play = run(h, rate, 2.0, 256);
    const double f = freq(play.l, 0, play.l.size(), rate);
    char d[96];
    std::snprintf(d, sizeof d, "playback %.1f Hz, rms %.3f", f, rms(play.l, 0, play.l.size()));
    check(std::fabs(f / 330.0 - 1.0) < 0.02 && rms(play.l, 0, play.l.size()) > 0.02, "records the input, plays it", d);

    set(h, "loop_clear", 1);
    run(h, rate, 0.1, 256);
    set(h, "loop_clear", 0);
    const Out cleared = run(h, rate, 1.0, 256);
    std::snprintf(d, sizeof d, "rms after clear %.2g", rms(cleared.l, 24000, 48000));
    check(rms(cleared.l, 24000, 48000) < 1e-3, "Clear empties the buffer", d);
}

} // namespace

int measure(const char* path, const char* wav)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    const bool fx = text.find("voice = oneiroi_fx") != std::string::npos;
    const maquina::VoiceDef& d = def(fx);
    const maquina::PresetFile pf = maquina::parsePreset(d, text);
    if (!pf.error.empty()) { std::printf("%s: %s\n", path, pf.error.c_str()); return 1; }
    std::vector<float> v(maquina::numAllParams(d));
    for (uint32_t i = 0; i < v.size(); ++i) v[i] = float(maquina::paramAt(d, i).def);
    maquina::applyPreset(d, pf.values.data(), uint32_t(pf.values.size()), v.data());
    const double rate = 48000.0;
    OneiroiHost h(d, 7);
    h.setSampleRate(rate);
    for (uint32_t i = 0; i < d.numParams; ++i) h.setParam(i, v[i]);
    const size_t n = size_t(rate * 7);
    std::vector<float> in(n, 0.f);
    if (fx) {
        for (int k = 0; k < 11; ++k) { // plucks every 0.37 s, notes 60 / 46
            const size_t at = size_t((0.05 + 0.37 * k) * rate);
            const double hz = 440.0 * std::pow(2.0, ((k % 2 ? 46 : 60) - 69) / 12.0);
            for (size_t i = at; i < std::min(n, at + size_t(rate)); ++i) {
                const double t = double(i - at) / rate;
                in[i] += float(0.5 * std::exp(-t * 6.0) * std::sin(2 * M_PI * hz * t));
            }
        }
    } else {
        h.noteOn(uint32_t(0.2 * rate), 48, 100);
    }
    Out o { std::vector<float>(n), std::vector<float>(n) };
    for (size_t pos = 0; pos < n; pos += 256) {
        if (!fx && pos <= size_t(4 * rate) && pos + 256 > size_t(4 * rate)) h.noteOff(uint32_t(4 * rate - pos), 48);
        h.process(fx ? in.data() + pos : nullptr, fx ? in.data() + pos : nullptr, o.l.data() + pos, o.r.data() + pos,
                  uint32_t(std::min<size_t>(256, n - pos)));
    }
    double peak = 0.0;
    for (size_t i = 0; i < n; ++i) peak = std::max(peak, double(std::max(std::fabs(o.l[i]), std::fabs(o.r[i]))));
    const size_t a = size_t(1.0 * rate), b = size_t(4.0 * rate);
    long zc = 0;
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = a + 1; i < b; ++i) {
        zc += (o.l[i - 1] < 0.f) != (o.l[i] < 0.f);
        lr += double(o.l[i]) * o.r[i]; ll += double(o.l[i]) * o.l[i]; rr += double(o.r[i]) * o.r[i];
    }
    std::printf("%-34s peak %5.1f dBFS  rms %5.1f dB  tail(5-7s) %6.1f dB  zc %5.0f Hz  L/R corr %+.2f\n",
                pf.name.c_str(), 20 * std::log10(peak + 1e-12), 20 * std::log10(rms(o.l, a, b) + 1e-12),
                20 * std::log10(rms(o.l, size_t(5 * rate), n) + 1e-12), zc * rate / double(b - a) / 2.0,
                lr / std::sqrt(ll * rr + 1e-30));
    if (wav) {
        FILE* w = std::fopen(wav, "wb");
        if (!w) return 1;
        const uint32_t bytes = uint32_t(n * 8), r32 = uint32_t(rate);
        uint32_t u;
        uint16_t s16;
        std::fwrite("RIFF", 1, 4, w); u = 36 + bytes; std::fwrite(&u, 4, 1, w); std::fwrite("WAVEfmt ", 1, 8, w);
        u = 16; std::fwrite(&u, 4, 1, w); s16 = 3; std::fwrite(&s16, 2, 1, w); s16 = 2; std::fwrite(&s16, 2, 1, w);
        std::fwrite(&r32, 4, 1, w); u = r32 * 8; std::fwrite(&u, 4, 1, w); s16 = 8; std::fwrite(&s16, 2, 1, w);
        s16 = 32; std::fwrite(&s16, 2, 1, w); std::fwrite("data", 1, 4, w); std::fwrite(&bytes, 4, 1, w);
        for (size_t i = 0; i < n; ++i) { std::fwrite(&o.l[i], 4, 1, w); std::fwrite(&o.r[i], 4, 1, w); }
        std::fclose(w);
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 3 && !std::strcmp(argv[1], "--measure")) {
        int rc = 0;
        const char* wav = nullptr;
        for (int a = 2; a < argc; ++a)
            if (!std::strcmp(argv[a], "-w") && a + 1 < argc) wav = argv[++a];
        for (int a = 2; a < argc; ++a) {
            if (!std::strcmp(argv[a], "-w")) { ++a; continue; }
            rc |= measure(argv[a], wav);
        }
        return rc;
    }
    pipelineTests();
    pitchTests();
    gateTests();
    syncTests();
    looperTests();
    std::printf("oneiroi-test: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
