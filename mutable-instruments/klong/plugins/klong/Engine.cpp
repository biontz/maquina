// SPDX-License-Identifier: GPL-3.0-only
// Maquina Klong / Klong FX: KlongHost with the Rings headers
// (third_party/eurorack, MIT), kept apart from the DPF code. The Rings code
// itself is libklong.a, built with TEST defined (mutable-instruments/eurorack.mk);
// so is this.
#define TEST 1
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "KlongHost.cpp"
