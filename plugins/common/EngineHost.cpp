// EngineHost.cpp - see EngineHost.hpp.
// SPDX-License-Identifier: GPL-3.0-only
#include "EngineHost.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

#include "lxr_host.h"

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
#include <immintrin.h>
#define MAQUINA_CPU_RELAX() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
#define MAQUINA_CPU_RELAX() __asm__ __volatile__("yield")
#else
#define MAQUINA_CPU_RELAX() ((void)0)
#endif

namespace maquina {

namespace {

// One engine per process (per plugin binary): lock + which instance is resident.
std::atomic_flag gEngineLock = ATOMIC_FLAG_INIT;
EngineHost* gResident = nullptr;

constexpr double kEngineRate = LXR_ENGINE_RATE;

// Flush denormals like the Cortex-M7 build (FPDSCR.FZ); also keeps CPU sane.
struct ScopedFlushDenormals {
#if defined(__SSE__) || defined(_M_X64)
    unsigned int old;
    ScopedFlushDenormals() : old(_mm_getcsr()) { _mm_setcsr(old | 0x8040); }
    ~ScopedFlushDenormals() { _mm_setcsr(old); }
#elif defined(__aarch64__)
    uint64_t old;
    ScopedFlushDenormals()
    {
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(old));
        __asm__ __volatile__("msr fpcr, %0" ::"r"(old | (1ull << 24)));
    }
    ~ScopedFlushDenormals() { __asm__ __volatile__("msr fpcr, %0" ::"r"(old)); }
#else
    ScopedFlushDenormals() {}
#endif
};

} // namespace

EngineHost::EngineHost(const VoiceDef& def, uint32_t seed)
    : fDef(def),
      fDefaults(def.numParams, 0.f),
      fValues(def.numParams),
      fDirty((def.numParams + 31) / 32)
{
    for (auto& d : fDirty)
        d.store(0, std::memory_order_relaxed);

    acquire();
    lxr_global_init();
    fBlob.resize(lxr_state_size());
    // Fresh engine state with the firmware's Initkit loaded.
    lxr_state_reset();
    lxr_load_init_kit();
    // HiHat: one Amp Decay for both hats (see applyDirtyParams); the Initkit's
    // open decay would otherwise stay until the decay is first changed
    if (def.voice == LXR_VOICE_HIHAT)
        lxr_set_param(PAR_VELOD6_OPEN, lxr_get_param(PAR_VELOD6_CLOSED));
    lxr_seed_rng(seed);
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = fromFirmware(i, lxr_get_param(def.params[i].fwPar));
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
    lxr_state_save(fBlob.data());
    release();

    setSampleRate(fHostRate);
}

EngineHost::~EngineHost()
{
    while (gEngineLock.test_and_set(std::memory_order_acquire))
        MAQUINA_CPU_RELAX();
    if (gResident == this)
        gResident = nullptr;
    gEngineLock.clear(std::memory_order_release);
}

void EngineHost::acquire()
{
    for (unsigned spins = 0; gEngineLock.test_and_set(std::memory_order_acquire); ++spins) {
        if (spins < 4096)
            MAQUINA_CPU_RELAX();
        else
            std::this_thread::yield();
    }
    if (gResident != this) {
        if (gResident != nullptr)
            lxr_state_save(gResident->fBlob.data());
        if (!fBlob.empty())
            lxr_state_load(fBlob.data());
        gResident = this;
    }
}

void EngineHost::release()
{
    gEngineLock.clear(std::memory_order_release);
}

void EngineHost::setSampleRate(double hostRate)
{
    fHostRate = hostRate > 0.0 ? hostRate : 48000.0;
    fResampler.setup(kEngineRate, fHostRate);
    // Sound for an event at host frame H starts at engine frame
    // H*ratio + kEventDelay - q (block quantisation q in [0,63]); the resampler
    // shows input position k*ratio - 1 at output frame k.
    const double meanEngineDelay = kEventDelay - 31.5 + 1.0;
    fLatency = uint32_t(std::lround(meanEngineDelay / fResampler.ratio()));
    resetTiming();
}

void EngineHost::resetTiming()
{
    fResampler.reset();
    fEventHead = fEventCount = 0;
    fHostFrame = 0.0;
    fEngineFrame = 0.0;
}

// ---------------------------------------------------------------------------
// parameters

uint8_t EngineHost::toFirmware(uint32_t index, float value) const
{
    const VoiceParam& p = fDef.params[index];
    const long v = std::lround(value);
    switch (p.kind) {
    case ParamKind::Int:
        return uint8_t(std::clamp<long>(v - p.offset, p.fwMin, p.fwMax));
    case ParamKind::Retrigger:
        return v >= 1 ? uint8_t(fDef.voice + 1) : 0;
    case ParamKind::ModTarget: {
        const uint8_t t = p.targets[std::clamp<long>(v, 0, p.numLabels - 1)];
        // HiHat: "Amp Decay" modulates the decay of the hat this instance plays
        if (fDef.voice == LXR_VOICE_HIHAT && hatOpen() && lxr_mod_target_param(t) == PAR_VELOD6_CLOSED)
            return hatOpenDecayTarget(t);
        return t;
    }
    case ParamKind::Enum:
    case ParamKind::Toggle:
    case ParamKind::Note:
    default:
        return uint8_t(std::clamp<long>(v, p.fwMin, p.fwMax));
    }
}

float EngineHost::fromFirmware(uint32_t index, uint8_t fw) const
{
    const VoiceParam& p = fDef.params[index];
    switch (p.kind) {
    case ParamKind::Int:
        return float(int(fw) + p.offset);
    case ParamKind::Retrigger:
        return fw != 0 ? 1.f : 0.f;
    case ParamKind::ModTarget:
        for (uint8_t i = 0; i < p.numLabels; ++i)
            if (p.targets[i] == fw)
                return float(i);
        return 0.f;
    default:
        return float(fw);
    }
}

// modTargets[] index of PAR_VELOD6_OPEN, next to the closed decay's (fallback: closed)
uint8_t EngineHost::hatOpenDecayTarget(uint8_t closedTarget)
{
    uint8_t start, end;
    lxr_mod_target_range(LXR_VOICE_HIHAT, &start, &end);
    for (uint8_t i = start; i <= end; ++i)
        if (lxr_mod_target_param(i) == PAR_VELOD6_OPEN)
            return i;
    return closedTarget;
}

void EngineHost::setHatOpen(bool on)
{
    if (fHatOpen.exchange(on, std::memory_order_relaxed) == on)
        return;
    // re-send the modulation targets: "Amp Decay" follows the hat
    for (uint32_t i = 0; i < fDef.numParams; ++i)
        if (fDef.params[i].kind == ParamKind::ModTarget)
            fDirty[i / 32].fetch_or(1u << (i % 32), std::memory_order_release);
}

void EngineHost::setParam(uint32_t index, float value)
{
    if (index >= fDef.numParams)
        return;
    fValues[index].store(value, std::memory_order_relaxed);
    fDirty[index / 32].fetch_or(1u << (index % 32), std::memory_order_release);
}

float EngineHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float EngineHost::defaultParam(uint32_t index) const
{
    return index < fDef.numParams ? fDefaults[index] : 0.f;
}

void EngineHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i)
        setParam(i, fDefaults[i]);
}

void EngineHost::applyDirtyParams()
{
    for (uint32_t w = 0; w < fDirty.size(); ++w) {
        uint32_t bits = fDirty[w].exchange(0, std::memory_order_acquire);
        while (bits) {
            const int b = __builtin_ctz(bits);
            bits &= bits - 1;
            const uint32_t i = w * 32 + uint32_t(b);
            const uint8_t fw = toFirmware(i, fValues[i].load(std::memory_order_relaxed));
            lxr_set_param(fDef.params[i].fwPar, fw);
            // HiHat: one Amp Decay for both hats (each instance plays one of them)
            if (fDef.params[i].fwPar == PAR_VELOD6_CLOSED)
                lxr_set_param(PAR_VELOD6_OPEN, fw);
        }
    }
}

// ---------------------------------------------------------------------------
// events + rendering

void EngineHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (fEventCount >= kMaxEvents || velocity == 0)
        return;
    Event& e = fEvents[(fEventHead + fEventCount) % kMaxEvents];
    e.engineFrame = (fHostFrame + frameOffset) * fResampler.ratio() + kEventDelay;
    e.note = note;
    e.velocity = velocity;
    e.trigger = fDef.voice;
    ++fEventCount;
}

void EngineHost::triggerNote(uint8_t note, uint8_t velocity)
{
    const uint8_t trigNote = lxr_get_param(PAR_MIDI_NOTE1 + fDef.voice);
    if (trigNote == 0 && !noteFollowsPitch())
        note = 60; // SEQ_DEFAULT_NOTE: the voice's own pitch

    if (fDef.voice == LXR_VOICE_HIHAT && hatOpen()) {
        // The open hat's own note setting (PAR_MIDI_NOTE7) stays on Any; the
        // instance's Trigger Note filters here, as the firmware would.
        if (trigNote != 0) {
            if (note != trigNote)
                return;
            note = 60;
        }
        lxr_note_on(LXR_TRIGGER_HIHAT_OPEN, note, velocity);
        return;
    }
    lxr_note_on(fDef.voice, note, velocity);
}

void EngineHost::renderBlock(int16_t* lr)
{
    // apply every event that falls inside this engine block (or is overdue)
    const double blockEnd = fEngineFrame + LXR_BLOCK_SIZE;
    while (fEventCount > 0 && fEvents[fEventHead].engineFrame < blockEnd) {
        const Event& e = fEvents[fEventHead];
        triggerNote(e.note, e.velocity);
        fEventHead = (fEventHead + 1) % kMaxEvents;
        --fEventCount;
    }
    lxr_render_voice(fDef.voice, lr);
    fEngineFrame = blockEnd;
}

void EngineHost::process(float* outL, float* outR, uint32_t frames)
{
    ScopedFlushDenormals ftz;
    acquire();

    applyDirtyParams();
    if (fBpm > 0.0) {
        const uint16_t bpm = uint16_t(std::clamp(std::lround(fBpm), 1L, 999L));
        if (bpm != fEngineBpm) {
            lxr_set_bpm(bpm);
            fEngineBpm = bpm;
        }
    }

    fResampler.process(outL, outR, frames, [this](int16_t* lr) { renderBlock(lr); });
    fHostFrame += frames;

    release();
}

} // namespace maquina
