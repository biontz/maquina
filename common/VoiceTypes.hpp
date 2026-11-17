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

// VoiceDef::voice of the non-LXR voices
constexpr uint8_t kDredrumVoice = 16;  // Maquina Dredrum (euro-modules/dredrum/engine/)
constexpr uint8_t kPlethoraVoice = 17; // Maquina Plethora (befaco/plethora/engine/)
constexpr uint8_t kOneiroiVoice = 18;   // Maquina Oneiroi (befaco/oneiroi/engine/)
constexpr uint8_t kOneiroiFxVoice = 19; // Maquina Oneiroi FX (befaco/oneiroi/engine/, no oscillators)
constexpr uint8_t kKlangVoice = 20;     // Maquina Klang (mutable-instruments/klang/engine/, Plaits)
constexpr uint8_t kKlongVoice = 21;     // Maquina Klong (mutable-instruments/klong/engine/, Rings)
constexpr uint8_t kKlongFxVoice = 22;   // Maquina Klong FX (the same, excited by the audio input)
constexpr uint8_t kAcidaVoice = 23;     // Maquina Acida (openrb/acida/engine/, OpenRB-338's 303)
constexpr uint8_t kAcidaFxVoice = 24;   // Maquina Acida FX (OpenRB-338's distortion, PCF, compressor, delay)
// the LXR-02 voices (EngineHost); everything else is a Maquina port of another device
inline bool isLxrVoice(uint8_t voice) { return voice < kDredrumVoice; }
inline bool isOneiroi(uint8_t voice) { return voice == kOneiroiVoice || voice == kOneiroiFxVoice; }
inline bool isKlong(uint8_t voice) { return voice == kKlongVoice || voice == kKlongFxVoice; }
// the Mutable Instruments ports (Maquina Klang, Klong, Klong FX)
inline bool isMutable(uint8_t voice) { return voice == kKlangVoice || isKlong(voice); }
inline bool isAcida(uint8_t voice) { return voice == kAcidaVoice || voice == kAcidaFxVoice; }
// voices built from 3 channels/oscillators with their own random ROLL/RND
// switches; no extra (LXR) parameters
inline bool hasChannels(uint8_t voice) { return voice == kDredrumVoice || voice == kPlethoraVoice; }

struct VoiceDef {
    const char* id;
    const char* name;
    uint8_t voice;        // engine voice slot (LxrVoice), or kDredrumVoice ... kAcidaFxVoice
    const VoiceParam* params;
    uint32_t numParams;
};

} // namespace maquina
