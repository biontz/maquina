// SPDX-License-Identifier: GPL-3.0-only
// Maquina Klang: KlangHost with the Plaits headers (third_party/eurorack,
// MIT), kept apart from the DPF code. The Plaits code itself is libklang.a,
// built with TEST defined (mutable-instruments/eurorack.mk); so is this.
#define TEST 1
#include <cstdio> // plaits/user_data.h's TEST mock uses printf
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wformat"
#endif
#include "plaits/dsp/voice.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "KlangHost.cpp"
