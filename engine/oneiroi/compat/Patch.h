// Patch.h - Maquina stand-in for OwlProgram's LibSource/Patch.h.
//
// Oneiroi's DSP headers include "Patch.h" for the OWL basics (FloatArray,
// AudioBuffer, the maths helpers and the channel ids). The real Patch.h also
// pulls in the OWL patch runtime (parameters, resources, MIDI, the program
// vector), which only the module's UI layer (Ui.h, ParamController.h, Midi.h,
// Led.h) uses; Maquina replaces that layer (plugins/common/OneiroiHost.cpp).
//
// SPDX-License-Identifier: GPL-3.0-only
#ifndef __Patch_h__
#define __Patch_h__

#include "basicmaths.h"
#include "FloatArray.h"
#include "AudioBuffer.h"

enum PatchChannelId {
  LEFT_CHANNEL = 0,
  RIGHT_CHANNEL = 1
};

#endif // __Patch_h__
