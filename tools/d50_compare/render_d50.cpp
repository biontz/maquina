// render_d50 - offline LA-50 renderer for the D-50 A/B comparison.
//
//   render_d50 BANK.syx PATCH_INDEX NOTES.txt OUT.wav
//
// NOTES.txt: one note per line: "time_sec midi dur_sec [velocity]".
// Renders the patch from the bank at 32 kHz, mono, upper+lower tones summed.
//
// SPDX-License-Identifier: GPL-3.0-only
#include "../../lasynth/la50/engine/D50Sysex.hpp"
#include "../../lasynth/la50/engine/La50Engine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

using namespace la50;

int main(int argc, char** argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: render_d50 BANK.syx PATCH_INDEX NOTES.txt OUT.wav\n");
        return 1;
    }
    // read the bank
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("bank"); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(sz);
    if (fread(buf.data(), 1, sz, f) != (size_t)sz) return 1;
    fclose(f);
    SysexResult r = parseSysex(buf.data(), buf.size());
    int idx = atoi(argv[2]);
    if (idx < 0 || idx >= (int)r.patches.size()) {
        fprintf(stderr, "patch index %d out of range (%zu patches)\n", idx, r.patches.size());
        return 1;
    }
    // read the notes
    struct Ev { double t; int midi; double dur; int vel; };
    std::vector<Ev> evs;
    FILE* nf = fopen(argv[3], "r");
    if (!nf) { perror("notes"); return 1; }
    double t; int midi; double dur; int vel;
    while (fscanf(nf, "%lf %d %lf %d", &t, &midi, &dur, &vel) >= 3)
        evs.push_back({t, midi, dur, vel > 0 ? vel : 100});
    fclose(nf);
    double total = 0;
    for (const Ev& e : evs) total = std::max(total, e.t + e.dur);
    if (total < 1.0) total = 1.0;
    total += 1.0; // tail

    Engine engine;
    engine.patch() = r.patches[idx].data;
    const int frames = int(total * kEngineRate);
    std::vector<float> up(frames), lo(frames);
    struct Fire { int frame; bool on; int midi; int vel; };
    std::vector<Fire> fires;
    for (const Ev& e : evs) {
        fires.push_back({int(e.t * kEngineRate), true, e.midi, e.vel});
        fires.push_back({int((e.t + e.dur) * kEngineRate), false, e.midi, 0});
    }
    std::sort(fires.begin(), fires.end(), [](const Fire& a, const Fire& b) { return a.frame < b.frame; });
    size_t fi = 0;
    const int block = 256;
    for (int pos = 0; pos < frames; pos += block) {
        while (fi < fires.size() && fires[fi].frame < pos + block) {
            if (fires[fi].on) engine.noteOn(0, fires[fi].midi, fires[fi].vel);
            else engine.noteOff(0, fires[fi].midi);
            ++fi;
        }
        int n = std::min(block, frames - pos);
        engine.process(&up[pos], &lo[pos], n);
    }
    // write mono 16-bit WAV (sum of tones)
    FILE* o = fopen(argv[4], "wb");
    if (!o) { perror("out"); return 1; }
    uint32_t dataBytes = uint32_t(frames * 2);
    uint32_t byteRate = 2 * kEngineRate;
    uint16_t fmtTag = 1, ch = 1, bits = 16, blockAlign = 2;
    uint32_t fmtLen = 16;
    uint32_t sampleRate = kEngineRate;
    uint32_t riff = 36 + dataBytes;
    uint8_t hdr[44];
    memcpy(hdr, "RIFF", 4);      memcpy(hdr + 4, &riff, 4);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);  memcpy(hdr + 16, &fmtLen, 4);
    memcpy(hdr + 20, &fmtTag, 2); memcpy(hdr + 22, &ch, 2);
    memcpy(hdr + 24, &sampleRate, 4); memcpy(hdr + 28, &byteRate, 4);
    memcpy(hdr + 32, &blockAlign, 2); memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);  memcpy(hdr + 40, &dataBytes, 4);
    fwrite(hdr, 1, 44, o);
    for (int i = 0; i < frames; ++i) {
        float s = up[i] + lo[i];
        int16_t pcm = int16_t(std::clamp(s * 0.5f, -1.f, 1.f) * 32767.f);
        fwrite(&pcm, 2, 1, o);
    }
    fclose(o);
    printf("%s: patch %d (%s), %d notes, %.2f s\n", argv[4], idx, r.patches[idx].name.c_str(),
           (int)evs.size(), total);
    return 0;
}