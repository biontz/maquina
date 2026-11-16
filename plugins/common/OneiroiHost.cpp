// OneiroiHost.cpp - see OneiroiHost.hpp. Compiled after the vendored engine
// (engine/oneiroi/oneiroi_unity.cpp) in one translation unit: plugins/oneiroi*/Engine.cpp.
// SPDX-License-Identifier: GPL-3.0-only
#include "OneiroiHost.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "OneiroiControls.hpp"

namespace maquina {

namespace {

// Maquina envelope (OneiroiControls.hpp) per engine sample at 48 kHz
float attackStep(int v) // 0 = instant
{
    const double t = oneiroi::attackSeconds(v);
    return t <= 0.0 ? 1.f : float(1.0 / (t * OneiroiHost::kEngineRate));
}
float fallCoef(int v) // -60 dB after the time
{
    return float(std::pow(0.001, 1.0 / (oneiroi::fallSeconds(v) * OneiroiHost::kEngineRate)));
}

enum class Target : uint8_t { Knob, Fader, FaderExpo, Tune, Unison, Select4, Switch };

// one module control fed from a plugin parameter: the knob/fader value
// (0..1, smoothed per block with the module's coefficient) goes to dst
struct Control {
    const char* symbol;
    Target target;
    float coeff; // KnobController / FaderController lpCoeff (Ui.h)
};

// Ui.h's controllers: knob -> PatchCtrls (main and SHIFT value share the
// knob's smoothing), faders with lpCoeff 0.02
const Control kControls[] = {
    { "loop_speed", Target::Knob, 0.005f },     { "loop_start", Target::Knob, 0.005f },
    { "loop_length", Target::Knob, 0.005f },    { "loop_sos", Target::Knob, 0.005f },
    { "loop_filter", Target::Knob, 0.005f },    { "osc_pitch", Target::Tune, 0.995f },
    { "osc_detune", Target::Knob, 0.1f },       { "osc_unison", Target::Unison, 0.1f },
    { "osc2_mode", Target::Switch, 0.f },       { "flt_cutoff", Target::Knob, 0.01f },
    { "flt_reso", Target::Knob, 0.01f },        { "flt_mode", Target::Select4, 0.f },
    { "flt_pos", Target::Select4, 0.f },        { "res_tune", Target::Knob, 0.005f },
    { "res_feedback", Target::Knob, 0.01f },    { "res_dissonance", Target::Knob, 0.005f },
    { "echo_density", Target::Knob, 0.005f },   { "echo_repeats", Target::Knob, 0.01f },
    { "echo_filter", Target::Knob, 0.005f },    { "amb_spacetime", Target::Knob, 0.005f },
    { "amb_decay", Target::Knob, 0.01f },       { "amb_autopan", Target::Knob, 0.005f },
    { "mod_level", Target::Knob, 0.01f },       { "mod_speed", Target::Knob, 0.01f },
    { "mod_type", Target::Knob, 0.01f },        { "vol_input", Target::FaderExpo, 0.02f },
    { "vol_looper", Target::FaderExpo, 0.02f }, { "vol_sine", Target::FaderExpo, 0.02f },
    { "vol_osc2", Target::FaderExpo, 0.02f },   { "mix_filter", Target::Fader, 0.02f },
    { "mix_resonator", Target::Fader, 0.02f },  { "mix_echo", Target::Fader, 0.02f },
    { "mix_ambience", Target::Fader, 0.02f },
};
constexpr int kNumControls = int(sizeof(kControls) / sizeof(kControls[0]));

} // namespace

// ---------------------------------------------------------------------------
// the module's objects, as Oneiroi_1_2_2Patch.hpp creates them

struct OneiroiHost::Engine {
    OneiroiRng rng;
    PatchCtrls ctrls {};
    PatchCvs cvs {};
    PatchState state {};
    OneiroiCore* core = nullptr;
    Clock* clock = nullptr;
    AudioBuffer* buffer = nullptr;
    bool withOscillators = true;

    // Ui.h's controller state
    int param[kNumControls];
    float* dst[kNumControls] = {};
    float value[kNumControls] = {}; // smoothed control position 0..1
    bool first = true;
    float tune = 0.5f, unison = 0.55f, inputVol = 0.f, looperVol = 0.f, osc1Vol = 0.f, osc2Vol = 0.f;
    float noteCv = 0.f, notePot = 0.f;
    float gain[kBlock];

    Engine(uint32_t seed, bool fx)
    {
        rng.seed(seed);
        OneiroiRng::Scope scope(rng);
        withOscillators = !fx;
        state.sampleRate = float(kEngineRate);
        state.blockRate = float(kEngineRate) / kBlock;
        state.blockSize = kBlock;
        // Ui's constructor and LoadConfig() without a saved configuration
        state.funcMode = FuncMode::FUNC_MODE_NONE;
        state.inputLevel = FloatArray::create(kBlock);
        state.efModLevel = FloatArray::create(kBlock);
        state.outLevel = 1.f;
        state.randomSlew = kRandomSlewSamples;
        state.randomHasSlew = false;
        state.softTakeover = false;
        state.modAttenuverters = false;
        state.cvAttenuverters = false;
        state.c2 = 1102.f / 4095.f;
        state.c5 = 2334.f / 4095.f;
        state.pitchZero = 2128.f / 4041.f;
        state.speedZero = oneiroi::kSpeedZero;
        // Ui's first Poll() ends the startup phase before the first Process()
        state.startupPhase = StartupPhase::STARTUP_DONE;
        // the module's default modulation mapping (cutoff only) and CV amounts
        ctrls.filterCutoffModAmount = 0.5f;
        ctrls.looperSpeedCvAmount = ctrls.looperStartCvAmount = ctrls.looperLengthCvAmount = 1.f;
        ctrls.oscPitchCvAmount = ctrls.oscDetuneCvAmount = ctrls.filterCutoffCvAmount = 1.f;
        ctrls.resonatorTuneCvAmount = ctrls.echoDensityCvAmount = ctrls.ambienceSpacetimeCvAmount = 1.f;
        // the patch's constructor order: Ui, Oneiroi, Clock
        core = OneiroiCore::create(&ctrls, &cvs, &state);
        clock = Clock::create(&ctrls, &state);
        buffer = AudioBuffer::create(2, kBlock);
    }
    ~Engine()
    {
        OneiroiCore::destroy(core);
        Clock::destroy(clock);
        AudioBuffer::destroy(buffer);
        TapTempo::destroy(state.tempo);
        FloatArray::destroy(state.inputLevel);
        FloatArray::destroy(state.efModLevel);
    }
};

// ---------------------------------------------------------------------------

static int findSymbol(const VoiceDef& d, const char* symbol)
{
    for (uint32_t i = 0; i < d.numParams; ++i)
        if (!std::strcmp(d.params[i].symbol, symbol)) return int(i);
    return -1;
}

OneiroiHost::OneiroiHost(const VoiceDef& def, uint32_t seed)
    : fDef(def),
      fDefaults(def.numParams, 0.f),
      fValues(def.numParams),
      fEngine(new Engine(seed, def.voice == kOneiroiFxVoice)),
      fFifoL(kFifoFrames, 0.f),
      fFifoR(kFifoFrames, 0.f)
{
    for (uint32_t i = 0; i < def.numParams; ++i) {
        fDefaults[i] = float(def.params[i].def);
        fValues[i].store(fDefaults[i], std::memory_order_relaxed);
    }
    Engine& e = *fEngine;
    PatchCtrls& c = e.ctrls;
    for (int k = 0; k < kNumControls; ++k) {
        e.param[k] = -1;
        for (uint32_t i = 0; i < def.numParams; ++i)
            if (!std::strcmp(def.params[i].symbol, kControls[k].symbol)) e.param[k] = int(i);
        const char* s = kControls[k].symbol;
        float* d = nullptr;
        if (!std::strcmp(s, "loop_speed")) d = &c.looperSpeed;
        else if (!std::strcmp(s, "loop_start")) d = &c.looperStart;
        else if (!std::strcmp(s, "loop_length")) d = &c.looperLength;
        else if (!std::strcmp(s, "loop_sos")) d = &c.looperSos;
        else if (!std::strcmp(s, "loop_filter")) d = &c.looperFilter;
        else if (!std::strcmp(s, "osc_pitch")) d = &e.tune;
        else if (!std::strcmp(s, "osc_detune")) d = &c.oscDetune;
        else if (!std::strcmp(s, "osc_unison")) d = &e.unison;
        else if (!std::strcmp(s, "osc2_mode")) d = &c.oscUseWavetable;
        else if (!std::strcmp(s, "flt_cutoff")) d = &c.filterCutoff;
        else if (!std::strcmp(s, "flt_reso")) d = &c.filterResonance;
        else if (!std::strcmp(s, "flt_mode")) d = &c.filterMode;
        else if (!std::strcmp(s, "flt_pos")) d = &c.filterPosition;
        else if (!std::strcmp(s, "res_tune")) d = &c.resonatorTune;
        else if (!std::strcmp(s, "res_feedback")) d = &c.resonatorFeedback;
        else if (!std::strcmp(s, "res_dissonance")) d = &c.resonatorDissonance;
        else if (!std::strcmp(s, "echo_density")) d = &c.echoDensity;
        else if (!std::strcmp(s, "echo_repeats")) d = &c.echoRepeats;
        else if (!std::strcmp(s, "echo_filter")) d = &c.echoFilter;
        else if (!std::strcmp(s, "amb_spacetime")) d = &c.ambienceSpacetime;
        else if (!std::strcmp(s, "amb_decay")) d = &c.ambienceDecay;
        else if (!std::strcmp(s, "amb_autopan")) d = &c.ambienceAutoPan;
        else if (!std::strcmp(s, "mod_level")) d = &c.modLevel;
        else if (!std::strcmp(s, "mod_speed")) d = &c.modSpeed;
        else if (!std::strcmp(s, "mod_type")) d = &c.modType;
        else if (!std::strcmp(s, "vol_input")) d = &e.inputVol;
        else if (!std::strcmp(s, "vol_looper")) d = &e.looperVol;
        else if (!std::strcmp(s, "vol_sine")) d = &e.osc1Vol;
        else if (!std::strcmp(s, "vol_osc2")) d = &e.osc2Vol;
        else if (!std::strcmp(s, "mix_filter")) d = &c.filterVol;
        else if (!std::strcmp(s, "mix_resonator")) d = &c.resonatorVol;
        else if (!std::strcmp(s, "mix_echo")) d = &c.echoVol;
        else if (!std::strcmp(s, "mix_ambience")) d = &c.ambienceVol;
        e.dst[k] = d;
    }
    fIxOctave = findSymbol(def, "osc_octave");
    fIxRecord = findSymbol(def, "loop_record");
    fIxSource = findSymbol(def, "loop_source");
    fIxClear = findSymbol(def, "loop_clear");
    fIxDrone = findSymbol(def, "env_drone");
    fIxAttack = findSymbol(def, "env_attack");
    fIxDecay = findSymbol(def, "env_decay");
    fIxSustain = findSymbol(def, "env_sustain");
    fIxRelease = findSymbol(def, "env_release");
    fIxSync = findSymbol(def, "sync");
    // controls this plugin does not have stay at the module's neutral position
    e.unison = oneiroi::kCentre;
    c.looperFilter = c.echoFilter = 0.55f; // Ui.h: "Center is not 0.5"
    setSampleRate(fHostRate);
}

OneiroiHost::~OneiroiHost() = default;

void OneiroiHost::setSampleRate(double hostRate)
{
    fHostRate = hostRate > 0.0 ? hostRate : 48000.0;
    fIn.setup(fHostRate, kEngineRate);
    fOut.setup(kEngineRate, fHostRate);
    // worst-case lag of the pipeline in host frames: input filter (kHalf host
    // frames), filling a 32-frame engine block, output filter (kHalf engine
    // frames), plus rounding. The output FIFO starts with that much silence.
    const double toHost = fHostRate / kEngineRate;
    const double lag = (fIn.bypass() ? 0.0 : StreamResampler::kHalf) +
                       (kBlock + (fOut.bypass() ? 0.0 : StreamResampler::kHalf)) * toHost;
    fLatency = uint32_t(std::ceil(lag)) + 2;
    resetTiming();
}

void OneiroiHost::resetTiming()
{
    fIn.reset();
    fOut.reset();
    fBlockFill = 0;
    fEngineFrame = 0;
    std::fill(fFifoL.begin(), fFifoL.end(), 0.f);
    std::fill(fFifoR.begin(), fFifoR.end(), 0.f);
    fFifoRead = 0;
    fFifoCount = fLatency;
    fEventHead = fEventCount = 0;
    fHostFrame = 0;
    fBeatsPerFrame = 0.0;
}

bool OneiroiHost::clockExternal() const
{
    return fEngine->state.clockSource == ClockSource::CLOCK_SOURCE_EXTERNAL;
}

double OneiroiHost::clockPeriodSeconds() const
{
    return double(fEngine->state.tempo->getPeriodInSamples()) / double(fEngine->state.blockRate);
}

// ---------------------------------------------------------------------------
// parameters

void OneiroiHost::setParam(uint32_t index, float value)
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

float OneiroiHost::getParam(uint32_t index) const
{
    return index < fDef.numParams ? fValues[index].load(std::memory_order_relaxed) : 0.f;
}

float OneiroiHost::defaultParam(uint32_t index) const
{
    return index < fDef.numParams ? fDefaults[index] : 0.f;
}

void OneiroiHost::resetToDefaults()
{
    for (uint32_t i = 0; i < fDef.numParams; ++i) fValues[i].store(fDefaults[i], std::memory_order_relaxed);
}

int OneiroiHost::paramInt(int index) const
{
    return index < 0 ? 0 : int(std::lround(fValues[uint32_t(index)].load(std::memory_order_relaxed)));
}

// ---------------------------------------------------------------------------
// events

void OneiroiHost::pushEvent(double frame, EventKind kind, uint8_t note)
{
    if (fEventCount >= kMaxEvents) return;
    // keep the queue ordered (notes and sync pulses arrive separately)
    uint32_t pos = fEventCount;
    while (pos > 0 && fEvents[(fEventHead + pos - 1) % kMaxEvents].frame > frame) {
        fEvents[(fEventHead + pos) % kMaxEvents] = fEvents[(fEventHead + pos - 1) % kMaxEvents];
        --pos;
    }
    fEvents[(fEventHead + pos) % kMaxEvents] = { frame, kind, note };
    ++fEventCount;
}

void OneiroiHost::noteOn(uint32_t frameOffset, uint8_t note, uint8_t velocity)
{
    if (isFx() || note > 127) return;
    if (velocity == 0) {
        noteOff(frameOffset, note);
        return;
    }
    pushEvent(double(fHostFrame + frameOffset) * kEngineRate / fHostRate, EventKind::NoteOn, note);
}

void OneiroiHost::noteOff(uint32_t frameOffset, uint8_t note)
{
    if (isFx() || note > 127) return;
    pushEvent(double(fHostFrame + frameOffset) * kEngineRate / fHostRate, EventKind::NoteOff, note);
}

void OneiroiHost::setTransport(bool playing, double beat, double beatsPerBar, double beatsPerFrame)
{
    fPlaying = playing && beatsPerFrame > 0.0;
    fBeat = beat;
    fBeatsPerBar = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    fBeatsPerFrame = beatsPerFrame;
}

void OneiroiHost::applyEvent(const Event& ev)
{
    switch (ev.kind) {
    case EventKind::NoteOn:
        if (fHeld[ev.note]) { // repeated note-on: move it to the top
            int k = 0;
            while (k < fStackSize && fStack[k] != ev.note) ++k;
            for (; k + 1 < fStackSize; ++k) fStack[k] = fStack[k + 1];
            --fStackSize;
        }
        fHeld[ev.note] = 1;
        fStack[fStackSize++] = ev.note;
        fNote = ev.note;
        fGate = true;
        fEnv.stage = Envelope::Attack; // from the current level: no click
        break;
    case EventKind::NoteOff:
        if (!fHeld[ev.note]) break;
        fHeld[ev.note] = 0;
        for (int k = 0; k < fStackSize; ++k)
            if (fStack[k] == ev.note) {
                for (; k + 1 < fStackSize; ++k) fStack[k] = fStack[k + 1];
                --fStackSize;
                break;
            }
        if (fStackSize > 0) {
            fNote = fStack[fStackSize - 1]; // back to the previous note, legato
        } else {
            fGate = false;
        }
        break;
    case EventKind::Sync: break; // handled per block (runBlock)
    }
}

// ---------------------------------------------------------------------------
// the module's Ui::Poll(), for the parameters

void OneiroiHost::poll()
{
    Engine& e = *fEngine;
    PatchCtrls& c = e.ctrls;
    PatchState& s = e.state;

    for (int k = 0; k < kNumControls; ++k) {
        if (!e.dst[k] || e.param[k] < 0) continue;
        const VoiceParam& p = fDef.params[e.param[k]];
        const int v = paramInt(e.param[k]);
        switch (kControls[k].target) {
        case Target::Select4: *e.dst[k] = 0.125f + 0.25f * float(std::clamp(v, 0, 3)); continue;
        case Target::Switch: *e.dst[k] = v ? 1.f : 0.f; continue;
        default: break;
        }
        const float target = oneiroi::toKnob(p, float(v));
        float& val = e.value[k];
        if (e.first) val = target;
        else ONE_POLE(val, target, kControls[k].coeff);
        *e.dst[k] = val;
    }
    e.first = false;

    s.modActive = c.modLevel > 0.1f;

    // OCTAVE: the module quantizes the SHIFT position to 1..9; here the
    // parameter is the octave directly (0 = 5: note 60 plays C4)
    c.oscOctave = float(5 + std::clamp(paramInt(fIxOctave), -4, 4));

    // V/Oct: the MIDI note relative to C4, with the module's glide
    const float note = float(fNote - 60);
    const float interval = note - e.noteCv;
    if (interval < -0.4f || interval > 0.4f) e.noteCv = note;
    else e.noteCv += 0.1f * interval;

    float n = Modulate(e.tune - 0.5f, c.oscPitchModAmount, s.modValue, 0, 0, -1.f, 1.f, s.modAttenuverters);
    n = 12 * n + 12 * c.oscOctave;
    e.notePot += 0.1f * (n - e.notePot);
    c.oscPitch = M2F(e.notePot + e.noteCv);

    c.oscUnison = Clamp(CenterMap(e.unison), -1.f, 1.f);
    if (c.oscUnison >= -0.03f && c.oscUnison <= 0.03f) {
        c.oscUnison = 0.f;
        s.oscUnisonCenterFlag = true;
    } else {
        s.oscUnisonCenterFlag = false;
    }

    c.inputVol = MapExpo(e.inputVol);
    c.looperVol = MapExpo(e.looperVol);
    c.osc1Vol = MapExpo(e.osc1Vol);
    c.osc2Vol = MapExpo(e.osc2Vol);
    if (c.filterVol >= kOne) c.filterVol = 1.f;
    if (c.resonatorVol >= kOne) c.resonatorVol = 1.f;
    if (c.echoVol >= kOne) c.echoVol = 1.f;
    if (c.ambienceVol >= kOne) c.ambienceVol = 1.f;

    // RECORD (button or gate), PRE/POST, and SHIFT + RECORD = clear
    c.looperRecording = paramInt(fIxRecord) ? 1.f : 0.f;
    c.looperResampling = isFx() ? float(paramInt(fIxSource) != 0) : 1.f;
    const bool clear = paramInt(fIxClear) != 0;
    if (clear && !fLastClear) s.clearLooperFlag = true;
    fLastClear = clear;
}

// ---------------------------------------------------------------------------
// audio

void OneiroiHost::runBlock()
{
    Engine& e = *fEngine;

    // SYNC IN: a pulse in this block raises the input (the module's button
    // callback, before the block), held 2 blocks
    bool syncNow = false;
    int syncOffset = 0;
    // envelope + notes, per sample
    const bool drone = !isFx() && paramInt(fIxDrone) != 0;
    const float aStep = attackStep(paramInt(fIxAttack));
    const float dCoef = fallCoef(paramInt(fIxDecay));
    const float sLevel = float(oneiroi::sustainLevel(paramInt(fIxSustain)));
    const float rCoef = fallCoef(paramInt(fIxRelease));
    bool unity = true; // every gain 1: the module's own path
    for (int i = 0; i < kBlock; ++i) {
        const double end = double(fEngineFrame + i) + 1.0;
        while (fEventCount > 0 && fEvents[fEventHead].frame < end) {
            const Event& ev = fEvents[fEventHead];
            if (ev.kind == EventKind::Sync) {
                if (!syncNow) syncOffset = i;
                syncNow = true;
            } else {
                applyEvent(ev);
            }
            fEventHead = (fEventHead + 1) % kMaxEvents;
            --fEventCount;
        }
        if (isFx()) continue;
        Envelope& env = fEnv;
        const bool gate = fGate || drone;
        if (!gate && env.stage != Envelope::Idle && env.stage != Envelope::Release) env.stage = Envelope::Release;
        if (gate && (env.stage == Envelope::Idle || env.stage == Envelope::Release)) env.stage = Envelope::Attack;
        const float target = drone ? 1.f : sLevel;
        switch (env.stage) {
        case Envelope::Idle: env.value = 0.f; break;
        case Envelope::Attack:
            env.value += aStep;
            if (env.value >= 1.f) {
                env.value = 1.f;
                env.stage = Envelope::Decay;
            }
            break;
        case Envelope::Decay:
            env.value = target + (env.value - target) * (drone ? 0.f : dCoef);
            if (std::fabs(env.value - target) < 1e-5f) env.stage = Envelope::Sustain;
            break;
        case Envelope::Sustain: env.value = target; break;
        case Envelope::Release:
            env.value *= rCoef;
            if (env.value < 1e-5f) {
                env.value = 0.f;
                env.stage = Envelope::Idle;
            }
            break;
        }
        e.gain[i] = env.value;
        unity = unity && env.value == 1.f;
    }

    if (syncNow) {
        e.state.tempo->trigger(true, syncOffset);
        e.state.syncIn = true;
        fSyncHigh = 2;
    } else if (fSyncHigh > 0 && --fSyncHigh == 0) {
        e.state.tempo->trigger(false, 0);
        e.state.syncIn = false;
    }

    // Oneiroi_1_2_2Patch::processAudio(): clock, UI poll, engine
    FloatArray l = e.buffer->getSamples(LEFT_CHANNEL), r = e.buffer->getSamples(RIGHT_CHANNEL);
    for (int i = 0; i < kBlock; ++i) {
        l[i] = fBlockL[i];
        r[i] = fBlockR[i];
    }
    e.clock->Process();
    poll();
    e.core->Process(*e.buffer, isFx() || unity ? nullptr : e.gain, e.withOscillators);

    for (int i = 0; i < kBlock; ++i)
        fOut.push(l[i], r[i], [this](float ol, float orr) {
            const uint32_t w = (fFifoRead + fFifoCount) % kFifoFrames;
            if (fFifoCount < kFifoFrames) {
                fFifoL[w] = ol;
                fFifoR[w] = orr;
                ++fFifoCount;
            }
        });
    fEngineFrame += kBlock;
}

void OneiroiHost::engineFrame(float l, float r)
{
    fBlockL[fBlockFill] = l;
    fBlockR[fBlockFill] = r;
    if (++fBlockFill == kBlock) {
        runBlock();
        fBlockFill = 0;
    }
}

void OneiroiHost::process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames)
{
    OneiroiRng::Scope scope(fEngine->rng);

    // SYNC: pulses at the host's beat / bar / 4-bar lines in this block
    const int syncMode = paramInt(fIxSync);
    if (syncMode > 0 && fPlaying && fBeatsPerFrame > 0.0) {
        const double unit = syncMode == 1 ? 1.0 : syncMode == 2 ? fBeatsPerBar : 4.0 * fBeatsPerBar;
        const double endBeat = fBeat + fBeatsPerFrame * frames;
        for (double b = std::ceil(fBeat / unit - 1e-9) * unit; b < endBeat; b += unit) {
            const double f = std::max(0.0, (b - fBeat) / fBeatsPerFrame);
            pushEvent(double(fHostFrame + f) * kEngineRate / fHostRate, EventKind::Sync, 0);
        }
    }

    for (uint32_t done = 0; done < frames;) {
        const uint32_t n = std::min(frames - done, kChunk);
        for (uint32_t i = 0; i < n; ++i) {
            const float l = inL ? inL[done + i] : 0.f;
            const float r = inR ? inR[done + i] : (inL ? inL[done + i] : 0.f);
            fIn.push(l, r, [this](float el, float er) { engineFrame(el, er); });
        }
        for (uint32_t i = 0; i < n; ++i) {
            if (fFifoCount > 0) {
                outL[done + i] = fFifoL[fFifoRead];
                outR[done + i] = fFifoR[fFifoRead];
                fFifoRead = (fFifoRead + 1) % kFifoFrames;
                --fFifoCount;
            } else {
                outL[done + i] = outR[done + i] = 0.f;
                ++fUnderruns;
            }
        }
        done += n;
    }
    fHostFrame += frames;
}

} // namespace maquina
