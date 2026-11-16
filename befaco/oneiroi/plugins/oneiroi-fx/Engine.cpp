// SPDX-License-Identifier: GPL-3.0-only
// Maquina Oneiroi: the vendored Oneiroi engine (third_party/oneiroi,
// GPL-3.0-or-later) with the OwlProgram library parts it uses
// (third_party/owlprogram, GPL-3.0) and its host, kept apart from the DPF code.
// The vendored code is kept unmodified; its warnings are not ours to fix.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wreorder"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wregister"
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wdeprecated-register"
#endif
#include "oneiroi_unity.cpp"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "OneiroiHost.cpp"
