// La50Host.cpp - see La50Host.hpp. The engine is liblasynth.a (lasynth/Makefile);
// this file is the plumbing, compiled with the plugin
// (lasynth/la50/plugins/la50/Engine.cpp).
// SPDX-License-Identifier: GPL-3.0-only
#include "La50Host.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "La50Engine.hpp"
#include "La50Fx.hpp"

namespace maquina {

La50Host::La50Host(const VoiceDef& def, uint32_t)
    : fDef(def), fDefaults(def.numParams, 0.f), fValues(def.numParams), fEngine(new la50::Engine()),
      fFx(new la50::Fx())
{
    std::memcpy(fEngine->patch().data(), la50::kInitPatch, la50::kPatchBytes);
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
    setSampleRate(44100.0);
}

La50Host::~La50Host() = default;

void La50Host::setSampleRate(double hostRate)
{
    fStream.setup(hostRate, kEngineRate, false);
    resetTiming();
}

void La50Host::resetTiming()
{
    fStream.reset();
    fEventHead = fEventCount = 0;
    fEngine->silence();
    fFx->reset();
}

int La50Host::activeVoices() const { return fEngine->activeVoices(); }

void La50Host::setParam(uint32_t index, float value)
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

float La50Host::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float La50Host::defaultParam(uint32_t index) const { return index < fDef.numParams ? fDefaults[index] : 0.f; }

void La50Host::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

void La50Host::applyParams()
{
    la50::PatchData& pd = fEngine->patch();
    for (uint32_t i = 0; i < fDef.numParams; ++i) {
        const VoiceParam& p = fDef.params[i];
        if (p.fwPar >= la50::kPatchBytes) continue;
        const int shown = int(std::lround(fValues[i].load(std::memory_order_relaxed)));
        const int byte = p.kind == ParamKind::Int ? shown - p.offset : shown;
        pd[p.fwPar] = uint8_t(std::clamp(byte, 0, int(p.fwMax)));
    }
}

void La50Host::push(uint32_t frameOffset, EventType type, uint8_t a, int16_t b, uint8_t channel)
{
    if (fEventCount >= kMaxEvents) return;
    fEvents[(fEventHead + fEventCount) % kMaxEvents] = { fStream.engineFrameAt(frameOffset), type, a, channel, b };
    ++fEventCount;
}

void La50Host::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity, uint8_t channel)
{
    if (note <= 127 && velocity > 0) push(frameOffset, kNoteOn, note, int16_t(std::min<uint8_t>(velocity, 127)), channel);
}

void La50Host::noteOff(uint32_t frameOffset, uint8_t note, uint8_t channel)
{
    if (note <= 127) push(frameOffset, kNoteOff, note, 0, channel);
}

void La50Host::pitchBend(uint32_t frameOffset, int32_t bend)
{
    push(frameOffset, kBend, 0, int16_t(std::clamp(bend, -8192, 8191)), 0);
}

void La50Host::controlChange(uint32_t frameOffset, uint8_t cc, uint8_t value)
{
    if (cc == 1 || cc == 64 || cc == 65 || cc == 120 || cc == 123) push(frameOffset, kCc, cc, int16_t(value & 127), 0);
}

void La50Host::aftertouch(uint32_t frameOffset, uint8_t value) { push(frameOffset, kAfter, 0, int16_t(value & 127), 0); }

void La50Host::runBlock(uint64_t firstFrame, float* outL, float* outR)
{
    la50::Engine& e = *fEngine;
    applyParams();
    const double blockStart = double(firstFrame), blockEnd = double(firstFrame + kBlock);
    while (fEventCount > 0) {
        const Event& ev = fEvents[fEventHead];
        if (ev.frame >= (ev.type == kNoteOff ? blockStart : blockEnd)) break;
        switch (ev.type) {
        case kNoteOn: e.noteOn(ev.channel, ev.a, ev.b); break;
        case kNoteOff: e.noteOff(ev.channel, ev.a); break;
        case kBend: e.pitchBend(ev.b); break;
        case kAfter: e.aftertouch(ev.b); break;
        case kCc:
            if (ev.a == 1) e.modWheel(ev.b);
            else if (ev.a == 64) e.sustain(ev.b >= 64);
            else if (ev.a == 65) e.portamento(ev.b >= 64);
            else e.allNotesOff();
            break;
        }
        fEventHead = (fEventHead + 1) % kMaxEvents;
        --fEventCount;
    }
    float up[kBlock], lo[kBlock];
    e.process(up, lo, kBlock);
    fFx->process(e.patch(), up, lo, outL, outR, kBlock);
}

void La50Host::process(const float*, const float*, float* outL, float* outR, uint32_t frames)
{
    fStream.process(nullptr, nullptr, outL, outR, frames,
                    [this](uint64_t first, const float*, const float*, float* l, float* r) { runBlock(first, l, r); });
}

} // namespace maquina
