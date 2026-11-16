// ExtraParams.hpp - plugin parameters that are not firmware parameters.
// They follow the voice's generated table: index numParams + k.
//
//   note_pitch  LXR voices   "Key Track": Off = every note plays the set
//                            pitch, On = the played note transposes it
//                            around C4 (60)
//   gain        all voices   "Gain": output gain -24..+12 dB after the
//                            engine, smoothed; a mix control like the
//                            DAW's fader (not in presets, not randomized)
//
// LXR voices: note_pitch, gain. Maquina Dredrum, Plethora, Oneiroi and
// Oneiroi FX: gain only (their tables hold everything else).
//
// (Every note triggers: the hardware's per-voice trigger note and the HiHat's
// Closed/Open choice are not plugin parameters.)
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "VoiceTypes.hpp"

namespace maquina {

enum ExtraParamId : uint32_t { kExtraNotePitch = 0, kExtraGain = 1 };

static const char* const kExtraOffOn[] = { "Off", "On" };

// fwPar is unused for these (no firmware parameter behind them). Page 7 =
// the LXR mix page (the plugin maps it for Dredrum / Plethora; Oneiroi's
// page 7 is its MIX section too).
static const VoiceParam kExtraParams[] = {
    { "note_pitch", "Key Track", 7, 0, ParamKind::Toggle, 0, 0, 1, kExtraOffOn, 2, nullptr, 0 },
    { "gain", "Gain", 7, 0, ParamKind::Int, -24, 0, 36, nullptr, 0, nullptr, 0 }, // dB
};

inline uint32_t numExtraParams(const VoiceDef& d)
{
    return isLxrVoice(d.voice) ? 2u : 1u;
}
inline uint32_t numAllParams(const VoiceDef& d) { return d.numParams + numExtraParams(d); }
inline bool isExtraParam(const VoiceDef& d, uint32_t i) { return i >= d.numParams; }

// ExtraParamId of extra parameter k (0 .. numExtraParams(d) - 1)
inline ExtraParamId extraIdOf(const VoiceDef& d, uint32_t k)
{
    return isLxrVoice(d.voice) ? ExtraParamId(k) : kExtraGain;
}

// any parameter index, firmware table or extra
inline const VoiceParam& paramAt(const VoiceDef& d, uint32_t i)
{
    return i < d.numParams ? d.params[i] : kExtraParams[extraIdOf(d, i - d.numParams)];
}

} // namespace maquina
