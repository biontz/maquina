// La50Engine.cpp - see La50Engine.hpp.
//
// The TVA / TVF envelope code (phases, LA32Ramp increments from
// envLogarithmicTime, bias / velocity / key-follow scaling, the TVP timing
// formula) is adapted from Munt's mt32emu (TVA.cpp, TVF.cpp, TVP.cpp):
//   Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009 Dean Beeler, Jerome Fisher
//   Copyright (C) 2011-2026 Dean Beeler, Jerome Fisher, Sergey V. Mikayev
// licensed LGPL-2.1-or-later, used here under GPL-3.0-only (LGPL section 3).
//
// SPDX-License-Identifier: GPL-3.0-only
#include "La50Engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "LA32Ramp.h"
#include "LA32WaveGenerator.h"
#include "Tables.h"

using namespace MT32Emu;

namespace la50 {

namespace {

constexpr int kTick = 16; // control rate: 2 kHz
constexpr double kTickSec = double(kTick) / kEngineRate;
constexpr Bit32u kMaxCutoff = 240u << 18; // Munt Partial.cpp MAX_CUTOFF_VALUE

// structures 1..7: partial 1 / 2 PCM, ring modulation (D-50 owner's manual)
struct Structure { bool pcm[2]; bool ring; };
constexpr Structure kStructures[7] = { { { false, false }, false }, { { false, false }, true },
                                       { { true, false }, false },  { { true, false }, true },
                                       { { false, true }, true },   { { true, true }, false },
                                       { { true, true }, true } };

// Munt TVP: -1 .. 2, s1, s2 per 8192
constexpr int kPitchKeyfollowMult[17] = { -8192, -4096, -2048, 0, 1024, 2048, 3072, 4096, 5120,
                                          6144,  7168,  8192,  10240, 12288, 16384, 8198, 8226 };
// Munt TVF / TVA
constexpr int kKeyfollowMult21[17] = { -21, -10, -5, 0, 2, 5, 8, 10, 13, 16, 18, 21, 26, 32, 42, 21, 21 };
constexpr int kBiasLevelToBiasMult[15] = { 85, 42, 21, 16, 10, 5, 2, 0, -2, -5, -10, -16, -21, -74, -85 };
constexpr int kBiasLevelToAmpSubtractionCoeff[13] = { 255, 187, 137, 100, 74, 54, 40, 29, 21, 15, 10, 5, 0 };
constexpr Bit16u kLowerDurationToDivisor[8] = { 34078, 37162, 40526, 44194, 48194, 52556, 57312, 62499 };

// LA32 pitch (4096 per octave) of C4 for each wave kind with coarse C4,
// pitch key follow 1 (Munt TVP.cpp calcBasePitch for synth waves; PCM: the
// stand-ins' natural C4 at a sample step of 1)
constexpr int kSquareC4 = 37133, kSawC4 = 33037, kPcmC4 = 53248;

const Tables& tables()
{
    static const Tables& t = [] () -> const Tables& {
        const Tables& tb = Tables::getInstance();
        LA32IntPartialPair::initTables(tb);
        LA32Ramp::initTables(tb);
        return tb;
    }();
    return t;
}

int keyToPitch(int key) { return int(std::lround(double(key - 60) * 4096.0 / 12.0)); }

// Munt TVP.cpp setupPitchChange: an envelope time (1..112) -> seconds
double tvpSeconds(int changeDuration)
{
    if (changeDuration <= 0) return 0.0;
    const int d = std::min(changeDuration, 104) - 1;
    const int upper = d >> 3;
    const int bigTicks = kLowerDurationToDivisor[d & 7] >> (12 - upper);
    return double(bigTicks) * 256.0 / 500000.0;
}

int veloAmpSubtraction(int sens, int velocity) // Munt TVA calcVeloAmpSubtraction
{
    int mult = sens - 50;
    const int absMult = mult < 0 ? -mult : mult;
    mult = int(unsigned(mult * (velocity - 64)) << 2);
    return absMult - (mult >> 8);
}

int biasAmpSubtraction(int point, int level, int key) // Munt TVA calcBiasAmpSubtraction
{
    auto mult = [&](int bias) { return (bias * kBiasLevelToAmpSubtractionCoeff[std::clamp(level, 0, 12)]) >> 5; };
    if ((point & 0x40) == 0) {
        const int bias = point + 33 - key;
        if (bias > 0) return std::min(255, mult(bias));
    } else {
        const int bias = point - 31 - key;
        if (bias < 0) return std::min(255, mult(-bias));
    }
    return 0;
}

int baseCutoffOf(const uint8_t* pp, int basePitch, int key) // Munt TVF calcBaseCutoff
{
    int c = kKeyfollowMult21[std::min<int>(pp[P_TVF_KEYFOLLOW], 14)] -
            kKeyfollowMult21[std::min<int>(pp[P_WG_KEYFOLLOW], 16)];
    c *= key - 60;
    const int point = pp[P_TVF_BIAS_POINT], level = std::min<int>(pp[P_TVF_BIAS_LEVEL], 14);
    if ((point & 0x40) == 0) {
        int bias = point + 33 - key;
        if (bias > 0) c += -bias * kBiasLevelToBiasMult[level];
    } else {
        int bias = point - 31 - key;
        if (bias < 0) c += bias * kBiasLevelToBiasMult[level];
    }
    c += (pp[P_TVF_CUTOFF] << 4) - 800;
    if (c >= 0) {
        const int d = (basePitch >> 4) + c - 3584;
        if (d > 0) c -= d;
    } else if (c < -2048) {
        c = -2048;
    }
    c = (c + 2056) >> 4;
    return std::clamp(c, 0, 255);
}

float lfoRateHz(int rate) { return float(0.03 * std::pow(2.0, rate / 11.0)); }
float lfoDelaySec(int delay) { return delay <= 0 ? 0.f : float(0.02 * std::pow(2.0, delay / 12.0)); }

enum { TVA_BASIC = 0, TVA_ATTACK = 1, TVA_2 = 2, TVA_3 = 3, TVA_4 = 4, TVA_SUSTAIN = 5, TVA_RELEASE = 6, TVA_DEAD = 7,
       TVA_HELD = 8 };
enum { TVF_2 = 2, TVF_SUSTAIN = 5, TVF_RELEASE = 6, TVF_DONE = 7 };

} // namespace

// ---------------------------------------------------------------------------

struct PartialState {
    bool on = false, pcm = false, playing = false;
    const uint8_t* pp = nullptr; // the partial block
    LA32Ramp ampRamp, cutRamp;
    int tvaPhase = TVA_DEAD, tvaTarget = 0, basicAmp = 0, keyTimeSubA = 0;
    int tvfPhase = TVF_DONE, tvfTarget = 0, levelMult = 0, keyTimeSubF = 0, baseCutoff = 0;
    int pw0 = 0;               // pulse width at note on (0..255)
    int basePitch = 0;         // without modulation
    int keyfollow = 8192;
    Bit32u lastAmp = 0xFFFFFFFFu, lastCut = 0xFFFFFFFFu;
    int ampMod = 0, cutMod = 0; // LFO / aftertouch, in ramp units (<< 18)
};

struct Engine::Voice {
    bool active = false, gate = false, held = false;
    int tone = 0, key = 60, velocity = 100, channel = 0;
    uint32_t age = 0;
    LA32IntPartialPair pair;
    PartialState p[2];
    // pitch
    double keyPitch = 0, keyTarget = 0, glideK = 1.0;
    // the tone's pitch envelope, per note
    int penvPhase = 0;
    double penv = 0, penvFrom = 0, penvTo = 0, penvT = 0, penvDur = 0;
    // LFOs: key-synced phases, time since note on
    double lfoPhase[3] = {};
    float lfoHold[3] = {};
    double sinceOn = 0;
};

Engine::Engine() : fPcm(&PcmBank::standIns()), fVoices(new Voice[kVoices])
{
    tables();
}

Engine::~Engine() = default;

// ---------------------------------------------------------------------------
// envelopes (Munt TVA / TVF)

namespace {

const uint8_t* commonOf(const PatchData& pd, int tone) { return pd.data() + (tone ? kLowerCommon : kUpperCommon); }
const uint8_t* partialOf(const PatchData& pd, int tone, int k)
{
    return pd.data() + (tone ? (k ? kLowerP2 : kLowerP1) : (k ? kUpperP2 : kUpperP1));
}

void tvaStart(PartialState& s, int target, int increment, int phase)
{
    s.tvaTarget = target;
    s.tvaPhase = phase;
    s.ampRamp.startRamp(Bit8u(std::clamp(target, 0, 255)), Bit8u(increment));
}

void tvfStart(PartialState& s, int target, int increment, int phase)
{
    s.tvfTarget = target;
    s.tvfPhase = phase;
    s.cutRamp.startRamp(Bit8u(std::clamp(target, 0, 255)), Bit8u(increment));
}

int envIncrement(int target, int current, int time, int& newTarget)
{
    const Bit8u* logTime = tables().envLogarithmicTime;
    newTarget = target;
    if (time <= 0) return target >= current ? (0x80 | 127) : 127;
    int delta = target - current;
    if (delta == 0) {
        if (target == 0) { delta = 1; newTarget = 1; }
        else { delta = -1; newTarget = target - 1; }
    }
    int inc = logTime[std::min(delta < 0 ? -delta : delta, 255)] - time;
    if (inc <= 0) inc = 1;
    return delta < 0 ? (inc | 0x80) : inc;
}

// the release (T5) ramp to the end level, as the D-50 sets it (v2.22 program ROM): the
// time, less the key follow, scaled by 1.25 and clamped to 1..127, from 128; no level
// term; descending unless the End level is 1
int releaseIncrement(int t5, bool rising)
{
    const int t = std::max(t5, 0);
    const int speed = 128 - std::clamp(t + (t >> 2), 1, 127);
    return rising ? speed : (0x80 | speed);
}

void tvaNextPhase(PartialState& s, bool canSustain, int velocity)
{
    const uint8_t* pp = s.pp;
    int newPhase = s.tvaPhase + 1;
    if (s.tvaPhase == TVA_HELD) return;
    if (newPhase == TVA_DEAD) { // the release reached its end level
        if (pp[P_TVA_END]) { s.tvaPhase = TVA_HELD; return; }
        s.tvaPhase = TVA_DEAD;
        s.playing = false;
        return;
    }
    const int levels[4] = { pp[P_TVA_L1], pp[P_TVA_L2], pp[P_TVA_L3], pp[P_TVA_SUS] };
    const int times[5] = { pp[P_TVA_T1], pp[P_TVA_T2], pp[P_TVA_T3], pp[P_TVA_T4], pp[P_TVA_T5] };
    if (newPhase == TVA_SUSTAIN || newPhase == TVA_RELEASE) {
        if (!canSustain) { // released before the sustain: the release, to the end level
            const bool rising = pp[P_TVA_END] != 0;
            tvaStart(s, rising ? s.basicAmp + 100 : 0, releaseIncrement(times[4] - s.keyTimeSubA, rising), TVA_RELEASE);
            return;
        }
        tvaStart(s, s.basicAmp + levels[3], 0, TVA_SUSTAIN);
        return;
    }
    const int idx = s.tvaPhase; // 0..3: level / time index of this segment
    int time = times[idx];
    if (newPhase == TVA_ATTACK) {
        time -= ((velocity - 64) >> (6 - std::min<int>(pp[P_TVA_VELO_FOLLOW], 4))) + s.keyTimeSubA;
        if (time <= 0 && times[idx] != 0) time = 1;
    } else {
        time -= s.keyTimeSubA;
    }
    int target;
    const int inc = envIncrement(s.basicAmp + levels[idx], s.tvaTarget, time, target);
    tvaStart(s, target, inc, newPhase);
}

void tvaRelease(PartialState& s)
{
    if (s.tvaPhase >= TVA_RELEASE) return;
    const bool rising = s.pp[P_TVA_END] != 0;
    tvaStart(s, rising ? s.basicAmp + 100 : 0, releaseIncrement(s.pp[P_TVA_T5] - s.keyTimeSubA, rising),
             TVA_RELEASE);
}

void tvfNextPhase(PartialState& s, bool canSustain)
{
    const uint8_t* pp = s.pp;
    const int newPhase = s.tvfPhase + 1;
    const int levels[4] = { pp[P_TVF_L1], pp[P_TVF_L2], pp[P_TVF_L3], pp[P_TVF_SUS] };
    const int times[5] = { pp[P_TVF_T1], pp[P_TVF_T2], pp[P_TVF_T3], pp[P_TVF_T4], pp[P_TVF_T5] };
    if (newPhase >= TVF_DONE) {
        tvfStart(s, s.tvfTarget, 0, TVF_DONE);
        return;
    }
    if (newPhase == TVF_SUSTAIN || newPhase == TVF_RELEASE) {
        if (!canSustain) {
            const int end = pp[P_TVF_END] ? (s.levelMult * 100) >> 8 : 0;
            tvfStart(s, end, releaseIncrement(times[4] - s.keyTimeSubF, end > s.tvfTarget), TVF_DONE - 1);
            return;
        }
        tvfStart(s, (s.levelMult * levels[3]) >> 8, 0, newPhase);
        return;
    }
    const int idx = s.tvfPhase;
    int target;
    const int inc = envIncrement((s.levelMult * levels[idx]) >> 8, s.tvfTarget, times[idx] - s.keyTimeSubF, target);
    tvfStart(s, target, inc, newPhase);
}

void tvfRelease(PartialState& s)
{
    if (s.tvfPhase >= TVF_RELEASE) return;
    const int end = s.pp[P_TVF_END] ? (s.levelMult * 100) >> 8 : 0;
    tvfStart(s, end, releaseIncrement(s.pp[P_TVF_T5] - s.keyTimeSubF, end > s.tvfTarget), TVF_DONE - 1);
}

} // namespace

// ---------------------------------------------------------------------------
// notes

Engine::Voice* Engine::allocate()
{
    Voice* best = nullptr;
    for (int k = 0; k < kVoices; ++k)
        if (!fVoices[k].active) return &fVoices[k];
    // steal: the oldest released voice, else the oldest
    for (int pass = 0; pass < 2 && !best; ++pass)
        for (int k = 0; k < kVoices; ++k) {
            Voice& v = fVoices[k];
            if (pass == 0 && (v.gate || v.held)) continue;
            if (!best || v.age < best->age) best = &v;
        }
    return best;
}

void Engine::startNote(int tone, int key, int velocity, int channel)
{
    Voice* vp = allocate();
    if (!vp) return;
    Voice& v = *vp;
    const PatchData& pd = fPatch;
    const uint8_t* com = commonOf(pd, tone);
    const uint8_t* patch = pd.data() + kPatch;
    const Structure st = kStructures[std::min<int>(com[C_STRUCTURE], 6)];
    const int mute = com[C_PARTIAL_MUTE];
    const int balance = com[C_PARTIAL_BALANCE];

    v.active = true;
    v.gate = true;
    v.held = false;
    v.tone = tone;
    v.key = key;
    v.velocity = velocity;
    v.channel = channel;
    v.age = ++fAge;
    v.sinceOn = 0;

    // the played key with the tone's key shift; portamento from the previous key
    const int shifted = key + (tone ? patch[K_L_KEY_SHIFT] : patch[K_U_KEY_SHIFT]) - 24;
    v.keyTarget = keyToPitch(shifted);
    const int portaMode = patch[K_PORTA_MODE]; // U, L, UL
    const bool porta = fPorta && patch[K_PORTA_TIME] > 0 && (portaMode == 2 || portaMode == tone) && fLastKey[tone] >= 0;
    v.keyPitch = porta ? double(keyToPitch(fLastKey[tone])) : v.keyTarget;
    v.glideK = porta ? 1.0 - std::exp(-kTickSec / (0.002 * std::pow(2.0, patch[K_PORTA_TIME] / 13.0))) : 1.0;
    fLastKey[tone] = shifted;

    // pitch envelope from L0
    v.penvPhase = 0;
    v.penv = v.penvFrom = v.penvTo = 0;
    v.penvT = v.penvDur = 0;

    // LFOs: key sync (LFO 1 "Key") restarts per note; "On" restarts the tone's
    // free-running LFO when no other note of the tone sounds
    bool toneSounding = false;
    for (int k = 0; k < kVoices; ++k)
        if (&fVoices[k] != &v && fVoices[k].active && fVoices[k].gate && fVoices[k].tone == tone) toneSounding = true;
    const int syncs[3] = { com[C_LFO1_SYNC], com[C_LFO2_SYNC], com[C_LFO3_SYNC] };
    for (int l = 0; l < 3; ++l) {
        v.lfoPhase[l] = 0;
        if (syncs[l] == 1 && !toneSounding) fLfoPhase[tone][l] = 0;
    }

    const double toneFine = (tone ? patch[K_L_FINE] : patch[K_U_FINE]) - 50;
    v.pair.init(st.ring, true);
    for (int k = 0; k < 2; ++k) {
        PartialState& s = v.p[k];
        const uint8_t* pp = partialOf(pd, tone, k);
        s.pp = pp;
        s.on = (mute >> k) & 1;
        s.pcm = st.pcm[k];
        s.playing = s.on;
        s.lastAmp = s.lastCut = 0xFFFFFFFFu;
        s.ampMod = s.cutMod = 0;
        const LA32PartialPair::PairType type = k ? LA32PartialPair::SLAVE : LA32PartialPair::MASTER;
        if (!s.on) {
            v.pair.deactivate(type);
            s.tvaPhase = TVA_DEAD;
            continue;
        }
        // pitch without modulation
        s.keyfollow = kPitchKeyfollowMult[std::min<int>(pp[P_WG_KEYFOLLOW], 16)];
        // PCM waves carry their own natural pitch (the stand-in loops use the
        // D-50's measured loop roots): transpose from it, as the real chip does
        int waveBase = s.pcm ? kPcmC4 : (pp[P_WG_WAVEFORM] ? kSawC4 : kSquareC4);
        if (s.pcm)
            waveBase += fPcm->wave(pp[P_WG_PCM_WAVE]).pitchOffset();
        s.basePitch = waveBase + (pp[P_WG_COARSE] - 36) * 4096 / 12 + (pp[P_WG_FINE] - 50) * 4096 / 1200 +
                      int(toneFine * 4096 / 1200);
        const int keyPitch = int((v.keyTarget * s.keyfollow) / 8192);
        // TVA (Munt calcBasicAmp, D-50 fields; tone level / volume are in the mixer)
        const int pBal = k == 0 ? std::min(100, (100 - balance) * 2) : std::min(100, balance * 2);
        int amp = 155 - tables().levelToAmpSubtraction[pp[P_TVA_LEVEL]] -
                  veloAmpSubtraction(pp[P_TVA_VELO], velocity) -
                  biasAmpSubtraction(pp[P_TVA_BIAS_POINT], pp[P_TVA_BIAS_LEVEL], key) -
                  tables().levelToAmpSubtraction[std::clamp(pBal, 0, 100)];
        amp = std::clamp(amp, 0, 155);
        if (!s.pcm) amp = std::max(0, amp - (pp[P_TVF_RESO] >> 1));
        s.basicAmp = amp;
        const int tkf = pp[P_TVA_TKF];
        s.keyTimeSubA = tkf ? (key - 60) >> (4 - std::min(tkf, 4)) : 0; // D-50: 4 - TKF (TVF: 5 - TKF)
        s.ampRamp.reset();
        if (pp[P_TVA_T1] == 0) tvaStart(s, amp + pp[P_TVA_L1], 0x80 | 127, TVA_ATTACK);
        else tvaStart(s, amp, 0x80 | 127, TVA_BASIC);
        // TVF (synth partials)
        if (!s.pcm) {
            s.baseCutoff = baseCutoffOf(pp, s.basePitch + keyPitch, key);
            int mult = (velocity * pp[P_TVF_ENV_VELO]) >> 6;
            mult += 109 - pp[P_TVF_ENV_VELO];
            mult += (key - 60) >> (4 - std::min<int>(pp[P_TVF_ENV_DKF], 4));
            mult = std::max(mult, 0) * pp[P_TVF_ENV_DEPTH] >> 6;
            s.levelMult = std::min(mult, 255);
            const int ftkf = pp[P_TVF_ENV_TKF];
            s.keyTimeSubF = ftkf ? (key - 60) >> (5 - std::min(ftkf, 4)) : 0;
            s.cutRamp.reset();
            s.tvfTarget = 0;
            int target;
            const int inc = envIncrement((s.levelMult * pp[P_TVF_L1]) >> 8, 0, pp[P_TVF_T1] - s.keyTimeSubF, target);
            tvfStart(s, target, inc, TVF_2 - 1);
            // pulse width (Munt Partial.cpp: velocity around 64)
            s.pw0 = std::clamp((velocity - 64) * (pp[P_WG_PW_VELO] - 7) +
                                   tables().pulseWidth100To255[std::min<int>(pp[P_WG_PW], 100)],
                               0, 255);
            v.pair.initSynth(type, pp[P_WG_WAVEFORM] != 0, Bit8u(s.pw0), Bit8u(pp[P_TVF_RESO] + 1));
        } else {
            const PcmWave& w = fPcm->wave(pp[P_WG_PCM_WAVE]);
            if (w.data.empty()) {
                v.pair.deactivate(type);
                s.on = s.playing = false;
                continue;
            }
            v.pair.initPCM(type, w.data.data(), Bit32u(w.data.size()), w.looped);
        }
    }
    updateVoice(v, false); // the first modulation values before the first sample
}

void Engine::releaseVoice(Voice& v)
{
    v.gate = false;
    v.held = false;
    for (auto& s : v.p) {
        if (!s.on) continue;
        tvaRelease(s);
        if (!s.pcm) tvfRelease(s);
    }
    v.penvPhase = 4; // to the end level
    v.penvFrom = v.penv;
    v.penvT = 0;
    const uint8_t* com = commonOf(fPatch, v.tone);
    v.penvTo = (com[C_PENV_END] - 50) * 4096.0 / 50.0;
    v.penvDur = tvpSeconds(com[C_PENV_T4] * 2);
}

void Engine::releaseNote(int tone, int key)
{
    const uint8_t* patch = fPatch.data() + kPatch;
    const int holdMode = patch[K_HOLD_MODE]; // U, L, UL
    const bool hold = fSustain && (holdMode == 2 || holdMode == tone);
    for (int k = 0; k < kVoices; ++k) {
        Voice& v = fVoices[k];
        if (!v.active || !v.gate || v.tone != tone || v.key != key) continue;
        if (hold) {
            v.gate = false;
            v.held = true;
            continue;
        }
        releaseVoice(v);
    }
}

namespace {
// key modes: which tones a note plays, which tone is solo
void tonesFor(int keyMode, int splitKey, int channel, int key, bool& upper, bool& lower)
{
    switch (keyMode) {
    case 0: case 4: upper = true; lower = false; break;                    // Whole, Whole-S
    case 1: case 5: upper = lower = true; break;                           // Dual, Dual-S
    case 2: case 6: case 7: upper = key >= splitKey; lower = !upper; break; // Split
    default: lower = channel == 1; upper = !lower; break;                  // Separate: channel 2 = Lower
    }
}
bool soloTone(int keyMode, int tone)
{
    switch (keyMode) {
    case 4: case 5: case 6: case 8: return tone == 0;
    case 7: return tone == 1;
    default: return false;
    }
}
} // namespace

void Engine::noteOn(int channel, int note, int velocity)
{
    if (velocity <= 0) { noteOff(channel, note); return; }
    const uint8_t* patch = fPatch.data() + kPatch;
    const int keyMode = std::min<int>(patch[K_KEY_MODE], 8);
    bool up = false, lo = false;
    tonesFor(keyMode, 36 + patch[K_SPLIT_POINT], channel, note, up, lo);
    for (int tone = 0; tone < 2; ++tone) {
        if (!(tone ? lo : up)) continue;
        if (soloTone(keyMode, tone)) {
            // last-note priority: the sounding note ends, the new one starts
            int& n = fNumHeld[tone];
            if (n < 16) fHeld[tone][n++] = note;
            for (int k = 0; k < kVoices; ++k)
                if (fVoices[k].active && fVoices[k].gate && fVoices[k].tone == tone) releaseNote(tone, fVoices[k].key);
        }
        startNote(tone, note, velocity, channel);
    }
}

void Engine::noteOff(int channel, int note)
{
    const uint8_t* patch = fPatch.data() + kPatch;
    const int keyMode = std::min<int>(patch[K_KEY_MODE], 8);
    bool up = false, lo = false;
    tonesFor(keyMode, 36 + patch[K_SPLIT_POINT], channel, note, up, lo);
    for (int tone = 0; tone < 2; ++tone) {
        if (soloTone(keyMode, tone)) {
            int& n = fNumHeld[tone];
            const bool wasLast = n > 0 && fHeld[tone][n - 1] == note; // the one sounding
            int j = 0;
            for (int i = 0; i < n; ++i)
                if (fHeld[tone][i] != note) fHeld[tone][j++] = fHeld[tone][i];
            n = j;
            bool sounding = false;
            for (int k = 0; k < kVoices; ++k)
                if (fVoices[k].active && fVoices[k].gate && fVoices[k].tone == tone && fVoices[k].key == note)
                    sounding = true;
            if (sounding) {
                releaseNote(tone, note);
                if (wasLast && n > 0) startNote(tone, fHeld[tone][n - 1], 100, channel); // back to the held one
            }
            continue;
        }
        if (tone ? lo : up) releaseNote(tone, note);
    }
}

void Engine::pitchBend(int bend) { fBend = std::clamp(bend, -8192, 8191); }
void Engine::modWheel(int value) { fMod = std::clamp(value, 0, 127); }
void Engine::aftertouch(int value) { fAfter = std::clamp(value, 0, 127); }
void Engine::portamento(bool on) { fPorta = on; }

void Engine::sustain(bool on)
{
    fSustain = on;
    if (on) return;
    for (int k = 0; k < kVoices; ++k)
        if (fVoices[k].active && fVoices[k].held) releaseVoice(fVoices[k]);
}

void Engine::allNotesOff()
{
    fSustain = false;
    fNumHeld[0] = fNumHeld[1] = 0;
    for (int k = 0; k < kVoices; ++k) {
        Voice& v = fVoices[k];
        if (!v.active) continue;
        if (v.gate || v.held) releaseVoice(v);
    }
}

void Engine::silence()
{
    for (int k = 0; k < kVoices; ++k) {
        fVoices[k].active = false;
        fVoices[k].pair.deactivate(LA32PartialPair::MASTER);
        fVoices[k].pair.deactivate(LA32PartialPair::SLAVE);
    }
    fNumHeld[0] = fNumHeld[1] = 0;
    fLastKey[0] = fLastKey[1] = -1;
    fSustain = false;
}

int Engine::activeVoices() const
{
    int n = 0;
    for (int k = 0; k < kVoices; ++k) n += fVoices[k].active;
    return n;
}

// ---------------------------------------------------------------------------
// control rate: LFOs, pitch envelope, portamento, modulation

namespace {
float lfoValue(int wave, double phase, float hold)
{
    switch (wave) {
    case 0: return float(phase < 0.5 ? 4 * phase - 1 : 3 - 4 * phase);      // triangle
    case 1: return float(1 - 2 * phase);                                      // saw (down)
    case 2: return phase < 0.5 ? 1.f : -1.f;                                  // square
    default: return hold;                                                     // random
    }
}
} // namespace

void Engine::tick()
{
    const PatchData& pd = fPatch;
    // the tones' free-running LFOs
    for (int tone = 0; tone < 2; ++tone) {
        const uint8_t* com = commonOf(pd, tone);
        const int rates[3] = { com[C_LFO1_RATE], com[C_LFO2_RATE], com[C_LFO3_RATE] };
        for (int l = 0; l < 3; ++l) {
            double& ph = fLfoPhase[tone][l];
            ph += lfoRateHz(rates[l]) * kTickSec;
            if (ph >= 1.0) {
                ph -= std::floor(ph);
                fRandom = fRandom * 1664525u + 1013904223u;
                fLfoHold[tone][l] = float(int32_t(fRandom)) / 2147483648.f;
            }
        }
    }
    for (int k = 0; k < kVoices; ++k)
        if (fVoices[k].active) updateVoice(fVoices[k], true);
}

void Engine::updateVoice(Voice& v, bool advance)
{
    const PatchData& pd = fPatch;
    const uint8_t* patch = pd.data() + kPatch;
    const double bendSemis = double(fBend) / 8192.0 * patch[K_BENDER_RANGE];
    const double afterSemis = double(fAfter) / 127.0 * (int(patch[K_AFTER_BEND]) - 12);
    const double dt = advance ? kTickSec : 0.0;
    {
        const uint8_t* com = commonOf(pd, v.tone);
        v.sinceOn += dt;
        // LFOs (key sync per voice for LFO 1 "Key"), faded in over their delay
        const int waves[3] = { com[C_LFO1_WAVE], com[C_LFO2_WAVE], com[C_LFO3_WAVE] };
        const int rates[3] = { com[C_LFO1_RATE], com[C_LFO2_RATE], com[C_LFO3_RATE] };
        const int delays[3] = { com[C_LFO1_DELAY], com[C_LFO2_DELAY], com[C_LFO3_DELAY] };
        float lfo[3];
        for (int l = 0; l < 3; ++l) {
            const bool keySync = l == 0 && com[C_LFO1_SYNC] == 2;
            double ph;
            float hold;
            if (keySync) {
                v.lfoPhase[l] += lfoRateHz(rates[l]) * dt;
                if (v.lfoPhase[l] >= 1.0) {
                    v.lfoPhase[l] -= std::floor(v.lfoPhase[l]);
                    fRandom = fRandom * 1664525u + 1013904223u;
                    v.lfoHold[l] = float(int32_t(fRandom)) / 2147483648.f;
                }
                ph = v.lfoPhase[l];
                hold = v.lfoHold[l];
            } else {
                ph = fLfoPhase[v.tone][l];
                hold = fLfoHold[v.tone][l];
            }
            const float d = lfoDelaySec(delays[l]);
            const float fade = d <= 0.f ? 1.f : float(std::min(1.0, v.sinceOn / d));
            lfo[l] = lfoValue(waves[l], ph, hold) * fade;
        }
        // pitch envelope: L0 -> T1 L1 -> T2 L2 -> T3 sustain; T4 end level after the release
        {
            const double scale = 4096.0 / 50.0; // +-50 = +-1 octave
            const int tkf = com[C_PENV_TKF];
            const int keySub = tkf ? (v.key - 60) >> (5 - std::min(tkf, 4)) : 0;
            if (v.penvPhase < 3 && v.penvT >= v.penvDur) {
                // next segment
                const int levels[4] = { com[C_PENV_L0], com[C_PENV_L1], com[C_PENV_L2], com[C_PENV_SUS] };
                const int times[3] = { com[C_PENV_T1], com[C_PENV_T2], com[C_PENV_T3] };
                if (v.penvPhase == 0 && v.penvDur == 0 && v.penvT == 0) v.penv = (levels[0] - 50) * scale;
                v.penvFrom = v.penv;
                v.penvTo = (levels[v.penvPhase + 1] - 50) * scale;
                v.penvDur = tvpSeconds(std::max(0, times[v.penvPhase] * 2 - keySub));
                v.penvT = 0;
                ++v.penvPhase;
            }
            v.penvT += dt;
            const double f = v.penvDur <= 0 ? 1.0 : std::min(1.0, v.penvT / v.penvDur);
            v.penv = v.penvFrom + (v.penvTo - v.penvFrom) * f;
        }
        // portamento
        if (advance) v.keyPitch += (v.keyTarget - v.keyPitch) * v.glideK;
        const int velo = com[C_PENV_VELO];
        const double penv = v.penv * (velo == 0 ? 1.0 : std::pow(v.velocity / 127.0, velo));
        const double pmodDepth = (com[C_PMOD_LFO] + fMod * com[C_PMOD_LEVER] / 127.0 + fAfter * com[C_PMOD_AFTER] / 127.0);
        const double pmodAL = (fMod * com[C_PMOD_LEVER] / 127.0 + fAfter * com[C_PMOD_AFTER] / 127.0);
        for (int pi = 0; pi < 2; ++pi) {
            PartialState& s = v.p[pi];
            if (!s.on || !s.playing) continue;
            const uint8_t* pp = s.pp;
            const LA32PartialPair::PairType type = pi ? LA32PartialPair::SLAVE : LA32PartialPair::MASTER;
            // pitch: key (key follow) + base + P-ENV + LFO 1 + bender + aftertouch bend
            double pitch = s.basePitch + v.keyPitch * s.keyfollow / 8192.0;
            const int penvMode = pp[P_WG_PENV_MODE];
            if (penvMode == 1) pitch += penv;
            else if (penvMode == 2) pitch -= penv;
            const int lfoMode = pp[P_WG_LFO_MODE];
            const double vib = lfo[0] * 512.0 / 100.0; // depth 100 = +-1.5 semitones
            if (lfoMode == 1) pitch += vib * pmodDepth;
            else if (lfoMode == 2) pitch -= vib * pmodDepth;
            else if (lfoMode == 3) pitch += vib * pmodAL;
            const int bendMode = pp[P_WG_BEND_MODE];
            if (bendMode) {
                double b = (bendSemis + afterSemis) * 4096.0 / 12.0;
                if (bendMode == 1) b = b * s.keyfollow / 8192.0;
                pitch += b;
            }
            v.pair.setPitch(type, Bit16u(std::clamp(int(std::lround(pitch)), 0, 59392)));
            auto lfoSel = [&](int sel) { return (sel & 1 ? -1.f : 1.f) * lfo[std::min(sel >> 1, 2)]; };
            // TVA: LFO and aftertouch, in amp units (1 = 0.38 dB)
            const double tremolo = lfoSel(pp[P_TVA_LFO]) * pp[P_TVA_LFO_DEPTH] * 0.16 +
                                   fAfter / 127.0 * (int(pp[P_TVA_AFTER]) - 7) * 2.0;
            s.ampMod = int(std::lround(tremolo * double(1 << 18)));
            if (!s.pcm) {
                const double cut = lfoSel(pp[P_TVF_LFO]) * pp[P_TVF_LFO_DEPTH] * 0.4 +
                                   fAfter / 127.0 * (int(pp[P_TVF_AFTER]) - 7) * 4.0;
                s.cutMod = int(std::lround(cut * double(1 << 18)));
                const int pw = std::clamp(int(s.pw0 + lfoSel(pp[P_WG_PW_LFO]) * pp[P_WG_PW_LFO_DEPTH] * 1.27 +
                                              fAfter / 127.0 * (int(pp[P_WG_PW_AFTER]) - 7) * 18.0),
                                          0, 255);
                v.pair.setPulseWidth(type, Bit8u(pw));
                s.lastCut = 0xFFFFFFFFu; // takes effect with the next setCutoff
            }
        }
    }
}

// ---------------------------------------------------------------------------
// audio

void Engine::process(float* upper, float* lower, int frames)
{
    for (int i = 0; i < frames; ++i) {
        if (fTickPos == 0) tick();
        fTickPos = (fTickPos + 1) % kTick;
        float out[2] = { 0.f, 0.f };
        for (int k = 0; k < kVoices; ++k) {
            Voice& v = fVoices[k];
            if (!v.active) continue;
            const bool canSustain = v.gate || v.held;
            bool any = false;
            for (int pi = 0; pi < 2; ++pi) {
                PartialState& s = v.p[pi];
                if (!s.on || !s.playing) continue;
                const LA32PartialPair::PairType type = pi ? LA32PartialPair::SLAVE : LA32PartialPair::MASTER;
                if (!v.pair.isActive(type)) { // a one-shot PCM wave ended
                    s.playing = false;
                    continue;
                }
                const Bit32u ramp = s.ampRamp.nextValue();
                if (s.ampRamp.checkInterrupt()) tvaNextPhase(s, canSustain, v.velocity);
                if (!s.playing) {
                    v.pair.deactivate(type);
                    continue;
                }
                any = true;
                int64_t amp = int64_t(67117056) - int64_t(ramp) - s.ampMod;
                amp = std::clamp<int64_t>(amp, 0, 67117056);
                if (Bit32u(amp) != s.lastAmp) {
                    s.lastAmp = Bit32u(amp);
                    v.pair.setAmp(type, Bit32u(amp));
                }
                if (!s.pcm) {
                    const Bit32u mod = s.cutRamp.nextValue();
                    if (s.cutRamp.checkInterrupt()) tvfNextPhase(s, canSustain);
                    int64_t cut = (int64_t(s.baseCutoff) << 18) + mod + s.cutMod;
                    cut = std::clamp<int64_t>(cut, 0, kMaxCutoff);
                    if (Bit32u(cut) != s.lastCut) {
                        s.lastCut = Bit32u(cut);
                        v.pair.setCutoff(type, Bit32u(cut));
                    }
                }
                v.pair.generateNextSample(type);
            }
            if (!any) {
                v.active = false;
                continue;
            }
            out[v.tone] += float(v.pair.nextOutSample()) * (1.f / 32768.f);
        }
        upper[i] = out[0];
        lower[i] = out[1];
    }
}

} // namespace la50
