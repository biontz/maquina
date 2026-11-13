// ExtraParams.hpp - plugin parameters that are not firmware parameters.
// They follow the voice's generated table: index numParams + k.
//
//   note_pitch  all voices   "Key Track" (transpose by the played note)
//   hat_mode    HiHat only   Closed / Open: which hat this instance plays
//
// Maquina Dredrum has none (its table holds everything, see gen_params.py).
//
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "VoiceTypes.hpp"

namespace maquina {

enum ExtraParamId : uint32_t { kExtraNotePitch = 0, kExtraHatMode = 1 };

static const char* const kExtraOffOn[] = { "Off", "On" };
static const char* const kExtraHatModes[] = { "Closed", "Open" };

// fwPar is unused for these (no firmware parameter behind them).
static const VoiceParam kExtraParams[] = {
    { "note_pitch", "Key Track", 7, 0, ParamKind::Toggle, 0, 0, 1, kExtraOffOn, 2, nullptr, 0 },
    { "hat_mode", "Hat", 7, 0, ParamKind::Enum, 0, 0, 1, kExtraHatModes, 2, nullptr, 0 },
};

constexpr uint8_t kHiHatVoice = 5; // LXR_VOICE_HIHAT

inline uint32_t numExtraParams(const VoiceDef& d)
{
    return d.voice == kDredrumVoice ? 0u : d.voice == kHiHatVoice ? 2u : 1u;
}
inline uint32_t numAllParams(const VoiceDef& d) { return d.numParams + numExtraParams(d); }
inline bool isExtraParam(const VoiceDef& d, uint32_t i) { return i >= d.numParams; }

// any parameter index, firmware table or extra
inline const VoiceParam& paramAt(const VoiceDef& d, uint32_t i)
{
    return i < d.numParams ? d.params[i] : kExtraParams[i - d.numParams];
}

} // namespace maquina
