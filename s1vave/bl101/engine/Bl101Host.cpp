// Bl101Host.cpp - see Bl101Host.hpp. The DSP is libbl101.a (Bl101Dsp.cpp, built
// bit-exact with -fwrapv, without -ffast-math: s1vave/Makefile); this file is
// only the plumbing and is compiled with the plugin
// (s1vave/bl101/plugins/bl101*/Engine.cpp).
// SPDX-License-Identifier: GPL-3.0-only
#include "Bl101Host.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Bl101Dsp.hpp"

namespace maquina {

Bl101Host::Bl101Host(const VoiceDef& def, uint32_t)
    : fDef(def), fDefaults(def.numParams, 0.f), fValues(def.numParams)
{
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
        const uint16_t par = def.params[i].fwPar;
        if (par >= kMaskBase && par < kMaskBase + 4) fMask[par - kMaskBase] = int(i);
        if (par >= kBitsLoBase && par < kBitsLoBase + 4) fBitsLo[par - kBitsLoBase] = int(i);
        if (par >= kBitsHiBase && par < kBitsHiBase + 4) fBitsHi[par - kBitsHiBase] = int(i);
    }
    if (isFx()) fFx.reset(new bl101::Fx());
    else fSynth.reset(new bl101::Synth());
    setSampleRate(44100.0);
}

Bl101Host::~Bl101Host() = default;

void Bl101Host::setSampleRate(double hostRate)
{
    if (isFx()) fFxStream.setup(hostRate, kEngineRate, true);
    else fStream.setup(hostRate, kEngineRate, false);
    resetTiming();
}

void Bl101Host::resetTiming()
{
    fStream.reset();
    fFxStream.reset();
    fEventHead = fEventCount = 0;
    if (fSynth) fSynth->silence();
    if (fFx) fFx->silence();
}

// ---------------------------------------------------------------------------
// parameters

void Bl101Host::setParam(uint32_t index, float value)
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

float Bl101Host::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float Bl101Host::defaultParam(uint32_t index) const { return index < fDef.numParams ? fDefaults[index] : 0.f; }

void Bl101Host::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int Bl101Host::paramInt(uint32_t index) const
{
    return int(std::lround(fValues[index].load(std::memory_order_relaxed)));
}

uint16_t Bl101Host::chopMask(uint32_t s) const
{
    if (s >= 4 || fMask[s] < 0) return 0xFFFFu;
    const int sel = paramInt(uint32_t(fMask[s]));
    if (sel >= 1 && sel <= 6) return bl101::kChopMaskPresets[sel - 1];
    const int lo = fBitsLo[s] < 0 ? 255 : paramInt(uint32_t(fBitsLo[s]));
    const int hi = fBitsHi[s] < 0 ? 255 : paramInt(uint32_t(fBitsHi[s]));
    return uint16_t((std::clamp(hi, 0, 255) << 8) | std::clamp(lo, 0, 255));
}

// every parameter into the engine (once per block: a few dozen bytes)
void Bl101Host::applyParams()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) {
        const VoiceParam& p = fDef.params[i];
        const int shown = paramInt(i);
        const int byte = std::clamp(p.kind == ParamKind::Int ? shown - p.offset : shown, 0, 255);
        if (p.fwPar < kDrawBase) {
            if (fSynth) fSynth->setByte(p.fwPar, uint8_t(byte));
            else fFx->setByte(p.fwPar, uint8_t(byte));
        } else if (p.fwPar < kDrawBase + 16 && fSynth) {
            fSynth->setDrawStep(p.fwPar - kDrawBase, int16_t((byte - 128) * 256));
        }
    }
    if (fSynth)
        for (uint32_t s = 0; s < 4; ++s) fSynth->setChopMask(s, chopMask(s));
}

// ---------------------------------------------------------------------------
// tempo, MIDI

void Bl101Host::setTempo(double bpm)
{
    if (bpm > 0.0) fBpm = std::clamp(bpm, 20.0, 999.0);
}

void Bl101Host::push(uint32_t frameOffset, EventType type, uint8_t a, int16_t b)
{
    if (isFx() || fEventCount >= kMaxEvents) return;
    fEvents[(fEventHead + fEventCount) % kMaxEvents] = { fStream.engineFrameAt(frameOffset), type, a, b };
    ++fEventCount;
}

void Bl101Host::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (note <= 127 && velocity > 0) push(frameOffset, kNoteOn, note, int16_t(std::min<uint8_t>(velocity, 127)));
}

void Bl101Host::noteOff(uint32_t frameOffset, uint8_t note)
{
    if (note <= 127) push(frameOffset, kNoteOff, note, 0);
}

void Bl101Host::pitchBend(uint32_t frameOffset, int32_t bend)
{
    push(frameOffset, kBend, 0, int16_t(std::clamp(bend, -8192, 8191)));
}

void Bl101Host::controlChange(uint32_t frameOffset, uint8_t cc, uint8_t value)
{
    if (cc == 1 || cc == 64 || cc == 120 || cc == 123) push(frameOffset, kCc, cc, int16_t(value & 127));
}

// ---------------------------------------------------------------------------
// audio

void Bl101Host::runBlock(uint64_t firstFrame, float* outL, float* outR)
{
    bl101::Synth& syn = *fSynth;
    applyParams();
    syn.setTempo(fBpm);
    // events due in this block (note-offs from the block before it on)
    const double blockStart = double(firstFrame), blockEnd = double(firstFrame + kBlock);
    while (fEventCount > 0) {
        const Event& ev = fEvents[fEventHead];
        if (ev.frame >= (ev.type == kNoteOff ? blockStart : blockEnd)) break;
        switch (ev.type) {
        case kNoteOn: syn.noteOn(ev.a, uint8_t(ev.b)); break;
        case kNoteOff: syn.noteOff(ev.a); break;
        case kBend: syn.setBend(ev.b); break;
        case kCc:
            if (ev.a == 1) syn.setModWheel(uint8_t(ev.b));
            else if (ev.a == 64) syn.setSustain(ev.b >= 64);
            else syn.panic();
            break;
        }
        fEventHead = (fEventHead + 1) % kMaxEvents;
        --fEventCount;
    }
    int32_t buf[2 * kBlock];
    syn.process(buf, kBlock);
    for (int i = 0; i < kBlock; ++i) {
        outL[i] = float(buf[2 * i]) * (1.f / 32768.f);
        outR[i] = float(buf[2 * i + 1]) * (1.f / 32768.f);
    }
}

void Bl101Host::runFxBlock(const float* inL, const float* inR, float* outL, float* outR)
{
    applyParams();
    fFx->setTempo(fBpm);
    int32_t buf[2 * kFxBlock];
    for (int i = 0; i < kFxBlock; ++i) {
        // Q15; louder than full scale is kept (the firmware's lines saturate,
        // its softclip limits), up to +-4
        buf[2 * i] = int32_t(std::lround(std::clamp(inL[i], -4.f, 4.f) * 32768.f));
        buf[2 * i + 1] = int32_t(std::lround(std::clamp(inR[i], -4.f, 4.f) * 32768.f));
    }
    fFx->process(buf, kFxBlock);
    for (int i = 0; i < kFxBlock; ++i) {
        outL[i] = float(buf[2 * i]) * (1.f / 32768.f);
        outR[i] = float(buf[2 * i + 1]) * (1.f / 32768.f);
    }
}

void Bl101Host::process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames)
{
    if (isFx()) {
        fFxStream.process(inL, inR, outL, outR, frames,
                          [this](uint64_t, const float* il, const float* ir, float* l, float* r) {
                              runFxBlock(il, ir, l, r);
                          });
        return;
    }
    fStream.process(nullptr, nullptr, outL, outR, frames,
                    [this](uint64_t first, const float*, const float*, float* l, float* r) { runBlock(first, l, r); });
}

} // namespace maquina
