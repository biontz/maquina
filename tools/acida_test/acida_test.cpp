// acida_test - checks the C++ port of OpenRB-338's 303 and effects
// (openrb/acida/engine/AcidaDsp.cpp) against the Rust engine bit for bit.
//
//   acida_test REFDIR SCRIPT...     run each scenario script (tools/acida_ref/
//                                   scenarios/*.txt; the language is described
//                                   in tools/acida_ref/src/main.rs) and compare
//                                   with REFDIR/<name>.f32 from acida_ref
//   acida_test --tables REF.f32     compare the computed tables
//   acida_test --mutate ...         flip one operation in the port's output
//                                   path (the comparison must then fail)
//   acida_test --write DIR SCRIPT...  write the port's output (no compare)
//   acida_test --host               AcidaHost behaviour: pitch, every host
//                                   rate without underruns, notes, the FX
//                                   chain's latency and delay time
//
// Exit 0 when everything matches.
// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "AcidaDsp.hpp"
#include "AcidaHost.hpp"
#include "VoiceTables.generated.hpp"

using namespace maquina::acida;

namespace {

bool gMutate = false;

struct Input {
    int kind = 0; // 0 none, 1 saw, 2 noise
    uint32_t p = 1;
    float amp = 0.f;
    uint32_t n = 0, seed = 0;
    float next()
    {
        float v = 0.f;
        if (kind == 1) {
            const float x = float(n % p) * (2.0f / float(p)) - 1.0f;
            v = x * amp;
        } else if (kind == 2) {
            seed = seed * 1664525u + 1013904223u;
            const float x = float(seed >> 9) / 4194304.0f - 1.0f;
            v = x * amp;
        }
        ++n;
        return v;
    }
};

bool readF32(const std::string& path, std::vector<float>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const size_t n = size_t(f.tellg()) / 4;
    f.seekg(0);
    out.resize(n);
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(n * 4));
    return bool(f);
}

void writeF32(const std::string& path, const std::vector<float>& v)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size() * 4));
}

std::string baseName(const std::string& path)
{
    size_t s = path.find_last_of('/');
    std::string b = s == std::string::npos ? path : path.substr(s + 1);
    size_t d = b.rfind('.');
    return d == std::string::npos ? b : b.substr(0, d);
}

bool runScript(const std::string& path, std::vector<float>& out)
{
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return false;
    }
    const Tables& t = Tables::get();
    std::string dev;
    std::unique_ptr<Tb303> tb;
    std::unique_ptr<Distortion> dist;
    std::unique_ptr<Pcf> pcf;
    std::unique_ptr<Compressor> comp;
    std::unique_ptr<Delay> delay;
    uint32_t tempo = 120000;
    int32_t tick = 0, tickstep = 0;
    Input in;
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        const size_t h = line.find('#');
        if (h != std::string::npos) line.resize(h);
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd)) continue;
        if (cmd == "device") {
            ss >> dev;
            if (dev == "tb303") tb.reset(new Tb303());
            else if (dev == "dist") dist.reset(new Distortion());
            else if (dev == "pcf") pcf.reset(new Pcf());
            else if (dev == "comp" || dev == "comp_stereo") comp.reset(new Compressor());
            else if (dev == "delay") delay.reset(new Delay());
            else {
                std::fprintf(stderr, "%s:%d: unknown device %s\n", path.c_str(), ln, dev.c_str());
                return false;
            }
        } else if (cmd == "vintage") {
            int v;
            ss >> v;
            if (tb) tb->setVintage(v != 0);
        } else if (cmd == "param") {
            int c, v;
            ss >> c >> v;
            if (tb) tb->setParam(uint8_t(c), uint8_t(v));
            if (dist) dist->setParam(uint8_t(c), uint8_t(v));
            if (pcf) pcf->setParam(uint8_t(c), uint8_t(v));
            if (comp) comp->setParam(uint8_t(c), uint8_t(v));
            if (delay) delay->setParam(uint8_t(c), uint8_t(v));
        } else if (cmd == "on") {
            int n, v;
            ss >> n >> v;
            if (tb) tb->keyOn(uint8_t(n), uint8_t(v));
        } else if (cmd == "off") {
            int n;
            ss >> n;
            if (tb) tb->keyOff(uint8_t(n));
        } else if (cmd == "tempo") {
            ss >> tempo;
        } else if (cmd == "tickstep") {
            ss >> tickstep;
        } else if (cmd == "input") {
            std::string k;
            ss >> k;
            in.kind = k == "saw" ? 1 : k == "noise" ? 2 : 0;
            if (in.kind) {
                ss >> in.p >> in.amp;
                if (in.kind == 2) in.seed = in.p;
            }
        } else if (cmd == "run") {
            int n;
            ss >> n;
            for (int blk = 0; blk < n; ++blk) {
                tick += tickstep;
                Block a, b;
                for (int i = 0; i < kBlock; ++i) a[i] = in.next();
                if (tb) {
                    tb->process(a, t);
                    out.insert(out.end(), a, a + kBlock);
                } else if (dist) {
                    dist->process(a, t);
                    out.insert(out.end(), a, a + kBlock);
                } else if (pcf) {
                    pcf->process(a, t, tick);
                    out.insert(out.end(), a, a + kBlock);
                } else if (comp && dev == "comp") {
                    comp->process(a);
                    out.insert(out.end(), a, a + kBlock);
                } else if (comp) {
                    for (int i = 0; i < kBlock; ++i) b[i] = a[kBlock - 1 - i] * 0.5f;
                    comp->processStereo(a, b);
                    for (int i = 0; i < kBlock; ++i) {
                        out.push_back(a[i]);
                        out.push_back(b[i]);
                    }
                } else if (delay) {
                    Block l = {}, r = {};
                    delay->process(a, l, r, t, tempo);
                    for (int i = 0; i < kBlock; ++i) {
                        out.push_back(l[i]);
                        out.push_back(r[i]);
                    }
                }
            }
        } else {
            std::fprintf(stderr, "%s:%d: unknown command %s\n", path.c_str(), ln, cmd.c_str());
            return false;
        }
    }
    if (gMutate && out.size() > 100) {
        // one ulp in one sample: the comparison must catch it
        uint32_t u;
        std::memcpy(&u, &out[out.size() / 2], 4);
        u ^= 1u;
        std::memcpy(&out[out.size() / 2], &u, 4);
    }
    return true;
}

bool compare(const char* what, const std::vector<float>& ref, const std::vector<float>& got)
{
    if (ref.size() != got.size()) {
        std::printf("FAIL %s: %zu samples, reference %zu\n", what, got.size(), ref.size());
        return false;
    }
    for (size_t i = 0; i < ref.size(); ++i)
        if (std::memcmp(&ref[i], &got[i], 4) != 0) {
            std::printf("FAIL %s: first difference at %zu: %.9g (reference %.9g)\n", what, i, double(got[i]),
                        double(ref[i]));
            return false;
        }
    double peak = 0.0;
    for (float x : got) peak = std::fmax(peak, std::fabs(double(x)));
    std::printf("ok   %-24s %8zu samples bit-exact (peak %.3f)\n", what, got.size(), peak);
    return true;
}

std::vector<float> tableDump()
{
    const Tables& t = Tables::get();
    std::vector<float> v;
    auto add = [&](const float* p, size_t n) { v.insert(v.end(), p, p + n); };
    add(t.level, 128);
    add(&t.pan[0][0][0], 128 * 128 * 2);
    add(t.tb303Pitch, 6144);
    add(&t.tb303Filter[0][0][0], 8 * 1024 * 4);
    add(&t.pcfLowpass[0][0][0], 8 * 512 * 3);
    add(&t.pcfBandpass[0][0][0], 8 * 512 * 3);
    add(&t.distShaper[0][0], 32 * 256);
    for (const Smoother& s : { t.tb303Smoother, t.pcfSmoother }) {
        v.push_back(s.b2);
        v.push_back(s.a);
        v.push_back(s.g);
    }
    return v;
}


// ---------------------------------------------------------------------------
// AcidaHost

int gHostFails = 0;

void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++gHostFails;
}

const maquina::VoiceDef& voiceDef(const char* id)
{
    for (const maquina::VoiceDef& d : maquina::k_voices)
        if (!std::strcmp(d.id, id)) return d;
    std::fprintf(stderr, "no voice %s\n", id);
    std::exit(2);
}

int ix(const maquina::VoiceDef& d, const char* sym)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, sym)) return int(i);
    std::fprintf(stderr, "no parameter %s\n", sym);
    std::exit(2);
}

// frequency from the positive-going zero crossings of x[from..]
double frequency(const std::vector<float>& x, size_t from, double rate)
{
    size_t first = 0, last = 0;
    int n = 0;
    for (size_t i = from + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.f && x[i] >= 0.f) {
            if (!n) first = i;
            last = i;
            ++n;
        }
    return n > 1 ? double(n - 1) * rate / double(last - first) : 0.0;
}

void hostTests()
{
    const maquina::VoiceDef& da = voiceDef("acida");
    const maquina::VoiceDef& dfx = voiceDef("acida_fx");

    // pitch: MIDI 69 = 440 Hz at the Tune centre (OpenRB: note 36 = the
    // pattern's C, 55 * 2^(3/12) Hz); saw, filter open, no resonance
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        maquina::AcidaHost h(da, 1);
        h.setSampleRate(rate);
        h.setParam(uint32_t(ix(da, "cutoff")), 127);
        h.setParam(uint32_t(ix(da, "resonance")), 0);
        h.setParam(uint32_t(ix(da, "env_mod")), 0);
        h.setParam(uint32_t(ix(da, "decay")), 127);
        const uint32_t n = uint32_t(rate);
        std::vector<float> l(n), r(n);
        h.noteOn(0, 69, 80);
        for (uint32_t done = 0; done < n; done += 512) {
            const uint32_t k = std::min<uint32_t>(512, n - done);
            h.process(l.data() + done, r.data() + done, k);
        }
        const double f = frequency(l, n / 4, rate);
        double peak = 0.0;
        for (float x : l) peak = std::fmax(peak, std::fabs(double(x)));
        char buf[160];
        std::snprintf(buf, sizeof buf, "acida %.0f Hz: A4 %.2f Hz, peak %.3f, latency %u, underruns %llu", rate, f,
                      peak, h.latencyFrames(), (unsigned long long)h.underruns());
        check(std::fabs(f - 440.0) < 0.5 && peak > 0.05 && peak <= 1.0 && h.underruns() == 0 &&
                  l == r,
              buf);
    }

    // Tune set before the first note applies to it: +63 = an octave up
    {
        maquina::AcidaHost h(da, 1);
        h.setSampleRate(44100.0);
        h.setParam(uint32_t(ix(da, "tune")), 63);
        h.setParam(uint32_t(ix(da, "cutoff")), 127);
        h.setParam(uint32_t(ix(da, "resonance")), 0);
        std::vector<float> l(44100), r(44100);
        h.noteOn(0, 57, 80);
        h.process(l.data(), r.data(), 44100);
        const double f = frequency(l, 11025, 44100.0);
        check(std::fabs(f - 440.0) < 0.5, "acida: Tune +63 plays MIDI 57 at " + std::to_string(f) + " Hz (440)");
    }

    // a note whose note-off comes in the same block still sounds; a note
    // outside the 303's pitch table (MIDI 127 at Tune +63: 103 semitones
    // above the pattern's C, the table ends at 47) is ignored
    {
        maquina::AcidaHost h(da, 1);
        h.setSampleRate(44100.0);
        std::vector<float> l(4096), r(4096);
        h.noteOn(10, 48, 100);
        h.noteOff(20, 48);
        h.process(l.data(), r.data(), 4096);
        double peak = 0.0;
        for (float x : l) peak = std::fmax(peak, std::fabs(double(x)));
        check(peak > 0.01, "acida: a 10-frame note sounds (peak " + std::to_string(peak) + ")");
        maquina::AcidaHost h2(da, 1);
        h2.setSampleRate(44100.0);
        h2.setParam(uint32_t(ix(da, "tune")), 63);
        h2.noteOn(0, 127, 100);
        h2.process(l.data(), r.data(), 4096);
        peak = 0.0;
        for (float x : l) peak = std::fmax(peak, std::fabs(double(x)));
        check(peak < 1e-4, "acida: a note below the pitch table is ignored (peak " + std::to_string(peak) + ")");
    }

    // FX: everything off = the input, delayed by the latency, at every rate
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        maquina::AcidaHost h(dfx, 1);
        h.setSampleRate(rate);
        h.setParam(uint32_t(ix(dfx, "dist_on")), 0);
        const uint32_t n = 20000;
        std::vector<float> in(n, 0.f), l(n), r(n);
        in[1000] = 0.5f;
        for (uint32_t done = 0; done < n; done += 333) {
            const uint32_t k = std::min<uint32_t>(333, n - done);
            h.process(in.data() + done, in.data() + done, l.data() + done, r.data() + done, k);
        }
        size_t at = 0;
        for (size_t i = 0; i < n; ++i)
            if (std::fabs(l[i]) > std::fabs(l[at])) at = i;
        char buf[160];
        std::snprintf(buf, sizeof buf, "acida_fx %.0f Hz: dry impulse at +%zu (latency %u), underruns %llu", rate,
                      at - 1000, h.latencyFrames(), (unsigned long long)h.underruns());
        const long d = long(at) - 1000 - long(h.latencyFrames());
        check(d >= -2 && d <= 2 && h.underruns() == 0, buf);
    }

    // FX delay: 4 sixteenths at 120 BPM = one beat = 22050 frames at 44.1 kHz
    {
        maquina::AcidaHost h(dfx, 1);
        h.setSampleRate(44100.0);
        h.setParam(uint32_t(ix(dfx, "dist_on")), 0);
        h.setParam(uint32_t(ix(dfx, "delay_on")), 1);
        h.setParam(uint32_t(ix(dfx, "delay_steps")), 4);
        h.setParam(uint32_t(ix(dfx, "delay_mode")), 1);
        h.setParam(uint32_t(ix(dfx, "delay_send")), 127);
        h.setParam(uint32_t(ix(dfx, "delay_feedback")), 0);
        h.setTempo(120.0);
        const uint32_t n = 40000;
        std::vector<float> in(n, 0.f), l(n), r(n);
        in[1000] = 0.5f;
        // the delay's gains fade in (one-pole per block): start it early
        for (uint32_t done = 0; done < n; done += 256) {
            const uint32_t k = std::min<uint32_t>(256, n - done);
            h.process(in.data() + done, in.data() + done, l.data() + done, r.data() + done, k);
        }
        size_t at = 1000 + 10000;
        for (size_t i = 1000 + 10000; i < n; ++i)
            if (std::fabs(l[i]) > std::fabs(l[at])) at = i;
        const long echo = long(at) - 1000 - long(h.latencyFrames());
        check(std::abs(echo - 22050) <= 1 && std::fabs(l[at]) > 0.05,
              "acida_fx: the delay's echo after " + std::to_string(echo) + " frames (one beat: 22050)");
    }

    // FX PCF ticks follow the transport
    {
        maquina::AcidaHost h(dfx, 1);
        h.setSampleRate(48000.0);
        h.setParam(uint32_t(ix(dfx, "pcf_on")), 1);
        std::vector<float> in(480, 0.f), l(480), r(480);
        h.setTempo(120.0);
        h.setTransport(true, 8.0, 4.0, 2.0 / 48000.0); // bar 3, 120 BPM
        h.process(in.data(), in.data(), l.data(), r.data(), 480);
        // the last block run lies within the first 480 frames: tick ~ 8 * 192
        check(h.tick() >= 8 * 192 && h.tick() < 8 * 192 + 30,
              "acida_fx: the PCF's tick follows the transport (" + std::to_string(h.tick()) + ")");
    }
    std::printf("%d host check(s) failed\n", gHostFails);
}
} // namespace

int main(int argc, char** argv)
{
    int a = 1;
    if (a < argc && !std::strcmp(argv[a], "--mutate")) {
        gMutate = true;
        ++a;
    }
    if (argc - a == 1 && !std::strcmp(argv[a], "--host")) {
        hostTests();
        return gHostFails ? 1 : 0;
    }
    if (argc - a == 2 && !std::strcmp(argv[a], "--tables")) {
        std::vector<float> ref;
        if (!readF32(argv[a + 1], ref)) {
            std::fprintf(stderr, "cannot read %s\n", argv[a + 1]);
            return 2;
        }
        std::vector<float> got = tableDump();
        if (gMutate) got[got.size() / 2] = std::nextafter(got[got.size() / 2], 2.f);
        return compare("tables", ref, got) ? 0 : 1;
    }
    if (argc - a >= 2 && !std::strcmp(argv[a], "--write")) {
        const std::string dir = argv[a + 1];
        for (int i = a + 2; i < argc; ++i) {
            std::vector<float> out;
            if (!runScript(argv[i], out)) return 2;
            writeF32(dir + "/" + baseName(argv[i]) + ".f32", out);
        }
        return 0;
    }
    if (argc - a < 2) {
        std::fprintf(stderr, "usage: acida_test [--mutate] REFDIR SCRIPT... | --tables REF.f32 | --write DIR SCRIPT...\n");
        return 2;
    }
    const std::string refdir = argv[a];
    int fails = 0;
    for (int i = a + 1; i < argc; ++i) {
        std::vector<float> ref, out;
        const std::string name = baseName(argv[i]);
        if (!readF32(refdir + "/" + name + ".f32", ref)) {
            std::printf("FAIL %s: no reference (make acida-ref)\n", name.c_str());
            ++fails;
            continue;
        }
        if (!runScript(argv[i], out)) return 2;
        if (!compare(name.c_str(), ref, out)) ++fails;
    }
    std::printf("%d scenario(s) failed\n", fails);
    return fails ? 1 : 0;
}
