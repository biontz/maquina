// DrumPlugin.cpp - the DPF plugin shared by all voice plugins.
// Each plugin directory compiles this with its own DistrhoPluginInfo.h and
// MAQUINA_VOICE_INDEX (index into k_voices).
//
// SPDX-License-Identifier: GPL-3.0-only
#include "DistrhoPlugin.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>
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
#elif MAQUINA_ACIDA
#include "AcidaHost.hpp"
#elif MAQUINA_BL101
#include "Bl101Host.hpp"
#elif MAQUINA_LA50
#include "La50Host.hpp"
#else
#include "EngineHost.hpp"
#endif
#include "ExtraParams.hpp"
#include "Randomizer.hpp"
#include "FactoryPresets.generated.hpp"
#include "ProgramMap.hpp"
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
#elif MAQUINA_ACIDA
using Engine = maquina::AcidaHost; // Maquina Acida / Acida FX: OpenRB-338's 303 and effects
#elif MAQUINA_BL101
using Engine = maquina::Bl101Host; // Maquina BL-101 / BL-101 FX: S1Vave's synth and effects
#elif MAQUINA_LA50
using Engine = maquina::La50Host; // Maquina LA-50: LA synthesis playing D-50 patches
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
// Maquina Acida / Acida FX pages (tools/gen_params.py ACIDA_*PAGE_TITLES)
static const char* const kAcidaPageNames[8] = { "Synth", "Play", "", "", "", "", "", "Mix" };
static const char* const kAcidaPageSymbols[8] = { "synth", "play", "", "", "", "", "", "mix" };
static const char* const kAcidaFxPageNames[8] = { "Distortion", "PCF", "Compressor", "Delay", "", "", "", "Mix" };
static const char* const kAcidaFxPageSymbols[8] = { "dist", "pcf", "comp", "delay", "", "", "", "mix" };
// Maquina BL-101 / BL-101 FX pages (tools/gen_params.py BL101_*PAGE_TITLES)
static const char* const kBl101PageNames[10] = { "Modulator", "VCO", "Source Mixer", "VCF", "Envelope",
                                                 "Voice", "Chord", "Mix", "Draw", "Chop" };
static const char* const kBl101PageSymbols[10] = { "modulator", "vco", "mixer", "vcf", "env",
                                                   "voice", "chord", "mix", "draw", "chop" };
static const char* const kBl101FxPageNames[8] = { "Chorus", "Delay", "Reverb", "Lo-Fi", "", "", "", "Mix" };
static const char* const kBl101FxPageSymbols[8] = { "chorus", "delay", "reverb", "lofi", "", "", "", "mix" };
// Maquina LA-50 pages (tools/gen_params.py LA50_PAGE_TITLES)
static const char* const kLa50PageNames[8] = { "Patch", "Upper Common", "Upper Partial 1", "Upper Partial 2",
                                               "Lower Common", "Lower Partial 1", "Lower Partial 2", "Mix" };
static const char* const kLa50PageSymbols[8] = { "patch", "upper", "upper_p1", "upper_p2", "lower", "lower_p1",
                                                 "lower_p2", "mix" };
static bool isDredrum() { return voiceDef().voice == maquina::kDredrumVoice; }
static bool isPlethora() { return voiceDef().voice == maquina::kPlethoraVoice; }
static bool isOneiroi() { return maquina::isOneiroi(voiceDef().voice); }
static bool isKlang() { return voiceDef().voice == maquina::kKlangVoice; }
static bool isKlong() { return maquina::isKlong(voiceDef().voice); }
static bool isAcida() { return maquina::isAcida(voiceDef().voice); }
static bool isBl101() { return maquina::isBl101(voiceDef().voice); }
static bool isLa50() { return maquina::isLa50(voiceDef().voice); }

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

    // host programs: Init + the factory presets. VST3 hosts send MIDI Program
    // Change only through this list (as loadProgram()), so it has 128 entries
    // there: with a bank selected by CC 0/32, every program of the bank is reachable.
    static uint32_t numPrograms()
    {
#ifdef DISTRHO_PLUGIN_TARGET_VST3
        return std::max<uint32_t>(128u, 1u + factory().count);
#else
        return 1u + factory().count;
#endif
    }

    DrumPlugin()
        : Plugin(numParams(), numPrograms(), 2),
          fEngine(voiceDef(), noiseSeed(this)),
          fScratch(numParams())
    {
        fEngine.setSampleRate(getSampleRate());
        setLatency(fEngine.latencyFrames());
        // MIDI Bank Select / Program Change: the preset map, rebuilt by a
        // background thread when the user library changes (ProgramMap.hpp)
        fLibSignature = maquina::librarySignature(voiceDef());
        fMap.store(new maquina::ProgramMap(maquina::buildProgramMap(voiceDef(), factory())));
        fRescan = std::thread([this] { rescanLoop(); });
    }

    ~DrumPlugin() override
    {
        fQuit.store(true);
        if (fRescan.joinable()) fRescan.join();
        delete fMap.load();
        for (auto& r : fRetired) delete r.first;
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
        if (voiceDef().voice == maquina::kAcidaFxVoice)
            return "Distortion, PCF filter, compressor and tempo delay from OpenRB-338, a reimplementation of "
                   "the ReBirth RB-338 engine (unofficial; not affiliated with Reason Studios or Roland).";
        if (isLa50())
            return "LA-synthesis synth that plays D-50 patches (import .syx banks): two tones of two partials, "
                   "synth waves after Munt's LA32 model, chorus, EQ, reverb (unofficial; not affiliated with Roland).";
        if (voiceDef().voice == maquina::kBl101FxVoice)
            return "Chorus, ping-pong delay, plate reverb and lo-fi slot from the S1Vave firmware by Andrew GG "
                   "(unofficial; not affiliated with its author or with Roland).";
        if (isBl101())
            return "8-voice analog-modelling mono/poly synth from the S1Vave firmware by Andrew GG, with its "
                   "OSC DRAW and OSC CHOP (unofficial; not affiliated with its author or with Roland).";
        if (isAcida())
            return "Acid bass line voice from OpenRB-338's 303, a bit-exact reimplementation of the ReBirth "
                   "RB-338 engine (unofficial; not affiliated with Reason Studios or Roland).";
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
               : isAcida()    ? "https://github.com/pingw33n/openrb-338"
               : isBl101()    ? "https://github.com/AndrewGG88/S1Vave"
               : isLa50()     ? "https://github.com/ngeiswei/dsynkant"
                              : "https://github.com/erica-synths/lxr-desktop-app";
    }
    const char* getLicense() const override { return "GPL-3.0-only"; }
    uint32_t getVersion() const override { return d_version(0, 2, 0); }

    void initPortGroup(uint32_t groupId, PortGroup& group) override
    {
        const uint8_t page = pageGroups().pageOfGroup[groupId];
        if (isOneiroi()) {
            group.name = kOneiroiPageNames[page % 10];
            group.symbol = kOneiroiPageSymbols[page % 10];
            return;
        }
        if (isAcida()) {
            const bool fx = voiceDef().voice == maquina::kAcidaFxVoice;
            group.name = (fx ? kAcidaFxPageNames : kAcidaPageNames)[page & 7];
            group.symbol = (fx ? kAcidaFxPageSymbols : kAcidaPageSymbols)[page & 7];
            return;
        }
        if (isLa50()) {
            group.name = kLa50PageNames[page & 7];
            group.symbol = kLa50PageSymbols[page & 7];
            return;
        }
        if (isBl101()) {
            const bool fx = voiceDef().voice == maquina::kBl101FxVoice;
            group.name = fx ? kBl101FxPageNames[page & 7] : kBl101PageNames[page % 10];
            group.symbol = fx ? kBl101FxPageSymbols[page & 7] : kBl101PageSymbols[page % 10];
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

        if (extraId(index) == maquina::kExtraMidiProgram) { // read-only: the last program loaded
            p.hints = kParameterIsOutput | kParameterIsInteger;
            p.ranges.min = -1.f;
            p.ranges.max = float(2u * maquina::kMidiProgramToggle - 1u);
            p.ranges.def = -1.f;
            return;
        }

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
        if (index == 0) name = "Init";
        else if (index <= factory().count) name = factory().presets[index - 1].name;
        else { // VST3 padding (numPrograms): a program of the selected bank (CC 0/32)
            char buf[32];
            std::snprintf(buf, sizeof buf, "Program %u", index);
            name = buf;
        }
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
        else if (!std::strcmp(key, "preset")) { // the editor chose a preset, or a project is loading
            fPresetName = value;
            fMidiNameSet.store(false);
            fProgramOut = -1.f; // "Program": the state's name is the current one again
        }
    }

    String getState(const char* key) const override
    {
        if (!std::strcmp(key, "rand_levels")) return fRandLevels;
        if (!std::strcmp(key, "preset")) return fMidiNameSet.load() ? String(fMidiName) : fPresetName;
        return String();
    }

    // The host's program list. Bank 0 (no CC 0/32 received): Init or a factory
    // preset, the sound only; trigger settings stay. Another bank: that bank's
    // program (ProgramMap.hpp), so VST3 hosts' program changes reach every bank.
    void loadProgram(uint32_t index) override
    {
        const uint32_t msb = fBankMsb, lsb = fBankLsb;
        if (msb != 0 || lsb != 0) {
            const maquina::ProgramMap* m = fMap.load(std::memory_order_acquire);
            if (const maquina::ProgramEntry* e = m && index < 128 ? m->at(msb, lsb, index) : nullptr) {
                applyEntry(*e, false);
                fPresetName = e->name.c_str();
                fMidiNameSet.store(false);
                noteProgram(msb, lsb, index);
            }
            return;
        }
        if (index > factory().count) return; // VST3 padding: nothing in bank 0
        std::vector<float> v(numParams());
        for (uint32_t i = 0; i < v.size(); ++i) v[i] = getParameterValue(i);
        if (index == 0)
            maquina::applyPreset(voiceDef(), nullptr, 0, v.data());
        else {
            const maquina::FactoryPreset& fp = factory().presets[index - 1];
            maquina::applyPreset(voiceDef(), fp.values, fp.numValues, v.data());
        }
        for (uint32_t i = 0; i < v.size(); ++i)
            if (maquina::isSoundParam(voiceDef(), i)) setParameterValue(i, v[i]);
        fPresetName = index == 0 ? "Init" : factory().presets[index - 1].name;
        fMidiNameSet.store(false);
        // no "Program" report here: hosts also call this when loading a project (the saved
        // program, before the parameters and the preset name), and the editor shows the
        // host's own program changes through programLoaded()
        fProgramOut = -1.f;
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (extraId(index)) {
        case maquina::kExtraNotePitch: return fEngine.noteFollowsPitch() ? 1.f : 0.f;
        case maquina::kExtraGain: return fGainDb;
        case maquina::kExtraMidiPc: return fMidiPc ? 1.f : 0.f;
        case maquina::kExtraMidiProgram: return fProgramOut;
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
        case maquina::kExtraMidiPc: fMidiPc = value >= 0.5f; break;
        case maquina::kExtraMidiProgram: break; // output
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
#if MAQUINA_ONEIROI || MAQUINA_ACIDA
        // SYNC (Oneiroi) / the PCF's patterns (Acida FX): the transport's
        // position in beats (bar 1 beat 1 = 0)
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
            // MIDI PC on: Bank Select (CC 0 MSB, CC 32 LSB) and Program Change load
            // presets (ProgramMap.hpp); they never reach the engine. Detected by the
            // status byte: DPF's CLAP wrapper gives every MIDI event size 3.
            if (fMidiPc && ev.size >= 2) {
                const uint8_t st = ev.data[0] & 0xF0;
                if (st == 0xC0) {
                    programChange(ev.data[1] & 0x7F);
                    continue;
                }
                if (st == 0xB0 && ev.size >= 3 && (ev.data[1] == 0 || ev.data[1] == 32)) {
                    if (ev.data[1] == 0) {
                        fBankMsb = ev.data[2] & 0x7F;
                        fBankLsb = 0; // a new MSB starts at its first page
                    } else {
                        fBankLsb = ev.data[2] & 0x7F;
                    }
                    continue;
                }
            }
#if MAQUINA_MIDI_PC_ONLY
            continue; // an effect: MIDI is only for Bank Select / Program Change
#endif
#if MAQUINA_LA50
            // Maquina LA-50: channels (Separate key modes), channel aftertouch
            if (ev.size == 2 && (ev.data[0] & 0xF0) == 0xD0) {
                fEngine.aftertouch(ev.frame, ev.data[1]);
                continue;
            }
            if (ev.size == 3 && (ev.data[0] & 0xE0) == 0x80) {
                const uint8_t ch = ev.data[0] & 0x0F;
                if ((ev.data[0] & 0xF0) == 0x90 && ev.data[2] != 0) fEngine.noteOn(ev.frame, ev.data[1], ev.data[2], ch);
                else fEngine.noteOff(ev.frame, ev.data[1], ch);
                continue;
            }
#endif
            if (ev.size != 3)
                continue;
            const uint8_t status = ev.data[0] & 0xF0;
            if (status == 0x90 && ev.data[2] != 0)
                fEngine.noteOn(ev.frame, ev.data[1], ev.data[2]);
            else if (status == 0x80 || status == 0x90)
                fEngine.noteOff(ev.frame, ev.data[1]);
#if MAQUINA_BL101 || MAQUINA_LA50
            // Maquina BL-101 / LA-50: the bender, the mod wheel (vibrato), sustain, all notes off
            else if (status == 0xE0)
                fEngine.pitchBend(ev.frame, int32_t(ev.data[1] | (ev.data[2] << 7)) - 8192);
            else if (status == 0xB0)
                fEngine.controlChange(ev.frame, ev.data[1], ev.data[2]);
#endif
        }
#endif

#if (MAQUINA_ONEIROI || MAQUINA_KLONG || MAQUINA_ACIDA || MAQUINA_BL101) && DISTRHO_PLUGIN_NUM_INPUTS == 2
        fEngine.process(inputs[0], inputs[1], outputs[0], outputs[1], frames);
#else
        (void)inputs;
        fEngine.process(outputs[0], outputs[1], frames);
#endif
        applyGain(outputs[0], outputs[1], frames);
    }

private:
    // ---- MIDI Bank Select / Program Change (ProgramMap.hpp)

    // a preset's sound parameters (the rest stays, as loadProgram()); notify =
    // tell the host (requestParameterValueChange) - from run(), not from the
    // host's own program change. No allocation (fScratch).
    void applyEntry(const maquina::ProgramEntry& e, bool notify)
    {
        const uint32_t n = numParams();
        for (uint32_t i = 0; i < n; ++i) fScratch[i] = getParameterValue(i);
        maquina::applyPreset(voiceDef(), e.init ? nullptr : e.values.data(), e.init ? 0u : uint32_t(e.values.size()),
                             fScratch.data());
        for (uint32_t i = 0; i < n; ++i) {
            if (!maquina::isSoundParam(voiceDef(), i)) continue;
            setParameterValue(i, fScratch[i]);
#if DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST
            if (notify) requestParameterValueChange(i, fScratch[i]);
#endif
        }
        (void)notify;
    }

    // the "Program" output (ExtraParams.hpp kExtraMidiProgram), for the editor and the host
    void noteProgram(uint32_t msb, uint32_t lsb, uint32_t program)
    {
        fProgramToggle = !fProgramToggle;
        fProgramOut = maquina::encodeMidiProgram(msb, lsb, program, fProgramToggle);
    }

    // MIDI Program Change in run(): the selected bank's program, if there is one
    void programChange(uint32_t program)
    {
        const maquina::ProgramMap* m = fMap.load(std::memory_order_acquire);
        const maquina::ProgramEntry* e = m ? m->at(fBankMsb, fBankLsb, program) : nullptr;
        if (!e) return;
        applyEntry(*e, true);
        // the preset name for getState(): a fixed buffer, no allocation here
        std::strncpy(fMidiName, e->name.c_str(), sizeof fMidiName - 1);
        fMidiName[sizeof fMidiName - 1] = '\0';
        fMidiNameSet.store(true);
        noteProgram(fBankMsb, fBankLsb, program);
    }

    // background thread: every 2 s, rebuild the map if the user library changed;
    // replaced maps are freed 2 s later (run() only holds one for a block)
    void rescanLoop()
    {
        using Clock = std::chrono::steady_clock;
        auto next = Clock::now() + std::chrono::seconds(2);
        while (!fQuit.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto now = Clock::now();
            fRetired.erase(std::remove_if(fRetired.begin(), fRetired.end(),
                                          [&](const std::pair<const maquina::ProgramMap*, Clock::time_point>& r) {
                                              if (now - r.second < std::chrono::seconds(2)) return false;
                                              delete r.first;
                                              return true;
                                          }),
                           fRetired.end());
            if (now < next) continue;
            next = now + std::chrono::seconds(2);
            const uint64_t sig = maquina::librarySignature(voiceDef());
            if (sig == fLibSignature) continue;
            fLibSignature = sig;
            auto* fresh = new maquina::ProgramMap(maquina::buildProgramMap(voiceDef(), factory()));
            fRetired.emplace_back(fMap.exchange(fresh, std::memory_order_acq_rel), Clock::now());
        }
    }

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

    // MIDI Bank Select / Program Change
    std::vector<float> fScratch;                       // applyEntry()'s values
    std::atomic<const maquina::ProgramMap*> fMap { nullptr };
    std::vector<std::pair<const maquina::ProgramMap*, std::chrono::steady_clock::time_point>> fRetired; // rescan thread
    uint64_t fLibSignature = 0;                        // rescan thread
    std::atomic<bool> fQuit { false };
    std::thread fRescan;
    bool fMidiPc = true;                               // "MIDI PC"
    uint32_t fBankMsb = 0, fBankLsb = 0;               // CC 0 / CC 32
    float fProgramOut = -1.f;                          // "Program" output
    bool fProgramToggle = false;
    char fMidiName[128] = "Init";                      // the name of a preset loaded by MIDI
    std::atomic<bool> fMidiNameSet { false };          // ... and that it is the current one

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
