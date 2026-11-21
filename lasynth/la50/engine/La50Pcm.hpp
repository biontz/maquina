// La50Pcm.hpp - the 100 PCM waves of Maquina LA-50's PCM partials.
//
// The D-50's waves are Roland's ROM and are not shipped. LA-50 plays either
//   - a wave ROM dump the user owns (La50Pcm::loadRom, a file chosen in the
//     editor), or
//   - built-in stand-ins (no Roland data): sounds synthesised here per wave,
//     after the wave's name: 1-47 attack transients (struck / plucked / blown /
//     bowed onsets, noise transients), 48-76 single-cycle or noise loops
//     (organs, pianos, basses, strings, reeds, vowels, the "Spectrum" sets),
//     77-100 loops made of two of those.
// Waves are stored as the LA32 wave generator wants them: 16-bit log samples
// (sign bit + 15-bit log magnitude, Munt's LA32WaveGenerator::pcmSampleToLogSample),
// at 32 kHz. Each wave's natural pitch is its rootHz (the stand-in loops use
// the D-50's measured loop roots - divisions of the 32 kHz clock; attacks and
// the unpitched Noise wave are C4, rootHz = 0 meaning "C4, no offset").
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace la50 {

struct PcmWave {
    std::vector<int16_t> data; // log samples
    bool looped = false;       // the whole wave loops
    float rootHz = 0.f;        // natural pitch; 0 = C4 (no offset)

    // the offset from the engine's C4 PCM base pitch, in 4096ths of an octave
    int pitchOffset() const;
};

class PcmBank {
public:
    static constexpr int kNumWaves = 100;

    // the built-in stand-ins (computed once, shared)
    static const PcmBank& standIns();

    // a ROM dump: returns false (and keeps nothing) if it isn't recognised
    bool loadRom(const std::string& path, std::string& error);
    bool fromRom() const { return fFromRom; }

    const PcmWave& wave(int index) const { return fWaves[size_t(index < 0 ? 0 : index > 99 ? 99 : index)]; }

    // linear -1..1 -> an LA32 log sample
    static int16_t toLog(float x);

private:
    std::vector<PcmWave> fWaves = std::vector<PcmWave>(kNumWaves);
    bool fFromRom = false;
    void makeStandIns();
};

} // namespace la50
