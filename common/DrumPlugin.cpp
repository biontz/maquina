// DrumPlugin.cpp - the DPF plugin shared by all voice plugins.
// Each plugin directory compiles this with its own DistrhoPluginInfo.h and
// MAQUINA_VOICE_INDEX (index into k_voices).
//
// SPDX-License-Identifier: GPL-3.0-only
#include "DistrhoPlugin.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#if MAQUINA_DREDRUM
#include "DredrumHost.hpp"
#elif MAQUINA_PLETHORA
#include "PlethoraHost.hpp"
#elif MAQUINA_ONEIROI
#include "OneiroiHost.hpp"
#elif MAQUINA_KLANG
#include "KlangHost.hpp"
#elif MAQUINA_KLONG
#include "KlongHost.hpp"
#else
#include "EngineHost.hpp"
#endif
#include "ExtraParams.hpp"
#include "Randomizer.hpp"
#include "FactoryPresets.generated.hpp"
#include "VoiceTables.generated.hpp"

#ifndef MAQUINA_VOICE_INDEX
#error "MAQUINA_VOICE_INDEX must be defined"
#endif

START_NAMESPACE_DISTRHO

#if MAQUINA_DREDRUM
using Engine = maquina::DredrumHost; // Maquina Dredrum: the Dredrum firmware engine
#elif MAQUINA_PLETHORA
using Engine = maquina::PlethoraHost; // Maquina Plethora: the Noise Plethora (VCV port)
#elif MAQUINA_ONEIROI
using Engine = maquina::OneiroiHost; // Maquina Oneiroi / Oneiroi FX: the Befaco Oneiroi
#elif MAQUINA_KLANG
using Engine = maquina::KlangHost; // Maquina Klang: the Mutable Instruments Plaits
#elif MAQUINA_KLONG
using Engine = maquina::KlongHost; // Maquina Klong / Klong FX: the Mutable Instruments Rings
#else
using Engine = maquina::EngineHost;  // the LXR-02 firmware engine
#endif
using maquina::ParamKind;
using maquina::VoiceDef;
using maquina::VoiceParam;
using maquina::paramAt;

static const VoiceDef& voiceDef() { return maquina::k_voices[MAQUINA_VOICE_INDEX]; }
static const maquina::VoicePresets& factory() { return maquina::k_factory_presets[MAQUINA_VOICE_INDEX]; }

static bool hasChannelsVoice() { return maquina::hasChannels(voiceDef().voice); }
// port group of the extra parameters: the LXR mix page, Dredrum's Mode /
// Plethora's Global page
static uint8_t extraPage() { return hasChannelsVoice() ? 3 : 7; }

// DPF wants port group ids 0..N-1 without gaps; the snare has no FM page.
struct PageGroups {
    static constexpr uint8_t kMaxPages = 16;
    int8_t groupOfPage[kMaxPages];
    uint8_t pageOfGroup[kMaxPages];
    uint8_t count = 0;
    PageGroups()
    {
        bool used[kMaxPages] = {};
        for (uint32_t i = 0; i < voiceDef().numParams; ++i)
            used[voiceDef().params[i].page % kMaxPages] = true;
        used[extraPage()] = true; // the extra parameters (ExtraParams.hpp)
        for (uint8_t pg = 0; pg < kMaxPages; ++pg) {
            groupOfPage[pg] = used[pg] ? int8_t(count) : -1;
            if (used[pg])
                pageOfGroup[count++] = pg;
        }
    }
};
static const PageGroups& pageGroups()
{
    static const PageGroups groups;
    return groups;
}

static const char* const kPageNames[8] = { "Oscillator", "Amp EG", "Modulation", "FM",
                                           "Transient", "Filter", "LFO", "Mix" };
static const char* const kPageSymbols[8] = { "osc", "amp", "mod", "fm",
                                             "transient", "filter", "lfo", "mix" };
// Maquina Dredrum / Plethora pages (tools/gen_params.py *_PAGE_TITLES)
static const char* const kDredrumPageNames[4] = { "Osc 1", "Osc 2", "Osc 3", "Mode" };
static const char* const kDredrumPageSymbols[4] = { "osc1", "osc2", "osc3", "mode" };
static const char* const kPlethoraPageNames[4] = { "Channel A", "Channel B", "Channel C", "Global" };
static const char* const kPlethoraPageSymbols[4] = { "chan_a", "chan_b", "chan_c", "global" };
// Maquina Oneiroi pages: the module's sections (tools/gen_params.py ONEIROI_PAGE_TITLES)
static const char* const kOneiroiPageNames[10] = { "Looper", "Oscillators", "Filter", "Resonator", "Echo",
                                                   "Ambience", "Modulation", "Mix", "Envelope", "Global" };
static const char* const kOneiroiPageSymbols[10] = { "looper", "osc", "filter", "resonator", "echo",
                                                     "ambience", "mod", "mix", "env", "global" };
// Maquina Klang / Klong / Klong FX pages (tools/gen_params.py MUTABLE_PAGE_TITLES;
// page 1 is Klang's SOUND, Klong's RESONATOR)
static const char* const kMutablePageNames[8] = { "Model", "Sound", "Envelope", "LPG", "", "", "", "Mix" };
static const char* const kMutablePageSymbols[8] = { "model", "sound", "env", "lpg", "", "", "", "mix" };
static bool isDredrum() { return voiceDef().voice == maquina::kDredrumVoice; }
static bool isPlethora() { return voiceDef().voice == maquina::kPlethoraVoice; }
static bool isOneiroi() { return maquina::isOneiroi(voiceDef().voice); }
static bool isKlang() { return voiceDef().voice == maquina::kKlangVoice; }
static bool isKlong() { return maquina::isKlong(voiceDef().voice); }

// Noise seed: differs per instance (like the hardware RNG) unless
// MAQUINA_RNG_SEED is set, which makes renders reproducible for tests.
static uint32_t noiseSeed(const void* self)
{
    if (const char* env = std::getenv("MAQUINA_RNG_SEED"))
        return uint32_t(std::strtoul(env, nullptr, 0)) | 1u;
    return uint32_t(reinterpret_cast<uintptr_t>(self) >> 4) | 1u;
}

class DrumPlugin : public Plugin {
public:
    // parameters: the voice table, then the extras (ExtraParams.hpp)
    static uint32_t numParams() { return maquina::numAllParams(voiceDef()); }

    DrumPlugin()
        : Plugin(numParams(), 1 + factory().count, 2), // programs: Init + factory presets
          fEngine(voiceDef(), noiseSeed(this))
    {
        fEngine.setSampleRate(getSampleRate());
        setLatency(fEngine.latencyFrames());
    }

protected:
    const char* getLabel() const override { return DISTRHO_PLUGIN_LABEL; }
    const char* getDescription() const override
    {
        if (isDredrum())
            return "8-bit 3-oscillator percussion/drone voice from the Dredrum Eurorack firmware "
                   "(unofficial; not affiliated with its authors).";
        if (isPlethora())
            return "Noise voice after the Befaco Noise Plethora (its VCV Rack port): 2 x 60 algorithms "
                   "+ noise, filters, envelopes (unofficial; not affiliated with Befaco).";
        if (voiceDef().voice == maquina::kOneiroiFxVoice)
            return "Looper, filter, resonator, echo and reverb from the Befaco Oneiroi firmware "
                   "(unofficial; not affiliated with Befaco).";
        if (isOneiroi())
            return "Ambient/drone synth voice from the Befaco Oneiroi firmware: oscillators, looper, filter, "
                   "resonator, echo, reverb (unofficial; not affiliated with Befaco).";
        if (isKlang())
            return "Macro oscillator voice based on the Mutable Instruments Plaits firmware: 24 synthesis "
                   "models and its low-pass gate (unofficial; not affiliated with Mutable Instruments).";
        if (voiceDef().voice == maquina::kKlongFxVoice)
            return "Resonator effect based on the Mutable Instruments Rings firmware: the input excites "
                   "modal, string and FM resonators (unofficial; not affiliated with Mutable Instruments).";
        if (isKlong())
            return "Resonator voice based on the Mutable Instruments Rings firmware: modal, string and FM "
                   "resonators played by notes (unofficial; not affiliated with Mutable Instruments).";
        return "Drum voice from the LXR-02 firmware sound engine "
               "(unofficial; not affiliated with Erica Synths).";
    }
    const char* getMaker() const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getHomePage() const override
    {
        return isDredrum()     ? "https://github.com/buranelectrix/dredrum"
               : isPlethora() ? "https://github.com/VCVRack/Befaco"
               : isOneiroi()  ? "https://github.com/Befaco/Oneiroi"
               : isKlang() || isKlong() ? "https://github.com/pichenettes/eurorack"
                              : "https://github.com/erica-synths/lxr-desktop-app";
    }
    const char* getLicense() const override { return "GPL-3.0-only"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initPortGroup(uint32_t groupId, PortGroup& group) override
    {
        const uint8_t page = pageGroups().pageOfGroup[groupId];
        if (isOneiroi()) {
            group.name = kOneiroiPageNames[page % 10];
            group.symbol = kOneiroiPageSymbols[page % 10];
            return;
        }
        if (isKlang() || isKlong()) {
            group.name = page == 1 && isKlong() ? "Resonator" : kMutablePageNames[page & 7];
            group.symbol = page == 1 && isKlong() ? "resonator" : kMutablePageSymbols[page & 7];
            return;
        }
        if (isDredrum() || isPlethora()) {
            group.name = (isDredrum() ? kDredrumPageNames : kPlethoraPageNames)[page & 3];
            group.symbol = (isDredrum() ? kDredrumPageSymbols : kPlethoraPageSymbols)[page & 3];
            return;
        }
        const bool metal = voiceDef().voice >= 4;
        group.name = (page == 3 && metal) ? "Mod Oscillators" : kPageNames[page];
        group.symbol = (page == 3 && metal) ? "modosc" : kPageSymbols[page];
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        const VoiceParam& vp = paramAt(voiceDef(), index);
        p.hints = kParameterIsAutomatable | kParameterIsInteger;
        p.name = vp.name;      // short display names (tools/gen_params.py SHORT_NAMES), max 9 chars
        p.shortName = vp.name;
        p.symbol = vp.symbol;
        p.groupId = uint32_t(pageGroups().groupOfPage[extraId(index) != UINT32_MAX ? extraPage() : vp.page]);
        p.ranges.def = extraId(index) != UINT32_MAX ? float(vp.def) : fEngine.defaultParam(index);

        switch (vp.kind) {
        case ParamKind::Int:
            p.ranges.min = float(vp.fwMin + vp.offset);
            p.ranges.max = float(vp.fwMax + vp.offset);
            break;
        case ParamKind::Toggle:
        case ParamKind::Retrigger:
            p.hints |= kParameterIsBoolean;
            p.ranges.min = 0.f;
            p.ranges.max = 1.f;
            break;
        default: // Enum, ModTarget, Note: labelled steps
            p.ranges.min = 0.f;
            p.ranges.max = float(vp.numLabels - 1);
            break;
        }

        if (vp.labels != nullptr && vp.kind != ParamKind::Toggle && vp.kind != ParamKind::Retrigger) {
            ParameterEnumerationValue* values = new ParameterEnumerationValue[vp.numLabels];
            for (uint8_t i = 0; i < vp.numLabels; ++i) {
                values[i].value = float(i);
                values[i].label = vp.labels[i];
            }
            p.enumValues.count = vp.numLabels;
            p.enumValues.restrictedMode = true;
            p.enumValues.values = values;
        }
    }

    void initProgramName(uint32_t index, String& name) override
    {
        name = index == 0 ? "Init" : factory().presets[index - 1].name;
    }

    // State: the editor's randomize levels per section (Randomizer.hpp). Only
    // the editor uses them; the plugin just keeps them for the project.
    // and the name of the loaded preset ("*name" once edited), for the
    // editor's preset bar.
    void initState(uint32_t index, State& state) override
    {
        if (index == 0) {
            state.key = "rand_levels";
            state.label = "Randomize levels";
            state.defaultValue = maquina::RandSettings().toString().c_str();
        } else {
            state.key = "preset";
            state.label = "Preset name";
            state.defaultValue = "Init";
        }
    }

    void setState(const char* key, const char* value) override
    {
        if (!std::strcmp(key, "rand_levels"))
            fRandLevels = value;
        else if (!std::strcmp(key, "preset"))
            fPresetName = value;
    }

    String getState(const char* key) const override
    {
        if (!std::strcmp(key, "rand_levels")) return fRandLevels;
        if (!std::strcmp(key, "preset")) return fPresetName;
        return String();
    }

    // Init or a factory preset: the sound only; trigger settings stay.
    void loadProgram(uint32_t index) override
    {
        std::vector<float> v(numParams());
        for (uint32_t i = 0; i < v.size(); ++i) v[i] = getParameterValue(i);
        if (index == 0 || index > factory().count)
            maquina::applyPreset(voiceDef(), nullptr, 0, v.data());
        else {
            const maquina::FactoryPreset& fp = factory().presets[index - 1];
            maquina::applyPreset(voiceDef(), fp.values, fp.numValues, v.data());
        }
        for (uint32_t i = 0; i < v.size(); ++i)
            if (maquina::isSoundParam(voiceDef(), i)) setParameterValue(i, v[i]);
        fPresetName = index == 0 || index > factory().count ? "Init" : factory().presets[index - 1].name;
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (extraId(index)) {
        case maquina::kExtraNotePitch: return fEngine.noteFollowsPitch() ? 1.f : 0.f;
        case maquina::kExtraGain: return fGainDb;
        default: return fEngine.getParam(index);
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (extraId(index)) {
        case maquina::kExtraNotePitch: fEngine.setNoteFollowsPitch(value >= 0.5f); break;
        case maquina::kExtraGain:
            fGainDb = std::fmin(std::fmax(std::round(value), -24.f), 12.f);
            fGainTarget = std::pow(10.f, fGainDb / 20.f);
            break;
        default: fEngine.setParam(index, value); break;
        }
    }

    void activate() override
    {
        fEngine.resetTiming();
        fGain = fGainTarget;
    }

    void sampleRateChanged(double newSampleRate) override
    {
        fEngine.setSampleRate(newSampleRate);
        setLatency(fEngine.latencyFrames());
    }

#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
#else
    void run(const float** inputs, float** outputs, uint32_t frames) override
#endif
    {
        const TimePosition& pos = getTimePosition();
        if (pos.bbt.valid && pos.bbt.beatsPerMinute > 0.0)
            fEngine.setTempo(pos.bbt.beatsPerMinute);
#if MAQUINA_ONEIROI
        // SYNC: the transport's position in beats (bar 1 beat 1 = 0)
        if (pos.bbt.valid && pos.bbt.beatsPerMinute > 0.0 && pos.bbt.ticksPerBeat > 0.0) {
            const double beat = double(pos.bbt.bar - 1) * pos.bbt.beatsPerBar + double(pos.bbt.beat - 1) +
                                pos.bbt.tick / pos.bbt.ticksPerBeat;
            fEngine.setTransport(pos.playing, beat, pos.bbt.beatsPerBar,
                                 pos.bbt.beatsPerMinute / 60.0 / getSampleRate());
        } else {
            fEngine.setTransport(false, 0.0, 4.0, 0.0);
        }
#endif
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& ev = midiEvents[i];
            if (ev.size != 3)
                continue;
            const uint8_t status = ev.data[0] & 0xF0;
            if (status == 0x90 && ev.data[2] != 0)
                fEngine.noteOn(ev.frame, ev.data[1], ev.data[2]);
            else if (status == 0x80 || status == 0x90)
                fEngine.noteOff(ev.frame, ev.data[1]);
        }
#endif

#if (MAQUINA_ONEIROI || MAQUINA_KLONG) && DISTRHO_PLUGIN_NUM_INPUTS == 2
        fEngine.process(inputs[0], inputs[1], outputs[0], outputs[1], frames);
#else
        (void)inputs;
        fEngine.process(outputs[0], outputs[1], frames);
#endif
        applyGain(outputs[0], outputs[1], frames);
    }

private:
    // output gain (ExtraParams.hpp "gain"), ~5 ms smoothing; 0 dB = untouched
    void applyGain(float* l, float* r, uint32_t frames)
    {
        if (fGain == fGainTarget && fGain == 1.f)
            return;
        const float k = 1.f - std::exp(-1.f / (0.005f * float(getSampleRate())));
        for (uint32_t i = 0; i < frames; ++i) {
            fGain += (fGainTarget - fGain) * k;
            l[i] *= fGain;
            r[i] *= fGain;
        }
        if (std::fabs(fGain - fGainTarget) < 1e-6f)
            fGain = fGainTarget;
    }

    // ExtraParamId of an extra parameter, UINT32_MAX for a firmware one
    static uint32_t extraId(uint32_t index)
    {
        return maquina::isExtraParam(voiceDef(), index)
                   ? uint32_t(maquina::extraIdOf(voiceDef(), index - voiceDef().numParams))
                   : UINT32_MAX;
    }

    Engine fEngine;
    String fRandLevels { maquina::RandSettings().toString().c_str() };
    String fPresetName { "Init" };
    float fGainDb = 0.f, fGainTarget = 1.f, fGain = 1.f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPlugin)
};

Plugin* createPlugin() { return new DrumPlugin(); }

#if DISTRHO_PLUGIN_CLAP_REMOTE_CONTROLS
// CLAP remote-controls pages (Bitwig remote controls, controller mapping): the
// Machinista page layout from tools/pages.py. The hook and ClapRemotePage are
// declared by the DPF CLAP wrapper (patches/dpf/0002-clap-remote-controls.patch).
struct ClapRemotePage { const char* name; const char* symbols[8]; };
struct ClapRemotePages { const ClapRemotePage* pages; uint32_t count; };
#include "RemotePages.generated.hpp"

const ClapRemotePage* d_clapRemotePages(uint32_t* const count)
{
    const ClapRemotePages& voice = k_remote_pages[MAQUINA_VOICE_INDEX];
    *count = voice.count;
    return voice.pages;
}
#endif

END_NAMESPACE_DISTRHO
