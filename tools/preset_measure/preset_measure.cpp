// preset_measure - render one hit of a voice with given settings through the
// plugin's engine host (EngineHost for the LXR voices, DredrumHost,
// PlethoraHost, KlangHost, KlongHost; 44140 Hz output, velocity 127, note 60;
// Klong FX: a click into its input instead of the note) and report
// what it sounds like in numbers, since presets are designed without ears:
//   peak / RMS, length to -40 dB, pitch at the start and after 150 ms
//   (autocorrelation), brightness (zero-crossing rate, spectral centroid).
//
//   preset_measure VOICE [sym=value ...] [-f preset.maquina] [-w out.wav]
// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <type_traits>
#include <vector>

#include "DredrumHost.hpp"
#include "EngineHost.hpp"
#include "PlethoraHost.hpp"
#include "KlangHost.hpp"
#include "KlongHost.hpp"
#include "ExtraParams.hpp"
#include "VoiceTables.generated.hpp"

using namespace maquina;

static const double kRate = 44140.0;

static int findParam(const VoiceDef& d, const std::string& sym)
{
    for (uint32_t i = 0; i < numAllParams(d); ++i)
        if (sym == paramAt(d, i).symbol) return int(i);
    return -1;
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// pitch by normalised autocorrelation over [start, start+len): the highest
// peak after the correlation first goes negative
static double pitchAt(const std::vector<float>& x, size_t start, size_t len)
{
    if (start + len + 2000 > x.size()) return 0;
    double best = 0; int bestLag = 0; bool crossed = false;
    for (int lag = 8; lag < 1800; ++lag) { // up to 24 Hz
        double num = 0, e1 = 0, e2 = 0;
        for (size_t i = start; i < start + len; ++i) {
            num += x[i] * x[i + lag]; e1 += x[i] * x[i]; e2 += x[i + lag] * x[i + lag];
        }
        const double r = (e1 > 0 && e2 > 0) ? num / std::sqrt(e1 * e2) : 0;
        if (!crossed) { crossed = r < 0; continue; }
        if (r > best * 1.02) { best = r; bestLag = lag; }
    }
    return best > 0.5 ? kRate / bestLag : 0;
}

static double centroid(const std::vector<float>& x, size_t start, size_t n)
{
    if (start + n > x.size()) return 0;
    double num = 0, den = 0;
    for (int k = 1; k < int(n / 2); k += 2) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
            re += w * x[start + i] * std::cos(2 * M_PI * k * i / n);
            im -= w * x[start + i] * std::sin(2 * M_PI * k * i / n);
        }
        const double m = std::sqrt(re * re + im * im), f = k * kRate / n;
        num += m * f; den += m;
    }
    return den > 0 ? num / den : 0;
}

// spectral flatness (geometric / arithmetic mean of the power spectrum,
// 200 Hz..16 kHz): ~1 white noise, ~0 pure tones
static double flatness(const std::vector<float>& x, size_t start, size_t n)
{
    if (start + n > x.size()) return 0;
    double logSum = 0, sum = 0; int cnt = 0;
    for (int k = int(200.0 * n / kRate); k < int(16000.0 * n / kRate); ++k) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n);
            re += w * x[start + i] * std::cos(2 * M_PI * k * i / n);
            im -= w * x[start + i] * std::sin(2 * M_PI * k * i / n);
        }
        const double pw = re * re + im * im + 1e-12;
        logSum += std::log(pw); sum += pw; ++cnt;
    }
    return cnt ? std::exp(logSum / cnt) / (sum / cnt) : 0;
}

template <class Host>
static int measure(Host& host, const VoiceDef& d, const std::vector<std::pair<std::string, std::string>>& settings,
                   const char* wav);

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: %s VOICE [sym=val..] [-f file] [-w out.wav]\n", argv[0]); return 2; }
    const VoiceDef* def = nullptr;
    for (const auto& v : k_voices) if (!std::strcmp(v.id, argv[1])) def = &v;
    if (!def) { std::fprintf(stderr, "unknown voice %s\n", argv[1]); return 2; }
    const VoiceDef& d = *def;

    std::vector<std::pair<std::string, std::string>> settings;
    const char* wav = nullptr;
    for (int a = 2; a < argc; ++a) {
        if (!std::strcmp(argv[a], "-w") && a + 1 < argc) { wav = argv[++a]; continue; }
        if (!std::strcmp(argv[a], "-f") && a + 1 < argc) {
            std::ifstream f(argv[++a]);
            std::string line;
            while (std::getline(f, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#') continue;
                const size_t eq = line.find('=');
                if (eq != std::string::npos) settings.push_back({ trim(line.substr(0, eq)), trim(line.substr(eq + 1)) });
            }
            continue;
        }
        const char* eq = std::strchr(argv[a], '=');
        if (eq) settings.push_back({ std::string(argv[a], size_t(eq - argv[a])), std::string(eq + 1) });
    }

    if (d.voice == kDredrumVoice) {
        DredrumHost host(d, 7);
        return measure(host, d, settings, wav);
    }
    if (d.voice == kPlethoraVoice) {
        PlethoraHost host(d, 7);
        return measure(host, d, settings, wav);
    }
    if (d.voice == kKlangVoice) {
        KlangHost host(d, 7);
        return measure(host, d, settings, wav);
    }
    if (isKlong(d.voice)) {
        KlongHost host(d, 7);
        return measure(host, d, settings, wav);
    }
    EngineHost host(d, 7);
    return measure(host, d, settings, wav);
}

template <class Host>
static int measure(Host& host, const VoiceDef& d, const std::vector<std::pair<std::string, std::string>>& settings,
                   const char* wav)
{
    host.setSampleRate(kRate);
    for (auto& [k, v] : settings) {
        if (k == "name" || k == "category" || k == "tags" || k == "voice") continue;
        const int i = findParam(d, k);
        if (i < 0) { std::fprintf(stderr, "unknown parameter %s\n", k.c_str()); return 2; }
        const VoiceParam& p = paramAt(d, uint32_t(i));
        float val = float(std::atof(v.c_str()));
        if (p.labels && (p.kind != ParamKind::Int)) // label names allowed
            for (int l = 0; l < p.numLabels; ++l)
                if (v == p.labels[l]) val = float(l);
        if (k == "note_pitch") host.setNoteFollowsPitch(val >= 0.5f);
        else host.setParam(uint32_t(i), val);
    }

    const size_t total = size_t(kRate * 3.0), block = 256;
    std::vector<float> L(total), R(total), m(total);
    host.noteOn(0, 60, 127);
    // Klong FX: a 2 ms noise burst into the input (its onset strums)
    std::vector<float> in(total, 0.f);
    uint32_t noise = 1;
    for (size_t i = 0; i < size_t(kRate * 0.002); ++i) {
        noise = noise * 1664525u + 1013904223u;
        in[i] = 0.8f * (float(noise >> 8) / 8388608.f - 1.f);
    }
    for (size_t pos = 0; pos < total; pos += block) {
        const uint32_t n = uint32_t(std::min(block, total - pos));
        if constexpr (std::is_same_v<Host, KlongHost>)
            host.process(in.data() + pos, in.data() + pos, L.data() + pos, R.data() + pos, n);
        else
            host.process(L.data() + pos, R.data() + pos, n);
    }
    // skip the plugin latency
    const size_t lat = host.latencyFrames();
    for (size_t i = 0; i + lat < total; ++i) m[i] = 0.5f * (L[i + lat] + R[i + lat]);
    m.resize(total - lat);

    float peak = 0; size_t peakAt = 0;
    for (size_t i = 0; i < m.size(); ++i) if (std::fabs(m[i]) > peak) { peak = std::fabs(m[i]); peakAt = i; }
    // envelope in 5 ms windows -> length to -40 dB below peak window
    const size_t win = size_t(kRate * 0.005);
    double maxRms = 0; size_t end = 0; double energy = 0;
    std::vector<double> env;
    for (size_t i = 0; i + win <= m.size(); i += win) {
        double e = 0; for (size_t j = i; j < i + win; ++j) e += m[j] * m[j];
        energy += e;
        env.push_back(std::sqrt(e / win));
        maxRms = std::max(maxRms, env.back());
    }
    for (size_t w = 0; w < env.size(); ++w) if (env[w] > maxRms * 0.01) end = w;
    const double len = (end + 1) * 0.005;
    size_t zc = 0, zn = std::min(m.size(), size_t(kRate * 0.1));
    for (size_t i = 1; i < zn; ++i) zc += (m[i - 1] < 0) != (m[i] < 0);

    std::printf("%-7s peak %.2f (%+.1f dBFS)  rms %.3f  len %.0f ms  pitch %.0f Hz -> %.0f Hz  zc %.0f/s  centroid %.0f -> %.0f Hz\n",
                d.id, peak, 20 * std::log10(peak + 1e-9), std::sqrt(energy / m.size()), len * 1000,
                pitchAt(m, size_t(kRate * 0.01), 1500), pitchAt(m, size_t(kRate * 0.15), 3000),
                zc / 0.1, centroid(m, 0, 2048), centroid(m, size_t(kRate * 0.1), 2048));
    std::printf("        noise %.2f (flatness at 50 ms; ~1 noise, <0.1 tonal)\n", flatness(m, size_t(kRate * 0.05), 2048));

    if (wav) {
        FILE* f = std::fopen(wav, "wb");
        if (!f) return 1;
        const uint32_t frames = uint32_t(std::min(m.size(), size_t((len + 0.1) * kRate))), rate = 44140, bytes = frames * 2;
        uint8_t h[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\0\0\0\0\0\0\0\0\x02\0\x10\0data";
        uint32_t v = 36 + bytes; std::memcpy(h + 4, &v, 4);
        std::memcpy(h + 24, &rate, 4); v = rate * 2; std::memcpy(h + 28, &v, 4); std::memcpy(h + 40, &bytes, 4);
        std::fwrite(h, 1, 44, f);
        for (uint32_t i = 0; i < frames; ++i) { int16_t s = int16_t(std::lround(std::clamp(m[i], -1.f, 1.f) * 32767)); std::fwrite(&s, 2, 1, f); }
        std::fclose(f);
    }
    return 0;
}
