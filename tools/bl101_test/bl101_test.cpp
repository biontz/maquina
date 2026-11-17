// bl101_test - Maquina BL-101's engine port (s1vave/bl101/engine/Bl101Dsp.cpp) against the
// S1Vave firmware (tools/bl101_ref), sample for sample.
//
//   bl101_test REFDIR SCENARIO...   run each scenario (tools/bl101_ref/scenario.h)
//                                   through the port, compare with
//                                   REFDIR/<name>.raw (bl101_ref's output)
//   bl101_test --mutate REFDIR S    the same with one sample changed: must fail
//   bl101_test --blocks REFDIR S    the synth in uneven host-sized calls
//                                   (multiples of 16 frames), same result
//   bl101_test --host               Bl101Host (the plugins' engine host):
//                                   defaults = the firmware's, the DRAW / CHOP
//                                   parameter mapping, its output = the
//                                   engine's (44.1 kHz), MIDI controllers,
//                                   no underruns at other rates / block sizes
//
// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <cmath>

#include "Bl101Dsp.hpp"
#include "Bl101Host.hpp"
#include "FactoryPresets.generated.hpp"
#include "Presets.hpp"
#include "VoiceTables.generated.hpp"
#include "scenario.h"

namespace {

bool gMutate = false, gBlocks = false;

void apply(const sc_ev& e, bool fx, bl101::Synth& syn, bl101::Fx& efx)
{
    const char* c = e.cmd;
    auto clampByte = [](int32_t v) { return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v); };
    if (!std::strcmp(c, "p")) {
        if (fx) efx.setByte(uint32_t(e.a), clampByte(e.b));
        else syn.setByte(uint32_t(e.a), clampByte(e.b));
    } else if (!std::strcmp(c, "on")) syn.noteOn(uint8_t(e.a & 127), uint8_t(e.b & 127));
    else if (!std::strcmp(c, "off")) syn.noteOff(uint8_t(e.a & 127));
    else if (!std::strcmp(c, "bend")) syn.setBend(e.a);
    else if (!std::strcmp(c, "wheel")) syn.setModWheel(uint8_t(e.a & 127));
    else if (!std::strcmp(c, "sus")) syn.setSustain(e.a != 0);
    else if (!std::strcmp(c, "draw")) syn.setDrawStep(uint32_t(e.a), int16_t(e.b));
    else if (!std::strcmp(c, "chop")) syn.setChopMask(uint32_t(e.a), uint16_t(e.b));
    else if (!std::strcmp(c, "tempo")) { syn.setTempo(e.a); efx.setTempo(e.a); }
    else if (!std::strcmp(c, "panic")) syn.panic();
    else if (!std::strcmp(c, "factory")) {
        uint8_t bytes[bl101::kNumBytes];
        int16_t draw[16];
        uint16_t chop[4];
        bl101::factorySound(uint32_t(e.a) % bl101::numFactorySounds(), bytes, draw, chop);
        for (uint32_t i = 0; i < bl101::numFirmwareParams(); ++i) syn.setByte(i, bytes[i]);
        for (uint32_t i = 0; i < 16; ++i) syn.setDrawStep(i, draw[i]);
        for (uint32_t i = 0; i < 4; ++i) syn.setChopMask(i, chop[i]);
    } else {
        std::fprintf(stderr, "bl101_test: unknown command %s\n", c);
        std::exit(2);
    }
}

std::vector<int32_t> runPort(const scenario& sc)
{
    bl101::Synth syn;
    bl101::Fx efx;
    std::vector<int32_t> out(2u * sc.length);
    int k = 0;
    // --blocks: the synth in calls of 16..256 frames that still start on the
    // scenario's event blocks (events land between calls, as in the host)
    uint32_t seed = 12345;
    for (uint32_t f = 0; f < sc.length;) {
        while (k < sc.n && sc.ev[k].frame < f + sc.block) apply(sc.ev[k++], sc.fx != 0, syn, efx);
        uint32_t n = sc.block;
        if (gBlocks && !sc.fx) {
            seed = seed * 1664525u + 1013904223u;
            n = 16u * (1u + (seed >> 16) % 16u);
            if (k < sc.n) n = std::min(n, sc.ev[k].frame / 16u * 16u - f); // > f: applied above otherwise
            n = std::min(n, sc.length - f);
        }
        int32_t* buf = out.data() + 2u * f;
        if (sc.fx) {
            for (uint32_t i = 0; i < n; ++i) sc_input(f + i, &buf[2u * i], &buf[2u * i + 1u]);
            efx.process(buf, n);
        } else {
            syn.process(buf, n);
        }
        f += n;
    }
    return out;
}

int compare(const char* refDir, const char* script)
{
    static scenario sc;
    if (sc_load(script, &sc)) return 2;
    std::string name = script;
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0) name.resize(name.size() - 4);
    const std::string refPath = std::string(refDir) + "/" + name + ".raw";
    FILE* f = std::fopen(refPath.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "%s: cannot open\n", refPath.c_str()); return 2; }
    std::vector<int32_t> ref(2u * sc.length);
    const size_t got = std::fread(ref.data(), sizeof(int32_t), ref.size(), f);
    std::fclose(f);
    if (got != ref.size()) { std::fprintf(stderr, "%s: %zu of %zu samples\n", refPath.c_str(), got, ref.size()); return 1; }

    std::vector<int32_t> out = runPort(sc);
    if (gMutate && out.size() > 100) out[out.size() / 2] += 1;
    int64_t peak = 0;
    for (size_t i = 0; i < ref.size(); ++i) peak = std::max<int64_t>(peak, std::abs(int64_t(ref[i])));
    for (size_t i = 0; i < ref.size(); ++i)
        if (out[i] != ref[i]) {
            std::printf("%-28s MISMATCH at frame %zu (%s): port %d, firmware %d\n", name.c_str(), i / 2,
                        i % 2 ? "R" : "L", out[i], ref[i]);
            return 1;
        }
    std::printf("%-28s bit-exact, %u frames, peak %lld%s\n", name.c_str(), sc.length, (long long)peak,
                peak == 0 ? "  (SILENT)" : "");
    return peak == 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// --host

int gHostFails = 0;
void check(bool ok, const char* what)
{
    std::printf("  %-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++gHostFails;
}

const maquina::VoiceDef& voiceOf(uint8_t voice)
{
    for (const auto& d : maquina::k_voices)
        if (d.voice == voice) return d;
    std::fprintf(stderr, "no voice %u\n", voice);
    std::exit(2);
}

int indexOf(const maquina::VoiceDef& d, const char* symbol)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, symbol)) return int(i);
    std::fprintf(stderr, "no parameter %s\n", symbol);
    std::exit(2);
}

double rms(const std::vector<float>& v, size_t from = 0)
{
    double e = 0;
    for (size_t i = from; i < v.size(); ++i) e += double(v[i]) * v[i];
    return v.size() > from ? std::sqrt(e / double(v.size() - from)) : 0.0;
}

// render through the host in random block sizes; notes as (frame, note, vel; vel 0 = off)
struct HostNote { uint32_t frame; uint8_t note, vel; };
std::vector<float> renderHost(maquina::Bl101Host& h, double rate, uint32_t frames, const std::vector<HostNote>& notes,
                              bool randomBlocks, int32_t bendAt = -1)
{
    h.setSampleRate(rate);
    std::vector<float> l(frames), r(frames);
    uint32_t seed = 777, k = 0;
    for (uint32_t f = 0; f < frames;) {
        uint32_t n = randomBlocks ? 1u + ((seed = seed * 1664525u + 1013904223u) >> 16) % 700u : 256u;
        n = std::min(n, frames - f);
        for (; k < notes.size() && notes[k].frame < f + n; ++k) {
            if (notes[k].vel) h.noteOn(notes[k].frame - f, notes[k].note, notes[k].vel);
            else h.noteOff(notes[k].frame - f, notes[k].note);
        }
        if (bendAt >= 0 && uint32_t(bendAt) >= f && uint32_t(bendAt) < f + n) h.pitchBend(uint32_t(bendAt) - f, 8191);
        h.process(l.data() + f, r.data() + f, n);
        f += n;
    }
    return l;
}

int hostChecks()
{
    using maquina::Bl101Host;
    const maquina::VoiceDef& syn = voiceOf(maquina::kBl101Voice);
    const maquina::VoiceDef& fxd = voiceOf(maquina::kBl101FxVoice);
    std::printf("Bl101Host:\n");

    // defaults: the firmware's PDEF (BL-101 FX's reverb level is ours: 40)
    bool defOk = true;
    for (const maquina::VoiceDef* d : { &syn, &fxd })
        for (uint32_t i = 0; i < d->numParams; ++i) {
            const maquina::VoiceParam& p = d->params[i];
            if (p.fwPar >= Bl101Host::kDrawBase) continue;
            const int byte = p.kind == maquina::ParamKind::Int ? p.def - p.offset : p.def;
            const bool ours = d == &fxd && !std::strcmp(p.symbol, "rev_level");
            if (!ours && byte != bl101::paramDefault(p.fwPar)) {
                std::printf("    %s: default byte %d, firmware %u\n", p.symbol, byte, bl101::paramDefault(p.fwPar));
                defOk = false;
            }
            if (p.fwMax != bl101::paramMax(p.fwPar) && !(p.kind == maquina::ParamKind::Toggle)) {
                std::printf("    %s: max %u, firmware %u\n", p.symbol, p.fwMax, bl101::paramMax(p.fwPar));
                defOk = false;
            }
        }
    check(defOk, "parameter defaults and ranges = the firmware's PDEF");

    // the DRAW defaults = the INIT pattern's table (its wrapped last step too)
    {
        int16_t init[16];
        bl101::initDrawTable(init);
        bool ok = true;
        for (int k = 0; k < 16; ++k) {
            char sym[16];
            std::snprintf(sym, sizeof sym, "draw_%d", k + 1);
            ok &= syn.params[indexOf(syn, sym)].def * 256 == init[k];
        }
        check(ok, "draw_1..16 defaults = the firmware's INIT table");
    }

    // CHOP mask selector / Custom bits
    {
        Bl101Host h(syn, 1);
        const int sel = indexOf(syn, "chop_mask_saw"), lo = indexOf(syn, "chop_bits_saw_lo"),
                  hi = indexOf(syn, "chop_bits_saw_hi");
        bool ok = h.chopMask(1) == 0xFFFF;
        for (int k = 1; k <= 6; ++k) {
            h.setParam(uint32_t(sel), float(k));
            ok &= h.chopMask(1) == bl101::kChopMaskPresets[k - 1];
        }
        h.setParam(uint32_t(sel), 0.f);
        h.setParam(uint32_t(lo), 0x34);
        h.setParam(uint32_t(hi), 0xE7);
        ok &= h.chopMask(1) == 0xE734 && h.chopMask(0) == 0xFFFF;
        check(ok, "chop mask: the 6 firmware masks, Custom = hi:lo bytes");
    }

    // the host at 44.1 kHz = the engine, delayed by the latency
    {
        Bl101Host h(syn, 1);
        const uint32_t frames = 44096; // whole 16-frame blocks
        std::vector<HostNote> notes = { { 0, 48, 100 }, { 4410, 55, 90 }, { 8000, 48, 0 }, { 20000, 55, 0 },
                                        { 22050, 60, 127 }, { 30000, 60, 0 } };
        std::vector<float> got = renderHost(h, 44100.0, frames, notes, true);
        bl101::Synth ref;
        std::vector<int32_t> buf(2u * frames);
        size_t k = 0;
        for (uint32_t f = 0; f < frames; f += 16) {
            for (; k < notes.size() && notes[k].frame < f + 16; ++k) {
                // the host: note-ons in their block, note-offs one block later
                if (notes[k].vel) ref.noteOn(notes[k].note, notes[k].vel);
            }
            for (const HostNote& n : notes)
                if (!n.vel && n.frame < f && n.frame + 16 >= f) ref.noteOff(n.note);
            ref.process(buf.data() + 2u * f, 16);
        }
        const uint32_t lat = h.latencyFrames();
        bool ok = h.underruns() == 0;
        double maxErr = 0;
        for (uint32_t i = 0; i + lat < frames; ++i)
            maxErr = std::max(maxErr, std::fabs(double(got[i + lat]) - double(buf[2u * i]) / 32768.0));
        for (uint32_t i = 0; i < lat; ++i) ok &= got[i] == 0.f;
        ok &= maxErr == 0.0 && rms(got) > 0.01;
        char what[96];
        std::snprintf(what, sizeof what, "host output = engine output at 44.1 kHz (latency %u, err %.2g)", lat,
                      maxErr);
        check(ok, what);
    }

    // other rates, random host blocks: no underruns, sound, release to silence
    for (double rate : { 48000.0, 96000.0, 22050.0 }) {
        Bl101Host h(syn, 1);
        const uint32_t frames = uint32_t(rate * 3.0);
        std::vector<HostNote> notes = { { 100, 52, 100 }, { uint32_t(rate * 0.5), 52, 0 } };
        std::vector<float> got = renderHost(h, rate, frames, notes, true);
        const double head = rms(std::vector<float>(got.begin(), got.begin() + uint32_t(rate * 0.4)));
        const double tail = rms(got, uint32_t(rate * 2.5));
        char what[96];
        std::snprintf(what, sizeof what, "%.0f Hz: no underruns, sound %.3f, silent after release %.1e", rate, head,
                      tail);
        check(h.underruns() == 0 && head > 0.01 && tail < 1e-4, what);
    }

    // pitch bend reaches the engine (Bend VCO 2 semitones: the pitch moves)
    {
        Bl101Host a(syn, 1), b(syn, 1);
        std::vector<HostNote> notes = { { 0, 57, 100 } };
        std::vector<float> x = renderHost(a, 44100.0, 22050, notes, false);
        std::vector<float> y = renderHost(b, 44100.0, 22050, notes, false, 4410);
        bool same = true, differs = false;
        const uint32_t lat = a.latencyFrames();
        for (uint32_t i = 0; i < 22050; ++i) {
            if (i < 4400 + lat) same &= x[i] == y[i]; // (the 16-frame block holding frame 4410)
            else differs |= x[i] != y[i];
        }
        check(same && differs, "pitch bend: changes the sound from its frame on");
    }

    // the factory presets (tools/s1vave_presets.py) through the host = the
    // firmware's 64 factory sounds (the synth part), each one once (sounds
    // 18 and 30 are the same patch)
    {
        const maquina::VoicePresets* vp = nullptr;
        for (uint32_t k = 0; k < sizeof(maquina::k_voices) / sizeof(maquina::k_voices[0]); ++k)
            if (maquina::k_voices[k].voice == maquina::kBl101Voice) vp = &maquina::k_factory_presets[k];
        const uint32_t nSounds = bl101::numFactorySounds(), frames = 11024; // whole 16-frame blocks
        std::vector<std::vector<float>> refs(nSounds);
        for (uint32_t n = 0; n < nSounds; ++n) {
            bl101::Synth ref;
            uint8_t bytes[bl101::kNumBytes];
            int16_t draw[16];
            uint16_t chop[4];
            bl101::factorySound(n, bytes, draw, chop);
            for (uint32_t i = 0; i < bl101::numFirmwareParams(); ++i) ref.setByte(i, bytes[i]);
            for (uint32_t i = 0; i < 16; ++i) ref.setDrawStep(i, draw[i]);
            for (uint32_t i = 0; i < 4; ++i) ref.setChopMask(i, chop[i]);
            std::vector<int32_t> buf(2u * frames);
            for (uint32_t f = 0; f < frames; f += 16) {
                if (f == 0) ref.noteOn(57, 110);
                if (f == 6016) ref.noteOff(57); // the host: a note-off waits for the next block
                ref.process(buf.data() + 2u * f, 16);
            }
            refs[n].resize(frames);
            for (uint32_t i = 0; i < frames; ++i) refs[n][i] = float(buf[2u * i]) / 32768.f;
        }
        bool ok = vp && vp->count == nSounds;
        std::vector<int> used(nSounds, 0);
        for (uint32_t p = 0; ok && p < vp->count; ++p) {
            Bl101Host h(syn, 1);
            std::vector<float> v(maquina::numAllParams(syn), 0.f); // (+ gain: applyPreset covers it)
            for (uint32_t i = 0; i < syn.numParams; ++i) v[i] = h.getParam(i);
            maquina::applyPreset(syn, vp->presets[p].values, vp->presets[p].numValues, v.data());
            for (uint32_t i = 0; i < syn.numParams; ++i) h.setParam(i, v[i]);
            std::vector<float> got = renderHost(h, 44100.0, frames, { { 0, 57, 110 }, { 6000, 57, 0 } }, false);
            const uint32_t lat = h.latencyFrames();
            int match = -1;
            for (uint32_t n = 0; n < nSounds && match < 0; ++n) {
                if (used[n]) continue;
                bool same = true;
                for (uint32_t i = 0; same && i + lat < frames; ++i) same = got[i + lat] == refs[n][i];
                if (same) match = int(n);
            }
            if (match < 0) {
                std::printf("    preset \"%s\" plays none of the factory sounds\n", vp->presets[p].name);
                ok = false;
            } else {
                used[size_t(match)] = 1;
            }
        }
        check(ok, "factory presets = the firmware's 64 sounds (synth part), each once");
    }

    // BL-101 FX: passes the input, underrun-free at 48 kHz, reverb tail
    {
        Bl101Host h(fxd, 1);
        h.setSampleRate(48000.0);
        const uint32_t frames = 96000;
        std::vector<float> inL(frames, 0.f), inR(frames, 0.f), l(frames), r(frames);
        for (uint32_t i = 0; i < 4800; ++i) inL[i] = inR[i] = 0.3f * float(std::sin(i * 0.05));
        uint32_t seed = 99;
        for (uint32_t f = 0; f < frames;) {
            uint32_t n = std::min<uint32_t>(1u + ((seed = seed * 1664525u + 1013904223u) >> 16) % 500u, frames - f);
            h.process(inL.data() + f, inR.data() + f, l.data() + f, r.data() + f, n);
            f += n;
        }
        const double tail = rms(std::vector<float>(l.begin() + 9600, l.begin() + 30000));
        char what[96];
        std::snprintf(what, sizeof what, "BL-101 FX at 48 kHz: no underruns, reverb tail %.4f", tail);
        check(h.underruns() == 0 && rms(l) > 0.01 && tail > 1e-3, what);
    }

    std::printf(gHostFails ? "bl101_test --host: %d FAILED\n" : "bl101_test --host: all ok\n", gHostFails);
    return gHostFails ? 1 : 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--host")) return hostChecks();
    int a = 1;
    for (; a < argc && argv[a][0] == '-'; ++a) {
        if (!std::strcmp(argv[a], "--mutate")) gMutate = true;
        else if (!std::strcmp(argv[a], "--blocks")) gBlocks = true;
        else break;
    }
    if (argc - a < 2) {
        std::fprintf(stderr, "usage: bl101_test [--mutate] [--blocks] REFDIR SCENARIO...\n");
        return 2;
    }
    int fails = 0;
    for (int i = a + 1; i < argc; ++i) fails += compare(argv[a], argv[i]) != 0;
    if (fails) std::printf("bl101_test: %d scenario(s) FAILED\n", fails);
    return fails ? 1 : 0;
}
