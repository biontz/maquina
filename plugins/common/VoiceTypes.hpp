// VoiceTypes.hpp - parameter description types shared by the generated tables.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include "Parameters.h" // firmware ParamEnums (PAR_*)

namespace maquina {

enum class ParamKind : uint8_t {
    Int,       // firmware value fwMin..fwMax, shown as value + offset
    Enum,      // firmware value = label index
    Toggle,    // 0/1
    Retrigger, // Off/On -> firmware value 0 / own voice number + 1
    ModTarget, // label index -> modTargets[] index (targets[])
    Note,      // 0 = any note, else that note only
};

struct VoiceParam {
    const char* symbol;   // LV2 symbol: API, never rename
    const char* name;
    uint8_t page;         // firmware menu sub-page 0..7
    uint16_t fwPar;       // firmware ParamEnums index
    ParamKind kind;
    int16_t offset;       // display offset for Int (PM63 -> -63, coarse -> -60)
    uint8_t fwMin, fwMax; // range in plugin units before offset
    const char* const* labels;
    uint8_t numLabels;
    const uint8_t* targets; // ModTarget: modTargets[] index per label
    int16_t def;            // default in plugin units (firmware Initkit)
};

// VoiceDef::voice of Maquina Dredrum: not an LXR engine voice (engine/dredrum)
constexpr uint8_t kDredrumVoice = 16;

struct VoiceDef {
    const char* id;
    const char* name;
    uint8_t voice;        // engine voice slot (LxrVoice), or kDredrumVoice
    const VoiceParam* params;
    uint32_t numParams;
};

} // namespace maquina
