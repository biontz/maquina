// StringsHost.cpp - Maquina Strings, the editor-first skeleton: parameters + held notes,
// silent output (StringsHost.hpp).
// SPDX-License-Identifier: GPL-3.0-only
#include "StringsHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace maquina {

StringsHost::StringsHost(const VoiceDef& def, uint32_t /*seed*/)
    : fDef(def), fDefaults(def.numParams, 0.f), fValues(def.numParams)
{
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
}

void StringsHost::setParam(uint32_t index, float value)
{
    if (index >= fDef.numParams) return;
    const VoiceParam& p = fDef.params[index];
    float lo = float(p.fwMin + p.offset), hi = float(p.fwMax + p.offset);
    if (p.kind != ParamKind::Int) {
        lo = 0.f;
        hi = p.kind == ParamKind::Toggle ? 1.f : float(p.numLabels - 1);
    }
    fValues[index].store(std::clamp(std::round(value), lo, hi), std::memory_order_relaxed);
}

float StringsHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float StringsHost::defaultParam(uint32_t index) const { return index < fDef.numParams ? fDefaults[index] : 0.f; }

void StringsHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

void StringsHost::noteOn(uint32_t, uint8_t note, uint8_t) { fHeld[note & 127] = 1; }
void StringsHost::noteOff(uint32_t, uint8_t note) { fHeld[note & 127] = 0; }

void StringsHost::controlChange(uint32_t, uint8_t controller, uint8_t)
{
    if (controller == 120 || controller == 123) std::memset(fHeld, 0, sizeof fHeld); // all sound / notes off
}

void StringsHost::process(float* outL, float* outR, uint32_t frames)
{
    std::fill(outL, outL + frames, 0.f);
    std::fill(outR, outR + frames, 0.f);
}

int StringsHost::heldNotes() const
{
    int n = 0;
    for (uint8_t h : fHeld) n += h;
    return n;
}

} // namespace maquina
