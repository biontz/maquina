// mutable_test.cpp - Maquina Klang (Plaits) and Klong / Klong FX (Rings).
//
// 1. Reference: KlangHost / KlongHost at the engine's own rate (resamplers
//    bypassed) must equal the vendored module code driven directly, the way
//    the modules' main loops drive it (plaits.cc / rings.cc FillBuffer with
//    the knobs fixed), bit for bit, delayed by the reported latency. Every
//    Plaits model, every Rings model + polyphony, the FX's audio path.
//    --mutate: two scenarios change a little (Plaits: Timbre by 1 ulp; Rings:
//    the pitch by 1 cent: less is lost in its 1/256-semitone lookups); the check must fail.
// 2. Behaviour: BlockStream's latency (an identity engine), no output FIFO
//    underruns at any rate / host block size, MIDI A4 = 440 Hz on both,
//    Klong strums a chord one note per 10 ms, Klong FX rings with the input's
//    onsets, Klang's Key Track off keeps the pitch.
//
// SPDX-License-Identifier: GPL-3.0-only
#define TEST 1
#include <cstdio>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <functional>
#include <type_traits>
#include <random>
#include <string>
#include <vector>

#include "plaits/dsp/voice.h"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"
#include "stmlib/utils/buffer_allocator.h"
#include "stmlib/utils/random.h"

#include "BlockStream.hpp"
#include "KlangHost.hpp"
#include "KlongHost.hpp"
#include "VoiceTables.generated.hpp"

using namespace maquina;

static bool gMutate = false;
static int gFailures = 0;

static void fail(const std::string& what)
{
    std::printf("FAIL %s\n", what.c_str());
    ++gFailures;
}

static const VoiceDef& voice(const char* id)
{
    for (const VoiceDef& d : k_voices)
        if (!std::strcmp(d.id, id)) return d;
    std::fprintf(stderr, "no voice %s\n", id);
    std::exit(2);
}

static int ix(const VoiceDef& d, const char* sym)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, sym)) return int(i);
    std::fprintf(stderr, "%s: no parameter %s\n", d.id, sym);
    std::exit(2);
}

// an object in zeroed memory (the module's objects live in .bss)
template <class T>
struct Zeroed {
    T* p;
    Zeroed() : p(new (std::calloc(1, sizeof(T))) T) {}
    ~Zeroed() { p->~T(); std::free(p); }
    T& operator*() { return *p; }
};

struct Note {
    uint32_t frame; // host frame
    uint8_t note;
    bool on;
};

// render a host: frames in host blocks of `block`, notes at their frames
template <class Host>
static void render(Host& h, uint32_t frames, uint32_t block, const std::vector<Note>& notes, std::vector<float>& l,
                   std::vector<float>& r, const std::vector<float>* in = nullptr)
{
    l.assign(frames, 0.f);
    r.assign(frames, 0.f);
    for (uint32_t pos = 0; pos < frames;) {
        const uint32_t n = std::min(block, frames - pos);
        for (const Note& e : notes)
            if (e.frame >= pos && e.frame < pos + n) {
                if (e.on) h.noteOn(e.frame - pos, e.note, 100);
                else h.noteOff(e.frame - pos, e.note);
            }
        if constexpr (std::is_same_v<Host, KlongHost>) {
            const float* ip = in ? in->data() + pos : nullptr;
            h.process(ip, ip, l.data() + pos, r.data() + pos, n);
        } else {
            h.process(l.data() + pos, r.data() + pos, n);
        }
        pos += n;
    }
}

static bool sameDelayed(const char* what, const std::vector<float>& host, const std::vector<float>& ref,
                        uint32_t latency)
{
    double energy = 0.0;
    for (size_t j = 0; j + latency < host.size() && j < ref.size(); ++j) {
        energy += double(ref[j]) * ref[j];
        if (host[j + latency] != ref[j]) {
            fail(std::string(what) + ": differs at engine frame " + std::to_string(j) + " (host " +
                 std::to_string(host[j + latency]) + ", ref " + std::to_string(ref[j]) + ")");
            return false;
        }
    }
    for (uint32_t j = 0; j < latency && j < host.size(); ++j)
        if (host[j] != 0.f) {
            fail(std::string(what) + ": output before the latency");
            return false;
        }
    if (energy < 1e-6) {
        fail(std::string(what) + ": silent");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 1a. Klang vs plaits::Voice

static bool klangRef(int model)
{
    const VoiceDef& d = voice("klang");
    const uint32_t seed = 0x1234567u + uint32_t(model);
    const int coarse = -5, fine = 30, harm = 40, timbre = 90, morph = 20, fmEnv = 10, tEnv = -20, mEnv = 30,
              decay = 70, colour = 50;
    const uint32_t kBlocks = 2000, kFrames = kBlocks * KlangHost::kBlock;
    // notes on block boundaries: 72 on at block 10, off at 600; 65 on at 900 (retrigger
    // while 72 is held? no: 72 is off), 67 on at 1200 while 65 is held, both off at 1700
    struct BN { uint32_t block; uint8_t note; bool on; };
    const std::vector<BN> bn = { { 10, 72, true }, { 600, 72, false }, { 900, 65, true },
                                 { 1200, 67, true }, { 1700, 67, false }, { 1700, 65, false } };

    // host at the engine rate
    KlangHost h(d, seed);
    h.setSampleRate(KlangHost::kEngineRate);
    h.setParam(ix(d, "model"), float(model));
    h.setParam(ix(d, "coarse"), float(coarse));
    h.setParam(ix(d, "fine"), float(fine));
    h.setParam(ix(d, "harmonics"), float(harm));
    h.setParam(ix(d, "timbre"), float(timbre));
    h.setParam(ix(d, "morph"), float(morph));
    h.setParam(ix(d, "fm_env"), float(fmEnv));
    h.setParam(ix(d, "timbre_env"), float(tEnv));
    h.setParam(ix(d, "morph_env"), float(mEnv));
    h.setParam(ix(d, "lpg_decay"), float(decay));
    h.setParam(ix(d, "lpg_colour"), float(colour));
    h.setParam(ix(d, "output"), 2.f); // Out + Aux
    std::vector<Note> notes;
    for (const BN& b : bn) notes.push_back({ b.block * uint32_t(KlangHost::kBlock), b.note, b.on });
    std::vector<float> hl, hr;
    render(h, kFrames + h.latencyFrames(), 37, notes, hl, hr);

    // the module: plaits.cc Init() + FillBuffer(), Ui::Poll() with fixed knobs
    // zeroed, as the module's .bss
    std::vector<char> shared(16384, 0);
    Zeroed<plaits::Voice> vp;
    plaits::Voice& v = *vp;
    stmlib::Random::Seed(seed);
    stmlib::BufferAllocator allocator(shared.data(), shared.size());
    v.Init(&allocator);
    plaits::Patch p {};
    p.engine = (model + 8) % 24; // Model list: the original 16 first
    p.note = 60.f + float(coarse) + 0.01f * float(fine);
    p.harmonics = float(harm) / 127.f;
    p.timbre = float(timbre) / 127.f;
    p.morph = float(morph) / 127.f;
    p.frequency_modulation_amount = float(fmEnv) / 63.f;
    p.timbre_modulation_amount = float(tEnv) / 63.f;
    p.morph_modulation_amount = float(mEnv) / 63.f;
    p.decay = float(decay) / 127.f;
    p.lpg_colour = float(colour) / 127.f;
    if (gMutate && model == 5) p.timbre = std::nextafter(p.timbre, 1.f);
    plaits::Modulations m {};
    m.trigger_patched = true;
    std::vector<float> rl, rr;
    float pitchLp = 0.f;
    int key = 60;
    std::vector<int> held;
    bool retrig = false;
    for (uint32_t b = 0; b < kBlocks; ++b) {
        for (const BN& e : bn)
            if (e.block == b) {
                if (e.on) {
                    retrig = !held.empty();
                    held.push_back(e.note);
                    key = e.note;
                } else {
                    held.erase(std::find(held.begin(), held.end(), e.note));
                    if (!held.empty()) key = held.back();
                }
            }
        ONE_POLE(pitchLp, float(key - 60), 0.7f);
        m.note = pitchLp;
        m.trigger = !held.empty() && !retrig ? 1.f : 0.f;
        retrig = false;
        plaits::Voice::Frame f[KlangHost::kBlock];
        v.Render(p, m, f, KlangHost::kBlock);
        for (const auto& x : f) {
            rl.push_back(-float(x.out) / 32768.f);
            rr.push_back(-float(x.aux) / 32768.f);
        }
    }
    const std::string what = "klang model " + std::to_string(model);
    return sameDelayed((what + " out").c_str(), hl, rl, h.latencyFrames()) &&
           sameDelayed((what + " aux").c_str(), hr, rr, h.latencyFrames());
}

// ---------------------------------------------------------------------------
// 1b. Klong / Klong FX vs rings::Part / StringSynthPart

static bool klongRef(int model, int poly, bool fx)
{
    const VoiceDef& d = voice(fx ? "klong_fx" : "klong");
    const uint32_t seed = 0x2468aceu + uint32_t(model * 7 + poly);
    const int coarse = -12, fine = 0, structure = 70, brightness = 80, damping = 60, position = 30;
    const uint32_t kBlocks = 3000, kFrames = kBlocks * KlongHost::kBlock;
    struct BN { uint32_t block; uint8_t note; };
    // instrument: a chord on one block (strummed 21 blocks apart), then single notes
    const std::vector<BN> bn = { { 5, 60 }, { 5, 64 }, { 5, 67 }, { 800, 72 }, { 1500, 55 }, { 2200, 62 } };

    // the FX input: short noise bursts (onsets) every 0.25 s
    std::vector<float> in(kFrames + 512, 0.f);
    std::mt19937 rng(model * 31 + poly);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    for (uint32_t t = 2400; t + 200 < kFrames; t += 12000)
        for (uint32_t k = 0; k < 200; ++k) in[t + k] = u(rng) * (1.f - float(k) / 200.f);

    KlongHost h(d, seed);
    h.setSampleRate(KlongHost::kEngineRate);
    h.setParam(ix(d, "model"), float(model));
    h.setParam(ix(d, "polyphony"), float(poly));
    h.setParam(ix(d, "coarse"), float(coarse));
    h.setParam(ix(d, "fine"), float(fine));
    h.setParam(ix(d, "structure"), float(structure));
    h.setParam(ix(d, "brightness"), float(brightness));
    h.setParam(ix(d, "damping"), float(damping));
    h.setParam(ix(d, "position"), float(position));
    std::vector<Note> notes;
    if (!fx)
        for (const BN& b : bn) notes.push_back({ b.block * uint32_t(KlongHost::kBlock), b.note, true });
    std::vector<float> hl, hr;
    render(h, kFrames + h.latencyFrames(), 53, notes, hl, hr, fx ? &in : nullptr);

    // the module: rings.cc Init() + FillBuffer(), CvScaler::Read() with fixed knobs
    std::vector<uint16_t> reverbBuf(32768, 0);
    uint16_t* reverb = reverbBuf.data();
    Zeroed<rings::Part> partP;
    Zeroed<rings::StringSynthPart> synthP;
    Zeroed<rings::Strummer> strummerP;
    rings::Part& part = *partP;
    rings::StringSynthPart& synth = *synthP;
    rings::Strummer& strummer = *strummerP;
    stmlib::Random::Seed(seed);
    strummer.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
    part.Init(reverb);
    synth.Init(reverb);
    part.set_polyphony(poly);
    synth.set_polyphony(poly);
    const bool egg = model >= 6;
    if (egg) synth.set_fx(rings::FxType(model - 6));
    else part.set_model(rings::ResonatorModel(model));
    rings::Patch patch;
    patch.structure = float(structure) / 127.f;
    patch.brightness = float(brightness) / 127.f;
    patch.damping = float(damping) / 127.f;
    patch.position = float(position) / 127.f;
    int32_t chord = 0;
    float inLevel = 0.f;
    std::vector<int> pending;
    int inhibit = 0, key = 60;
    std::vector<float> rl, rr;
    float ib[24], ob[24], ab[24];
    for (uint32_t b = 0; b < kBlocks; ++b) {
        rings::PerformanceState ps;
        ps.internal_exciter = !fx;
        ps.internal_note = fx;
        ps.internal_strum = fx;
        ps.strum = false;
        if (!fx) {
            for (const BN& e : bn)
                if (e.block == b) pending.push_back(e.note);
            if (inhibit > 0) {
                --inhibit;
            } else if (!pending.empty()) {
                key = pending.front();
                pending.erase(pending.begin());
                ps.strum = true;
                inhibit = 20;
            }
        }
        ps.note = fx ? 0.f : float(key - 60);
        ps.tonic = 60.f + float(coarse) + 0.01f * float(fine);
        if (gMutate && model == 2 && poly == 1 && !fx) ps.tonic += 0.01f; // 1 cent
        ps.fm = 0.f;
        const float c = patch.structure * float(rings::kNumChords - 1);
        chord = std::clamp(int32_t(c + (c - float(chord) > 0.f ? -0.1f : 0.1f) + 0.5f), int32_t(0),
                           int32_t(rings::kNumChords - 1));
        ps.chord = chord;
        for (int i = 0; i < 24; ++i) {
            const float s = fx ? 0.5f * (in[b * 24 + i] + in[b * 24 + i]) : 0.f;
            if (egg) {
                ib[i] = s;
            } else {
                const float err = s * s - inLevel;
                inLevel += err * (err > 0.f ? 0.1f : 0.0001f);
                ib[i] = (inLevel <= 0.00003f ? (1.f / 0.00003f) * inLevel : 1.f) * s;
            }
        }
        if (egg) {
            strummer.Process(nullptr, 24, &ps);
            synth.Process(ps, patch, ib, ob, ab, 24);
        } else {
            strummer.Process(ib, 24, &ps);
            part.Process(ps, patch, ib, ob, ab, 24);
        }
        for (int i = 0; i < 24; ++i) {
            rl.push_back(std::clamp(ob[i], -1.f, 32767.f / 32768.f));
            rr.push_back(std::clamp(ab[i], -1.f, 32767.f / 32768.f));
        }
    }
    const std::string what = std::string(fx ? "klong_fx" : "klong") + " model " + std::to_string(model) +
                             " poly " + std::to_string(poly);
    return sameDelayed((what + " odd").c_str(), hl, rl, h.latencyFrames()) &&
           sameDelayed((what + " even").c_str(), hr, rr, h.latencyFrames());
}

// ---------------------------------------------------------------------------
// 2. behaviour

// BlockStream with an identity engine: an impulse comes out exactly latency() later
static void streamLatency()
{
    for (double rate : { 44100.0, 48000.0, 96000.0, 22050.0 }) {
        BlockStream<24> s;
        s.setup(rate, 48000.0, true);
        const uint32_t n = 8192, at = 1000;
        std::vector<float> in(n, 0.f), l(n), r(n);
        in[at] = 1.f;
        for (uint32_t pos = 0; pos < n; pos += 100) {
            const uint32_t k = std::min(100u, n - pos);
            s.process(in.data() + pos, in.data() + pos, l.data() + pos, r.data() + pos, k,
                      [](uint64_t, const float* il, const float* ir, float* ol, float* orr) {
                          for (int i = 0; i < 24; ++i) {
                              ol[i] = il[i];
                              orr[i] = ir[i];
                          }
                      });
        }
        uint32_t peak = 0;
        for (uint32_t i = 0; i < n; ++i)
            if (std::fabs(l[i]) > std::fabs(l[peak])) peak = i;
        if (peak != at + s.latency() || s.underruns())
            fail("BlockStream at " + std::to_string(int(rate)) + " Hz: impulse at +" + std::to_string(peak - at) +
                 ", latency " + std::to_string(s.latency()) + ", underruns " + std::to_string(s.underruns()));
    }
}

// the strongest spectral peak between lo and hi Hz (Hann-windowed DFT,
// 0.25 Hz steps, parabolic interpolation)
static double peakHz(const std::vector<float>& x, size_t from, size_t len, double rate, double lo, double hi)
{
    std::vector<double> mag;
    for (double f = lo; f <= hi; f += 0.25) {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < len; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(len));
            re += w * x[from + i] * std::cos(2.0 * M_PI * f * double(i) / rate);
            im += w * x[from + i] * std::sin(2.0 * M_PI * f * double(i) / rate);
        }
        mag.push_back(std::sqrt(re * re + im * im));
    }
    size_t k = 1;
    for (size_t i = 1; i + 1 < mag.size(); ++i)
        if (mag[i] > mag[k]) k = i;
    const double a = mag[k - 1], b = mag[k], c = mag[k + 1], den = a - 2 * b + c;
    return lo + 0.25 * (double(k) + (den != 0.0 ? 0.5 * (a - c) / den : 0.0));
}

template <class Host>
static void pitchTest(const char* id, std::function<void(Host&, const VoiceDef&)> setup, double rate, double expect,
                      double toleranceCents)
{
    const VoiceDef& d = voice(id);
    Host h(d, 1);
    h.setSampleRate(rate);
    setup(h, d);
    std::vector<float> l, r;
    render(h, uint32_t(rate * 1.0), 64, { { 100, 69, true } }, l, r);
    const double f = peakHz(l, size_t(rate * 0.3), size_t(rate * 0.3), rate, expect * 0.9, expect * 1.1);
    const double cents = 1200.0 * std::log2(f / expect);
    std::printf("  %s A4 at %.0f Hz host: %.2f Hz (%+.1f cents)\n", id, rate, f, cents);
    if (std::fabs(cents) > toleranceCents) fail(std::string(id) + " pitch");
}

template <class Host>
static void underrunTest(const char* id)
{
    const VoiceDef& d = voice(id);
    for (double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (uint32_t block : { 1u, 17u, 256u, 5000u }) {
            Host h(d, 3);
            h.setSampleRate(rate);
            std::vector<Note> notes;
            for (uint32_t t = 0; t < uint32_t(rate); t += uint32_t(rate / 7)) {
                notes.push_back({ t, uint8_t(48 + (t % 24)), true });
                notes.push_back({ t + uint32_t(rate / 20), uint8_t(48 + (t % 24)), false });
            }
            std::vector<float> l, r, in(uint32_t(rate) + 8, 0.1f);
            render(h, uint32_t(rate), block, notes, l, r, &in);
            if (h.underruns())
                fail(std::string(id) + " underruns at " + std::to_string(int(rate)) + " Hz, block " +
                     std::to_string(block));
        }
}

static void klongStrumTest()
{
    const VoiceDef& d = voice("klong");
    KlongHost h(d, 5);
    h.setSampleRate(44100.0);
    h.setParam(ix(d, "polyphony"), 4.f);
    std::vector<float> l, r;
    // a 4-note chord at once: 4 strums, the last 3 x 10 ms (21 blocks) later
    // (strums at blocks 0, 21, 42, 63; 25 ms = 46 blocks: 3 so far)
    render(h, 44100 / 40, 64, { { 10, 60, true }, { 10, 64, true }, { 10, 67, true }, { 10, 71, true } }, l, r);
    if (h.strums() != 3) fail("klong strums after 25 ms: " + std::to_string(h.strums()) + ", expected 3");
    render(h, 44100 / 10, 64, {}, l, r);
    if (h.strums() != 4) fail("klong strums: " + std::to_string(h.strums()) + ", expected 4");
}

static double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / double(std::max<size_t>(1, to - from)));
}

static void klongFxTest()
{
    const VoiceDef& d = voice("klong_fx");
    const double rate = 44100.0;
    // silence in: silence out (internal notes, internal strum)
    {
        KlongHost h(d, 7);
        h.setSampleRate(rate);
        std::vector<float> l, r, in(44100, 0.f);
        render(h, 44100, 128, {}, l, r, &in);
        if (rms(l, 0, l.size()) > 1e-6) fail("klong_fx: output without input");
    }
    // clicks: the resonator rings after each, and strums on the onsets
    {
        KlongHost h(d, 7);
        h.setSampleRate(rate);
        std::vector<float> l, r, in(88200, 0.f);
        for (uint32_t t = 4410; t < 88200; t += 22050) in[t] = in[t + 1] = 0.8f;
        render(h, 88200, 128, {}, l, r, &in);
        const double ring = rms(l, 4410 + 2000, 4410 + 8000);
        std::printf("  klong_fx: rms after a click %.4f, strums %u\n", ring, h.strums());
        if (ring < 1e-3) fail("klong_fx: no resonance after a click");
        if (h.strums() < 3) fail("klong_fx: onsets do not strum");
    }
    // Notes MIDI: notes strum, the input still excites
    {
        KlongHost h(d, 7);
        h.setSampleRate(rate);
        h.setParam(ix(d, "note_source"), 1.f);
        std::vector<float> l, r, in(44100, 0.f);
        for (uint32_t t = 0; t < 44100; ++t) in[t] = 0.05f * std::sin(0.31f * float(t));
        render(h, 44100, 128, { { 1000, 69, true }, { 20000, 64, true } }, l, r, &in);
        if (h.strums() != 2) fail("klong_fx MIDI: strums " + std::to_string(h.strums()) + ", expected 2");
    }
}

static void klangKeyTrackTest()
{
    const VoiceDef& d = voice("klang");
    const double rate = 48000.0;
    double f[2];
    for (int k = 0; k < 2; ++k) {
        KlangHost h(d, 1);
        h.setSampleRate(rate);
        h.setParam(ix(d, "key_track"), 0.f);
        h.setParam(ix(d, "harmonics"), 0.f);
        h.setParam(ix(d, "lpg_decay"), 127.f);
        std::vector<float> l, r;
        render(h, 48000, 64, { { 100, uint8_t(k ? 84 : 48), true } }, l, r);
        f[k] = peakHz(l, 14400, 9600, rate, 230.0, 290.0); // C4 = 261.6 Hz
    }
    std::printf("  klang Key Track off: note 48 %.1f Hz, note 84 %.1f Hz\n", f[0], f[1]);
    if (std::fabs(1200.0 * std::log2(f[0] / f[1])) > 3.0 || std::fabs(1200.0 * std::log2(f[0] / 261.63)) > 3.0)
        fail("klang Key Track off: not C4 for every note");
}

int main(int argc, char** argv)
{
    gMutate = argc > 1 && !std::strcmp(argv[1], "--mutate");

    int ok = 0, n = 0;
    for (int model = 0; model < KlangHost::kNumModels; ++model, ++n) ok += klangRef(model);
    for (int model = 0; model < 12; ++model)
        for (int poly : { 1, 2, 4 }) {
            if (model >= 6 && poly == 2) continue;
            ++n;
            ok += klongRef(model, poly, false);
        }
    for (int model : { 0, 1, 2, 3, 4, 5, 6, 8 }) {
        ++n;
        ok += klongRef(model, 2, true);
    }
    std::printf("reference: %d/%d scenarios bit-exact\n", ok, n);
    if (gMutate) {
        const bool caught = ok == n - 2;
        std::printf("mutation %s\n", caught ? "caught (2 scenarios differ)" : "NOT caught");
        return caught ? 0 : 1;
    }

    streamLatency();
    underrunTest<KlangHost>("klang");
    underrunTest<KlongHost>("klong");
    underrunTest<KlongHost>("klong_fx");
    pitchTest<KlangHost>("klang", [](KlangHost& h, const VoiceDef& d) {
        h.setParam(ix(d, "harmonics"), 0.f);
        h.setParam(ix(d, "timbre"), 0.f);
        h.setParam(ix(d, "morph"), 0.f);
        h.setParam(ix(d, "lpg_decay"), 127.f);
    }, 44100.0, 440.0, 2.0);
    pitchTest<KlangHost>("klang", [](KlangHost& h, const VoiceDef& d) {
        h.setParam(ix(d, "harmonics"), 0.f);
        h.setParam(ix(d, "timbre"), 0.f);
        h.setParam(ix(d, "morph"), 0.f);
        h.setParam(ix(d, "lpg_decay"), 127.f);
    }, 96000.0, 440.0, 2.0);
    pitchTest<KlongHost>("klong", [](KlongHost& h, const VoiceDef& d) {
        h.setParam(ix(d, "model"), 2.f); // inharmonic string, structure 0 = harmonic
        h.setParam(ix(d, "structure"), 0.f);
        h.setParam(ix(d, "damping"), 120.f);
    }, 44100.0, 440.0, 10.0); // Rings' string model itself plays A4 at ~442 Hz (bit-exact above)
    klongStrumTest();
    klongFxTest();
    klangKeyTrackTest();

    if (gFailures || ok != n) {
        std::printf("mutable_test: %d failure(s)\n", gFailures + (n - ok));
        return 1;
    }
    std::printf("mutable_test: OK\n");
    return 0;
}
