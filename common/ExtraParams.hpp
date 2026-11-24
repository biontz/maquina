// ExtraParams.hpp - plugin parameters that are not firmware parameters.
// They follow the voice's generated table: index numParams + k.
//
//   note_pitch  LXR voices   "Key Track": Off = every note plays the set
//                            pitch, On = the played note transposes it
//                            around C4 (60)
//   gain        all voices   "Gain": output gain -24..+12 dB after the
//                            engine, smoothed; a mix control like the
//                            DAW's fader (not in presets, not randomized)
//   midi_pc     all voices   "MIDI PC": On = MIDI Bank Select (CC 0/32) and
//                            Program Change load presets (ProgramMap.hpp);
//                            Off = ignored, e.g. where the host handles them
//                            itself (Zynthian). Not in presets, not randomized.
//   midi_program all voices  "Program": OUTPUT, the preset last loaded by MIDI
//                            or the host's program list: -1 = none yet, else
//                            (msb * 16384 + lsb * 128 + program) + 2^21 * g,
//                            g toggling on every load (so loading the same
//                            program again is still a change). The editor uses
//                            it to show the preset's name.
//
// LXR voices: note_pitch, gain, midi_pc, midi_program. All other voices: gain,
// midi_pc, midi_program (their tables hold everything else).
//
// (Every note triggers: the hardware's per-voice trigger note and the HiHat's
// Closed/Open choice are not plugin parameters.)
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "VoiceTypes.hpp"

namespace maquina {

enum ExtraParamId : uint32_t { kExtraNotePitch = 0, kExtraGain = 1, kExtraMidiPc = 2, kExtraMidiProgram = 3 };

// kExtraMidiProgram's value
constexpr uint32_t kMidiProgramToggle = 1u << 21;
inline float encodeMidiProgram(uint32_t msb, uint32_t lsb, uint32_t program, bool toggle)
{
    return float(msb * 16384u + lsb * 128u + program + (toggle ? kMidiProgramToggle : 0u));
}
inline bool decodeMidiProgram(float v, uint32_t& msb, uint32_t& lsb, uint32_t& program)
{
    if (v < 0.f) return false;
    const uint32_t c = uint32_t(v + 0.5f) % kMidiProgramToggle;
    msb = c / 16384u;
    lsb = (c / 128u) % 128u;
    program = c % 128u;
    return true;
}

static const char* const kExtraOffOn[] = { "Off", "On" };

// fwPar is unused for these (no firmware parameter behind them). Page 7 =
// the LXR mix page (the plugin maps it for Dredrum / Plethora; Oneiroi's
// page 7 is its MIX section too).
static const VoiceParam kExtraParams[] = {
    { "note_pitch", "Key Track", 7, 0, ParamKind::Toggle, 0, 0, 1, kExtraOffOn, 2, nullptr, 0 },
    { "gain", "Gain", 7, 0, ParamKind::Int, -24, 0, 36, nullptr, 0, nullptr, 0 }, // dB
    { "midi_pc", "MIDI PC", 7, 0, ParamKind::Toggle, 0, 0, 1, kExtraOffOn, 2, nullptr, 1 }, // default On
    { "midi_program", "Program", 7, 0, ParamKind::Int, -1, 0, 0, nullptr, 0, nullptr, -1 }, // output; range set by the plugin
};

inline uint32_t numExtraParams(const VoiceDef& d)
{
    return isLxrVoice(d.voice) ? 4u : 3u;
}
inline uint32_t numAllParams(const VoiceDef& d) { return d.numParams + numExtraParams(d); }
inline bool isExtraParam(const VoiceDef& d, uint32_t i) { return i >= d.numParams; }

// ExtraParamId of extra parameter k (0 .. numExtraParams(d) - 1)
inline ExtraParamId extraIdOf(const VoiceDef& d, uint32_t k)
{
    return isLxrVoice(d.voice) ? ExtraParamId(k) : ExtraParamId(k + 1);
}

// any parameter index, firmware table or extra
inline const VoiceParam& paramAt(const VoiceDef& d, uint32_t i)
{
    return i < d.numParams ? d.params[i] : kExtraParams[extraIdOf(d, i - d.numParams)];
}

} // namespace maquina
