// strings_test - Maquina Strings' engine and host behaviour checks (not bit-exact: the engine is
// our own). Prints one line per check, exits 1 if any fails.
//
//   pitch       A4 is 440 Hz within 3 cents: strings (Viola), wave layer; 16' (Cello) sounds an
//               octave down, OCTAVE +1 an octave up
//   layers      BLEND 0 / 1 computes only that layer; KEYS WAVE LOW / HIGH split at C4
//   mixer       a layer switched off is silent and not computed; its level scales it
//   voices      at most 8; a released note ends
//   vibrato     steady before VIB DELAY, moving after it
//   fx          every effect mode and the reverb: finite, not silent
//   fuzz        random parameters and notes: finite, bounded
//   host        StringsHost at 44.1 / 48 / 96 kHz: sound, no underruns, the latency it reports
//
// SPDX-License-Identifier: GPL-3.0-only
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "StringsEngine.hpp"
#include "StringsHost.hpp"
#include "VoiceTables.generated.hpp"

using namespace maquina;
using strings::Engine;
using strings::Params;

namespace {

int failures = 0;
void check(bool ok, const char* name, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void check(bool ok, const char* name, const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::printf("%-4s %-28s %s\n", ok ? "ok" : "FAIL", name, buf);
    if (!ok) ++failures;
}

// a clean single-layer setting: no effects, no vibrato, no detune, an open filter
Params dry()
{
    Params p;
    p.fxAmount = 0;
    p.reverb = 0;
    p.vibDepth = 0;
    p.detuneCt = 0;
    p.layerDetuneCt = 0;
    p.blendShift = 0;
    p.blendVelo = 0;
    p.attack = 0.005f;
    p.cutoffHz = 18000;
    p.reso = 0;
    p.filterEnv = 0;
    return p;
}

std::vector<float> render(Engine& e, double seconds, std::vector<float>* right = nullptr)
{
    std::vector<float> out;
    float l[strings::kMaxBlock], r[strings::kMaxBlock];
    const long blocks = long(seconds * strings::kRate / strings::kMaxBlock);
    for (long b = 0; b < blocks; ++b) {
        e.render(l, r, strings::kMaxBlock);
        out.insert(out.end(), l, l + strings::kMaxBlock);
        if (right) right->insert(right->end(), r, r + strings::kMaxBlock);
    }
    return out;
}

// YIN: the fundamental of x[from .. from + n) in Hz (0 if none)
double pitchOf(const std::vector<float>& x, size_t from, size_t n, double rate)
{
    const size_t maxLag = size_t(rate / 40.0);
    if (from + n + maxLag > x.size()) return 0;
    std::vector<double> d(maxLag + 1, 0.0), cmnd(maxLag + 1, 1.0);
    for (size_t tau = 1; tau <= maxLag; ++tau) {
        double s = 0;
        for (size_t i = 0; i < n; ++i) {
            const double v = double(x[from + i]) - double(x[from + i + tau]);
            s += v * v;
        }
        d[tau] = s;
    }
    double run = 0;
    for (size_t tau = 1; tau <= maxLag; ++tau) {
        run += d[tau];
        cmnd[tau] = run > 0 ? d[tau] * double(tau) / run : 1.0;
    }
    for (size_t tau = size_t(rate / 4000.0); tau < maxLag; ++tau)
        if (cmnd[tau] < 0.12) {
            while (tau + 1 < maxLag && cmnd[tau + 1] < cmnd[tau]) ++tau;
            const double a = cmnd[tau - 1], b = cmnd[tau], c = cmnd[tau + 1];
            const double den = a - 2 * b + c;
            const double t = double(tau) + (std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0);
            return rate / t;
        }
    return 0;
}
double cents(double f, double ref) { return f > 0 ? 1200.0 * std::log2(f / ref) : 1e9; }

double peakOf(const std::vector<float>& x)
{
    double p = 0;
    for (float v : x) p = std::fmax(p, std::fabs(v));
    return p;
}

void pitchChecks()
{
    const double fs = strings::kRate;
    const size_t at = size_t(0.4 * fs), n = 4096;
    struct Case { const char* name; float reg, blend; int octave; double expect; };
    const Case cases[] = {
        { "pitch strings (Viola) A4", 0.5f, 0.f, 0, 440.0 },
        { "pitch strings 16' (Cello)", 0.f, 0.f, 0, 220.0 },
        { "pitch wave A4", 0.5f, 1.f, 0, 440.0 },
        { "pitch wave octave +1", 0.5f, 1.f, 1, 880.0 },
    };
    for (const Case& c : cases) {
        Engine e(1);
        Params p = dry();
        p.reg = c.reg;
        p.blend = c.blend;
        p.octave = c.octave;
        p.tone = 1.f;
        e.setParams(p);
        e.noteOn(69, 100);
        const std::vector<float> x = render(e, 0.6);
        const double f = pitchOf(x, at, n, fs);
        const double ct = cents(f, c.expect);
        check(std::fabs(ct) < 3.0, c.name, "%.2f Hz (%+.2f cents from %.0f)", f, ct, c.expect);
    }
}

void layerChecks()
{
    {
        Engine e(1);
        Params p = dry();
        p.blend = 0.f;
        e.setParams(p);
        e.noteOn(60, 100);
        render(e, 0.2);
        check(e.blocksA() > 0 && e.blocksB() == 0, "layers: blend 0 = strings", "%llu / %llu blocks",
              (unsigned long long)e.blocksA(), (unsigned long long)e.blocksB());
    }
    {
        Engine e(1);
        Params p = dry();
        p.blend = 1.f;
        e.setParams(p);
        e.noteOn(60, 100);
        render(e, 0.2);
        check(e.blocksA() == 0 && e.blocksB() > 0, "layers: blend 1 = wave", "%llu / %llu blocks",
              (unsigned long long)e.blocksA(), (unsigned long long)e.blocksB());
    }
    // BLEND's ends are absolute: SHIFT and VELO cannot bring the other layer back
    for (int end = 0; end < 2; ++end) {
        Engine e(1);
        Params p = dry();
        p.blend = end ? 1.f : 0.f;
        p.blendShift = end ? -1.f : 1.f;
        p.blendVelo = 1.f;
        p.blendTime = 0.05f;
        e.setParams(p);
        e.noteOn(60, end ? 10 : 127); // soft (pulls towards the strings) / hard (towards the wave)
        render(e, 0.5);
        const bool ok = end ? e.blocksA() == 0 : e.blocksB() == 0;
        check(ok, end ? "layers: blend 1 + SHIFT -1" : "layers: blend 0 + SHIFT +1", "%llu / %llu blocks",
              (unsigned long long)e.blocksA(), (unsigned long long)e.blocksB());
    }
    for (int keys = 1; keys <= 2; ++keys) {
        Engine lo(1), hi(1);
        Params p = dry();
        p.keys = keys;
        lo.setParams(p);
        hi.setParams(p);
        lo.noteOn(48, 100); // below the split
        hi.noteOn(72, 100);
        render(lo, 0.2);
        render(hi, 0.2);
        const bool loWave = lo.blocksB() > 0 && lo.blocksA() == 0, hiWave = hi.blocksB() > 0 && hi.blocksA() == 0;
        const bool ok = keys == 1 ? (loWave && hi.blocksA() > 0 && hi.blocksB() == 0)
                                  : (hiWave && lo.blocksA() > 0 && lo.blocksB() == 0);
        check(ok, keys == 1 ? "layers: KEYS WAVE LOW" : "layers: KEYS WAVE HIGH", "C3 %s, C5 %s",
              loWave ? "wave" : "strings", hiWave ? "wave" : "strings");
    }
}

// the mixer: a layer switched off is silent and not computed; its level scales it before the blend
void mixerChecks()
{
    {
        Engine e(1);
        Params p = dry();
        p.blend = 0.f;
        p.stringsOn = false;
        e.setParams(p);
        e.noteOn(60, 100);
        const std::vector<float> x = render(e, 0.2);
        check(e.blocksA() == 0 && peakOf(x) == 0.0, "mixer: strings off", "%llu strings blocks, peak %g",
              (unsigned long long)e.blocksA(), peakOf(x));
    }
    {
        Engine e(1);
        Params p = dry();
        p.blend = 1.f;
        p.waveOn = false;
        e.setParams(p);
        e.noteOn(60, 100);
        const std::vector<float> x = render(e, 0.2);
        check(e.blocksB() == 0 && peakOf(x) == 0.0, "mixer: wave off", "%llu wave blocks, peak %g",
              (unsigned long long)e.blocksB(), peakOf(x));
    }
    for (int layer = 0; layer < 2; ++layer) {
        auto rms = [&](float level) {
            Engine e(1);
            Params p = dry();
            p.blend = layer ? 1.f : 0.f;
            (layer ? p.waveLevel : p.stringsLevel) = level;
            e.setParams(p);
            e.noteOn(60, 100);
            const std::vector<float> x = render(e, 0.5);
            double s2 = 0;
            for (size_t i = x.size() / 2; i < x.size(); ++i) s2 += double(x[i]) * x[i];
            return std::sqrt(s2 / double(x.size() / 2));
        };
        const double db = 20.0 * std::log10(rms(std::pow(10.f, -12.f / 20.f)) / rms(1.f));
        check(std::fabs(db + 12.0) < 0.1, layer ? "mixer: wave level -12 dB" : "mixer: strings level -12 dB",
              "%+.2f dB", db);
    }
}

void voiceChecks()
{
    Engine e(1);
    Params p = dry();
    p.release = 0.05f;
    e.setParams(p);
    for (int k = 0; k < 10; ++k) e.noteOn(uint8_t(48 + 2 * k), 100);
    render(e, 0.1);
    check(e.activeVoices() == 8, "voices: 10 notes -> 8", "%d active", e.activeVoices());
    for (int k = 0; k < 10; ++k) e.noteOff(uint8_t(48 + 2 * k));
    render(e, 0.6);
    check(e.activeVoices() == 0, "voices: released notes end", "%d active after 0.6 s", e.activeVoices());
    // the sustain pedal holds a released note
    Engine s(1);
    s.setParams(p);
    s.sustainPedal(true);
    s.noteOn(60, 100);
    s.noteOff(60);
    render(s, 0.3);
    const int held = s.activeVoices();
    s.sustainPedal(false);
    render(s, 0.5);
    check(held == 1 && s.activeVoices() == 0, "voices: sustain pedal", "held %d, then %d", held, s.activeVoices());
}

void vibratoCheck()
{
    const double fs = strings::kRate;
    Engine e(1);
    Params p = dry();
    p.blend = 0.f;
    p.reg = 0.5f;
    p.vibDepth = 1.f;
    p.vibDelay = 1.0f;
    p.vibHz = 5.f;
    e.setParams(p);
    e.noteOn(69, 100);
    const std::vector<float> x = render(e, 2.6);
    auto spread = [&](double t0, double t1) {
        double lo = 1e9, hi = -1e9;
        for (double t = t0; t < t1; t += 0.02) {
            const double c = cents(pitchOf(x, size_t(t * fs), 1024, fs), 440.0);
            lo = std::fmin(lo, c);
            hi = std::fmax(hi, c);
        }
        return hi - lo;
    };
    const double before = spread(0.2, 0.9), after = spread(1.8, 2.5);
    check(before < 3.0 && after > 20.0, "vibrato: delayed", "%.1f cents before VIB DELAY, %.1f after", before,
          after);
}

void fxChecks()
{
    static const char* const kNames[] = { "ensemble", "phaser", "chorus", "formant" };
    for (int m = 0; m < 4; ++m)
        for (int rev = 0; rev < 2; ++rev) {
            Engine e(1);
            Params p;
            p.fxMode = m;
            p.fxAmount = 1.f;
            p.reverb = rev ? 1.f : 0.f;
            e.setParams(p);
            for (uint8_t n : { 48, 55, 60, 64 }) e.noteOn(n, 100);
            const std::vector<float> x = render(e, 1.0);
            bool finite = true;
            for (float v : x) finite &= std::isfinite(v);
            const double pk = peakOf(x);
            char name[48];
            std::snprintf(name, sizeof name, "fx: %s%s", kNames[m], rev ? " + reverb" : "");
            check(finite && pk > 0.01 && pk < 2.0, name, "peak %.3f", pk);
        }
}

void fuzzCheck()
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.f, 1.f);
    Engine e(3);
    bool finite = true;
    double pk = 0;
    for (int round = 0; round < 60; ++round) {
        Params p;
        p.reg = u(rng);
        p.shape = int(u(rng) * 3) % 3;
        p.detuneCt = 25 * u(rng);
        p.tone = u(rng);
        p.vibDepth = u(rng);
        p.bank = int(u(rng) * 4) % 4;
        p.x = u(rng);
        p.y = u(rng);
        p.octave = int(u(rng) * 5) % 5 - 2;
        p.lfoHz = 0.05f + 10 * u(rng);
        p.lfoDepth = u(rng);
        p.envPos = 2 * u(rng) - 1;
        p.unison = 1 + int(u(rng) * 4) % 4;
        p.spreadCt = 50 * u(rng);
        p.filterMode = int(u(rng) * 4) % 4;
        p.cutoffHz = 20 * std::pow(900.f, u(rng));
        p.reso = u(rng);
        p.filterEnv = 2 * u(rng) - 1;
        p.keytrack = u(rng);
        p.blend = u(rng);
        p.blendShift = 2 * u(rng) - 1;
        p.blendTime = 0.05f + 5 * u(rng);
        p.keys = int(u(rng) * 3) % 3;
        p.attack = 0.001f + u(rng);
        p.decay = 0.001f + u(rng);
        p.sustain = u(rng);
        p.release = 0.001f + u(rng);
        p.velocity = u(rng);
        p.fxMode = int(u(rng) * 4) % 4;
        p.fxAmount = u(rng);
        p.reverb = u(rng);
        e.setParams(p);
        e.pitchBend(4 * u(rng) - 2);
        for (int k = 0; k < 3; ++k) {
            const uint8_t n = uint8_t(24 + int(u(rng) * 84));
            if (u(rng) < 0.6f) e.noteOn(n, uint8_t(1 + int(u(rng) * 126)));
            else e.noteOff(n);
        }
        const std::vector<float> x = render(e, 0.08);
        for (float v : x) {
            finite &= std::isfinite(v);
            pk = std::fmax(pk, std::fabs(v));
        }
    }
    check(finite && pk <= 2.0, "fuzz: 60 random settings", "finite %s, peak %.2f", finite ? "yes" : "NO", pk);
}

void hostChecks()
{
    const VoiceDef* found = nullptr;
    for (const VoiceDef& d : k_voices)
        if (d.voice == kStringsVoice) found = &d;
    const VoiceDef& def = *found;
    for (double rate : { 44100.0, 48000.0, 96000.0 }) {
        StringsHost h(def, 1);
        h.setSampleRate(rate);
        h.noteOn(0, 60, 100);
        h.noteOn(0, 67, 100);
        std::vector<float> l(static_cast<size_t>(rate)), r(static_cast<size_t>(rate));
        for (size_t at = 0; at < l.size(); at += 256) {
            const uint32_t n = uint32_t(std::min<size_t>(256, l.size() - at));
            h.process(l.data() + at, r.data() + at, n);
        }
        bool finite = true;
        for (float v : l) finite &= std::isfinite(v);
        size_t first = 0;
        while (first < l.size() && std::fabs(l[first]) < 1e-6f) ++first;
        char name[48];
        std::snprintf(name, sizeof name, "host @ %.1f kHz", rate / 1000.0);
        check(finite && peakOf(l) > 0.01 && h.underruns() == 0 && first >= h.latencyFrames(), name,
              "peak %.3f, underruns %llu, latency %u, first sound at %zu", peakOf(l),
              (unsigned long long)h.underruns(), h.latencyFrames(), first);
    }
    // the defaults reach the engine as the editor shows them
    StringsHost h(def, 1);
    const strings::Params p = h.engineParams();
    check(std::fabs(p.attack - 0.42f) < 1e-4f && std::fabs(p.cutoffHz - 1800.f) < 0.5f &&
              std::fabs(p.blendTime - 2.4f) < 1e-4f && std::fabs(p.vibHz - 6.f) < 1e-4f && p.unison == 1,
          "host: default parameters", "attack %.3f s, cutoff %.0f Hz, blend time %.2f s, vibrato %.1f Hz",
          double(p.attack), double(p.cutoffHz), double(p.blendTime), double(p.vibHz));
}

} // namespace

int main()
{
    pitchChecks();
    layerChecks();
    mixerChecks();
    voiceChecks();
    vibratoCheck();
    fxChecks();
    fuzzCheck();
    hostChecks();
    std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
