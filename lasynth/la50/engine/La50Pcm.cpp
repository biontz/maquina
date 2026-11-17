// La50Pcm.cpp - see La50Pcm.hpp. The stand-ins are simple recipes, not
// recordings: they give an imported D-50 patch the right kind of onset or
// loop (a mallet, a pluck, breath, a bowed or blown attack; an organ, a vowel,
// a bright spectrum) so it is recognisable, not authentic.
// SPDX-License-Identifier: GPL-3.0-only
#include "La50Pcm.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mutex>

namespace la50 {

namespace {

constexpr double kRate = 32000.0;
constexpr double kC4 = 261.6255653;
constexpr double kPi = 3.14159265358979323846;

struct Rng {
    uint32_t s;
    float next() // -1..1
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(int32_t(s)) / 2147483648.f;
    }
};

void normalise(std::vector<float>& x, float peak = 0.9f)
{
    float m = 0.f;
    for (float v : x) m = std::max(m, std::fabs(v));
    if (m > 0.f)
        for (float& v : x) v *= peak / m;
}

// a struck sound: partials (ratio to the fundamental, amplitude, decay in s) + a click
std::vector<float> struck(std::initializer_list<std::array<double, 3>> partials, double seconds, double click, uint32_t seed)
{
    std::vector<float> x(size_t(seconds * kRate));
    Rng r { seed };
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = double(i) / kRate;
        double v = 0;
        for (const auto& p : partials)
            v += p[1] * std::exp(-t / p[2]) * std::sin(2 * kPi * kC4 * p[0] * t);
        v += click * r.next() * std::exp(-t / 0.004);
        x[i] = float(v);
    }
    // a short fade at the end (the wave stops there)
    const size_t fade = std::min<size_t>(x.size(), 800);
    for (size_t k = 0; k < fade; ++k) x[x.size() - 1 - k] *= float(k) / float(fade);
    normalise(x);
    return x;
}

// a plucked string (Karplus-Strong at C4): brightness 0..1, decay per period
std::vector<float> plucked(double seconds, double brightness, double damping, uint32_t seed, double pickNoise = 0.0)
{
    const double period = kRate / kC4;
    const size_t n = size_t(period);
    const double frac = period - double(n);
    std::vector<float> line(n + 2), x(size_t(seconds * kRate));
    Rng r { seed };
    for (auto& v : line) v = r.next();
    // brightness: the excitation's lowpass
    for (int pass = 0; pass < int((1.0 - brightness) * 6); ++pass)
        for (size_t k = 1; k < line.size(); ++k) line[k] = 0.5f * (line[k] + line[k - 1]);
    size_t w = 0;
    float prev = 0.f;
    for (size_t i = 0; i < x.size(); ++i) {
        const size_t a = w, b = (w + 1) % line.size();
        const float y = float((1.0 - frac) * line[a] + frac * line[b]);
        const float filt = float(damping) * 0.5f * (y + prev);
        prev = y;
        line[w] = filt;
        w = (w + 1) % (n + 1);
        x[i] = y + float(pickNoise * r.next() * std::exp(-double(i) / (0.003 * kRate)));
    }
    const size_t fade = std::min<size_t>(x.size(), 1000);
    for (size_t k = 0; k < fade; ++k) x[x.size() - 1 - k] *= float(k) / float(fade);
    normalise(x);
    return x;
}

// a blown or bowed onset: harmonics (amplitudes), breath noise, attack time, length
std::vector<float> onset(std::initializer_list<double> harmonics, double noise, double attack, double seconds, uint32_t seed,
                         double vibrato = 0.0)
{
    std::vector<float> x(size_t(seconds * kRate));
    Rng r { seed };
    float lp = 0.f;
    double phase = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = double(i) / kRate;
        const double env = std::min(1.0, t / attack) * std::min(1.0, (seconds - t) / 0.08);
        phase += kC4 * (1.0 + vibrato * std::sin(2 * kPi * 5.5 * t)) / kRate;
        double v = 0;
        int h = 1;
        for (double a : harmonics) v += a * std::sin(2 * kPi * phase * h++);
        lp += 0.25f * (r.next() - lp);
        const double breath = noise * lp * (1.6 * std::exp(-t / (attack * 0.7)) + 0.3);
        x[i] = float(env * v + std::min(1.0, t / 0.002) * breath);
    }
    normalise(x);
    return x;
}

// a noise transient: colour 0 (dark) .. 1 (bright), decay, optional pitched component
std::vector<float> noisy(double seconds, double colour, double decay, uint32_t seed, double tone = 0.0, double ratio = 1.0)
{
    std::vector<float> x(size_t(seconds * kRate));
    Rng r { seed };
    float lp = 0.f, hp = 0.f;
    const float k = float(0.05 + 0.9 * colour);
    for (size_t i = 0; i < x.size(); ++i) {
        const double t = double(i) / kRate;
        lp += k * (r.next() - lp);
        const float h = lp - hp;
        hp += 0.02f * (lp - hp);
        const double env = std::exp(-t / decay) * std::min(1.0, t / 0.001);
        x[i] = float(env * (h + tone * std::sin(2 * kPi * kC4 * ratio * t)));
    }
    normalise(x);
    return x;
}

// a loop: 13 cycles of C4 in 1590 samples (13 x 122.31: in tune within 0.03 samples)
constexpr size_t kLoopLen = 1590;
constexpr int kLoopCycles = 13;
std::vector<float> cycle(std::initializer_list<double> harmonics, double phaseSpread = 0.0)
{
    std::vector<float> x(kLoopLen);
    for (size_t i = 0; i < x.size(); ++i) {
        double v = 0;
        int h = 1;
        for (double a : harmonics) {
            v += a * std::sin(2 * kPi * double(kLoopCycles * h) * double(i) / double(kLoopLen) + phaseSpread * h * h);
            ++h;
        }
        x[i] = float(v);
    }
    normalise(x);
    return x;
}

// harmonics 1..n with an amplitude per harmonic from a formant-like envelope (vowels)
std::vector<float> vowel(double f1, double f2, double f3, int n, double tilt)
{
    std::vector<float> x(kLoopLen);
    for (size_t i = 0; i < x.size(); ++i) {
        double v = 0;
        for (int h = 1; h <= n; ++h) {
            const double f = kC4 * h;
            auto peak = [&](double c, double bw) { return 1.0 / (1.0 + std::pow((f - c) / bw, 2)); };
            const double a = (peak(f1, 120) + 0.6 * peak(f2, 180) + 0.3 * peak(f3, 250)) * std::pow(h, -tilt);
            v += a * std::sin(2 * kPi * double(kLoopCycles * h) * double(i) / double(kLoopLen));
        }
        x[i] = float(v);
    }
    normalise(x);
    return x;
}

// a band of harmonics (the "Spectrum" loops): harmonics lo..hi, equal amplitudes
std::vector<float> band(int lo, int hi)
{
    std::vector<float> x(kLoopLen);
    for (size_t i = 0; i < x.size(); ++i) {
        double v = 0;
        for (int h = lo; h <= hi; ++h)
            v += std::sin(2 * kPi * double(kLoopCycles * h) * double(i) / double(kLoopLen) + 0.37 * h * h);
        x[i] = float(v);
    }
    normalise(x);
    return x;
}

std::vector<float> saw(int n, double tilt = 1.0)
{
    std::vector<float> x(kLoopLen);
    for (size_t i = 0; i < x.size(); ++i) {
        double v = 0;
        for (int h = 1; h <= n; ++h)
            v += std::pow(h, -tilt) * std::sin(2 * kPi * double(kLoopCycles * h) * double(i) / double(kLoopLen));
        x[i] = float(v);
    }
    normalise(x);
    return x;
}

std::vector<float> square(int n, double tilt = 1.0)
{
    std::vector<float> x(kLoopLen);
    for (size_t i = 0; i < x.size(); ++i) {
        double v = 0;
        for (int h = 1; h <= n; h += 2)
            v += std::pow(h, -tilt) * std::sin(2 * kPi * double(kLoopCycles * h) * double(i) / double(kLoopLen));
        x[i] = float(v);
    }
    normalise(x);
    return x;
}

std::vector<float> mix(const std::vector<float>& a, const std::vector<float>& b, float gb)
{
    std::vector<float> x(a.size());
    for (size_t i = 0; i < x.size(); ++i) x[i] = a[i] + gb * b[i % b.size()];
    normalise(x);
    return x;
}

} // namespace

int16_t PcmBank::toLog(float x)
{
    // LA32 log sample: magnitude L (15 bits) with (32787 - L) * 2 = attenuation
    // in 1/4096 octaves (Munt LA32WaveGenerator::pcmSampleToLogSample)
    const float m = std::fabs(x);
    int l = 0;
    if (m > 1e-6f) {
        const double att = -std::log2(double(m)) * 2048.0;
        l = std::clamp(int(std::lround(32787.0 - att)), 0, 32767);
    }
    return int16_t(x < 0 ? (0x8000 | l) : l);
}

const PcmBank& PcmBank::standIns()
{
    static PcmBank bank;
    static std::once_flag once;
    std::call_once(once, [] { bank.makeStandIns(); });
    return bank;
}

void PcmBank::makeStandIns()
{
    std::vector<std::vector<float>> w(kNumWaves);
    std::vector<bool> loop(kNumWaves, false);
    // 1-47: one-shot onsets
    w[0] = struck({ { { 1, 1, 0.25 } }, { { 4, 0.5, 0.06 } }, { { 9.9, 0.2, 0.02 } } }, 0.6, 0.15, 1);    // Marimba
    w[1] = struck({ { { 1, 1, 1.2 } }, { { 4, 0.35, 0.3 } }, { { 10, 0.1, 0.05 } } }, 1.4, 0.05, 2);     // Vibraphone
    w[2] = struck({ { { 1, 1, 0.12 } }, { { 3, 0.7, 0.05 } }, { { 6, 0.4, 0.02 } } }, 0.35, 0.3, 3);     // Xylophone 1
    w[3] = struck({ { { 1, 1, 0.08 } }, { { 3.9, 0.6, 0.04 } }, { { 9, 0.4, 0.015 } } }, 0.3, 0.4, 4);   // Xylophone 2
    w[4] = struck({ { { 0.5, 1, 0.2 } }, { { 1, 0.6, 0.1 } }, { { 2.7, 0.3, 0.03 } } }, 0.5, 0.2, 5);    // Log Bass
    w[5] = struck({ { { 1, 0.5, 0.05 } }, { { 2.3, 0.4, 0.03 } } }, 0.2, 0.9, 6);                        // Hammer
    w[6] = noisy(0.6, 0.15, 0.15, 7, 1.5, 0.25);                                                         // Japanese Drum
    w[7] = struck({ { { 1, 1, 0.4 } }, { { 5.4, 0.3, 0.05 } } }, 0.8, 0.1, 8);                          // Kalimba
    w[8] = plucked(0.6, 0.9, 0.995, 9, 0.3);                                                             // Pluck 1
    w[9] = struck({ { { 3.1, 1, 0.08 } }, { { 4.7, 0.7, 0.06 } }, { { 7.2, 0.5, 0.04 } } }, 0.3, 0.3, 10); // Chink
    w[10] = struck({ { { 2, 1, 0.3 } }, { { 5.1, 0.4, 0.1 } } }, 0.6, 0.2, 11);                         // Agogo
    w[11] = struck({ { { 6, 1, 1.0 } }, { { 9.2, 0.6, 0.8 } }, { { 13.5, 0.4, 0.5 } } }, 1.5, 0.1, 12);  // Triangle
    w[12] = struck({ { { 1, 1, 1.5 } }, { { 2.76, 0.6, 0.8 } }, { { 5.4, 0.4, 0.4 } }, { { 8.9, 0.3, 0.2 } } }, 2.0,
                   0.05, 13);                                                                            // Bells
    w[13] = noisy(0.25, 0.9, 0.06, 14);                                                                  // Nail File
    w[14] = noisy(0.08, 0.8, 0.015, 15, 0.5, 4);                                                         // Pick
    w[15] = plucked(1.2, 0.5, 0.998, 16, 0.2);                                                           // Low Piano
    w[16] = plucked(1.0, 0.65, 0.997, 17, 0.2);                                                          // Mid Piano
    w[17] = plucked(0.8, 0.8, 0.995, 18, 0.2);                                                           // High Piano
    w[18] = plucked(0.8, 1.0, 0.996, 19, 0.4);                                                           // Harpsichord
    w[19] = plucked(1.2, 0.6, 0.998, 20, 0.05);                                                          // Harp
    w[20] = struck({ { { 2, 1, 0.15 } }, { { 3, 0.7, 0.1 } }, { { 4, 0.3, 0.08 } } }, 0.4, 0.05, 21);  // Organ Percussion
    w[21] = plucked(1.0, 0.9, 0.997, 22, 0.3);                                                           // Steel Strings
    w[22] = plucked(1.0, 0.55, 0.996, 23, 0.15);                                                         // Nylon Strings
    w[23] = plucked(0.9, 0.7, 0.996, 24, 0.2);                                                           // Electric Guitar 1
    w[24] = plucked(0.9, 0.8, 0.995, 25, 0.3);                                                           // Electric Guitar 2
    w[25] = plucked(0.9, 1.0, 0.993, 26, 0.6);                                                           // Dirty Guitar
    w[26] = plucked(0.7, 0.6, 0.996, 27, 0.5);                                                           // Pick Bass
    w[27] = plucked(0.6, 0.85, 0.994, 28, 0.8);                                                          // Pop Bass
    w[28] = struck({ { { 1, 1, 0.12 } }, { { 2, 0.5, 0.05 } } }, 0.3, 0.6, 29);                         // Thump
    w[29] = plucked(0.8, 0.4, 0.997, 30, 0.3);                                                           // Upright Bass
    w[30] = onset({ 1, 0.05, 0.6, 0.05, 0.3, 0.05, 0.15 }, 0.4, 0.05, 0.5, 31);                          // Clarinet
    w[31] = noisy(0.5, 0.4, 0.15, 32, 0.15, 1);                                                          // Breath
    w[32] = noisy(0.6, 0.7, 0.25, 33);                                                                   // Steamer
    w[33] = onset({ 1, 0.25, 0.1, 0.04 }, 0.8, 0.05, 0.45, 34);                                          // High Flute
    w[34] = onset({ 1, 0.4, 0.15, 0.05 }, 0.7, 0.07, 0.5, 35);                                           // Low Flute
    w[35] = noisy(0.4, 0.6, 0.3, 36);                                                                    // Guiro
    w[36] = onset({ 1, 0.3, 0.2, 0.1 }, 1.0, 0.08, 0.6, 37, 0.01);                                       // Indian Flute
    w[37] = onset({ 0.2, 1, 0.5, 0.3, 0.2 }, 0.6, 0.04, 0.4, 38);                                        // Flute Harmonics
    w[38] = onset({ 1, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2 }, 0.5, 0.03, 0.4, 39);                              // Lips 1
    w[39] = onset({ 1, 0.9, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2 }, 0.4, 0.02, 0.35, 40);                        // Lips 2
    w[40] = onset({ 1, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.25, 0.2 }, 0.3, 0.04, 0.5, 41);              // Trumpet
    w[41] = onset({ 1, 0.8, 0.6, 0.5, 0.35, 0.25, 0.15 }, 0.3, 0.06, 0.6, 42);                           // Trombones
    w[42] = onset({ 1, 0.7, 0.5, 0.4, 0.3, 0.2, 0.15, 0.1 }, 0.5, 0.08, 0.7, 43);                        // Contrabass
    w[43] = onset({ 1, 0.6, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15 }, 0.6, 0.07, 0.6, 44);                       // Cello
    w[44] = onset({ 1, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15, 0.12, 0.1 }, 0.9, 0.06, 0.5, 45);                 // Violin Bow
    w[45] = onset({ 1, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15, 0.12 }, 0.5, 0.1, 0.8, 46, 0.006);                // Violins
    w[46] = plucked(0.4, 0.6, 0.99, 47, 0.1);                                                            // Pizzicato
    // 48-76: loops
    for (int k = 47; k < 76; ++k) loop[size_t(k)] = true;
    w[47] = cycle({ 1, 1, 0.8, 0.7, 0, 0.5, 0, 0.4 });                                                   // Drawbars
    w[48] = cycle({ 0.6, 1, 0.8, 0.9, 0, 0.6, 0, 0.5, 0, 0.3 });                                         // High Organ
    w[49] = cycle({ 1, 0.9, 0.5, 0.2 });                                                                 // Low Organ
    w[50] = cycle({ 1, 0.15, 0.05, 0.02 });                                                              // E.Piano 1
    w[51] = cycle({ 1, 0.3, 0.1, 0.05, 0.02 });                                                          // E.Piano 2
    w[52] = square(21, 0.6);                                                                             // Clavi
    w[53] = saw(30, 0.7);                                                                                // Harpsichord
    w[54] = saw(12, 1.3);                                                                                // E.Bass 1
    w[55] = cycle({ 1, 0.4, 0.15, 0.05 });                                                               // Ac.Bass
    w[56] = saw(16, 1.1);                                                                                // E.Bass 2
    w[57] = square(9, 1.2);                                                                              // E.Bass 3
    w[58] = saw(20, 1.0);                                                                                // E.Guitar
    w[59] = vowel(500, 1500, 2600, 24, 0.4);                                                             // Cello
    w[60] = vowel(700, 2000, 3200, 30, 0.3);                                                             // Violin
    w[61] = square(25, 0.8);                                                                             // Reed
    w[62] = vowel(600, 1300, 2800, 28, 0.3);                                                             // Sax 1
    w[63] = vowel(800, 1800, 3000, 28, 0.25);                                                            // Sax 2
    w[64] = vowel(750, 1200, 2600, 20, 0.6);                                                             // Aah
    w[65] = vowel(350, 800, 2400, 16, 0.8);                                                              // Ooh
    w[66] = vowel(500, 1000, 2500, 18, 0.7);                                                             // Male 1
    w[67] = band(1, 3);                                                                                  // Spectrum 1
    w[68] = band(2, 6);                                                                                  // Spectrum 2
    w[69] = band(4, 10);                                                                                 // Spectrum 3
    w[70] = band(8, 16);                                                                                 // Spectrum 4
    w[71] = band(12, 24);                                                                                // Spectrum 5
    w[72] = band(1, 24);                                                                                 // Spectrum 6
    w[73] = band(16, 40);                                                                                // Spectrum 7
    w[74] = vowel(400, 900, 2300, 18, 0.7);                                                              // Male 2
    {                                                                                                    // Noise
        std::vector<float> n(32000);
        Rng r { 76 };
        for (auto& v : n) v = r.next();
        normalise(n);
        w[75] = n;
    }
    // 77-100: pairs of the loops above
    static const int pairs[24][2] = { { 47, 67 }, { 48, 70 }, { 50, 72 }, { 52, 73 }, { 53, 69 }, { 54, 68 },
                                      { 56, 71 }, { 58, 70 }, { 59, 64 }, { 60, 65 }, { 61, 72 }, { 62, 69 },
                                      { 63, 71 }, { 64, 66 }, { 65, 74 }, { 66, 68 }, { 67, 73 }, { 68, 72 },
                                      { 69, 71 }, { 70, 73 }, { 49, 64 }, { 51, 66 }, { 55, 75 }, { 57, 75 } };
    for (int k = 0; k < 24; ++k) {
        w[size_t(76 + k)] = mix(w[size_t(pairs[k][0])], w[size_t(pairs[k][1])], pairs[k][1] == 75 ? 0.15f : 0.7f);
        loop[size_t(76 + k)] = true;
    }
    for (int k = 0; k < kNumWaves; ++k) {
        PcmWave& pw = fWaves[size_t(k)];
        pw.looped = loop[size_t(k)];
        pw.data.resize(w[size_t(k)].size());
        for (size_t i = 0; i < pw.data.size(); ++i) pw.data[i] = toLog(w[size_t(k)][i]);
    }
}

bool PcmBank::loadRom(const std::string& path, std::string& error)
{
    // The D-50 wave ROM layout (the 100 waves' addresses, lengths, loops and
    // sample format) is not known well enough yet to read a dump reliably.
    (void)path;
    error = "D-50 wave ROM dumps are not supported yet";
    return false;
}

} // namespace la50
