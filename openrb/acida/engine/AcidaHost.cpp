// AcidaHost.cpp - see AcidaHost.hpp. The DSP is libacida.a (AcidaDsp.cpp,
// built bit-exact without -ffast-math: openrb/Makefile); this file is only
// the plumbing and is compiled with the plugin
// (openrb/acida/plugins/acida*/Engine.cpp).
// SPDX-License-Identifier: GPL-3.0-only
#include "AcidaHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "AcidaDsp.hpp"

namespace maquina {

namespace {

// parameter slots (fIx): the symbols of tools/gen_params.py acida_params()
enum Slot : int {
    sTune, sCutoff, sResonance, sEnvMod, sDecay, sAccent, sWaveform, sEngine, sAccentVel,
    sDistOn, sDistAmount, sDistShape,
    sPcfOn, sPcfFreq, sPcfReso, sPcfAmount, sPcfWave, sPcfDecay, sPcfMode,
    sCompOn, sCompAmount, sCompThreshold,
    sDelayOn, sDelaySend, sDelaySteps, sDelayMode, sDelayFeedback, sDelayPan,
    kNumSlots
};
const char* const kSlotSymbols[kNumSlots] = {
    "tune", "cutoff", "resonance", "env_mod", "decay", "accent", "waveform", "engine", "accent_vel",
    "dist_on", "dist_amount", "dist_shape",
    "pcf_on", "pcf_freq", "pcf_reso", "pcf_amount", "pcf_wave", "pcf_decay", "pcf_mode",
    "comp_on", "comp_amount", "comp_threshold",
    "delay_on", "delay_send", "delay_steps", "delay_mode", "delay_feedback", "delay_pan",
};

int findSymbol(const VoiceDef& d, const char* symbol)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, symbol)) return int(i);
    return -1;
}

// ReBirth's 768 ticks per bar of four beats
constexpr double kTicksPerBeat = 192.0;

} // namespace

struct AcidaHost::Engine {
    const acida::Tables& t = acida::Tables::get();
    acida::Tb303 tb;
    acida::Distortion dist[2];
    acida::Pcf pcf[2];
    acida::Compressor comp;
    std::unique_ptr<acida::Delay> delay;
    float outGain = 1.f; // Acida: the mixer's channel level 100 and master 100
};

AcidaHost::AcidaHost(const VoiceDef& def, uint32_t)
    : fDef(def), fDefaults(def.numParams, 0.f), fValues(def.numParams), fEngine(new Engine())
{
    static_assert(kNumSlots <= 32, "fIx");
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
    for (int s = 0; s < kNumSlots; ++s) fIx[s] = findSymbol(def, kSlotSymbols[s]);
    if (isFx()) fEngine->delay.reset(new acida::Delay());
    // frame/mod.rs: channel gain PAN[100][64] = T[100], master 2 * T[100]
    const float t100 = fEngine->t.level[100];
    fEngine->outGain = float(double(t100) * (2.0 * double(t100)));
    setSampleRate(44100.0);
}

AcidaHost::~AcidaHost() = default;

void AcidaHost::setSampleRate(double hostRate)
{
    fStream.setup(hostRate, kEngineRate, isFx());
    resetTiming();
}

void AcidaHost::resetTiming()
{
    fStream.reset();
    fEventHead = fEventCount = 0;
    fHostFrames = 0;
    fBeatHostFrame = 0;
    fEngine->tb.silence();
}

// ---------------------------------------------------------------------------
// parameters

void AcidaHost::setParam(uint32_t index, float value)
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

float AcidaHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float AcidaHost::defaultParam(uint32_t index) const { return index < fDef.numParams ? fDefaults[index] : 0.f; }

void AcidaHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int AcidaHost::paramInt(int index) const
{
    return index < 0 ? 0 : int(std::lround(fValues[uint32_t(index)].load(std::memory_order_relaxed)));
}

// the engine byte of a parameter: the shown value minus its display offset
uint8_t AcidaHost::byteOf(int index) const
{
    if (index < 0) return 0;
    const VoiceParam& p = fDef.params[index];
    const int v = paramInt(index) - (p.kind == ParamKind::Int ? p.offset : 0);
    return uint8_t(std::clamp(v, 0, 127));
}

// ---------------------------------------------------------------------------
// transport, notes

void AcidaHost::setTempo(double bpm)
{
    if (bpm > 0.0) fBpm = std::clamp(bpm, 20.0, 999.0);
}

void AcidaHost::setTransport(bool playing, double beat, double, double beatsPerSample)
{
    fPlaying = playing && beatsPerSample > 0.0;
    fBeat = beat;
    fBeatsPerSample = beatsPerSample;
    fBeatHostFrame = fHostFrames;
}

void AcidaHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (note > 127 || velocity == 0 || fEventCount >= kMaxEvents) return;
    fEvents[(fEventHead + fEventCount) % kMaxEvents] = { fStream.engineFrameAt(frameOffset), note, velocity };
    ++fEventCount;
}

void AcidaHost::noteOff(uint32_t frameOffset, uint8_t note)
{
    if (note > 127 || fEventCount >= kMaxEvents) return;
    fEvents[(fEventHead + fEventCount) % kMaxEvents] = { fStream.engineFrameAt(frameOffset), note, 0 };
    ++fEventCount;
}

// ---------------------------------------------------------------------------
// audio

void AcidaHost::runBlock(uint64_t firstFrame, const float* inL, const float* inR, float* outL, float* outR)
{
    Engine& e = *fEngine;
    const acida::Tables& t = e.t;
    using acida::kBlock;

    if (!isFx()) {
        // the knobs first: a note-on reads Tune, Resonance, Decay and Accent
        e.tb.setVintage(paramInt(fIx[sEngine]) == 1);
        e.tb.setParam(acida::Tb303::TUNE, byteOf(fIx[sTune]));
        e.tb.setParam(acida::Tb303::CUTOFF, byteOf(fIx[sCutoff]));
        e.tb.setParam(acida::Tb303::RESONANCE, byteOf(fIx[sResonance]));
        e.tb.setParam(acida::Tb303::ENVMOD, byteOf(fIx[sEnvMod]));
        e.tb.setParam(acida::Tb303::DECAY, byteOf(fIx[sDecay]));
        e.tb.setParam(acida::Tb303::ACCENT, byteOf(fIx[sAccent]));
        e.tb.setParam(acida::Tb303::WAVEFORM, uint8_t(paramInt(fIx[sWaveform]) == 1));
        // notes due in this block (note-offs from the block before it on)
        const double blockStart = double(firstFrame), blockEnd = double(firstFrame + kBlock);
        while (fEventCount > 0) {
            const Event& ev = fEvents[fEventHead];
            if (ev.frame >= (ev.velocity ? blockEnd : blockStart)) break;
            if (ev.velocity)
                e.tb.keyOn(ev.note, ev.velocity, uint8_t(std::clamp(paramInt(fIx[sAccentVel]), 1, 127)));
            else
                e.tb.keyOff(ev.note);
            fEventHead = (fEventHead + 1) % kMaxEvents;
            --fEventCount;
        }
        acida::Block bus;
        e.tb.process(bus, t);
        const double g = e.outGain;
        for (int i = 0; i < kBlock; ++i) outL[i] = outR[i] = float(std::clamp(double(bus[i]) * g, -1.0, 1.0));
        return;
    }

    fEventHead = fEventCount = 0; // no notes

    // the song position of this block (ticks), from the host transport
    const double hostFrame = double(firstFrame) * fStream.hostRate() / kEngineRate;
    if (fPlaying) {
        const double beat = fBeat + (hostFrame - double(fBeatHostFrame)) * fBeatsPerSample;
        fFreeTick = std::max(0.0, beat * kTicksPerBeat);
    } else {
        fFreeTick += double(kBlock) * fBpm / 60.0 * kTicksPerBeat / kEngineRate;
        if (fFreeTick > 1e9) fFreeTick = std::fmod(fFreeTick, 768.0 * 4096.0);
    }
    fTickAtBlock = int32_t(fFreeTick);
    const uint32_t tempo = uint32_t(std::lround(fBpm * 1000.0));

    for (int c = 0; c < 2; ++c) {
        e.dist[c].setParam(acida::Distortion::ENABLED, uint8_t(paramInt(fIx[sDistOn])));
        e.dist[c].setParam(acida::Distortion::AMOUNT, byteOf(fIx[sDistAmount]));
        e.dist[c].setParam(acida::Distortion::SHAPE, byteOf(fIx[sDistShape]));
        e.pcf[c].setParam(acida::Pcf::ENABLED, uint8_t(paramInt(fIx[sPcfOn])));
        e.pcf[c].setParam(acida::Pcf::FREQUENCY, byteOf(fIx[sPcfFreq]));
        e.pcf[c].setParam(acida::Pcf::RESONANCE, byteOf(fIx[sPcfReso]));
        e.pcf[c].setParam(acida::Pcf::AMOUNT, byteOf(fIx[sPcfAmount]));
        e.pcf[c].setParam(acida::Pcf::WAVE, byteOf(fIx[sPcfWave]));
        e.pcf[c].setParam(acida::Pcf::DECAY, byteOf(fIx[sPcfDecay]));
        e.pcf[c].setParam(acida::Pcf::MODE, paramInt(fIx[sPcfMode]) == 0 ? 1 : 0); // LP = 1
    }
    e.comp.setParam(acida::Compressor::ENABLED, uint8_t(paramInt(fIx[sCompOn])));
    e.comp.setParam(acida::Compressor::AMOUNT, byteOf(fIx[sCompAmount]));
    e.comp.setParam(acida::Compressor::THRESHOLD, byteOf(fIx[sCompThreshold]));
    acida::Delay& dl = *e.delay;
    dl.setParam(acida::Delay::ENABLED, uint8_t(paramInt(fIx[sDelayOn])));
    dl.setParam(acida::Delay::STEPS, byteOf(fIx[sDelaySteps]));
    dl.setParam(acida::Delay::MODE, uint8_t(paramInt(fIx[sDelayMode])));
    dl.setParam(acida::Delay::FEEDBACK, byteOf(fIx[sDelayFeedback]));
    dl.setParam(acida::Delay::PAN, byteOf(fIx[sDelayPan]));

    acida::Block l, r, send;
    for (int i = 0; i < kBlock; ++i) {
        l[i] = inL[i];
        r[i] = inR[i];
    }
    e.dist[0].process(l, t);
    e.dist[1].process(r, t);
    e.pcf[0].process(l, t, fTickAtBlock);
    e.pcf[1].process(r, t, fTickAtBlock);
    e.comp.processStereo(l, r);
    // the send bus: 1e-6 + the mono sum * T[send]
    const double sg = double(t.level[byteOf(fIx[sDelaySend])]);
    for (int i = 0; i < kBlock; ++i)
        send[i] = float((double(l[i]) + double(r[i])) * 0.5 * sg + double(acida::kGuard));
    dl.process(send, l, r, t, tempo);
    for (int i = 0; i < kBlock; ++i) {
        outL[i] = std::clamp(l[i], -1.f, 1.f);
        outR[i] = std::clamp(r[i], -1.f, 1.f);
    }
}

void AcidaHost::process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames)
{
    fStream.process(inL, inR, outL, outR, frames,
                    [this](uint64_t first, const float* il, const float* ir, float* l, float* r) {
                        runBlock(first, il, ir, l, r);
                    });
    fHostFrames += frames;
}

} // namespace maquina
