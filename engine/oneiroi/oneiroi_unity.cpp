// oneiroi_unity.cpp - the vendored Oneiroi engine (third_party/oneiroi) and
// the OwlProgram library parts it uses (third_party/owlprogram), in one
// translation unit. Include paths: engine/oneiroi/compat (first: Patch.h,
// heap.h stand-ins), third_party/oneiroi, third_party/owlprogram/LibSource.
//
// Oneiroi's headers define some non-inline functions (Commons.h), so they may
// be included by one translation unit only: this one (plus what includes it).
// SPDX-License-Identifier: GPL-3.0-only
#include "owl_runtime.cpp"

// OwlProgram library sources, unmodified. basicmaths.c's random generator
// (one global seed) is renamed away: owl_runtime.cpp provides it per instance.
#define arm_srand32 owlprogram_global_arm_srand32
#define arm_rand32 owlprogram_global_arm_rand32
#define randf owlprogram_global_randf
#include "basicmaths.c"
#undef arm_srand32
#undef arm_rand32
#undef randf
#include "fastpow.c"
// fastlog.c uses 'register', which C++17 removed (clang: an error by default)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wregister"
#endif
#include "fastlog.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "FloatArray.cpp"
#include "SimpleValue.cpp"

// the engine: Oneiroi.h with Maquina's hooks, and the module's sync clock
#include "OneiroiCore.h"
#include "Clock.h"
