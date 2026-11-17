// La50Engine.hpp - Maquina LA-50's sound engine: LA synthesis playing D-50
// patches (a PatchData of 448 bytes: D50Sysex.hpp, La50Layout.generated.hpp).
//
// Not a D-50 emulation verified against the instrument (there is no open
// reference to compare with): the parts are
//   - the partials' wave generators: Munt's model of the LA32 chip (the MT-32's
//     LA synthesis, the D-50's sibling), vendored unmodified in
//     third_party/munt except one marked setter: square / saw synth waves with
//     pulse width, cutoff and resonance, PCM playback, and the partial pairs'
//     mixing / ring modulation (LA32IntPartialPair);
//   - TVA and TVF envelopes and their key / velocity / bias scaling adapted
//     from Munt's TVA.cpp / TVF.cpp (LA32Ramp ramps), which use the same 0-100
//     time / level scales, extended with the D-50's end levels;
//   - the rest written from the D-50 owner's manual: the 7 structures, the
//     tone's pitch envelope and 3 LFOs, pitch / PW / TVF / TVA modulation, key
//     modes (Whole, Dual, Split, Separate and their solo variants), tone
//     balance, portamento, hold, bender and aftertouch.
// Runs at 32 kHz (the LA32's rate); 16 voices of 2 partials each (a Dual or
// Split note of both tones takes 2). The tone outputs go to La50Fx (EQ,
// chorus, reverb) in La50Host.
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <memory>

#include "D50Sysex.hpp"
#include "La50Pcm.hpp"

namespace la50 {

constexpr int kEngineRate = 32000;
constexpr int kVoices = 16;

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // the patch (read every control tick; changes take effect then, the
    // per-note values at the next note)
    PatchData& patch() { return fPatch; }
    const PatchData& patch() const { return fPatch; }
    void setPcm(const PcmBank* bank) { fPcm = bank ? bank : &PcmBank::standIns(); }

    // MIDI: channel 0..15 (Separate key modes: channel 2 plays the Lower tone)
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void pitchBend(int bend);      // -8192..8191
    void modWheel(int value);      // CC 1 (the bender lever's modulation)
    void aftertouch(int value);    // channel pressure
    void sustain(bool on);         // CC 64 (Hold Mode: which tones)
    void portamento(bool on);      // CC 65 (Portamento Mode: which tones)
    void allNotesOff();
    void silence();

    // n frames at 32 kHz: the Upper and Lower tones' outputs (mono, about +-1)
    void process(float* upper, float* lower, int frames);

    int activeVoices() const;

    struct Voice; // La50Engine.cpp

private:
    void startNote(int tone, int key, int velocity, int channel);
    void releaseNote(int tone, int key);
    void releaseVoice(Voice& v);
    Voice* allocate();
    void tick();
    void updateVoice(Voice& v, bool advance);

    PatchData fPatch {};
    const PcmBank* fPcm;
    std::unique_ptr<Voice[]> fVoices;
    uint32_t fAge = 0;
    int fTickPos = 0;
    int fBend = 0, fMod = 0, fAfter = 0;
    bool fSustain = false, fPorta = false;
    int fLastKey[2] = { -1, -1 };      // portamento: the previous key per tone
    int fHeld[2][16] = {}, fNumHeld[2] = {}; // solo modes: held keys per tone
    // the tones' LFOs that run free (sync Off / On): phase per tone and LFO
    double fLfoPhase[2][3] = {};
    float fLfoHold[2][3] = {};
    uint32_t fRandom = 0x1234567u;
};

} // namespace la50
