// La50Pcm.cpp - see La50Pcm.hpp. The stand-ins are simple recipes, not
// recordings: they give an imported D-50 patch the right kind of onset or
// loop (a mallet, a pluck, breath, a bowed or blown attack; an organ, a vowel,
// a bright spectrum) so it is recognisable, not authentic.
//
// The construction follows what the decoded-ROM analysis of the D-50's sample
// ROMs has established publicly (Michi71/PicoVintageSynthCollection
// tools/d5_extract, and the VOGONS LA-synth sample ROM thread it documents;
// no Roland data is used here):
//  - every sustained loop is ONE cycle whose period divides the 32 kHz clock
//    (128 W = 250 Hz ... 2048 W = 15.625 Hz; Noise: 16384 W = 1.953125 Hz),
//    tile-locked: the loop wraps with no discontinuity and never flutters;
//  - the Spectrum loops are inharmonic by construction (one huge period, not
//    a harmonic band); Noise loops seamlessly (its last word is its first);
//  - one-shot attacks sit in 2048-word page slots (64 ms each, sizes 2048<<n)
//    padded with digital silence - the decay belongs to the synth partial's
//    envelopes, not the sample;
//  - each loop wave carries its true root (250/125/62.5/31.25/15.625 Hz) and
//    the engine transposes from it, the way the real chip does. Attack roots
//    are not published, so attacks stay pitched at C4 (pitchOffset 0).
//
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

// the loop roots the D-50's ROM uses: periods that divide the 32 kHz clock
constexpr double kRoot250 = 250.0;                // 128-word cycles
constexpr double kRoot125 = 125.0;                // 256
constexpr double kRoot62_5 = 62.5;                // 512
constexpr double kRoot31_25 = 31.25;              // 1024
constexpr double kRoot15_625 = 15.625;            // 2048 (the Spectrum loops)
constexpr size_t kNoiseLoopLen = 16384;           // Noise: its own 8 pages

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

// pad a one-shot attack to a whole number of 2048-word pages with digital
// silence (the all-zero word in this log format), as the ROM pads its slots
void padToPage(std::vector<float>& x, size_t minPages = 1)
{
    const size_t slot = ((x.size() + 2047) / 2048) * 2048;
    x.resize(std::max(slot, minPages * 2048), 0.f);
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
    // the slot's digital-silence padding replaces the fade: the patch's own
    // envelopes carry the decay from the end of the onset
    padToPage(x);
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
    padToPage(x);
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
    padToPage(x);
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
    padToPage(x);
    normalise(x);
    return x;
}

// ---- the loops: ONE exact cycle whose period divides the 32 kHz clock ----

// a cycle of `words` samples: harmonics (amplitudes) of a `rootHz` fundamental
std::vector<float> cycleWords(size_t words, std::initializer_list<double> harmonics, double phaseSpread = 0.0)
{
    std::vector<float> x(words, 0.f);
    const int nMax = int(words / 2); // Nyquist
    int h = 1;
    for (double a : harmonics) {
        if (h <= nMax)
            for (size_t i = 0; i < words; ++i)
                x[i] += float(a * std::sin(2 * kPi * double(h) * double(i) / double(words) + phaseSpread * h * h));
        ++h;
    }
    normalise(x);
    return x;
}

// a sawtooth stack at a power-of-two period
std::vector<float> sawWords(size_t words, int n, double tilt = 1.0)
{
    std::vector<float> x(words, 0.f);
    for (size_t i = 0; i < words; ++i) {
        double v = 0;
        for (int h = 1; h <= n && h * 2 <= int(words); ++h)
            v += std::pow(h, -tilt) * std::sin(2 * kPi * double(h) * double(i) / double(words));
        x[i] = float(v);
    }
    normalise(x);
    return x;
}

std::vector<float> squareWords(size_t words, int n, double tilt = 1.0)
{
    std::vector<float> x(words, 0.f);
    for (size_t i = 0; i < words; ++i) {
        double v = 0;
        for (int h = 1; h <= n && h * 2 <= int(words); h += 2)
            v += std::pow(h, -tilt) * std::sin(2 * kPi * double(h) * double(i) / double(words));
        x[i] = float(v);
    }
    normalise(x);
    return x;
}

// a vowel: harmonics of rootHz weighted by three formant peaks (absolute Hz)
std::vector<float> vowelWords(size_t words, double rootHz, double f1, double f2, double f3, double tilt)
{
    std::vector<float> x(words, 0.f);
    const int n = int(words / 2);
    for (int h = 1; h <= n; ++h) {
        const double f = rootHz * h;
        auto peak = [&](double c, double bw) { return 1.0 / (1.0 + std::pow((f - c) / bw, 2)); };
        const double a = (peak(f1, 120) + 0.6 * peak(f2, 180) + 0.3 * peak(f3, 250)) * std::pow(h, -tilt);
        for (size_t i = 0; i < words; ++i)
            x[i] += float(a * std::sin(2 * kPi * double(h) * double(i) / double(words)));
    }
    normalise(x);
    return x;
}

// an inharmonic "Spectrum": one 2048-word period of non-integer partial
// ratios - the measured character of Spect1..7 (a single 15.625 Hz period,
// not a harmonic band)
std::vector<float> spectrum(uint32_t seed, double lo, double hi, int count)
{
    Rng r { seed };
    std::vector<float> x(2048, 0.f);
    for (int k = 0; k < count; ++k) {
        const double ratio = lo + (hi - lo) * (double(k) / std::max(1, count - 1)) * (0.85 + 0.3 * r.next());
        const double amp = 1.0 / std::sqrt(double(k + 1));
        for (size_t i = 0; i < x.size(); ++i)
            x[i] += float(amp * std::sin(2 * kPi * ratio * double(i) / double(x.size())));
    }
    normalise(x);
    return x;
}

// the Noise loop: 16384 words that wrap seamlessly (its last word continues
// into its first, as the measured ROM's does - the jump is exactly zero)
std::vector<float> noiseLoop(uint32_t seed)
{
    std::vector<float> x(kNoiseLoopLen);
    Rng r { seed };
    for (auto& v : x) v = r.next();
    // crossfade the tail into the head so x[N-1] flows into x[0] with no step
    const size_t f = 128;
    for (size_t i = 0; i < f; ++i) {
        const float w = float(i) / float(f);
        x[x.size() - f + i] = (1.f - w) * x[x.size() - f + i] + w * x[i];
    }
    x[x.size() - 1] = x[x.size() - f - 1] * 0.5f + x[f] * 0.5f; // the seam sample
    normalise(x);
    return x;
}

// a combination (waves 77-100): two loops resampled into one cycle. The real
// 77-100 are address ranges over the primary material and remain unresolved;
// this keeps the stand-in approximation, now period-exact.
std::vector<float> combine(const std::vector<float>& a, const std::vector<float>& b, float gb)
{
    std::vector<float> bb;
    if (b.size() == a.size()) {
        bb = b;
    } else { // resample b's cycle into a's length (both are single periods)
        bb.assign(a.size(), 0.f);
        const double rate = double(b.size()) / double(a.size());
        for (size_t i = 0; i < bb.size(); ++i) {
            const double p = double(i) * rate;
            const size_t i0 = size_t(p) % b.size();
            const size_t i1 = (i0 + 1) % b.size();
            bb[i] = float(b[i0] * (1.0 - (p - std::floor(p))) + b[i1] * (p - std::floor(p)));
        }
    }
    std::vector<float> x(a.size());
    for (size_t i = 0; i < x.size(); ++i) x[i] = a[i] + gb * bb[i];
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

int PcmWave::pitchOffset() const
{
    // 4096ths of an octave from C4 (the engine's PCM base pitch)
    if (!(rootHz > 0.f))
        return 0;
    return int(std::lround(4096.0 * std::log2(double(rootHz) / kC4)));
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
    std::vector<float> root(kNumWaves, 0.f); // 0 = C4, no offset

    // ---- 1-47: one-shot onsets, padded to page slots with digital silence ----
    // (slot sizes follow the ROM's 2048<<n law; the exact sizes per wave are
    // unpublished, so these are ear-chosen within that law)
    w[0] = struck({ { { 1, 1, 0.25 } }, { { 4, 0.5, 0.06 } }, { { 9.9, 0.2, 0.02 } } }, 0.25, 0.15, 1);     // Marimba
    w[1] = struck({ { { 1, 1, 1.2 } }, { { 4, 0.35, 0.3 } }, { { 10, 0.1, 0.05 } } }, 0.5, 0.05, 2);          // Vibraphone
    w[2] = struck({ { { 1, 1, 0.12 } }, { { 3, 0.7, 0.05 } }, { { 6, 0.4, 0.02 } } }, 0.12, 0.3, 3);          // Xylophone 1
    w[3] = struck({ { { 1, 1, 0.08 } }, { { 3.9, 0.6, 0.04 } }, { { 9, 0.4, 0.015 } } }, 0.1, 0.4, 4);       // Xylophone 2
    w[4] = struck({ { { 0.5, 1, 0.2 } }, { { 1, 0.6, 0.1 } }, { { 2.7, 0.3, 0.03 } } }, 0.2, 0.2, 5);        // Log Bass
    w[5] = struck({ { { 1, 0.5, 0.05 } }, { { 2.3, 0.4, 0.03 } } }, 0.08, 0.9, 6);                           // Hammer
    w[6] = noisy(0.25, 0.15, 0.15, 7, 1.5, 0.25);                                                           // Japanese Drum
    w[7] = struck({ { { 1, 1, 0.4 } }, { { 5.4, 0.3, 0.05 } } }, 0.3, 0.1, 8);                               // Kalimba
    w[8] = plucked(0.25, 0.9, 0.995, 9, 0.3);                                                               // Pluck 1
    w[9] = struck({ { { 3.1, 1, 0.08 } }, { { 4.7, 0.7, 0.06 } }, { { 7.2, 0.5, 0.04 } } }, 0.12, 0.3, 10);  // Chink
    w[10] = struck({ { { 2, 1, 0.3 } }, { { 5.1, 0.4, 0.1 } } }, 0.25, 0.2, 11);                            // Agogo
    w[11] = struck({ { { 6, 1, 1.0 } }, { { 9.2, 0.6, 0.8 } }, { { 13.5, 0.4, 0.5 } } }, 0.5, 0.1, 12);       // Triangle
    w[12] = struck({ { { 1, 1, 1.5 } }, { { 2.76, 0.6, 0.8 } }, { { 5.4, 0.4, 0.4 } }, { { 8.9, 0.3, 0.2 } } }, 0.5, 0.05,
                   13);                                                                                     // Bells
    w[13] = noisy(0.25, 0.9, 0.06, 14);                                                                     // Nail File
    w[14] = noisy(0.06, 0.8, 0.015, 15, 0.5, 4);                                                            // Pick
    w[15] = plucked(0.5, 0.5, 0.998, 16, 0.2);                                                              // Low Piano
    w[16] = plucked(0.4, 0.65, 0.997, 17, 0.2);                                                             // Mid Piano
    w[17] = plucked(0.3, 0.8, 0.995, 18, 0.2);                                                              // High Piano
    w[18] = plucked(0.25, 1.0, 0.996, 19, 0.4);                                                            // Harpsichord
    w[19] = plucked(0.5, 0.6, 0.998, 20, 0.05);                                                             // Harp
    w[20] = struck({ { { 2, 1, 0.15 } }, { { 3, 0.7, 0.1 } }, { { 4, 0.3, 0.08 } } }, 0.2, 0.05, 21);       // Organ Percussion
    w[21] = plucked(0.4, 0.9, 0.997, 22, 0.3);                                                              // Steel Strings
    w[22] = plucked(0.4, 0.55, 0.996, 23, 0.15);                                                           // Nylon Strings
    w[23] = plucked(0.35, 0.7, 0.996, 24, 0.2);                                                            // Electric Guitar 1
    w[24] = plucked(0.35, 0.8, 0.995, 25, 0.3);                                                            // Electric Guitar 2
    w[25] = plucked(0.3, 1.0, 0.993, 26, 0.6);                                                             // Dirty Guitar
    w[26] = plucked(0.3, 0.6, 0.996, 27, 0.5);                                                             // Pick Bass
    w[27] = plucked(0.25, 0.85, 0.994, 28, 0.8);                                                           // Pop Bass
    w[28] = struck({ { { 1, 1, 0.12 } }, { { 2, 0.5, 0.05 } } }, 0.12, 0.6, 29);                            // Thump
    w[29] = plucked(0.35, 0.4, 0.997, 30, 0.3);                                                             // Upright Bass
    w[30] = onset({ 1, 0.05, 0.6, 0.05, 0.3, 0.05, 0.15 }, 0.4, 0.05, 0.25, 31);                            // Clarinet
    w[31] = noisy(0.25, 0.4, 0.15, 32, 0.15, 1);                                                            // Breath
    w[32] = noisy(0.25, 0.7, 0.25, 33);                                                                     // Steamer
    w[33] = onset({ 1, 0.25, 0.1, 0.04 }, 0.8, 0.05, 0.25, 34);                                             // High Flute
    w[34] = onset({ 1, 0.4, 0.15, 0.05 }, 0.7, 0.07, 0.25, 35);                                            // Low Flute
    w[35] = noisy(0.25, 0.6, 0.3, 36);                                                                      // Guiro
    w[36] = onset({ 1, 0.3, 0.2, 0.1 }, 1.0, 0.08, 0.3, 37, 0.01);                                          // Indian Flute
    w[37] = onset({ 0.2, 1, 0.5, 0.3, 0.2 }, 0.6, 0.04, 0.25, 38);                                          // Flute Harmonics
    w[38] = onset({ 1, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2 }, 0.5, 0.03, 0.25, 39);                                // Lips 1
    w[39] = onset({ 1, 0.9, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2 }, 0.4, 0.02, 0.25, 40);                           // Lips 2
    w[40] = onset({ 1, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.25, 0.2 }, 0.3, 0.04, 0.25, 41);                // Trumpet
    w[41] = onset({ 1, 0.8, 0.6, 0.5, 0.35, 0.25, 0.15 }, 0.3, 0.06, 0.3, 42);                              // Trombones
    w[42] = onset({ 1, 0.7, 0.5, 0.4, 0.3, 0.2, 0.15, 0.1 }, 0.5, 0.08, 0.3, 43);                           // Contrabass
    w[43] = onset({ 1, 0.6, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15 }, 0.6, 0.07, 0.3, 44);                          // Cello
    w[44] = onset({ 1, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15, 0.12, 0.1 }, 0.9, 0.06, 0.3, 45);                    // Violin Bow
    w[45] = onset({ 1, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15, 0.12 }, 0.5, 0.1, 0.4, 46, 0.006);                   // Violins
    w[46] = plucked(0.12, 0.6, 0.99, 47, 0.1);                                                              // Pizzicato

    // ---- 48-76: the sustained loops - one power-of-two cycle at its true root ----
    for (int k = 47; k < 76; ++k) loop[size_t(k)] = true;
    w[47] = cycleWords(512, { 1, 1, 0.8, 0.7, 0, 0.5, 0, 0.4 });                                            // 48 Drawbars (62.5 Hz)
    root[47] = float(kRoot62_5);
    w[48] = cycleWords(128, { 0.6, 1, 0.8, 0.9, 0, 0.6, 0, 0.5, 0, 0.3 });                                  // 49 High Organ (250 Hz)
    root[48] = float(kRoot250);
    w[49] = cycleWords(256, { 1, 0.9, 0.5, 0.2 });                                                          // 50 Low Organ (125 Hz)
    root[49] = float(kRoot125);
    w[50] = cycleWords(256, { 1, 0.15, 0.05, 0.02 });                                                      // 51 E.Piano 1 (125 Hz)
    root[50] = float(kRoot125);
    w[51] = cycleWords(256, { 1, 0.3, 0.1, 0.05, 0.02 });                                                   // 52 E.Piano 2 (125 Hz)
    root[51] = float(kRoot125);
    w[52] = squareWords(512, 64, 0.6);                                                                      // 53 Clavi (62.5 Hz)
    root[52] = float(kRoot62_5);
    w[53] = sawWords(512, 80, 0.7);                                                                         // 54 Harpsichord (62.5 Hz)
    root[53] = float(kRoot62_5);
    w[54] = sawWords(512, 48, 1.3);                                                                         // 55 E.Bass 1 (62.5 Hz)
    root[54] = float(kRoot62_5);
    w[55] = cycleWords(512, { 1, 0.4, 0.15, 0.05 });                                                        // 56 Ac.Bass (62.5 Hz)
    root[55] = float(kRoot62_5);
    w[56] = sawWords(512, 64, 1.1);                                                                         // 57 E.Bass 2 (62.5 Hz)
    root[56] = float(kRoot62_5);
    w[57] = squareWords(512, 36, 1.2);                                                                      // 58 E.Bass 3 (62.5 Hz)
    root[57] = float(kRoot62_5);
    w[58] = sawWords(512, 80, 1.0);                                                                         // 59 E.Guitar (62.5 Hz)
    root[58] = float(kRoot62_5);
    w[59] = vowelWords(128, kRoot250, 500, 1500, 2600, 0.4);                                                // 60 Cello (250 Hz)
    root[59] = float(kRoot250);
    w[60] = vowelWords(128, kRoot250, 700, 2000, 3200, 0.3);                                                // 61 Violin (250 Hz)
    root[60] = float(kRoot250);
    w[61] = squareWords(512, 100, 0.8);                                                                     // 62 Reed (62.5 Hz)
    root[61] = float(kRoot62_5);
    w[62] = vowelWords(256, kRoot125, 600, 1300, 2800, 0.3);                                                // 63 Sax 1 (125 Hz)
    root[62] = float(kRoot125);
    w[63] = vowelWords(256, kRoot125, 800, 1800, 3000, 0.25);                                               // 64 Sax 2 (125 Hz)
    root[63] = float(kRoot125);
    w[64] = vowelWords(128, kRoot250, 750, 1200, 2600, 0.6);                                                // 65 Aah (250 Hz)
    root[64] = float(kRoot250);
    w[65] = vowelWords(1024, kRoot31_25, 350, 800, 2400, 0.8);                                             // 66 Ooh (31.25 Hz)
    root[65] = float(kRoot31_25);
    w[66] = vowelWords(256, kRoot125, 500, 1000, 2500, 0.7);                                                // 67 Male 1 (125 Hz)
    root[66] = float(kRoot125);
    // 68-74 Spectrum 1-7: one inharmonic 2048-word period each (15.625 Hz)
    w[67] = spectrum(101, 1, 4, 24);                                                                        // Spectrum 1
    w[68] = spectrum(102, 2, 8, 24);                                                                         // Spectrum 2
    w[69] = spectrum(103, 4, 16, 24);                                                                        // Spectrum 3
    w[70] = spectrum(104, 8, 32, 24);                                                                        // Spectrum 4
    w[71] = spectrum(105, 12, 48, 24);                                                                       // Spectrum 5
    w[72] = spectrum(106, 1, 48, 24);                                                                        // Spectrum 6
    w[73] = spectrum(107, 16, 80, 24);                                                                       // Spectrum 7
    for (int k = 67; k <= 73; ++k) root[size_t(k)] = float(kRoot15_625);
    w[74] = vowelWords(256, kRoot125, 400, 900, 2300, 0.7);                                                 // 75 Male 2 (125 Hz)
    root[74] = float(kRoot125);
    w[75] = noiseLoop(76);                                                                                  // 76 Noise (16384 W)
    root[75] = 0.f; // unpitched: transposition stays at C4

    // ---- 77-100: combinations of two loops (approximations: the real ones
    //      are address ranges over the primary material and are unresolved) ----
    static const int pairs[24][2] = { { 47, 67 }, { 48, 70 }, { 50, 72 }, { 52, 73 }, { 53, 69 }, { 54, 68 },
                                      { 56, 71 }, { 58, 70 }, { 59, 64 }, { 60, 65 }, { 61, 72 }, { 62, 69 },
                                      { 63, 71 }, { 64, 66 }, { 65, 74 }, { 66, 68 }, { 67, 73 }, { 68, 72 },
                                      { 69, 71 }, { 70, 73 }, { 49, 64 }, { 51, 66 }, { 55, 75 }, { 57, 75 } };
    for (int k = 0; k < 24; ++k) {
        const int a = pairs[k][0], b = pairs[k][1];
        w[size_t(76 + k)] = combine(w[size_t(a)], w[size_t(b)], b == 75 ? 0.15f : 0.7f);
        loop[size_t(76 + k)] = true;
        root[size_t(76 + k)] = root[size_t(a)];
    }

    for (int k = 0; k < kNumWaves; ++k) {
        PcmWave& pw = fWaves[size_t(k)];
        pw.looped = loop[size_t(k)];
        pw.rootHz = root[size_t(k)];
        pw.data.resize(w[size_t(k)].size());
        for (size_t i = 0; i < pw.data.size(); ++i) pw.data[i] = toLog(w[size_t(k)][i]);
    }
}

bool PcmBank::loadRom(const std::string& path, std::string& error)
{
    // The D-50 wave ROM layout (the 100 waves' addresses, lengths, loops and
    // sample format) is documented publicly now (d5_extract; see the header
    // comment), but no ROM data is shipped here and the loader is not wired
    // in yet.
    (void)path;
    error = "D-50 wave ROM dumps are not supported yet";
    return false;
}

} // namespace la50