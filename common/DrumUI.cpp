// DrumUI.cpp - NanoVG editor shared by all voice plugins.
//
// Flat, compact, knob-first (the K4REdit style): one panel per voice, sections
// follow the LXR-02's menu pages (Maquina Dredrum: its 3 oscillators),
// envelopes are drawn from the firmware's own time/slope curves and can be
// edited by dragging the contour.
//
// Controls: drag up/down (Shift = fine), mouse wheel steps, double-click
// resets to the default. Selectors: click the arrows to step, click the value
// for a list, or drag. Envelope: drag the handles (time) or the curve (slope).
//
// Randomize, the RANDOM panel at the bottom: RANDOM rolls a new sound, BACK
// returns to the sound before the first roll. Per group (OSC, ENV, ...): click
// the title to lock/unlock it, drag the range bar's handles to set the window
// (min..max % of each control's range) the random values are picked from.
// Maquina Dredrum: each OSC section's title bar has ROLL (randomize only that
// oscillator) and RND ON/OFF (whether RANDOM includes it); BACK undoes both.
// Maquina Oneiroi / Oneiroi FX: the module's randomizer instead (TARGET,
// AMOUNT, RANDOM, UNDO/REDO; Randomizer.hpp oneiroiRandomize).
//
// SPDX-License-Identifier: GPL-3.0-only
#include "DistrhoUI.hpp"

#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <random>
#include <vector>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>

#include "ExtraParams.hpp"
#include "Randomizer.hpp"
#if MAQUINA_LA50
#include "D50Sysex.hpp"
#endif
#include "FactoryPresets.generated.hpp"
#include "VoiceTables.generated.hpp"

START_NAMESPACE_DISTRHO

using maquina::ParamKind;
using maquina::VoiceDef;
using maquina::VoiceParam;
using maquina::paramAt;

namespace maquina_ui {

const VoiceDef& voiceDef() { return maquina::k_voices[MAQUINA_VOICE_INDEX]; }

// ---------------------------------------------------------------------------
// look

#include "ui/Theme.hpp"

constexpr float kWidth = theme().width;
constexpr float kHeaderH = kNew ? kChromeHeaderH : 30.f;
constexpr float kMargin = 8.f;
constexpr float kGap = 6.f;
constexpr float kTitleH = 18.f;
constexpr float kCellH = 64.f;
constexpr float kSectionPad = 8.f;
constexpr float kKnobW = 54.f;
constexpr float kSelectW = 96.f;
constexpr float kToggleW = 54.f;
constexpr float kGraphW = 168.f;

// the classic palette; the new look's chrome tokens (Theme.hpp) for the browser, dialogs, popups
constexpr Rgb kBg = kNew ? Rgb { 0x14, 0x15, 0x18 } : Rgb { 0x1c, 0x1c, 0x1c };
constexpr Rgb kPanel = kNew ? kChromeBg : Rgb { 0x26, 0x26, 0x26 };
constexpr Rgb kBorder = kNew ? kChromeBtnLine : Rgb { 0x36, 0x36, 0x36 };
constexpr Rgb kWell = kNew ? kChromeWell : Rgb { 0x1a, 0x1a, 0x1a };
constexpr Rgb kTrack = kNew ? Rgb { 0x2e, 0x34, 0x3d } : Rgb { 0x45, 0x45, 0x45 };
constexpr Rgb kText = kNew ? kChromeText : Rgb { 0xc8, 0xc8, 0xc8 };
constexpr Rgb kDim = kNew ? kChromeText2 : Rgb { 0x80, 0x80, 0x80 };
// parameter labels + section titles: lighter than kDim, below kText
constexpr Rgb kLabel = kNew ? Rgb { 0xc3, 0xca, 0xd3 } : Rgb { 0xa8, 0xa8, 0xa8 };
constexpr Rgb kAccent = kNew ? theme().accent : Rgb { 0xff, 0x9f, 0x2e };

// a: 0..255 (DPF's Color takes alpha as a float 0..1)
Color col(Rgb c, int a = 255) { return Color(c.r, c.g, c.b, float(a) / 255.f); }

#include "ui/Fonts.hpp"
#include "ui/Controls.cpp"

// ---------------------------------------------------------------------------
// firmware envelope models (SlopeEg2.c / Decay.c), for drawing only

constexpr double kBlockSec = 64.0 / 44140.0;

double egRate(int v, double k) // value change per 64-sample block
{
    const double x = v / 127.0;
    const double r = (1.0 - ((1.0 + k) * x / (1.0 + k * std::fabs(x)))) * 2.0;
    return std::min(1.0, r);
}
// an envelope stage that never ends; finite, as the build uses -ffast-math
// (no infinities: std::isinf would be folded away)
constexpr double kNever = 1e9;
double egSeconds(int v, double k)
{
    const double r = egRate(v, k);
    return r <= 1e-9 ? kNever : kBlockSec / r;
}
constexpr double kAttackK = 2 * 0.99 / (1 - 0.99);
constexpr double kDecayK = 2 * 0.999 / (1 - 0.999);
constexpr double kPitchK = 2 * 0.99 / (1 - 0.99);

double slopeShape(double val, int slope, double scale) // (1+k)x/(1+k|x|)
{
    double a = (slope / 127.0 - 0.5) * scale;
    a = std::clamp(a, -0.995, 0.995);
    const double k = 2 * a / (1 - a);
    return (1 + k) * val / (1 + k * std::fabs(val));
}

// time axis of the graphs: log, 1 ms .. 4 s
constexpr double kTMin = 0.001, kTMax = 4.0;
float timeToX(double t) // 0..1
{
    if (!(t > kTMin)) return 0.f;
    if (t >= kTMax) return 1.f;
    return float(std::log(t / kTMin) / std::log(kTMax / kTMin));
}

void fmtTime(char* buf, size_t n, double t)
{
    if (t >= kNever) std::snprintf(buf, n, "inf");
    else if (t < 1.0) std::snprintf(buf, n, "%.0f ms", t * 1000.0);
    else std::snprintf(buf, n, "%.2f s", t);
}

// ---------------------------------------------------------------------------
// panel description: sections in display order, items by LV2 symbol

// Draw / Chop: Maquina BL-101's OSC DRAW table (draw_1..16) and CHOP masks
enum class GraphKind { None, Amp, Pitch, Draw, Chop };
// how an item is shown: by its kind (knob / selector / toggle), or as
// Maquina BL-101's panel controls: a vertical slider, a slide switch
enum class ItemStyle : uint8_t { Auto, Slider, Switch, Narrow }; // Narrow: a slim selector
struct ItemSpec { const char* symbol; const char* label; ItemStyle style = ItemStyle::Auto; };
struct SectionSpec {
    const char* title;
    GraphKind graph;
    ItemSpec items[16];
};
constexpr ItemStyle kSl = ItemStyle::Slider, kSw = ItemStyle::Switch, kNarrow = ItemStyle::Narrow;

const SectionSpec kSections[] = {
    { "OSCILLATOR", GraphKind::None,
      { { "osc_coarse", "Coarse" }, { "osc_fine", "Fine" }, { "osc_wave", "Wave" }, { "osc_pwm", "PWM" } } },
    { "NOISE", GraphKind::None, { { "noise_freq", "Freq" }, { "noise_mix", "Mix" } } },
    { "AMP EG", GraphKind::Amp,
      { { "amp_attack", "Attack" }, { "amp_decay", "Decay" }, { "amp_slope", "Slope" },
        { "amp_repeat", "Repeat" } } },
    { "PITCH EG", GraphKind::Pitch,
      { { "pitch_amount", "Amount" }, { "pitch_decay", "Decay" }, { "pitch_slope", "Slope" } } },
    { "FM", GraphKind::None,
      { { "fm_amount", "Amount" }, { "fm_freq", "Freq" }, { "fm_wave", "Wave" }, { "fm_mode", "Mode" } } },
    { "MOD OSCILLATORS", GraphKind::None,
      { { "mod1_freq", "Freq 1" }, { "mod1_gain", "Gain 1" }, { "mod1_wave", "Wave 1" },
        { "mod2_freq", "Freq 2" }, { "mod2_gain", "Gain 2" }, { "mod2_wave", "Wave 2" } } },
    { "TRANSIENT", GraphKind::None,
      { { "trans_wave", "Wave" }, { "trans_volume", "Volume" }, { "trans_freq", "Freq" } } },
    { "FILTER", GraphKind::None,
      { { "filter_type", "Type" }, { "filter_freq", "Cutoff" }, { "filter_reso", "Reso" },
        { "filter_drive", "Drive" } } },
    { "LFO", GraphKind::None,
      { { "lfo_wave", "Wave" }, { "lfo_rate", "Rate" }, { "lfo_sync", "Sync" }, { "lfo_amount", "Amount" },
        { "lfo_offset", "Phase" }, { "lfo_target", "Target" }, { "lfo_retrigger", "Retrig" } } },
    { "VELOCITY", GraphKind::None,
      { { "velo_dest", "Dest" }, { "velo_amount", "Amount" }, { "velo_volume", "> Vol" } } },
    // + the output gain (ExtraParams.hpp, all voices; Dredrum/Plethora: Gain only;
    // Oneiroi: the module's faders before it)
    { "MIX", GraphKind::None,
      { { "volume", "Volume" }, { "pan", "Pan" }, { "drive", "Drive" }, { "decimation", "Rate" },
        { "vol_input", "Input" }, { "vol_looper", "Looper" }, { "vol_sine", "Sine" }, { "vol_osc2", "Saw / WT" },
        { "mix_filter", "Filter" }, { "mix_resonator", "Reso" }, { "mix_echo", "Echo" },
        { "mix_ambience", "Ambience" }, { "output", "Output" }, { "patch_level", "Level" }, { "gain", "Gain" } } },
    // Key Track: every note triggers; on = the played note sets the pitch
    { "KEY", GraphKind::None, { { "note_pitch", "Track" } } },
    // Maquina Dredrum
    { "OSC 1", GraphKind::None,
      { { "osc1_wave", "Wave" }, { "osc1_pitch", "Pitch" }, { "osc1_drop", "Drop" }, { "osc1_decay", "Decay" },
        { "osc1_level", "Level" }, { "osc1_track", "Track" }, { "osc1_mute", "Mute" },
        { "osc1_octave", "Octave" },
        { "osc1_semi", "Semi" } } },
    { "OSC 2", GraphKind::None,
      { { "osc2_wave", "Wave" }, { "osc2_pitch", "Pitch" }, { "osc2_drop", "Drop" }, { "osc2_decay", "Decay" },
        { "osc2_level", "Level" }, { "osc2_track", "Track" }, { "osc2_mute", "Mute" },
        { "osc2_octave", "Octave" },
        { "osc2_semi", "Semi" } } },
    { "OSC 3", GraphKind::None,
      { { "osc3_wave", "Wave" }, { "osc3_pitch", "Pitch" }, { "osc3_drop", "Drop" }, { "osc3_decay", "Decay" },
        { "osc3_level", "Level" }, { "click", "Click" }, { "osc3_track", "Track" }, { "osc3_mute", "Mute" },
        { "osc3_octave", "Octave" },
        { "osc3_semi", "Semi" } } },
    { "MODE", GraphKind::None, { { "trig_mode", "Trigger" } } },
    // Maquina Plethora
    { "CHANNEL A", GraphKind::None,
      { { "a_algo", "Algorithm" }, { "a_x", "X" }, { "a_y", "Y" }, { "a_ftype", "Filter" }, { "a_cutoff", "Cutoff" },
        { "a_reso", "Reso" }, { "a_attack", "Attack" }, { "a_decay", "Decay" }, { "a_level", "Level" },
        { "a_mute", "Mute" } } },
    { "CHANNEL B", GraphKind::None,
      { { "b_algo", "Algorithm" }, { "b_x", "X" }, { "b_y", "Y" }, { "b_ftype", "Filter" }, { "b_cutoff", "Cutoff" },
        { "b_reso", "Reso" }, { "b_attack", "Attack" }, { "b_decay", "Decay" }, { "b_level", "Level" },
        { "b_mute", "Mute" } } },
    { "CHANNEL C", GraphKind::None,
      { { "c_source", "Source" }, { "c_grit", "Grit" }, { "c_ftype", "Filter" }, { "c_cutoff", "Cutoff" },
        { "c_reso", "Reso" }, { "c_attack", "Attack" }, { "c_decay", "Decay" }, { "c_level", "Level" },
        { "c_mute", "Mute" } } },
    { "GLOBAL", GraphKind::None, { { "env_sync", "Env Sync" }, { "sync", "Sync" } } }, // + MODE (trig_mode), shared with Dredrum
    // Maquina Oneiroi / Oneiroi FX: the module's sections (SHIFT functions next to their knob)
    { "LOOPER", GraphKind::None,
      { { "loop_speed", "Speed" }, { "loop_start", "Start" }, { "loop_length", "Length" }, { "loop_sos", "SOS" },
        { "loop_filter", "Filter" }, { "loop_record", "Record" }, { "loop_source", "Source" },
        { "loop_clear", "Clear" } } },
    { "OSCILLATORS", GraphKind::None,
      { { "osc_pitch", "Pitch" }, { "osc_octave", "Octave" }, { "osc_detune", "Detune" }, { "osc_unison", "Unison" },
        { "osc2_mode", "Osc 2" } } },
    { "MODULATION", GraphKind::None,
      { { "mod_level", "Level" }, { "mod_speed", "Speed" }, { "mod_type", "Type" } } },
    { "FILTER", GraphKind::None,
      { { "flt_cutoff", "Cutoff" }, { "flt_reso", "Reso" }, { "flt_mode", "Mode" }, { "flt_pos", "Position" } } },
    { "RESONATOR", GraphKind::None,
      { { "res_tune", "Tune" }, { "res_feedback", "Feedback" }, { "res_dissonance", "Dissonance" } } },
    { "ECHO", GraphKind::None,
      { { "echo_density", "Density" }, { "echo_repeats", "Repeats" }, { "echo_filter", "Filter" } } },
    { "AMBIENCE", GraphKind::None,
      { { "amb_spacetime", "Spacetime" }, { "amb_decay", "Decay" }, { "amb_autopan", "Auto Pan" } } },
    { "ENVELOPE", GraphKind::None,
      { { "env_attack", "Attack" }, { "env_decay", "Decay" }, { "env_sustain", "Sustain" },
        { "env_release", "Release" }, { "env_drone", "Drone" } } },
    // Maquina Klang / Klong / Klong FX (+ MIX: Output, Gain)
    { "MODEL", GraphKind::None,
      { { "model", "Model" }, { "polyphony", "Poly" }, { "note_source", "Notes" }, { "coarse", "Coarse" },
        { "fine", "Fine" }, { "key_track", "Track" } } },
    { "SOUND", GraphKind::None, { { "harmonics", "Harmonics" }, { "timbre", "Timbre" }, { "morph", "Morph" } } },
    { "ENV MOD", GraphKind::None,
      { { "fm_env", "> FM" }, { "timbre_env", "> Timbre" }, { "morph_env", "> Morph" } } },
    { "LPG", GraphKind::None, { { "lpg_decay", "Decay" }, { "lpg_colour", "Colour" }, { "lpg_mode", "Mode" } } },
    { "RESONATOR", GraphKind::None,
      { { "structure", "Structure" }, { "brightness", "Brightness" }, { "damping", "Damping" },
        { "position", "Position" } } },
    // Maquina Acida (+ MIX: Gain)
    { "SYNTH", GraphKind::None,
      { { "waveform", "Wave" }, { "tune", "Tune" }, { "cutoff", "Cutoff" }, { "resonance", "Reso" },
        { "env_mod", "Env Mod" }, { "decay", "Decay" }, { "accent", "Accent" } } },
    { "PLAY", GraphKind::None, { { "engine", "Engine" }, { "accent_vel", "Acc Vel" } } },
    // Maquina Acida FX (+ MIX: Gain)
    { "DISTORTION", GraphKind::None,
      { { "dist_on", "On" }, { "dist_amount", "Amount" }, { "dist_shape", "Shape" } } },
    { "PCF", GraphKind::None,
      { { "pcf_on", "On" }, { "pcf_mode", "Mode" }, { "pcf_wave", "Wave" }, { "pcf_freq", "Freq" },
        { "pcf_reso", "Reso" }, { "pcf_amount", "Amount" }, { "pcf_decay", "Decay" } } },
    { "COMPRESSOR", GraphKind::None,
      { { "comp_on", "On" }, { "comp_amount", "Amount" }, { "comp_threshold", "Thresh" } } },
    { "DELAY", GraphKind::None,
      { { "delay_on", "On" }, { "delay_mode", "Mode" }, { "delay_steps", "Steps" }, { "delay_send", "Send" },
        { "delay_feedback", "Feedback" }, { "delay_pan", "Pan" } } },
    // Maquina BL-101: the panel's strip (sliders, slide switches; the
    // selectors stand for its rotary switches), then the rest as knobs
    { "MODULATOR", GraphKind::None, { { "modulator_rate", "Rate", kSl }, { "modulator_wave", "Wave", kNarrow } } },
    { "VCO", GraphKind::None,
      { { "vco_mod", "Mod", kSl }, { "vco_range", "Range", kNarrow }, { "vco_pw", "PW", kSl },
        { "vco_pwm_src", "PWM", kSw } } },
    { "SOURCE MIXER", GraphKind::None,
      { { "mix_pulse", "Pulse", kSl }, { "mix_saw", "Saw", kSl }, { "mix_sub", "Sub", kSl },
        { "mix_sub_type", "Sub Osc", kSw }, { "mix_noise", "Noise", kSl }, { "mix_noise_type", "Type", kSw } } },
    { "VCF", GraphKind::None,
      { { "vcf_freq", "Freq", kSl }, { "vcf_res", "Res", kSl }, { "vcf_env", "Env", kSl }, { "vcf_mod", "Mod", kSl },
        { "vcf_kybd", "Kybd", kSl } } },
    { "VCA", GraphKind::None, { { "vca_mode", "VCA", kSw } } },
    { "ENV", GraphKind::None,
      { { "env_trigger", "Trigger", kSw }, { "adsr_attack", "A", kSl }, { "adsr_decay", "D", kSl },
        { "adsr_sustain", "S", kSl }, { "adsr_release", "R", kSl } } },
    { "LFO MODE", GraphKind::None,
      { { "modulator_sync", "Sync" }, { "modulator_fast", "Fast" }, { "modulator_trig", "Key Trig" } } },
    { "VOICE", GraphKind::None,
      { { "voice_mode", "Voicing" }, { "porta_mode", "Porta" }, { "porta_time", "Time" },
        { "transpose", "Transpose" } } },
    { "CHORD", GraphKind::None,
      { { "chord2_on", "Voice 2" }, { "chord2_shift", "Shift" }, { "chord3_on", "Voice 3" },
        { "chord3_shift", "Shift" }, { "chord4_on", "Voice 4" }, { "chord4_shift", "Shift" } } },
    { "CONTROL", GraphKind::None,
      { { "vco_fine", "Fine" }, { "bend_vco", "Bend VCO" }, { "bend_vcf", "Bend VCF" },
        { "stereo_spread", "Spread" }, { "unison_detune", "Detune" } } },
    { "DRAW", GraphKind::Draw, { { "draw_mode", "Mode" }, { "draw_mult", "Multiply" } } },
    { "CHOP", GraphKind::Chop, { { "chop_overtone", "Overtone" }, { "chop_comb", "Comb" } } },
    // Maquina BL-101 FX (+ MIX: Gain)
    { "CHORUS", GraphKind::None, { { "chorus_mode", "Mode" } } },
    { "DELAY", GraphKind::None,
      { { "dly_level", "Level" }, { "dly_time", "Time" }, { "dly_feedback", "Feedback" }, { "dly_sync", "Sync" } } },
    { "REVERB", GraphKind::None,
      { { "rev_type", "Type" }, { "rev_level", "Level" }, { "rev_time", "Time" }, { "rev_predelay", "Pre-Delay" } } },
    { "LO-FI", GraphKind::None, { { "lofi_mode", "Mode" }, { "lofi_amount", "Amount" }, { "lofi_mix", "Mix" } } },
};

enum class CtlType { Knob, Select, Toggle, Slider, Switch };

struct Ctl {
    uint32_t param;
    CtlType type;
    const char* label;
    float x, y, w, h;   // cell (the new look: the hit area)
    // the new look's fixed layouts (size > 0): the control's centre and size (knob
    // diameter; a switch / selector: its body w x h around the centre), the label's
    // bottom line and the value's centre line ('\n' in label: two lines)
    float ax = 0, ay = 0, size = 0, bw = 0, bh = 0, labelY = 0, valueY = 0;
};
struct Graph {
    GraphKind kind;
    float x, y, w, h;
    int attack = -1, decay = -1, slope = -1, amount = -1;
    // Maquina BL-101: DRAW: the mode and draw_1 (the 16 steps follow it);
    // CHOP: overtone, the mask selectors and the Custom bits per source
    int mode = -1, first = -1;
    int mask[4] = { -1, -1, -1, -1 }, bitsLo[4] = { -1, -1, -1, -1 }, bitsHi[4] = { -1, -1, -1, -1 };
};
struct Box { float x, y, w, h; bool hit(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; } };
struct Section {
    const char* title;
    float x, y, w, h;
    // Maquina Dredrum OSC / Plethora CHANNEL sections: 0..2 (else -1), with a ROLL
    // button and the "main RANDOM includes it" switch in the title bar
    int osc = -1;
    Box roll {}, randOn {};
};
// the RANDOM panel: two buttons and one level selector per Randomizer group
// (Oneiroi: the module's TARGET and AMOUNT switches instead of the groups)
struct RandPanel {
    Box random, back;
    Box oTarget, oAmount;
    Box cell[maquina::kNumRandGroups]; // title (lock toggle) + range bar
    bool used[maquina::kNumRandGroups]; // group exists for this voice
};

int findParam(const char* symbol)
{
    const VoiceDef& d = voiceDef();
    for (uint32_t i = 0; i < maquina::numAllParams(d); ++i)
        if (!std::strcmp(paramAt(d, i).symbol, symbol))
            return int(i);
    return -1;
}

// the style an item was given (kSections), Auto if none
ItemStyle styleOf(uint32_t i);

CtlType typeOf(uint32_t i)
{
    switch (styleOf(i)) {
    case ItemStyle::Slider: return CtlType::Slider;
    case ItemStyle::Switch: return CtlType::Switch;
    default: break;
    }
    switch (paramAt(voiceDef(), i).kind) {
    case ParamKind::Int: return CtlType::Knob;
    case ParamKind::Toggle:
    case ParamKind::Retrigger: return CtlType::Toggle;
    default: return CtlType::Select;
    }
}

ItemStyle styleOf(uint32_t i)
{
    const char* sym = paramAt(voiceDef(), i).symbol;
    for (const SectionSpec& s : kSections)
        for (const ItemSpec& it : s.items) {
            if (!it.symbol) break;
            if (!std::strcmp(it.symbol, sym)) return it.style;
        }
    return ItemStyle::Auto;
}

constexpr float kSliderW = 26.f;
constexpr float kSliderCellH = 150.f; // a row with sliders / switches
constexpr float kGraphCellH = 104.f;  // a row with the DRAW / CHOP editors
// a slide switch: the lever plus its position labels
float switchWidth(uint32_t i)
{
    const VoiceParam& p = paramAt(voiceDef(), i);
    size_t longest = 3;
    for (uint8_t k = 0; p.labels && k < p.numLabels; ++k) longest = std::max(longest, std::strlen(p.labels[k]));
    return 20.f + 4.6f * float(longest);
}

float cellWidth(uint32_t i)
{
    switch (typeOf(i)) {
    case CtlType::Knob: return kKnobW;
    case CtlType::Toggle: return kToggleW;
    case CtlType::Slider: return kSliderW;
    case CtlType::Switch: return switchWidth(i);
    default: return styleOf(i) == ItemStyle::Narrow ? 70.f : kSelectW;
    }
}

float graphWidth(GraphKind k)
{
    switch (k) {
    case GraphKind::Draw: return 16 * 12.f;
    case GraphKind::Chop: return 36.f + 16 * 10.f + 48.f;
    default: return kGraphW;
    }
}

// Maquina LA-50: the PG-1000 programmer's slider field. Each slot has a partial
// field, a common field and a patch field (symbols without the u_p1_ / u_ /
// patch_ prefix, nullptr = none); the selection (PATCH / COMMON / PARTIAL)
// decides which one the slider edits.
struct La50Slot {
    Box box;
    const char* field[3]; // partial, common, patch
    const char* label[3];
    int param[7] = { -1, -1, -1, -1, -1, -1, -1 }; // partial U1 U2 L1 L2, common U L, patch
};
struct La50Group { float x0, x1, y; const char* title; int kind; }; // kind: 0 partial, 1 common, 2 patch
struct La50Panel {
    bool used = false;
    std::vector<La50Slot> slots;
    std::vector<La50Group> groups;
    Box lcd, patch, common[2], partial[4], mute[2], import, init;
    Box env[3];  // TVF, TVA (the selected partial), P-ENV (its tone)
    Box gainBox; // the output gain's place (row 3's free end)
    Box pcmPrev, pcmName, pcmNext; // the PCM wave selector (row 1's free end)
};

// the new look's panel decorations: printed rules, the logo block
struct Deco {
    enum Kind { VRule, HRule, Logo } kind;
    Box b;
};

struct Layout {
    std::vector<Section> sections;
    std::vector<Ctl> ctls;
    std::vector<Graph> graphs;
    RandPanel rand;
    La50Panel la50;
    float height = 400.f;
    // the new look: the machine panel, the random drawer, decorations
    Box panel {}, drawer {};
    std::vector<Deco> decos;
};

void makeNewLayout(Layout& L); // ui/Layouts.cpp

// the PG-1000's three rows (D-50 field names, tools/la50_layout.py)
struct La50SlotSpec { const char* partial; const char* pLabel; const char* common; const char* cLabel;
                      const char* patch; const char* kLabel; };
const La50SlotSpec kLa50Row1[] = {
    { "wg_coarse", "COARSE", nullptr, nullptr, "l_key_shift", "L-KEY SH" },
    { "wg_fine", "FINE", nullptr, nullptr, "l_fine", "L-FINE" },
    { "wg_keyfollow", "KF", nullptr, nullptr, "u_key_shift", "U-KEY SH" },
    { "wg_lfo_mode", "LFO MD", nullptr, nullptr, "u_fine", "U-FINE" },
    { "wg_penv_mode", "ENV MD", nullptr, nullptr, "tone_balance", "TONE BAL" },
    { "wg_bend_mode", "BEND MD", nullptr, nullptr, "bender_range", "BEND RNG" },
    { "wg_waveform", "WF", nullptr, nullptr, "after_bend", "AFT BEND" },
    { "wg_pw", "PW", nullptr, nullptr, "porta_mode", "PORTA MD" },
    { "wg_pw_velo", "PW VELO", nullptr, nullptr, "porta_time", "PORTA T" },
    { "wg_pw_after", "PW AFTER", nullptr, nullptr, "hold_mode", "HOLD MD" },
    { "wg_pw_lfo", "PWM LFO", nullptr, nullptr, "chase_mode", "CHASE MD" },
    { "wg_pw_lfo_depth", "PWM DPTH", nullptr, nullptr, "chase_level", "CHASE L" },
    { nullptr, nullptr, nullptr, nullptr, "chase_time", "CHASE T" }, // the PCM wave: its own selector, right
    { nullptr, nullptr, nullptr, nullptr, "key_mode", "KEY MODE" },
    { nullptr, nullptr, nullptr, nullptr, "split_point", "SPLIT" },
};
const La50SlotSpec kLa50Row2[] = {
    { "tvf_cutoff", "CUTOFF", "structure", "STRUCT", nullptr, nullptr },
    { "tvf_reso", "RESO", "partial_balance", "PART BAL", nullptr, nullptr },
    { "tvf_keyfollow", "KF", "pmod_lfo", "LFO D", nullptr, nullptr },
    { "tvf_bias_point", "BIAS PT", "pmod_lever", "LEVER", nullptr, nullptr },
    { "tvf_bias_level", "BIAS LV", "pmod_after", "AFTER", nullptr, nullptr },
    { "tvf_env_depth", "ENV D", "lfo1_wave", "WF", nullptr, nullptr },
    { "tvf_env_velo", "ENV VELO", "lfo1_rate", "RATE", nullptr, nullptr },
    { "tvf_lfo", "LFO SEL", "lfo1_delay", "DELAY", nullptr, nullptr },
    { "tvf_lfo_depth", "LFO D", "lfo1_sync", "SYNC", nullptr, nullptr },
    { "tvf_after", "AFTER", "lfo2_wave", "WF", nullptr, nullptr },
    { "tvf_t1", "T1", "lfo2_rate", "RATE", nullptr, nullptr },
    { "tvf_t2", "T2", "lfo2_delay", "DELAY", nullptr, nullptr },
    { "tvf_t3", "T3", "lfo2_sync", "SYNC", nullptr, nullptr },
    { "tvf_t4", "T4", "lfo3_wave", "WF", nullptr, nullptr },
    { "tvf_t5", "T5", "lfo3_rate", "RATE", nullptr, nullptr },
    { "tvf_env_tkf", "TKF", "lfo3_delay", "DELAY", nullptr, nullptr },
    { "tva_t1", "T1", "lfo3_sync", "SYNC", nullptr, nullptr },
    { "tva_t2", "T2", "penv_t1", "T1", nullptr, nullptr },
    { "tva_t3", "T3", "penv_t2", "T2", nullptr, nullptr },
    { "tva_t4", "T4", "penv_t3", "T3", nullptr, nullptr },
    { "tva_t5", "T5", "penv_t4", "T4", nullptr, nullptr },
    { "tva_tkf", "TKF", "penv_tkf", "TKF", nullptr, nullptr },
};
const La50SlotSpec kLa50Row3[] = {
    { "tva_level", "LEVEL", "eq_low_freq", "FREQ", nullptr, nullptr },
    { "tva_velo", "VELO", "eq_low_gain", "GAIN", nullptr, nullptr },
    { "tva_bias_point", "BIAS PT", "eq_high_freq", "FREQ", nullptr, nullptr },
    { "tva_bias_level", "BIAS LV", "eq_high_q", "Q", nullptr, nullptr },
    { "tva_lfo", "LFO SEL", "eq_high_gain", "GAIN", nullptr, nullptr },
    { "tva_lfo_depth", "LFO D", "chorus_type", "TYPE", nullptr, nullptr },
    { "tva_after", "AFTER", "chorus_rate", "RATE", nullptr, nullptr },
    { "tvf_l1", "L1", "chorus_depth", "DEPTH", nullptr, nullptr },
    { "tvf_l2", "L2", "chorus_balance", "BAL", nullptr, nullptr },
    { "tvf_l3", "L3", nullptr, nullptr, "output_mode", "OUT MODE" },
    { "tvf_sus", "SUS L", nullptr, nullptr, "reverb_type", "REV TYPE" },
    { "tvf_end", "END L", nullptr, nullptr, "reverb_balance", "REV BAL" },
    { "tvf_env_dkf", "DKF", nullptr, nullptr, "total_volume", "VOLUME" },
    { "tva_l1", "L1", "penv_l0", "L0", nullptr, nullptr },
    { "tva_l2", "L2", "penv_l1", "L1", nullptr, nullptr },
    { "tva_l3", "L3", "penv_l2", "L2", nullptr, nullptr },
    { "tva_sus", "SUS L", "penv_sus", "SUS L", nullptr, nullptr },
    { "tva_end", "END L", "penv_end", "END L", nullptr, nullptr },
    { "tva_velo_follow", "VELO", "penv_velo", "VELO", nullptr, nullptr },
};
// group brackets: row, first, last column, kind, title
struct La50GroupSpec { int row, c0, c1, kind; const char* title; };
const La50GroupSpec kLa50Groups[] = {
    { 0, 0, 5, 0, "WG PITCH" }, { 0, 6, 11, 0, "WG WAVEFORM" }, { 0, 13, 14, 2, "KEY" },
    { 1, 0, 9, 0, "TVF" }, { 1, 10, 15, 0, "TVF ENV" }, { 1, 16, 21, 0, "TVA ENV" },
    { 1, 2, 4, 1, "PITCH MOD" }, { 1, 5, 8, 1, "LFO 1" }, { 1, 9, 12, 1, "LFO 2" }, { 1, 13, 16, 1, "LFO 3" },
    { 1, 17, 21, 1, "PITCH ENV" },
    { 2, 0, 6, 0, "TVA" }, { 2, 7, 12, 0, "TVF ENV" }, { 2, 13, 18, 0, "TVA ENV" },
    { 2, 0, 1, 1, "LOW EQ" }, { 2, 2, 4, 1, "HIGH EQ" }, { 2, 5, 8, 1, "CHORUS" }, { 2, 9, 12, 2, "OUTPUT" },
    { 2, 13, 18, 1, "PITCH ENV" },
};

int findParam(const char* symbol);

// the slider field and the select panel; returns the y below it
float makeLa50(Layout& L, float y)
{
    La50Panel& P = L.la50;
    P.used = true;
    const float panelW = 150.f, gx = kMargin + panelW + kGap, gw = kWidth - kMargin - gx;
    // a small gap between the partial groups (WG PITCH | WG WAVEFORM ...; at most 2 per row)
    const float groupGap = 14.f;
    const float colW = (gw - 2 * groupGap) / 22.f;
    const float rowH = 196.f;
    auto colX = [&](int row, int c) {
        float x = gx + c * colW;
        for (const La50GroupSpec& g : kLa50Groups)
            if (g.row == row && (g.kind == 0 || g.row == 0) && g.c0 > 0 && g.c0 <= c) x += groupGap;
        return x;
    };
    // select panel
    float py = y;
    P.lcd = { kMargin, py, panelW, 74 };
    py += 82;
    P.patch = { kMargin, py + 14, panelW, 22 };
    py += 44;
    P.common[0] = { kMargin, py + 14, panelW / 2 - 3, 22 };
    P.common[1] = { kMargin + panelW / 2 + 3, py + 14, panelW / 2 - 3, 22 };
    py += 44;
    for (int k = 0; k < 4; ++k) P.partial[k] = { kMargin + k * (panelW + 4) / 4, py + 14, panelW / 4 - 4, 22 };
    py += 44;
    P.mute[0] = { kMargin, py + 14, panelW / 2 - 3, 22 };
    P.mute[1] = { kMargin + panelW / 2 + 3, py + 14, panelW / 2 - 3, 22 };
    py += 52;
    P.init = { kMargin, py, 52, 26 };
    P.import = { kMargin + 58, py, panelW - 58, 26 };
    // envelope shapes under the buttons
    {
        const float ey = py + 46, bottom = y + 3 * 196.f - 8, gap = 8;
        const float eh = (bottom - ey - 2 * gap) / 3;
        for (int k = 0; k < 3; ++k) P.env[k] = { kMargin, ey + k * (eh + gap), panelW, eh };
    }
    // the rows
    const La50SlotSpec* rows[3] = { kLa50Row1, kLa50Row2, kLa50Row3 };
    const int counts[3] = { int(sizeof(kLa50Row1) / sizeof(kLa50Row1[0])), int(sizeof(kLa50Row2) / sizeof(kLa50Row2[0])),
                            int(sizeof(kLa50Row3) / sizeof(kLa50Row3[0])) };
    static const char* const kPartial[4] = { "u_p1_", "u_p2_", "l_p1_", "l_p2_" };
    static const char* const kCommon[2] = { "u_", "l_" };
    for (int r = 0; r < 3; ++r) {
        const float ry = y + r * rowH;
        for (int c = 0; c < counts[r]; ++c) {
            const La50SlotSpec& sp = rows[r][c];
            La50Slot sl;
            sl.box = { colX(r, c), ry + 26, colW, rowH - 64 };
            sl.field[0] = sp.partial; sl.field[1] = sp.common; sl.field[2] = sp.patch;
            sl.label[0] = sp.pLabel; sl.label[1] = sp.cLabel; sl.label[2] = sp.kLabel;
            char sym[64];
            for (int k = 0; k < 4 && sp.partial; ++k) {
                std::snprintf(sym, sizeof sym, "%s%s", kPartial[k], sp.partial);
                sl.param[k] = findParam(sym);
            }
            for (int k = 0; k < 2 && sp.common; ++k) {
                std::snprintf(sym, sizeof sym, "%s%s", kCommon[k], sp.common);
                sl.param[4 + k] = findParam(sym);
            }
            if (sp.patch) {
                std::snprintf(sym, sizeof sym, "patch_%s", sp.patch);
                sl.param[6] = findParam(sym);
            }
            P.slots.push_back(sl);
        }
    }
    // the PCM wave selector in the free end of row 1
    {
        // up in the title strip, from the CHASE T column on; its label on the column-label line
        const float fx = colX(0, counts[0] - 3) + 14, fw = std::min(200.f, gx + gw - fx - 8), py0 = y + 1;
        P.pcmPrev = { fx, py0, 18, 16 };
        P.pcmName = { fx + 20, py0, fw - 40, 16 };
        P.pcmNext = { fx + fw - 18, py0, 18, 16 };
    }
    // the output gain (ExtraParams) in the free end of row 3 (no MIX row of its own)
    {
        const float fx = colX(2, counts[2] - 1) + colW + 10, fw = gx + gw - fx;
        P.gainBox = { fx, y + 2 * rowH + 26, fw, rowH - 64 };
        const int gain = findParam("gain");
        if (gain >= 0)
            L.ctls.push_back({ uint32_t(gain), CtlType::Knob, "Gain", fx + (fw - 70) / 2, y + 2 * rowH + 50, 70, kCellH });
    }
    for (const La50GroupSpec& g : kLa50Groups) {
        const float ry = y + g.row * rowH;
        P.groups.push_back({ colX(g.row, g.c0) + 2, colX(g.row, g.c1) + colW - 2,
                             g.kind == 0 ? ry + 6 : ry + rowH - 14, g.title, g.kind });
    }
    L.sections.push_back({ "", gx - 4, y, gw + 4, 3 * rowH - 4 });
    return y + 3 * rowH;
}

Layout makeLayout()
{
    Layout L;
    if (kNew) {
        makeNewLayout(L);
        return L;
    }
    struct Pending {
        const SectionSpec* spec;
        std::vector<std::pair<uint32_t, const char*>> items;
        float natural;
    };
    std::vector<Pending> secs;
    for (const SectionSpec& s : kSections) {
        Pending p { &s, {}, 0.f };
        for (const ItemSpec& it : s.items) {
            if (!it.symbol) break;
            const int idx = findParam(it.symbol);
            if (idx >= 0) p.items.push_back({ uint32_t(idx), it.label });
        }
        if (p.items.empty()) continue;
        if (voiceDef().voice == maquina::kLa50Voice && !std::strcmp(s.title, "MIX")) continue;
        float w = 2 * kSectionPad + (s.graph != GraphKind::None ? graphWidth(s.graph) + kGap : 0.f);
        for (auto& it : p.items) w += cellWidth(it.first);
        w += kGap * float(p.items.size() - 1);
        p.natural = w;
        secs.push_back(std::move(p));
    }

    // Rows per voice family, chosen so each row is nearly full (3 rows).
    // Sections a voice does not have are skipped; anything not listed (or a
    // row that would overflow) falls back to greedy packing.
    // (MIX with Gain is too wide to share a row with LFO and VELOCITY)
    static const char* const kDrumRows[4][6] = {
        { "OSCILLATOR", "AMP EG", "PITCH EG" },
        { "TRANSIENT", "LFO", "VELOCITY" },
        { "FM", "FILTER", "KEY", "MIX" } };
    static const char* const kSnareRows[4][6] = {
        { "OSCILLATOR", "NOISE", "PITCH EG", "KEY" },
        { "TRANSIENT", "LFO", "VELOCITY" },
        { "AMP EG", "FILTER", "MIX" } };
    static const char* const kMetalRows[4][6] = {
        { "OSCILLATOR", "AMP EG", "KEY", "VELOCITY" },
        { "MOD OSCILLATORS", "TRANSIENT", "FILTER" },
        { "LFO", "MIX" } };
    static const char* const kDredrumRows[4][6] = {
        { "OSC 1" },
        { "OSC 2" },
        { "OSC 3", "MODE", "MIX" } };
    static const char* const kPlethoraRows[4][6] = {
        { "CHANNEL A" },
        { "CHANNEL B" },
        { "CHANNEL C", "GLOBAL", "MODE", "MIX" } };
    static const char* const kOneiroiRows[4][6] = {
        { "LOOPER", "OSCILLATORS", "MODULATION" },
        { "FILTER", "RESONATOR", "ECHO", "AMBIENCE", "GLOBAL" },
        { "MIX", "ENVELOPE" } };
    static const char* const kKlangRows[4][6] = {
        { "MODEL", "SOUND" },
        { "ENV MOD", "LPG", "MIX" },
        {} };
    static const char* const kKlongRows[4][6] = {
        { "MODEL", "MIX" },
        { "RESONATOR" },
        {} };
    static const char* const kAcidaRows[4][6] = {
        { "SYNTH" },
        { "PLAY", "MIX" },
        {} };
    static const char* const kAcidaFxRows[4][6] = {
        { "DISTORTION", "COMPRESSOR", "MIX" },
        { "PCF" },
        { "DELAY" } };
    // Maquina BL-101: the panel's strip first
    static const char* const kBl101Rows[4][6] = {
        { "MODULATOR", "VCO", "SOURCE MIXER", "VCF", "VCA", "ENV" },
        { "LFO MODE", "VOICE", "CHORD" },
        { "CONTROL", "MIX" },
        { "DRAW", "CHOP" } };
    static const char* const kBl101FxRows[4][6] = {
        { "CHORUS", "DELAY", "REVERB" },
        { "LO-FI", "MIX" } };
    const uint8_t v = voiceDef().voice;
    const char* const (*plan)[6] = v == maquina::kBl101Voice     ? kBl101Rows
                                   : v == maquina::kBl101FxVoice ? kBl101FxRows
                                   : v == maquina::kAcidaVoice      ? kAcidaRows
                                   : v == maquina::kAcidaFxVoice  ? kAcidaFxRows
                                   : v == maquina::kKlangVoice    ? kKlangRows
                                   : maquina::isKlong(v)          ? kKlongRows
                                   : maquina::isOneiroi(v)        ? kOneiroiRows
                                   : v == maquina::kPlethoraVoice ? kPlethoraRows
                                   : v == maquina::kDredrumVoice ? kDredrumRows
                                   : v <= 2                    ? kDrumRows
                                   : v == 3                    ? kSnareRows
                                                               : kMetalRows;

    const float avail = kWidth - 2 * kMargin;
    std::vector<std::vector<Pending*>> rows;
    std::vector<bool> placed(secs.size(), false);
    for (int r = 0; r < 4; ++r) {
        std::vector<Pending*> row;
        float used = 0.f;
        for (int k = 0; k < 6 && plan[r][k]; ++k)
            for (size_t i = 0; i < secs.size(); ++i)
                if (!placed[i] && !std::strcmp(secs[i].spec->title, plan[r][k])) {
                    const float need = (row.empty() ? 0.f : kGap) + secs[i].natural;
                    if (used + need > avail) continue;
                    used += need;
                    row.push_back(&secs[i]);
                    placed[i] = true;
                }
        if (!row.empty()) rows.push_back(row);
    }
    for (size_t i = 0; i < secs.size(); ++i) { // leftovers: greedy
        if (placed[i]) continue;
        float used = 0.f;
        if (!rows.empty())
            for (auto* s : rows.back()) used += s->natural + kGap;
        if (rows.empty() || used + secs[i].natural > avail)
            rows.emplace_back();
        rows.back().push_back(&secs[i]);
    }

    float y = kHeaderH + kMargin;
    if (voiceDef().voice == maquina::kLa50Voice) y = makeLa50(L, y) + kGap;
    for (auto& row : rows) {
        // the row's cell height: knobs, or BL-101's sliders / editors
        float cellH = kCellH;
        for (auto* s : row) {
            if (s->spec->graph == GraphKind::Draw || s->spec->graph == GraphKind::Chop) cellH = std::max(cellH, kGraphCellH);
            for (auto& it : s->items)
                if (typeOf(it.first) == CtlType::Slider || typeOf(it.first) == CtlType::Switch)
                    cellH = std::max(cellH, kSliderCellH);
        }
        const float rowH = kTitleH + cellH + kSectionPad;
        float natural = 0.f;
        for (auto* s : row) natural += s->natural;
        const float spare = avail - natural - kGap * float(row.size() - 1);
        float x = kMargin;
        for (auto* s : row) {
            const float w = s->natural + spare * (s->natural / natural);
            L.sections.push_back({ s->spec->title, x, y, w, rowH });
            const char* t = s->spec->title;
            const int chan = voiceDef().voice == maquina::kDredrumVoice && !std::strncmp(t, "OSC ", 4) ? t[4] - '1'
                             : voiceDef().voice == maquina::kPlethoraVoice && !std::strncmp(t, "CHANNEL ", 8)
                                 ? t[8] - 'A'
                                 : -1;
            if (chan >= 0) {
                Section& sec = L.sections.back();
                sec.osc = chan;
                sec.roll = { x + w - kSectionPad - 40.f, y + 3, 40.f, kTitleH - 5 };
                sec.randOn = { sec.roll.x - kGap - 58.f, y + 3, 58.f, kTitleH - 5 };
            }
            // graph first; the stretch is shared by the graph and the items
            const float extra = w - s->natural;
            float cx = x + kSectionPad;
            const float cy = y + kTitleH;
            const bool hasGraph = s->spec->graph != GraphKind::None;
            if (hasGraph) {
                Graph g;
                g.kind = s->spec->graph;
                const float gw = graphWidth(g.kind) + extra * 0.5f;
                g.x = cx; g.y = cy + 2; g.w = gw; g.h = cellH - 6;
                if (g.kind == GraphKind::Draw) {
                    g.mode = findParam("draw_mode");
                    g.first = findParam("draw_1");
                } else if (g.kind == GraphKind::Chop) {
                    static const char* const kSrc[4] = { "sqr", "saw", "sub", "noise" };
                    char sym[32];
                    g.amount = findParam("chop_overtone");
                    for (int k = 0; k < 4; ++k) {
                        std::snprintf(sym, sizeof sym, "chop_mask_%s", kSrc[k]);
                        g.mask[k] = findParam(sym);
                        std::snprintf(sym, sizeof sym, "chop_bits_%s_lo", kSrc[k]);
                        g.bitsLo[k] = findParam(sym);
                        std::snprintf(sym, sizeof sym, "chop_bits_%s_hi", kSrc[k]);
                        g.bitsHi[k] = findParam(sym);
                    }
                } else if (g.kind == GraphKind::Amp) {
                    g.attack = findParam("amp_attack");
                    g.decay = findParam("amp_decay");
                    g.slope = findParam("amp_slope");
                } else {
                    g.decay = findParam("pitch_decay");
                    g.slope = findParam("pitch_slope");
                    g.amount = findParam("pitch_amount");
                }
                L.graphs.push_back(g);
                cx += gw + kGap;
            }
            const float itemExtra = (hasGraph ? extra * 0.5f : extra) / float(s->items.size());
            for (auto& it : s->items) {
                const CtlType t = typeOf(it.first);
                // sliders and switches don't grow: the stretch goes between them
                const bool fixedW = t == CtlType::Slider || t == CtlType::Switch;
                const float cw = cellWidth(it.first) + (fixedW ? 0.f : itemExtra);
                const float tall = fixedW ? cellH : kCellH;
                L.ctls.push_back({ it.first, t, it.second, cx + (fixedW ? itemExtra * 0.5f : 0.f), cy,
                                   cw, tall });
                cx += cw + (fixedW ? itemExtra : 0.f) + kGap;
            }
            x += w + kGap;
        }
        y += rowH + kGap;
    }

    // RANDOM panel: full width, [RANDOM]/[BACK] | one cell per group
    {
        const float h = kTitleH + kCellH + kSectionPad;
        L.sections.push_back({ "RANDOM", kMargin, y, avail, h });
        const float cy = y + kTitleH;
        float x = kMargin + kSectionPad;
        L.rand.random = { x, cy + 2, 96.f, 30.f };
        L.rand.back = { x, cy + 38, 96.f, 22.f };
        x += 96.f + 3 * kGap;
        if (maquina::isOneiroi(voiceDef().voice)) {
            // the module's two switches, as selectors
            L.rand.oTarget = { x, cy, kSelectW + 20.f, kCellH };
            L.rand.oAmount = { x + kSelectW + 20.f + kGap, cy, kSelectW + 20.f, kCellH };
            for (int i = 0; i < maquina::kNumRandGroups; ++i) L.rand.used[i] = false;
            y += h + kGap;
            L.height = y - kGap + kMargin;
            return L;
        }
        int n = 0;
        for (int i = 0; i < maquina::kNumRandGroups; ++i)
            n += (L.rand.used[i] = maquina::randGroupUsed(voiceDef(), i));
        const float cw = (kMargin + avail - kSectionPad - x - kGap * float(n - 1)) / float(n);
        for (int i = 0; i < maquina::kNumRandGroups; ++i) {
            L.rand.cell[i] = { x, cy, L.rand.used[i] ? cw : 0.f, L.rand.used[i] ? kCellH : 0.f };
            if (L.rand.used[i]) x += cw + kGap;
        }
        y += h + kGap;
    }
    L.height = y - kGap + kMargin;
    return L;
}

const Layout& layout()
{
    static const Layout l = makeLayout();
    return l;
}

// ---------------------------------------------------------------------------
// zoom: desktop scale detection + a remembered user choice

const float kZooms[] = { 0.75f, 1.f, 1.25f, 1.5f, 1.75f, 2.f, 2.5f, 3.f };
constexpr int kNumZooms = sizeof(kZooms) / sizeof(kZooms[0]);

int nearestZoom(double z)
{
    int best = 1;
    for (int i = 0; i < kNumZooms; ++i)
        if (std::fabs(kZooms[i] - z) < std::fabs(kZooms[best] - z)) best = i;
    return best;
}

// DPF only knows Xft.dpi on X11; many desktops (and XWayland) set GDK_SCALE
// or QT_SCALE_FACTOR instead.
double desktopScale(double dpfScale)
{
    if (dpfScale > 1.01) return dpfScale;
    for (const char* var : { "GDK_SCALE", "QT_SCALE_FACTOR", "ELM_SCALE" })
        if (const char* v = std::getenv(var)) {
            const double f = std::atof(v);
            if (f > 1.01 && f <= 4.0) return f;
        }
    return dpfScale > 0 ? dpfScale : 1.0;
}

// ~/.config/maquina (macOS: ~/Library/Application Support/Maquina), no
// trailing slash; empty if unknown
std::string configDir()
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    return base ? std::string(base) + "\\Maquina" : std::string();
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/Library/Application Support/Maquina";
    return std::string();
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) return std::string(x) + "/maquina";
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/.config/maquina";
    return std::string();
#endif
}

void makeDirs(const std::string& path) // mkdir -p
{
#ifndef _WIN32
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
#endif
}

std::string configPath()
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    return base ? std::string(base) + "\\Maquina\\ui.conf" : std::string();
#else
    const std::string dir = configDir();
    return dir.empty() ? dir : dir + "/ui.conf";
#endif
}

int loadZoom() // -1 if never set
{
    const std::string path = configPath();
    if (path.empty()) return -1;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return -1;
    float z = 0.f;
    const int ok = std::fscanf(f, "zoom=%f", &z);
    std::fclose(f);
    return ok == 1 && z > 0.f ? nearestZoom(z) : -1;
}

void saveZoom(int idx)
{
    std::string path = configPath();
    if (path.empty()) return;
#ifndef _WIN32
    const std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.substr(0, dir.rfind('/')).c_str(), 0755);
    mkdir(dir.c_str(), 0755);
#endif
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fprintf(f, "zoom=%.2f\n", kZooms[idx]);
        std::fclose(f);
    }
}

// header zoom control geometry (panel units)
constexpr float kZoomW = 92.f;
constexpr float kZoomX = kWidth - kMargin - kZoomW;
constexpr float kZoomY = 7.f, kZoomH = 17.f;

// header preset bar: [<] [ name  category ] [>] [SAVE]
constexpr float kPresetX = 170.f, kPresetNameW = 260.f, kArrowW = 18.f, kSaveW = 48.f;
constexpr float kPrevX = kPresetX, kNameX = kPrevX + kArrowW + 2, kNextX = kNameX + kPresetNameW + 2;
constexpr float kSaveX = kNextX + kArrowW + 8;
constexpr float kInitW = 40.f, kInitX = kSaveX + kSaveW + 6;

#include "ui/Layouts.cpp"

} // namespace maquina_ui
using namespace maquina_ui;

// ===========================================================================

class DrumUI : public UI {
public:
    DrumUI()
        : UI(uint(kWidth), uint(layout().height), false),
          fValues(maquina::numAllParams(voiceDef()), 0.f),
          fSections(layout().sections),
          fCtls(layout().ctls),
          fGraphs(layout().graphs),
          fHeight(layout().height)
    {
        loadSharedResources();
        if (kNew) loadThemeFonts(*this);
        for (uint32_t i = 0; i < fValues.size(); ++i)
            fValues[i] = defOf(i);
        loadPresetList();

        const int saved = loadZoom();
        fZoom = saved >= 0 ? saved : nearestZoom(desktopScale(getScaleFactor()));
        setGeometryConstraints(uint(kWidth * 0.75f), uint(fHeight * 0.75f), true);
        applyZoom();
    }

    void applyZoom()
    {
        const float z = kZooms[fZoom];
        setSize(uint(std::lround(kWidth * z)), uint(std::lround(fHeight * z)));
    }

protected:
    // ------------------------------------------------------------- host side
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < fValues.size()) {
            if (value != fValues[index] && !fApplying) { // edited elsewhere: keep this sound
                endRun();
                if (maquina::isSoundParam(voiceDef(), index)) fPresetDirty = true;
            }
            fValues[index] = value;
            repaint();
        }
    }

    void programLoaded(uint32_t index) override
    {
        endRun();
        const maquina::VoicePresets& fp = maquina::k_factory_presets[MAQUINA_VOICE_INDEX];
        if (index == 0 || index > fp.count)
            maquina::applyPreset(voiceDef(), nullptr, 0, fValues.data());
        else
            maquina::applyPreset(voiceDef(), fp.presets[index - 1].values, fp.presets[index - 1].numValues, fValues.data());
        fPreset = index == 0 || index > fp.count ? -1 : int(index - 1);
        fPresetName = fPreset < 0 ? "Init" : fPresets[size_t(fPreset)].name;
        fPresetDirty = false;
        repaint();
    }

    // ------------------------------------------------------------- values
    float minOf(uint32_t i) const
    {
        const VoiceParam& p = paramAt(voiceDef(), i);
        return p.kind == ParamKind::Int ? float(p.fwMin + p.offset) : 0.f;
    }
    float maxOf(uint32_t i) const
    {
        const VoiceParam& p = paramAt(voiceDef(), i);
        switch (p.kind) {
        case ParamKind::Int: return float(p.fwMax + p.offset);
        case ParamKind::Toggle:
        case ParamKind::Retrigger: return 1.f;
        default: return float(p.numLabels - 1);
        }
    }
    float defOf(uint32_t i) const { return float(paramAt(voiceDef(), i).def); }
    int intOf(int param) const { return param < 0 ? 0 : int(std::lround(fValues[uint32_t(param)])); }
    int fwOf(int param) const // firmware 0..127 value of an Int parameter
    {
        if (param < 0) return 0;
        return intOf(param) - voiceDef().params[uint32_t(param)].offset;
    }

    void setValue(uint32_t i, float v, bool gesture)
    {
        v = std::clamp(float(std::lround(v)), minOf(i), maxOf(i));
        if (v == fValues[i]) return;
        if (!fApplying) {
            endRun();
            if (maquina::isSoundParam(voiceDef(), i)) fPresetDirty = true;
        }
        fValues[i] = v;
        if (gesture) editParameter(i, true);
        setParameterValue(i, v);
        if (gesture) editParameter(i, false);
        repaint();
    }

    void valueText(uint32_t i, char* buf, size_t n) const
    {
        const float v = fValues[i];
        const VoiceParam& p = paramAt(voiceDef(), i);
        const int iv = int(std::lround(v));
        if (p.kind == ParamKind::Int && !std::strcmp(p.symbol, "osc_pitch")) {
            std::snprintf(buf, n, "%+.1f st", double(iv) / 10.0); // Oneiroi: tenths of a semitone
        } else if (!std::strcmp(p.symbol, "env_attack")) { // Oneiroi's envelope (OneiroiControls.hpp)
            if (iv <= 0) std::snprintf(buf, n, "0 ms");
            else fmtTime(buf, n, maquina::oneiroi::attackSeconds(iv));
        } else if (!std::strcmp(p.symbol, "env_decay") || !std::strcmp(p.symbol, "env_release")) {
            fmtTime(buf, n, maquina::oneiroi::fallSeconds(iv));
        } else if (!std::strcmp(p.symbol, "env_sustain")) {
            std::snprintf(buf, n, "%.0f %%", maquina::oneiroi::sustainLevel(iv) * 100.0);
        } else if (kNew && !std::strcmp(p.symbol, "gain")) {
            std::snprintf(buf, n, "%+d dB", iv);
        } else if (p.kind == ParamKind::Int) {
            if (p.offset < 0) std::snprintf(buf, n, "%+d", iv); // bipolar
            else std::snprintf(buf, n, "%d", iv);
        } else if (p.labels && iv >= 0 && iv < p.numLabels) {
            std::snprintf(buf, n, "%s", p.labels[iv]);
        } else {
            std::snprintf(buf, n, "%d", iv);
        }
    }

    // ------------------------------------------------------------- drawing
    void onNanoDisplay() override
    {
        // test hook (screenshots): MAQUINA_UI_PRESET=<factory preset name> loads it at the first draw
        if (!fPresetHookDone) {
            fPresetHookDone = true;
            if (const char* want = std::getenv("MAQUINA_UI_PRESET"))
                for (size_t i = 0; i < fPresets.size(); ++i)
                    if (!fPresets[i].user && strcasecmp(fPresets[i].name.c_str(), want) == 0) { loadPreset(int(i)); break; }
        }
        const float s = getWidth() / kWidth;
        scale(s, s);

        if (kNew) {
            drawNewLook();
            if (fPopup >= 0) drawPopup();
            if (fBrowser) drawBrowser();
            if (fSaveDlg) drawSaveDialog();
            return;
        }

        beginPath();
        rect(0, 0, kWidth, fHeight);
        fillColor(col(kBg));
        fill();

        // header
        fontSize(14);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kText));
        char title[64];
        std::snprintf(title, sizeof(title), "MAQUINA  %s", voiceDef().name);
        for (char* c = title; *c; ++c) *c = char(std::toupper(*c));
        text(kMargin + 2, kHeaderH / 2 + 2, title, nullptr);
        fontSize(10);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(col(kDim));
        text(kZoomX - 14, kHeaderH / 2 + 2,
             voiceDef().voice == maquina::kDredrumVoice    ? "based on Dredrum firmware  \xc2\xb7  unofficial"
             : voiceDef().voice == maquina::kPlethoraVoice ? "based on Befaco Noise Plethora  \xc2\xb7  unofficial"
             : maquina::isOneiroi(voiceDef().voice)        ? "based on Befaco Oneiroi firmware  \xc2\xb7  unofficial"
             : voiceDef().voice == maquina::kKlangVoice    ? "based on Mutable Instruments Plaits  \xc2\xb7  unofficial"
             : maquina::isKlong(voiceDef().voice)          ? "based on Mutable Instruments Rings  \xc2\xb7  unofficial"
             : maquina::isAcida(voiceDef().voice)          ? "based on OpenRB-338  \xc2\xb7  unofficial"
             : maquina::isBl101(voiceDef().voice)          ? "based on S1Vave by Andrew GG  \xc2\xb7  unofficial"
             : maquina::isLa50(voiceDef().voice)           ? "LA synthesis \xc2\xb7 reads D-50 patches \xc2\xb7 dsynkant, Munt \xc2\xb7 unofficial"
                                                           : "based on Erica Synths LXR-02 firmware  \xc2\xb7  unofficial",
             nullptr);

        // zoom control: [-] 150% [+]
        beginPath();
        rect(kZoomX + 0.5f, kZoomY + 0.5f, kZoomW - 1, kZoomH - 1);
        fillColor(col(kWell));
        fill();
        strokeColor(col(kBorder));
        strokeWidth(1);
        stroke();
        fillColor(col(kDim));
        fontSize(13);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(kZoomX + 9, kZoomY + kZoomH / 2 + 1, "\xe2\x88\x92", nullptr);
        text(kZoomX + kZoomW - 9, kZoomY + kZoomH / 2 + 1, "+", nullptr);
        char zbuf[16];
        std::snprintf(zbuf, sizeof zbuf, "%d%%", int(std::lround(kZooms[fZoom] * 100)));
        fontSize(10);
        fillColor(col(kText));
        text(kZoomX + kZoomW / 2, kZoomY + kZoomH / 2 + 1, zbuf, nullptr);

        for (const Section& sec : fSections) {
            beginPath();
            rect(sec.x + 0.5f, sec.y + 0.5f, sec.w - 1, sec.h - 1);
            fillColor(col(kPanel));
            fill();
            strokeColor(col(kBorder));
            strokeWidth(1);
            stroke();
            fontSize(10);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(col(kLabel));
            text(sec.x + kSectionPad, sec.y + kTitleH / 2 + 1, sec.title, nullptr);
            if (sec.osc >= 0) drawOscRandom(sec);
        }

        if (layout().la50.used) drawLa50();
        drawRandPanel();
        for (const Graph& g : fGraphs)
            drawGraph(g);
        for (const Ctl& c : fCtls) {
            switch (c.type) {
            case CtlType::Knob:
                drawKnob(c);
                break;
            case CtlType::Select: drawSelect(c); break;
            case CtlType::Toggle: drawToggle(c); break;
            case CtlType::Slider: drawSlider(c); break;
            case CtlType::Switch: drawSwitch(c); break;
            }
        }
        if (fPopup >= 0)
            drawPopup();
        drawPresetBar();
        if (fBrowser) drawBrowser();
        if (fSaveDlg) drawSaveDialog();
    }

    // ------------------------------------------------------------- the new look
    // (docs/ui-redesign.md; geometry: ui/Layouts.cpp, drawing primitives: ui/Controls.cpp)
    void drawNewLook()
    {
        const Theme& T = theme();
        const Layout& L = layout();
        useFace(*this, "mono");
        beginPath();
        rect(0, 0, kWidth, fHeight);
        fillColor(col(T.window));
        fill();
        drawHeaderNew();
        if (kTheme == ThemeId::Silver303) drawSilverPanel(*this, L.panel.x, L.panel.y, L.panel.w, L.panel.h, T.panelRadius);
        for (const Deco& d : L.decos) drawDeco(d);
        for (const Ctl& c : fCtls) drawCtlNew(c);
        drawDrawerNew();
    }

    void drawHeaderNew()
    {
        const Theme& T = theme();
        const HeaderGeo& H = headerGeo();
        beginPath();
        rect(0, 0, kWidth, kChromeHeaderH);
        fillColor(col(kChromeBg));
        fill();
        beginPath();
        rect(0, kChromeHeaderH - 1, kWidth, 1);
        fillColor(col(kChromeLine));
        fill();
        // MAQUINA <NAME>
        useFace(*this, "mono-sb");
        fontPx(*this, 13);
        textLetterSpacing(13 * 0.12f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kChromeText));
        text(H.titleX, H.titleY, "MAQUINA", nullptr);
        std::string name = voiceDef().name;
        for (char& ch : name) ch = char(std::toupper(ch));
        fillColor(col(T.accentText));
        text(H.titleX + 8 * 13 * (kMonoAdv + 0.12f), H.titleY, name.c_str(), nullptr);
        textLetterSpacing(0);
        useFace(*this, "mono");
        // preset bar: [‹] [ name  category ▾ ] [›] [SAVE] [INIT]
        drawChromeButton(*this, H.prev.x, H.prev.y, H.prev.w, H.prev.h, "\xe2\x80\xb9", false);
        drawChromeButton(*this, H.next.x, H.next.y, H.next.w, H.next.h, "\xe2\x80\xba", false);
        const Box& nb = H.name;
        beginPath();
        roundedRect(nb.x + 0.5f, nb.y + 0.5f, nb.w - 1, nb.h - 1, 6);
        fillColor(col(kChromeWell));
        fill();
        strokeColor(col(fBrowser ? T.accent : kChromeBtnLine));
        strokeWidth(1);
        stroke();
        const float my = nb.y + nb.h / 2 + 1;
        fontPx(*this, 12);
        float catW = 0;
        std::string cat;
        if (fPreset >= 0) {
            const PresetEntry& e = fPresets[size_t(fPreset)];
            cat = e.user && e.category != "User" ? "User \xc2\xb7 " + e.category : e.category;
        }
        catW = textWidth(cat.c_str()) + 16;
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(col(kChromeText2));
        text(nb.x + nb.w - 26, my, cat.c_str(), nullptr);
        beginPath(); // the list mark ▾
        moveTo(nb.x + nb.w - 18, my - 3);
        lineTo(nb.x + nb.w - 10, my - 3);
        lineTo(nb.x + nb.w - 14, my + 2);
        closePath();
        fill();
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kChromeText));
        lcdText(nb.x + 10, my, nb.w - 20 - catW - 8, fPresetName + (fPresetDirty ? " *" : ""));
        drawChromeButton(*this, H.save.x, H.save.y, H.save.w, H.save.h, "SAVE", fSaveDlg);
        drawChromeButton(*this, H.init.x, H.init.y, H.init.w, H.init.h, "INIT", false);
        // credit (never wraps: cut to the room left of the zoom buttons)
        fontPx(*this, 11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kChromeText2));
        const float room = H.creditRight - (H.init.x + H.init.w + 12);
        const float cw = std::min(room, textWidth(H.credit));
        lcdText(H.creditRight - cw, my, room, H.credit);
        // zoom: [–] 100% [+]
        drawChromeButton(*this, H.minus.x, H.minus.y, H.minus.w, H.minus.h, "\xe2\x80\x93", false);
        drawChromeButton(*this, H.plus.x, H.plus.y, H.plus.w, H.plus.h, "+", false);
        char zbuf[16];
        std::snprintf(zbuf, sizeof zbuf, "%d%%", int(std::lround(kZooms[fZoom] * 100)));
        fontPx(*this, 12);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(kChromeText));
        text(H.pct.x + H.pct.w / 2, my, zbuf, nullptr);
    }

    void drawDrawerNew()
    {
        const Theme& T = theme();
        const Layout& L = layout();
        const RandPanel& R = L.rand;
        beginPath();
        rect(L.drawer.x, L.drawer.y, L.drawer.w, L.drawer.h);
        fillColor(col(kChromeBg));
        fill();
        beginPath();
        rect(L.drawer.x, L.drawer.y, L.drawer.w, 1);
        fillColor(col(kChromeLine));
        fill();
        // RANDOM (accent) and BACK
        beginPath();
        roundedRect(R.random.x, R.random.y, R.random.w, R.random.h, 6);
        fillColor(col(T.accent));
        fill();
        useFace(*this, "mono-sb");
        fontPx(*this, 12);
        textLetterSpacing(12 * 0.08f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(T.onAccent));
        text(R.random.x + R.random.w / 2, R.random.y + R.random.h / 2 + 1, "RANDOM", nullptr);
        textLetterSpacing(0);
        useFace(*this, "mono");
        drawChromeButton(*this, R.back.x, R.back.y, R.back.w, R.back.h, "BACK", false, fRunActive);
        for (int g = 0; g < maquina::kNumRandGroups; ++g) {
            if (!R.used[g]) continue;
            const Box& c = R.cell[g];
            const bool locked = fRand.lock[g];
            const Color tc = col(locked ? T.accentText : kChromeText);
            drawPadlock12(*this, c.x, c.y + 1, locked, tc);
            useFace(*this, "mono-sb");
            fontPx(*this, 11);
            textLetterSpacing(11 * 0.08f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(tc);
            scissor(c.x, c.y, c.w, 17);
            text(c.x + 18, c.y + 8, maquina::kRandGroups[g].title, nullptr);
            resetScissor();
            textLetterSpacing(0);
            useFace(*this, "mono");
            const Bar b = barOf(c);
            const bool hot = fRangeDrag / 2 == g && fRangeDrag >= 0;
            drawRangeBar(*this, b.x, b.y, b.w, fRand.min[g], fRand.max[g], locked, !locked && (fRandHover == g || hot),
                         hot ? fRangeDrag % 2 : -1);
            char buf[32];
            if (locked) std::snprintf(buf, sizeof buf, "locked");
            else std::snprintf(buf, sizeof buf, "%d\xe2\x80\x93%d %%", fRand.min[g], fRand.max[g]);
            fontPx(*this, 11);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(col(hot ? kChromeText : kChromeText2));
            text(c.x, c.y + 43, buf, nullptr);
        }
    }

    // panel print: upper case, letter-spaced; '\n' = next line; (x, yBottom) = the last
    // line's centre-bottom
    void printLabel(const char* label, float x, float yCentreLast, float px, float spacingEm, Rgb c)
    {
        std::string s = label ? label : "";
        for (char& ch : s) ch = char(std::toupper(ch));
        std::vector<std::string> lines;
        size_t a = 0;
        for (size_t b; (b = s.find('\n', a)) != std::string::npos; a = b + 1) lines.push_back(s.substr(a, b - a));
        lines.push_back(s.substr(a));
        textLetterSpacing(px * spacingEm);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(c));
        const float lh = px * 1.15f;
        for (size_t k = 0; k < lines.size(); ++k)
            text(x, yCentreLast - lh * float(lines.size() - 1 - k), lines[k].c_str(), nullptr);
        textLetterSpacing(0);
    }

    void drawDeco(const Deco& d)
    {
        const Theme& T = theme();
        switch (d.kind) {
        case Deco::VRule:
        case Deco::HRule:
            beginPath();
            rect(d.b.x, d.b.y, d.b.w, d.b.h);
            fillColor(col(T.print));
            fill();
            break;
        case Deco::Logo: {
            // "Acida" / BASS LINE VOICE / the smiley (mockups/acida.html)
            const float cx = d.b.x + d.b.w / 2, top = d.b.y + (d.b.h - 136) / 2;
            useFace(*this, "logo");
            fontPx(*this, 32);
            textLetterSpacing(-0.64f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(col(T.print));
            text(cx, top + 16, "Acida", nullptr);
            textLetterSpacing(0);
            useFace(*this, "mono-sb");
            fontPx(*this, 9.5f);
            textLetterSpacing(9.5f * 0.12f);
            fillColor(col(T.value));
            text(cx, top + 42, voiceDef().voice == maquina::kAcidaFxVoice ? "EFFECT CHAIN" : "BASS LINE VOICE", nullptr);
            textLetterSpacing(0);
            useFace(*this, "mono");
            drawSmiley(*this, cx, top + 99, 74, T.print);
            break;
        }
        }
    }

    float valueFraction(uint32_t i) const
    {
        const float lo = minOf(i), hi = maxOf(i);
        return hi > lo ? (fValues[i] - lo) / (hi - lo) : 0.f;
    }

    void drawCtlNew(const Ctl& c)
    {
        const Theme& T = theme();
        const bool hot = fDrag == int(c.param) || fPopup == int(c.param);
        char buf[48];
        valueText(c.param, buf, sizeof buf);
        switch (c.type) {
        case CtlType::Knob:
            useFace(*this, "print");
            fontPx(*this, 11);
            printLabel(c.label, c.ax, c.labelY - 6.5f, 11, 0.07f, T.print);
            drawKnobStyled(*this, T.knob, c.ax, c.ay, c.size, valueFraction(c.param), hot);
            useFace(*this, "mono");
            fontPx(*this, 11);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(col(T.value));
            text(c.ax, c.valueY, buf, nullptr);
            break;
        case CtlType::Switch: {
            const VoiceParam& p = paramAt(voiceDef(), c.param);
            const int n = std::max<int>(1, p.numLabels), cur = intOf(int(c.param));
            useFace(*this, "print");
            fontPx(*this, 11);
            printLabel(c.label, c.x + c.w / 2, c.labelY, 11, 0.07f, T.print);
            const float sx = c.ax - c.bw / 2, sy = c.ay - c.bh / 2;
            drawSlideSwitchV(*this, sx, sy, c.bw, c.bh, n, cur, hot);
            fontPx(*this, 10);
            for (int k = 0; k < n; ++k) {
                std::string l = p.labels && k < p.numLabels ? p.labels[k] : "";
                for (char& ch : l) ch = char(std::toupper(ch));
                textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
                fillColor(col(T.print, k == cur ? 255 : 150));
                text(sx - 10, slideSwitchPosY(sy, c.bh, n, k), l.c_str(), nullptr);
            }
            useFace(*this, "mono");
            break;
        }
        case CtlType::Select: {
            useFace(*this, "print");
            fontPx(*this, 11);
            printLabel(c.label, c.ax, c.labelY, 11, 0.07f, T.print);
            float bx, by, bw, bh;
            selectBox(c, bx, by, bw, bh);
            const Rgb btn { 0x2a, 0x2a, 0x28 }, line { 0x1d, 0x1d, 0x1b };
            drawStepButton(*this, bx, by + 1, 22, bh - 2, false, btn, line, kChromeText);
            drawStepButton(*this, bx + bw - 22, by + 1, 22, bh - 2, true, btn, line, kChromeText);
            useFace(*this, "mono");
            std::string v = buf;
            for (char& ch : v) ch = char(std::toupper(ch));
            drawLedWindow(*this, bx + 26, by, bw - 52, bh, v.c_str(), { 0xff, 0x5a, 0x40 }, 14);
            if (hot) {
                beginPath();
                roundedRect(bx + 26 - 1, by - 1, bw - 50, bh + 2, 4);
                strokeColor(col(T.accent, 160));
                strokeWidth(1);
                stroke();
            }
            break;
        }
        default: break; // later steps: toggles, sliders on the new panels
        }
    }

    // ------------------------------------------------------------- randomize
    // Maquina Dredrum OSC title bar: [RND ON|OFF] [ROLL]
    void drawOscRandom(const Section& sec)
    {
        const bool on = fRand.oscOn[sec.osc];
        const Box& t = sec.randOn;
        beginPath();
        rect(t.x + 0.5f, t.y + 0.5f, t.w - 1, t.h - 1);
        fillColor(col(kWell));
        fill();
        strokeColor(col(on ? kAccent : kBorder));
        strokeWidth(1);
        stroke();
        fontSize(9);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(on ? col(kAccent) : col(kDim));
        text(t.x + t.w / 2, t.y + t.h / 2 + 1, on ? "RND ON" : "RND OFF", nullptr);

        const Box& r = sec.roll;
        beginPath();
        rect(r.x + 0.5f, r.y + 0.5f, r.w - 1, r.h - 1);
        fillColor(col(kWell));
        fill();
        strokeColor(col(kBorder));
        strokeWidth(1);
        stroke();
        fillColor(col(kText));
        text(r.x + r.w / 2, r.y + r.h / 2 + 1, "ROLL", nullptr);
    }

    void drawButton(const Box& b, const char* label, bool enabled, bool accent)
    {
        beginPath();
        rect(b.x + 0.5f, b.y + 0.5f, b.w - 1, b.h - 1);
        fillColor(accent && enabled ? col(kAccent) : col(kWell));
        fill();
        strokeColor(col(accent && enabled ? kAccent : kBorder));
        strokeWidth(1);
        stroke();
        fontSize(12);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(accent ? col(kBg) : enabled ? col(kText) : col(kDim, 110));
        text(b.x + b.w / 2, b.y + b.h / 2 + 1, label, nullptr);
    }
    // range bar of a group cell
    struct Bar { float x, y, w; };
    static Bar barOf(const Box& c) { return kNew ? Bar { c.x, c.y + 25, c.w } : Bar { c.x + 8, c.y + 34, c.w - 16 }; }
    static Box titleOf(const Box& c) { return { c.x, c.y, c.w, kNew ? 17.f : 20.f }; }

    void drawPadlock(float x, float y, bool closed, Color c) // ~8 x 10
    {
        beginPath();
        rect(x, y + 4, 8, 5.5f);
        fillColor(c);
        fill();
        beginPath();
        if (closed) arc(x + 4, y + 4, 2.6f, float(M_PI), float(2 * M_PI), CW);
        else { arc(x + 6.5f, y + 2, 2.4f, float(M_PI), float(2 * M_PI), CW); }
        strokeColor(c);
        strokeWidth(1.3f);
        stroke();
    }

    // Oneiroi: a switch of the module's randomizer, [<] value [>]
    void drawRandSwitch(const Box& c, const char* label, const char* value)
    {
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(kLabel));
        text(c.x + c.w / 2, c.y + 8, label, nullptr);
        const float bx = c.x + 2, bw = c.w - 4, by = c.y + 18, bh = 22, my = by + bh / 2;
        beginPath();
        rect(bx + 0.5f, by + 0.5f, bw - 1, bh - 1);
        fillColor(col(kWell));
        fill();
        strokeColor(col(kBorder));
        strokeWidth(1);
        stroke();
        fillColor(col(kDim));
        beginPath();
        moveTo(bx + 5, my); lineTo(bx + 10, my - 4); lineTo(bx + 10, my + 4); closePath();
        fill();
        beginPath();
        moveTo(bx + bw - 5, my); lineTo(bx + bw - 10, my - 4); lineTo(bx + bw - 10, my + 4); closePath();
        fill();
        fontSize(11);
        fillColor(col(kText));
        text(bx + bw / 2, my + 1, value, nullptr);
    }

    void drawRandPanel()
    {
        const RandPanel& R = layout().rand;
        if (maquina::isOneiroi(voiceDef().voice)) {
            drawButton(R.random, "RANDOM", true, true);
            drawButton(R.back, fRedo ? "REDO" : "UNDO", fRunActive, false);
            drawRandSwitch(R.oTarget, "TARGET", maquina::kOneiroiTargetLabels[fRand.oTarget % 4]);
            drawRandSwitch(R.oAmount, "AMOUNT", maquina::kOneiroiAmountLabels[fRand.oAmount % 3]);
            fontSize(10);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(col(kDim));
            const float hx = R.oAmount.x + R.oAmount.w + 3 * kGap;
            const bool fx = voiceDef().voice == maquina::kOneiroiFxVoice;
            text(hx, R.oTarget.y + 22,
                 fx ? "The module's randomizer: it moves the knobs of the target (Looper: speed, start,"
                    : "The module's randomizer: it moves the knobs of the target (Osc: pitch, detune;",
                 nullptr);
            text(hx, R.oTarget.y + 36,
                 fx ? "length; Effects: the 8 effect knobs). Full = anywhere,"
                    : "Looper: speed, start, length; Effects: the 8 effect knobs). Full = anywhere,",
                 nullptr);
            text(hx, R.oTarget.y + 50, "Mid = up to 10 %, Low = up to 2 %. Faders, switches and SHIFT functions stay.",
                 nullptr);
            return;
        }
        drawButton(R.random, "RANDOM", true, true);
        drawButton(R.back, "BACK", fRunActive, false);
        for (int g = 0; g < maquina::kNumRandGroups; ++g) {
            if (!R.used[g]) continue;
            const Box& c = R.cell[g];
            const bool locked = fRand.lock[g];
            // title + lock
            fontSize(10);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            const char* title = maquina::kRandGroups[g].title;
            Rectangle<float> bounds;
            textBounds(0, 0, title, nullptr, bounds);
            const float tw = bounds.getWidth();
            const float tx = c.x + (c.w - tw - 13) / 2;
            drawPadlock(tx, c.y + 2, locked, locked ? col(kAccent) : col(kLabel));
            fillColor(locked ? col(kAccent) : col(kLabel));
            text(tx + 13, c.y + 7, title, nullptr);

            if (locked) globalAlpha(0.35f);
            const Bar b = barOf(c);
            const float x0 = b.x + b.w * fRand.min[g] / 100.f, x1 = b.x + b.w * fRand.max[g] / 100.f;
            beginPath();
            rect(b.x, b.y - 2, b.w, 4);
            fillColor(col(kTrack));
            fill();
            beginPath();
            rect(x0, b.y - 2, std::max(1.f, x1 - x0), 4);
            fillColor(col(kAccent));
            fill();
            for (int k = 0; k < 2; ++k) {
                const float hx = k ? x1 : x0;
                beginPath();
                rect(hx - 3, b.y - 7, 6, 14);
                fillColor(fRangeDrag == g * 2 + k ? col(kAccent) : col(kText));
                fill();
            }
            char buf[32];
            if (locked) std::snprintf(buf, sizeof buf, "locked");
            else std::snprintf(buf, sizeof buf, "%d\xe2\x80\x93%d %%", fRand.min[g], fRand.max[g]);
            fontSize(10);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(col(fRangeDrag / 2 == g && fRangeDrag >= 0 ? kAccent : kText));
            text(c.x + c.w / 2, c.y + c.h - 9, buf, nullptr);
            globalAlpha(1.f);
        }
    }
    int randCellAt(float x, float y) const
    {
        for (int g = 0; g < maquina::kNumRandGroups; ++g)
            if (layout().rand.cell[g].hit(x, y)) return g;
        return -1;
    }
    void saveRand() { setState("rand_levels", fRand.toString().c_str()); }
    void setRangeFromX(float x)
    {
        const int g = fRangeDrag / 2;
        const Bar b = barOf(layout().rand.cell[g]);
        const int pct = std::clamp(int(std::lround((x - b.x) / b.w * 100.f)), 0, 100);
        if (fRangeDrag % 2 == 0) fRand.min[g] = uint8_t(std::min(pct, int(fRand.max[g])));
        else fRand.max[g] = uint8_t(std::max(pct, int(fRand.min[g])));
        repaint();
    }

    void endRun() { fRunActive = false; }

    void applyValues(const std::vector<float>& v)
    {
        fApplying = true;
        for (uint32_t i = 0; i < fValues.size(); ++i)
            if (v[i] != fValues[i]) setValue(i, v[i], true);
        fApplying = false;
        repaint();
    }
    // onlyOsc >= 0: Maquina Dredrum's per-oscillator ROLL
    void doRandom(int onlyOsc = -1)
    {
        if (maquina::isOneiroi(voiceDef().voice)) {
            // the module: UNDO returns to the sound before the last roll, REDO back again
            fSnapshot = fValues;
            fSnapshotDirty = fPresetDirty;
            fRunActive = true;
            fRedo = false;
            std::vector<float> v = fValues;
            maquina::oneiroiRandomize(voiceDef(), fRand.oTarget, fRand.oAmount, v.data(), fRng);
            applyValues(v);
            fRunActive = true; // applyValues -> setValue ends a run only for edits made elsewhere
            fPresetDirty = true;
            return;
        }
        if (!fRunActive) {
            fSnapshot = fValues;
            fSnapshotDirty = fPresetDirty;
            fRunActive = true;
        }
        std::vector<float> v = fValues;
        maquina::randomize(voiceDef(), fRand, v.data(), fRng, onlyOsc);
        applyValues(v);
        fPresetDirty = true;
    }
    void doBack()
    {
        if (!fRunActive) return;
        if (maquina::isOneiroi(voiceDef().voice)) {
            std::vector<float> v = fSnapshot;
            fSnapshot = fValues;
            applyValues(v);
            fRunActive = true;
            fRedo = !fRedo;
            std::swap(fPresetDirty, fSnapshotDirty);
            repaint();
            return;
        }
        applyValues(fSnapshot);
        fRunActive = false;
        fPresetDirty = fSnapshotDirty;
        repaint();
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (!std::strcmp(key, "rand_levels")) {
            fRand.fromString(value);
            repaint();
        } else if (!std::strcmp(key, "preset")) { // "name" or "*name" (edited)
            fPresetDirty = value[0] == '*';
            fPresetName = value + (fPresetDirty ? 1 : 0);
            fPreset = -1;
            for (size_t i = 0; i < fPresets.size(); ++i)
                if (fPresets[i].name == fPresetName) { fPreset = int(i); break; }
            repaint();
        }
    }

    // ------------------------------------------------------------- presets
    struct PresetEntry {
        std::string name, category;
        std::vector<std::string> tags;
        std::vector<maquina::PresetValue> values;
        bool user = false;
        std::string path; // user presets: the file
    };

    std::string userPresetDir() const
    {
        const std::string base = configDir();
        if (base.empty()) return base;
#ifdef _WIN32
        return base + "\\presets\\" + voiceDef().id;
#else
        return base + "/presets/" + voiceDef().id;
#endif
    }

    void loadPresetList()
    {
        loadUserCats();
        loadRatings();
        loadHidden();
        fPresets.clear();
        const maquina::VoicePresets& fp = maquina::k_factory_presets[MAQUINA_VOICE_INDEX];
        for (uint32_t k = 0; k < fp.count; ++k) {
            if (std::find(fHidden.begin(), fHidden.end(), fp.presets[k].name) != fHidden.end()) continue;
            PresetEntry e;
            e.name = fp.presets[k].name;
            e.category = mapLabel("cat", fp.presets[k].category);
            for (const std::string& t : maquina::splitTags(fp.presets[k].tags)) {
                const std::string m = mapLabel("tag", t);
                if (!m.empty() && std::find(e.tags.begin(), e.tags.end(), m) == e.tags.end()) e.tags.push_back(m);
            }
            e.values.assign(fp.presets[k].values, fp.presets[k].values + fp.presets[k].numValues);
            fPresets.push_back(std::move(e));
        }
#ifndef _WIN32
        std::vector<PresetEntry> user;
        const std::string dir = userPresetDir();
        if (DIR* dp = dir.empty() ? nullptr : opendir(dir.c_str())) {
            while (dirent* de = readdir(dp)) {
                const std::string f = de->d_name;
                if (f.size() <= maquina::kPresetExtLen || f.compare(f.size() - maquina::kPresetExtLen, maquina::kPresetExtLen, maquina::kPresetExt) != 0) continue;
                std::ifstream in(dir + "/" + f);
                std::stringstream ss;
                ss << in.rdbuf();
                maquina::PresetFile pf = maquina::parsePreset(voiceDef(), ss.str());
                if (pf.name.empty() || (pf.voice != voiceDef().id)) continue;
                PresetEntry e;
                e.name = pf.name;
                e.category = pf.category;
                e.tags = pf.tags;
                e.values = pf.values;
                e.user = true;
                e.path = dir + "/" + f;
                user.push_back(std::move(e));
            }
            closedir(dp);
        }
        std::sort(user.begin(), user.end(), [](const PresetEntry& a, const PresetEntry& b) { return a.name < b.name; });
        for (auto& e : user) fPresets.push_back(std::move(e));
#endif
        fPreset = -1;
        for (size_t i = 0; i < fPresets.size(); ++i)
            if (fPresets[i].name == fPresetName) { fPreset = int(i); break; }
        static bool testDialog = testHook("dialog"); // test hook: the first user preset's edit dialog, in edit mode
        if (testDialog) {
            testDialog = false;
            for (size_t i = 0; i < fPresets.size(); ++i)
                if (fPresets[i].user) { openEditDialog(int(i)); fDlgEdit = true; break; }
        }
    }

    void savePresetState()
    {
        setState("preset", ((fPresetDirty ? "*" : "") + fPresetName).c_str());
    }

    // INIT: every sound parameter to its default (the init patch); the output gain stays
    void initPatch()
    {
        std::vector<float> v = fValues;
        for (uint32_t i = 0; i < voiceDef().numParams; ++i)
            if (std::strcmp(paramAt(voiceDef(), i).symbol, "gain")) v[i] = defOf(i);
        endRun();
        applyValues(v);
        fPreset = -1;
        fPresetName = "Init";
        fPresetDirty = false;
        fLaLast = -1;
        fLaMsg = maquina::isLa50(voiceDef().voice) ? "patch initialised" : "";
        savePresetState();
        repaint();
    }

    void loadPreset(int idx)
    {
        if (idx < 0 || idx >= int(fPresets.size())) return;
        const PresetEntry& e = fPresets[size_t(idx)];
        std::vector<float> v = fValues;
        maquina::applyPreset(voiceDef(), e.values.data(), uint32_t(e.values.size()), v.data());
        endRun();
        applyValues(v);
        fPreset = idx;
        fPresetName = e.name;
        fPresetDirty = false;
        savePresetState();
        repaint();
    }

    // browser filter
    bool presetVisible(const PresetEntry& e) const
    {
        const int nc = numCats();
        if (fBrowseCat == nc + 1 && !e.user) return false;
        if (fBrowseCat == nc + 2 && ratingOf(e) == 0) return false;
        if (fBrowseCat >= 1 && fBrowseCat <= nc && e.category != saveCatName(fBrowseCat - 1)) return false;
        for (int t = 0; t < numTags(); ++t)
            if (fTagMask & (1u << t))
                if (std::find(e.tags.begin(), e.tags.end(), fTagList[size_t(t)]) == e.tags.end()) return false;
        if (!fSearch.empty()) {
            std::string a = e.name, b = fSearch;
            for (auto& c : a) c = char(std::tolower(c));
            for (auto& c : b) c = char(std::tolower(c));
            if (a.find(b) == std::string::npos) return false;
        }
        return true;
    }

    // ---- small drawing helpers
    // text(): an empty label draws nothing (DPF asserts on empty strings)
    float text(float x, float y, const char* t, const char* end)
    {
        if (t == nullptr || t[0] == '\0' || t == end) return x;
        return UI::text(x, y, t, end);
    }
    float textWidth(const char* t)
    {
        if (t == nullptr || t[0] == '\0') return 0.f;
        Rectangle<float> b;
        textBounds(0, 0, t, nullptr, b);
        return b.getWidth();
    }
    void frame(float x, float y, float w, float h, Color fillC, Color strokeC)
    {
        beginPath();
        rect(x + 0.5f, y + 0.5f, w - 1, h - 1);
        fillColor(fillC);
        fill();
        strokeColor(strokeC);
        strokeWidth(1);
        stroke();
    }
    // chip: returns its width
    float chip(float x, float y, const char* label, bool on, float h = 18.f)
    {
        fontSize(10);
        const float w = textWidth(label) + 14;
        frame(x, y, w, h, on ? col(kAccent) : col(kWell), on ? col(kAccent) : col(kBorder));
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(on ? col(kBg) : col(kText));
        text(x + w / 2, y + h / 2 + 1, label, nullptr);
        return w;
    }
    float chipWidth(const char* label)
    {
        fontSize(10);
        return textWidth(label) + 14;
    }

    void drawPresetBar()
    {
        const float y = kZoomY, h = kZoomH, my = y + h / 2 + 1;
        frame(kPrevX, y, kArrowW, h, col(kWell), col(kBorder));
        frame(kNextX, y, kArrowW, h, col(kWell), col(kBorder));
        fillColor(col(kDim));
        beginPath(); moveTo(kPrevX + 6, my - 1); lineTo(kPrevX + 11, my - 5); lineTo(kPrevX + 11, my + 3); closePath(); fill();
        beginPath(); moveTo(kNextX + 12, my - 1); lineTo(kNextX + 7, my - 5); lineTo(kNextX + 7, my + 3); closePath(); fill();
        frame(kNameX, y, kPresetNameW, h, col(kWell), col(fBrowser ? kAccent : kBorder));
        const std::string name = fPresetName + (fPresetDirty ? " *" : "");
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kText));
        float catW = 0.f;
        if (fPreset >= 0) {
            const PresetEntry& e = fPresets[size_t(fPreset)];
            const std::string cat = e.user && e.category != "User" ? "User \xc2\xb7 " + e.category : e.category;
            fontSize(9);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fillColor(col(kDim));
            catW = textWidth(cat.c_str()) + 10;
            text(kNameX + kPresetNameW - 16, my, cat.c_str(), nullptr);
        }
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kText));
        scissor(kNameX + 4, y, kPresetNameW - 8, h);
        lcdText(kNameX + 7, my, kPresetNameW - 7 - 16 - catW, name);
        resetScissor();
        fillColor(col(kDim)); // list mark
        beginPath();
        moveTo(kNameX + kPresetNameW - 11, my - 3); lineTo(kNameX + kPresetNameW - 5, my - 3);
        lineTo(kNameX + kPresetNameW - 8, my + 1); closePath();
        fill();
        frame(kSaveX, y, kSaveW, h, col(kWell), col(fSaveDlg ? kAccent : kBorder));
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(kText));
        text(kSaveX + kSaveW / 2, my, "SAVE", nullptr);
        frame(kInitX, y, kInitW, h, col(kWell), col(kBorder));
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(kText));
        text(kInitX + kInitW / 2, my, "INIT", nullptr);
    }

    // ---- browser geometry (panel units)
    struct BrowserGeo { float x, y, w, h, catX, catW, rightX, rightW; };
    BrowserGeo browserGeo() const
    {
        BrowserGeo g;
        g.x = kMargin; g.y = kHeaderH; g.w = kWidth - 2 * kMargin; g.h = fHeight - kHeaderH - kMargin;
        g.catX = g.x + 10; g.catW = 120;
        g.rightX = g.catX + g.catW + 14; g.rightW = g.x + g.w - 10 - g.rightX;
        return g;
    }
    static constexpr float kCatRowH = 20.f, kListRowH = 19.f, kListColW = 300.f, kListGap = 22.f, kIconW = 18.f, kStarW = 11.f, kRestoreW = 190.f;

    // text cut with an ellipsis to fit w
    void fitText(float x, float y, float w, std::string s)
    {
        if (w <= 0 || s.empty()) return;
        if (textWidth(s.c_str()) > w) {
            while (!s.empty() && textWidth((s + "\xe2\x80\xa6").c_str()) > w) {
                s.pop_back();
                while (!s.empty() && (uint8_t(s.back()) & 0xc0) == 0x80) s.pop_back(); // a whole UTF-8 character
                if (!s.empty() && (uint8_t(s.back()) & 0xc0) == 0xc0) s.pop_back();
            }
            while (!s.empty() && s.back() == ' ') s.pop_back();
            s += "\xe2\x80\xa6";
        }
        text(x, y, s.c_str(), nullptr);
    }
    // a five-pointed star centred in the box: filled, or an outline
    void star(float x, float y, float w, float h, Color c, bool filled)
    {
        const float cx = x + w / 2, cy = y + h / 2 + 0.5f, ro = std::min(w, h) * 0.36f, ri = ro * 0.45f;
        beginPath();
        for (int k = 0; k < 10; ++k) {
            const float a = -1.5707963f + k * 0.62831853f, r = (k & 1) ? ri : ro;
            if (k == 0) moveTo(cx + r * std::cos(a), cy + r * std::sin(a));
            else lineTo(cx + r * std::cos(a), cy + r * std::sin(a));
        }
        closePath();
        if (filled) { fillColor(c); fill(); }
        else { strokeColor(c); strokeWidth(1.2f); stroke(); }
    }
    // a small pencil pointing down-left
    void pen(float x, float y, float w, float h, Color c)
    {
        const float cx = x + w / 2, cy = y + h / 2, r = std::min(w, h) * 0.30f;
        strokeColor(c);
        strokeWidth(2.2f);
        beginPath(); moveTo(cx + r, cy - r); lineTo(cx - r * 0.45f, cy + r * 0.45f); stroke();
        beginPath(); // the tip
        moveTo(cx - r * 0.62f, cy + r * 0.12f); lineTo(cx - r * 0.12f, cy + r * 0.62f);
        lineTo(cx - r, cy + r); closePath();
        fillColor(c); fill();
    }
    // a waste bin
    void bin(float x, float y, float w, float h, Color c)
    {
        const float cx = x + w / 2, cy = y + h / 2 + 0.5f, bw = 7.f, bh = 8.f;
        strokeColor(c);
        strokeWidth(1.2f);
        beginPath();
        moveTo(cx - bw / 2 - 1.5f, cy - bh / 2 - 1.5f); lineTo(cx + bw / 2 + 1.5f, cy - bh / 2 - 1.5f); // lid
        moveTo(cx - 1.5f, cy - bh / 2 - 3); lineTo(cx + 1.5f, cy - bh / 2 - 3);                         // handle
        moveTo(cx - bw / 2, cy - bh / 2); lineTo(cx - bw / 2 + 0.7f, cy + bh / 2);
        lineTo(cx + bw / 2 - 0.7f, cy + bh / 2); lineTo(cx + bw / 2, cy - bh / 2);
        moveTo(cx, cy - bh / 2 + 2); lineTo(cx, cy + bh / 2 - 2);                                       // ribs
        stroke();
    }


    // lays out search, tag chips and the list; calls fn(kind, index, x, y, w, h)
    // kind: 0 category, 1 search box, 2 tag chip, 3 preset row, 4 close, 5 clear tags,
    // 6 + CATEGORY (or its name field), 7 delete the selected user category,
    // 10 / 12 a preset row's pen (edit) / bin (delete), 20..24 its rating stars 1..5 - hit areas
    // only, drawn with the row; 13 restore the hidden factory presets
    template <class F> void browserItems(F&& fn)
    {
        const BrowserGeo g = browserGeo();
        const int nc = numCats();
        if (fBrowseCat >= 1 && fBrowseCat <= nc && !(fCatInput && fCatEditIdx == fBrowseCat - 1)) { // before its row: the icons win
            fn(7, fBrowseCat - 1, g.catX + g.catW - 18, g.y + 30 + fBrowseCat * kCatRowH + 1, 16, kCatRowH - 4);
            fn(14, fBrowseCat - 1, g.catX + g.catW - 36, g.y + 30 + fBrowseCat * kCatRowH + 1, 16, kCatRowH - 4);
        }
        for (int i = 0; i < nc + 3; ++i)
            fn(0, i, g.catX, g.y + 30 + i * kCatRowH, g.catW, kCatRowH - 2);
        fn(6, 0, g.catX, g.y + 30 + (nc + 3) * kCatRowH + 6, g.catW, kCatRowH - 2);
        fn(4, 0, g.x + g.w - 26, g.y + 6, 20, 18);
        if (!fHidden.empty()) fn(13, 0, g.x + g.w - 10 - kRestoreW, g.y + g.h - 30, kRestoreW, 22);
        float ty = g.y + 30;
        fn(1, 0, g.rightX, ty, 180, 20);
        float tx = g.rightX + 190;
        auto place = [&](float w) {
            if (tx + w > g.rightX + g.rightW - 50) { tx = g.rightX + 190; ty += 24; }
            const float x0 = tx;
            tx += w + 5;
            return x0;
        };
        for (int t = 0; t < numTags(); ++t) {
            const bool editing = fTagInput && fTagEditIdx == t;
            const std::string& lbl = editing ? fTagName : fTagList[size_t(t)];
            const float w = std::max(chipWidth((lbl + (editing ? "|" : "")).c_str()), 30.f) + (fTagEdit ? 14 : 0);
            const float x0 = place(w);
            if (fTagEdit && !editing) fn(16, t, x0 + w - 16, ty + 1, 16, 18); // before its chip: the x wins
            fn(2, t, x0, ty + 1, w, 18);
        }
        if (fTagEdit && numTags() < 32) {
            const bool adding = fTagInput && fTagEditIdx < 0;
            const float w = std::max(chipWidth(adding ? (fTagName + "|").c_str() : "+ TAG"), 40.f);
            const float x0 = place(w); // before ty is read: place() may wrap
            fn(17, 0, x0, ty + 1, w, 18);
        }
        {
            const float w = chipWidth(fTagEdit ? "done" : "edit tags");
            const float x0 = place(w);
            fn(15, 0, x0, ty + 1, w, 18);
        }
        fn(5, 0, g.rightX + g.rightW - 44, g.y + 31, 44, 18);
        const float ly = ty + 32;
        const int rows = std::max(1, int((g.y + g.h - 40 - ly) / kListRowH)); // the action bar below
        const int cols = std::max(1, int(g.rightW / kListColW));
        const float cw = (g.rightW + kListGap) / float(cols);
        std::vector<size_t> order;
        for (size_t i = 0; i < fPresets.size(); ++i)
            if (presetVisible(fPresets[i])) order.push_back(i);
        if (fBrowseCat == nc + 2) // Rated: best first
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ratingOf(fPresets[a]) > ratingOf(fPresets[b]); });
        int n = 0;
        for (size_t i : order) {
            if (n < rows * cols) {
                const float rx = g.rightX + (n / rows) * cw, ry = ly + (n % rows) * kListRowH;
                const float rw = cw - kListGap, ih = kListRowH - 1;
                fn(12, int(i), rx + rw - kIconW, ry, kIconW, ih); // before its row: the icons win
                fn(10, int(i), rx + rw - 2 * kIconW, ry, kIconW, ih);
                const float sx = rx + rw - 2 * kIconW - 5 * kStarW - 4;
                for (int k = 0; k < 5; ++k) fn(20 + k, int(i), sx + k * kStarW, ry, kStarW, ih);
                fn(3, int(i), rx, ry, rw, ih);
            }
            ++n;
        }
        fListHidden = std::max(0, n - rows * cols);
        fListShown = n;
    }

    void drawBrowser()
    {
        const BrowserGeo g = browserGeo();
        beginPath();
        rect(0, kHeaderH - 2, kWidth, fHeight - kHeaderH + 2);
        fillColor(col(kBg, 200));
        fill();
        frame(g.x, g.y, g.w, g.h, col(kPanel), col(kAccent, 160));
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kDim));
        text(g.catX, g.y + 14, "CATEGORY", nullptr);
        text(g.rightX, g.y + 14, "PRESETS", nullptr);
        const int nc = numCats();
        const Color kRed(0xe0, 0x5a, 0x4a);

        browserItems([&](int kind, int i, float x, float y, float w, float h) {
            switch (kind) {
            case 0: {
                const char* label = i == 0 ? "All" : i <= nc ? saveCatName(i - 1) : i == nc + 1 ? "User" : "Rated";
                const bool on = fBrowseCat == i;
                if (fCatInput && fCatEditIdx >= 0 && i == fCatEditIdx + 1) { // renaming it
                    frame(x, y, w, h, col(kWell), col(kAccent));
                    fontSize(11);
                    textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                    fillColor(col(kText));
                    text(x + 8, y + h / 2 + 1, (fCatName + "|").c_str(), nullptr);
                    break;
                }
                if (on || (fBrowseHoverKind == 0 && fBrowseHover == i)) {
                    beginPath(); rect(x, y, w, h);
                    fillColor(on ? col(kAccent) : col(kTrack)); fill();
                }
                fontSize(11);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(on ? col(kBg) : col(kText));
                text(x + 8, y + h / 2 + 1, label, nullptr);
                if (i == nc + 2) star(x + w - kIconW - 2, y, kIconW, h, on ? col(kBg) : col(kAccent), true);  // Rated
                if (on && i >= 1 && i <= nc) { // pen (rename) and x (delete; red = click again)
                    pen(x + w - 36, y + 1, 16, h - 2, fBrowseHoverKind == 14 ? col(kText) : col(kBg));
                    const bool armed = fCatDelArm == i - 1;
                    const float bx = x + w - 18, by = y + 1, bw = 16, bh = h - 2;
                    if (armed) { beginPath(); rect(bx, by, bw, bh); fillColor(kRed); fill(); }
                    strokeColor(armed || fBrowseHoverKind == 7 ? col(kText) : col(kBg));
                    strokeWidth(1.5f);
                    beginPath();
                    moveTo(bx + 4, by + 4); lineTo(bx + bw - 4, by + bh - 4);
                    moveTo(bx + bw - 4, by + 4); lineTo(bx + 4, by + bh - 4);
                    stroke();
                }
                break;
            }
            case 1: {
                frame(x, y, w, h, col(kWell), col(fSearchFocus ? kAccent : kBorder));
                fontSize(11);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (fSearch.empty() && !fSearchFocus) {
                    fillColor(col(kDim));
                    text(x + 7, y + h / 2 + 1, "Search...", nullptr);
                } else {
                    fillColor(col(kText));
                    const std::string t = fSearch + (fSearchFocus ? "|" : "");
                    text(x + 7, y + h / 2 + 1, t.c_str(), nullptr);
                }
                break;
            }
            case 2:
                if (fTagInput && fTagEditIdx == i) { // renaming it
                    frame(x, y, w, h, col(kWell), col(kAccent));
                    fontSize(10);
                    textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                    fillColor(col(kText));
                    text(x + 7, y + h / 2 + 1, (fTagName + "|").c_str(), nullptr);
                } else if (fTagEdit) {
                    const bool armed = fTagDelArm == i;
                    frame(x, y, w, h, col(kWell), armed ? kRed : col(kBorder));
                    fontSize(10);
                    textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                    fillColor(col(kText));
                    text(x + 7, y + h / 2 + 1, fTagList[size_t(i)].c_str(), nullptr);
                    const float cx = x + w - 8, cy = y + h / 2;
                    if (armed) { beginPath(); rect(x + w - 15, y + 2, 13, h - 4); fillColor(kRed); fill(); }
                    strokeColor(armed || (fBrowseHoverKind == 16 && fBrowseHover == i) ? col(kText) : col(kDim));
                    strokeWidth(1.3f);
                    beginPath();
                    moveTo(cx - 3, cy - 3); lineTo(cx + 3, cy + 3);
                    moveTo(cx + 3, cy - 3); lineTo(cx - 3, cy + 3);
                    stroke();
                } else chip(x, y, fTagList[size_t(i)].c_str(), (fTagMask >> i) & 1u);
                break;
            case 15: chip(x, y, fTagEdit ? "done" : "edit tags", fTagEdit); break;
            case 17:
                fontSize(10);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (fTagInput && fTagEditIdx < 0) {
                    frame(x, y, w, h, col(kWell), col(kAccent));
                    fillColor(col(kText));
                    text(x + 7, y + h / 2 + 1, (fTagName + "|").c_str(), nullptr);
                } else {
                    frame(x, y, w, h, col(kPanel), col(kBorder));
                    fillColor(col(kDim));
                    text(x + 7, y + h / 2 + 1, "+ TAG", nullptr);
                }
                break;
            case 3: {
                const PresetEntry& e = fPresets[size_t(i)];
                const int hk = fBrowseHover == i ? fBrowseHoverKind : -1;
                const bool cur = i == fPreset, hot = hk == 3 || hk >= 10;
                const int rating = ratingOf(e), shown = hk >= 20 ? hk - 19 : rating; // hovering a star previews it
                const bool odd = int(std::lround(y / kListRowH)) & 1; // table stripes
                if (cur || hot || odd) {
                    beginPath(); rect(x, y, w, h + 1);
                    fillColor(cur ? col(kAccent) : hot ? col(kTrack) : Color(255, 255, 255, 0.035f)); // alpha is 0..1
                    fill();
                }
                const Color fg = cur ? col(kBg) : col(kText), dim = cur ? col(kBg, 170) : col(kDim);
                auto iconCol = [&](int k) { return hk == k ? (cur ? col(kText) : col(kAccent)) : dim; };
                const float bx = x + w - kIconW, px = bx - kIconW, sx = px - 5 * kStarW - 4;
                if (rating > 0 || cur || hot) {
                    const Color on = hk >= 20 ? (cur ? col(kText) : col(kAccent)) : cur ? col(kBg) : col(kAccent);
                    for (int k = 0; k < 5; ++k)
                        star(sx + k * kStarW - 1, y, kStarW + 2, h, k < shown ? on : dim, k < shown);
                }
                if (cur || hot || fDelArm == i) {
                    pen(px, y, kIconW, h, iconCol(10));
                    const bool armed = fDelArm == i;
                    if (armed) { beginPath(); rect(bx + 1, y + 1, kIconW - 2, h - 2); fillColor(kRed); fill(); }
                    bin(bx, y, kIconW, h, armed ? col(kText) : iconCol(12));
                }
                // the info (category, tags) right-aligned before the icons, the name cut to what's left
                const float iconsW = x + w - sx + 6;
                fontSize(9);
                std::string info = maquina::joinTags(e.tags);
                info = e.category + (info.empty() ? "" : "  " + info);
                const float infoW = std::min(textWidth(info.c_str()), (w - iconsW) * 0.45f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(dim);
                fitText(x + w - iconsW - infoW, y + h / 2 + 1, infoW, info);
                fontSize(11);
                fillColor(fg);
                fitText(x + 6, y + h / 2 + 1, w - iconsW - infoW - 18, e.name);
                break;
            }
            case 4: {
                strokeColor(col(kDim)); strokeWidth(1.5f);
                beginPath(); moveTo(x + 5, y + 4); lineTo(x + w - 5, y + h - 4);
                moveTo(x + w - 5, y + 4); lineTo(x + 5, y + h - 4); stroke();
                break;
            }
            case 5:
                if (fTagMask) chip(x, y, "clear", false);
                break;
            case 6:
                fontSize(10);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (fCatInput && fCatEditIdx < 0) {
                    frame(x, y, w, h, col(kWell), col(kAccent));
                    fillColor(col(kText));
                    text(x + 6, y + h / 2 + 1, (fCatName + "|").c_str(), nullptr);
                } else {
                    fillColor(col(kDim));
                    text(x + 8, y + h / 2 + 1, "+ CATEGORY", nullptr);
                }
                break;
            case 13: {
                const bool h13 = fBrowseHoverKind == 13;
                frame(x, y, w, h, col(kWell), h13 ? col(kAccent) : col(kBorder));
                fontSize(10);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(col(kText));
                const std::string t = "RESTORE " + std::to_string(fHidden.size()) + " FACTORY PRESET" + (fHidden.size() > 1 ? "S" : "");
                text(x + w / 2, y + h / 2 + 1, t.c_str(), nullptr);
                break;
            }
            case 7: break; // drawn with its row
            }
        });
        // the action bar's note
        {
            fontSize(10);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(col(kDim));
            const float ny = g.y + g.h - 19;
            std::string note = fLibMsg;
            if (note.empty()) {
                if (fCatDelArm >= 0) note = std::string("Click the red x to delete \"") + saveCatName(fCatDelArm) + "\" (its presets move to User).";
                else if (fTagDelArm >= 0) note = "Click the red x to delete the tag \"" + fTagList[size_t(fTagDelArm)] + "\" (it's removed from every preset).";
                else if (fCatInput || fTagInput) note = "Type the name, Enter to apply, Esc to cancel.";
                else if (fTagEdit) note = "Click a tag to rename it, its x to delete it, + TAG to add one.  DONE when finished.";
                else if (fDelArm >= 0) note = std::string("Click the red bin again to delete \"") + fPresets[size_t(fDelArm)].name + "\".";
                else note = "Stars: rate 1-5 (click the same star again to clear).  Pen: edit name, category, tags.  Bin: delete (factory presets are only hidden).";
            }
            text(g.rightX, ny, note.c_str(), nullptr);
        }
        if (fListShown == 0 || fListHidden > 0) {
            char buf[64];
            if (fListShown == 0) std::snprintf(buf, sizeof buf, "No presets match.");
            else std::snprintf(buf, sizeof buf, "+%d more - narrow the filter", fListHidden);
            fontSize(10);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fillColor(col(kDim));
            text(g.x + g.w - 10 - (fHidden.empty() ? 0 : kRestoreW + 12), g.y + g.h - 19, buf, nullptr);
        }
    }

    bool browserClick(float x, float y)
    {
        bool hit = false;
        browserItems([&](int kind, int i, float bx, float by, float w, float h) {
            if (hit || x < bx || x >= bx + w || y < by || y >= by + h) return;
            hit = true;
            fSearchFocus = kind == 1;
            if (kind != 6 && !(kind == 0 && fCatInput && fCatEditIdx == i - 1)) fCatInput = false;
            if (!((kind == 2 && fTagInput && fTagEditIdx == i) || (kind == 17 && fTagInput && fTagEditIdx < 0))) fTagInput = false;
            if (kind != 7) fCatDelArm = -1;
            if (kind != 16) fTagDelArm = -1;
            if (kind != 12) fDelArm = -1;
            fLibMsg.clear();
            switch (kind) {
            case 0:
                if (!fCatInput) fBrowseCat = i;
                break;
            case 2:
                if (!fTagEdit) fTagMask ^= 1u << i;
                else if (!fTagInput) { fTagInput = true; fTagEditIdx = i; fTagName = fTagList[size_t(i)]; }
                break;
            case 14: fCatInput = true; fCatEditIdx = i; fCatName = saveCatName(i); break;
            case 15: fTagEdit = !fTagEdit; fTagInput = false; break;
            case 16:
                if (fTagDelArm == i) deleteTag(i);
                else fTagDelArm = i;
                break;
            case 17:
                if (!fTagInput) { fTagInput = true; fTagEditIdx = -1; fTagName.clear(); }
                break;
            case 3: loadPreset(i); break;
            case 4: fBrowser = false; break;
            case 5: fTagMask = 0; break;
            case 6: fCatInput = true; fCatEditIdx = -1; fCatName.clear(); break;
            case 7:
                if (fCatDelArm == i) deleteCat(i);
                else fCatDelArm = i;
                break;
            case 10: openEditDialog(i); break;
            case 13: restoreFactory(); break;
            case 20: case 21: case 22: case 23: case 24: setRating(i, kind - 19); break;
            case 12:
                if (fDelArm == i) deletePreset(i);
                else fDelArm = i;
                break;
            }
        });
        if (!hit) { fSearchFocus = false; fCatInput = fTagInput = false; fCatDelArm = fDelArm = fTagDelArm = -1; }
        repaint();
        return true;
    }
    void browserHover(float x, float y)
    {
        int kind = -1, idx = -1;
        browserItems([&](int k, int i, float bx, float by, float w, float h) {
            if ((k == 0 || k == 3 || k >= 10) && (kind < 10 || kind == 13) && x >= bx && x < bx + w && y >= by && y < by + h) { kind = k; idx = i; }
        });
        if (kind != fBrowseHoverKind || idx != fBrowseHover) {
            fBrowseHoverKind = kind;
            fBrowseHover = idx;
            repaint();
        }
    }

    // ---- save dialog
    struct DlgGeo { float x, y, w, h; };
    DlgGeo dlgGeo() const { return { (kWidth - 600) / 2, kHeaderH + 20, 600, fDlgH }; }

    // kind: 0 name box, 1 category chip, 2 tag chip, 3 save, 4 cancel, 5 + CATEGORY, 6 + TAG,
    // 7 edit / done, 8 a category's x, 9 a tag's x (edit mode; before their chips: the x wins)
    template <class F> void dialogItems(F&& fn)
    {
        const DlgGeo g = dlgGeo();
        const float fx = g.x + 96, fw = g.w - 112;
        fn(0, 0, fx, g.y + 34, fw, 22);
        float cx = fx, cy = g.y + 66;
        auto place = [&](float& x, float& y, float w) {
            if (x + w > fx + fw) { x = fx; y += 24; }
            const float x0 = x;
            x += w + 5;
            return x0;
        };
        auto editW = [&](const std::string& label, bool editing, bool withX) {
            return std::max(chipWidth((label + (editing ? "|" : "")).c_str()), 30.f) + (withX ? 14 : 0);
        };
        for (int i = 0; i <= numCats(); ++i) {
            const bool editing = fCatInput && fCatEditIdx == i && i < numCats();
            const bool withX = fDlgEdit && i < numCats() && !editing;
            const float w = editW(editing ? fCatName : std::string(saveCatName(i)), editing, withX);
            const float x0 = place(cx, cy, w);
            if (withX) fn(8, i, x0 + w - 16, cy, 16, 18);
            fn(1, i, x0, cy, w, 18);
        }
        {
            const bool adding = fCatInput && fCatEditIdx < 0;
            const float w = std::max(chipWidth(adding ? (fCatName + "|").c_str() : "+ CATEGORY"), 60.f);
            const float x0 = place(cx, cy, w); // before cy is read: place() may wrap
            fn(5, 0, x0, cy, w, 18);
        }
        float tx = fx, ty = cy + 32;
        fDlgTagsY = ty;
        for (int t = 0; t < numTags(); ++t) {
            const bool editing = fTagInput && fTagEditIdx == t;
            const bool withX = fDlgEdit && !editing;
            const float w = editW(editing ? fTagName : fTagList[size_t(t)], editing, withX);
            const float x0 = place(tx, ty, w);
            if (withX) fn(9, t, x0 + w - 16, ty, 16, 18);
            fn(2, t, x0, ty, w, 18);
        }
        if (numTags() < 32) {
            const bool adding = fTagInput && fTagEditIdx < 0;
            const float w = std::max(chipWidth(adding ? (fTagName + "|").c_str() : "+ TAG"), 40.f);
            const float x0 = place(tx, ty, w);
            fn(6, 0, x0, ty, w, 18);
        }
        {
            const char* lbl = fDlgEdit ? "done" : "edit categories & tags";
            const float w = chipWidth(lbl);
            const float x0 = place(tx, ty, w);
            fn(7, 0, x0, ty, w, 18);
        }
        fDlgContentH = ty + 18 - g.y;
        fn(3, 0, g.x + g.w - 16 - 80 - 8 - 80, g.y + g.h - 38, 80, 24);
        fn(4, 0, g.x + g.w - 16 - 80, g.y + g.h - 38, 80, 24);
    }

    // a chip in edit mode: label, an x at its right end (red = armed); or a name field
    void editChip(float x, float y, float w, float h, const std::string& label, bool on, bool field, bool withX, bool armed, bool xHot)
    {
        if (field) {
            frame(x, y, w, h, col(kWell), col(kAccent));
            fontSize(10);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(col(kText));
            text(x + 7, y + h / 2 + 1, (label + "|").c_str(), nullptr);
            return;
        }
        if (!withX) { chip(x, y, label.c_str(), on); return; }
        frame(x, y, w, h, on ? col(kAccent) : col(kWell), armed ? Color(0xe0, 0x5a, 0x4a) : on ? col(kAccent) : col(kBorder));
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(on ? col(kBg) : col(kText));
        text(x + 7, y + h / 2 + 1, label.c_str(), nullptr);
        const float cx = x + w - 8, cy = y + h / 2;
        if (armed) { beginPath(); rect(x + w - 15, y + 2, 13, h - 4); fillColor(Color(0xe0, 0x5a, 0x4a)); fill(); }
        strokeColor(armed || xHot ? col(kText) : on ? col(kBg) : col(kDim));
        strokeWidth(1.3f);
        beginPath();
        moveTo(cx - 3, cy - 3); lineTo(cx + 3, cy + 3);
        moveTo(cx + 3, cy - 3); lineTo(cx - 3, cy + 3);
        stroke();
    }

    void drawSaveDialog()
    {
        dialogItems([](int, int, float, float, float, float) {}); // measure: the height follows the content
        fDlgH = std::max(250.f, fDlgContentH + 80);
        const DlgGeo g = dlgGeo();
        beginPath();
        rect(0, 0, kWidth, fHeight);
        fillColor(col(kBg, 170));
        fill();
        frame(g.x, g.y, g.w, g.h, col(kPanel), col(kAccent));
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kText));
        text(g.x + 16, g.y + 16, fEditIdx >= 0 ? "EDIT PRESET" : "SAVE PRESET", nullptr);
        fontSize(10);
        fillColor(col(kDim));
        text(g.x + 16, g.y + 45, "Name", nullptr);
        text(g.x + 16, g.y + 75, "Category", nullptr);
        text(g.x + 16, fDlgTagsY + 9, "Tags", nullptr);
        dialogItems([&](int kind, int i, float x, float y, float w, float h) {
            switch (kind) {
            case 0: {
                frame(x, y, w, h, col(kWell), col(kAccent));
                fontSize(12);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(col(kText));
                const std::string t = fSaveName + "|";
                text(x + 7, y + h / 2 + 1, t.c_str(), nullptr);
                break;
            }
            case 1: {
                const bool field = fCatInput && fCatEditIdx == i && i < numCats();
                editChip(x, y, w, h, field ? fCatName : std::string(saveCatName(i)), fSaveCat == i, field,
                         fDlgEdit && i < numCats(), fCatDelArm == i, fDlgHoverKind == 8 && fDlgHover == i);
                break;
            }
            case 2: {
                const bool field = fTagInput && fTagEditIdx == i;
                editChip(x, y, w, h, field ? fTagName : fTagList[size_t(i)], (fSaveTags >> i) & 1u, field,
                         fDlgEdit, fTagDelArm == i, fDlgHoverKind == 9 && fDlgHover == i);
                break;
            }
            case 5:
            case 6: {
                const bool adding = kind == 5 ? fCatInput && fCatEditIdx < 0 : fTagInput && fTagEditIdx < 0;
                if (adding) editChip(x, y, w, h, kind == 5 ? fCatName : fTagName, false, true, false, false, false);
                else {
                    frame(x, y, w, h, col(kPanel), col(kBorder));
                    fontSize(10); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(col(kDim));
                    text(x + 7, y + h / 2 + 1, kind == 5 ? "+ CATEGORY" : "+ TAG", nullptr);
                }
                break;
            }
            case 7: chip(x, y, fDlgEdit ? "done" : "edit categories & tags", fDlgEdit); break;
            case 3:
                frame(x, y, w, h, col(kAccent), col(kAccent));
                fontSize(11); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(col(kBg));
                text(x + w / 2, y + h / 2 + 1, "SAVE", nullptr);
                break;
            case 4:
                frame(x, y, w, h, col(kWell), col(kBorder));
                fontSize(11); textAlign(ALIGN_CENTER | ALIGN_MIDDLE); fillColor(col(kText));
                text(x + w / 2, y + h / 2 + 1, "CANCEL", nullptr);
                break;
            }
        });
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(fSaveError ? col(kAccent) : col(kDim));
        std::string msg = !fSaveMsg.empty() ? fSaveMsg : saveHint();
        if (fSaveMsg.empty()) {
            if (fCatInput || fTagInput) msg = "Type the name, Enter to apply, Esc to cancel.";
            else if (fCatDelArm >= 0) msg = std::string("Click the red x again to delete the category \"") + saveCatName(fCatDelArm) + "\" (its presets move to User).";
            else if (fTagDelArm >= 0) msg = "Click the red x again to delete the tag \"" + fTagList[size_t(fTagDelArm)] + "\" (removed from every preset).";
            else if (fDlgEdit) msg = "Click a category or tag to rename it, its x to delete it.  Changes apply to all presets.";
        }
        text(g.x + 16, g.y + g.h - 26, msg.c_str(), nullptr);
    }

    std::string saveHint() const
    {
        if (fEditIdx >= 0) return "Changes the name, category and tags; the sound stays as saved.";
        for (const auto& e : fPresets)
            if (e.name == fSaveName) return e.user ? "Replaces your preset of that name." : "Factory name - choose another.";
        return "Saved to " + userPresetDir();
    }

    void openSaveDialog()
    {
        closeSaveDialog();
        fBrowser = false;
        fEditIdx = -1;
        fSaveDlg = true;
        fSaveMsg.clear();
        fSaveError = false;
        fSaveName = fPresetName == "Init" ? std::string("My ") + voiceDef().name : fPresetName;
        fSaveCat = numCats(); // User, unless the preset has one of the voice's categories
        fSaveTags = 0;
        if (fPreset >= 0) {
            const PresetEntry& e = fPresets[size_t(fPreset)];
            for (int i = 0; i < numCats(); ++i)
                if (e.category == saveCatName(i)) fSaveCat = i;
            for (int t = 0; t < numTags(); ++t)
                if (std::find(e.tags.begin(), e.tags.end(), fTagList[size_t(t)]) != e.tags.end()) fSaveTags |= 1u << t;
        }
        repaint();
    }

    // the save dialog's categories: the voice's own, then "User" (index numCats())
    // categories: the plugin's own, then the user's (categories.txt in the preset folder);
    // index numCats() is "User"
    int numVoiceCats() const
    {
        const maquina::VoiceCategories* vc = maquina::categoriesOf(voiceDef().id);
        int nc = 0;
        while (vc && nc < 10 && vc->names[nc]) ++nc;
        return nc;
    }
    int numCats() const { return int(fCats.size()); }
    const char* saveCatName(int i) const { return i >= 0 && i < numCats() ? fCats[size_t(i)].c_str() : "User"; }
    int numTags() const { return int(fTagList.size()); }
    std::string catFile() const
    {
        const std::string dir = userPresetDir();
#ifdef _WIN32
        return dir.empty() ? dir : dir + "\\categories.txt";
#else
        return dir.empty() ? dir : dir + "/categories.txt";
#endif
    }
    // hidden.txt in the preset folder: factory presets deleted (or replaced by an edited copy), one name per line
    std::string hiddenFile() const
    {
        const std::string dir = userPresetDir();
        return dir.empty() ? dir : dir + "/hidden.txt";
    }
    void loadHidden()
    {
        fHidden.clear();
        std::ifstream in(hiddenFile());
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) fHidden.push_back(line);
        }
    }
    bool hideFactory(const std::string& name)
    {
        const std::string dir = userPresetDir();
        if (dir.empty()) return false;
        makeDirs(dir);
        if (std::find(fHidden.begin(), fHidden.end(), name) == fHidden.end()) fHidden.push_back(name);
        std::ofstream out(hiddenFile());
        for (const auto& h : fHidden) out << h << "\n";
        return bool(out);
    }
    void restoreFactory()
    {
        const size_t n = fHidden.size();
        fHidden.clear();
        std::remove(hiddenFile().c_str());
        fLibMsg = "Restored " + std::to_string(n) + " factory preset(s).";
        loadPresetList();
        repaint();
    }
    // ratings.txt in the preset folder: "<key> <1..5>" per line
    std::string ratingFile() const
    {
        const std::string dir = userPresetDir();
        return dir.empty() ? dir : dir + "/ratings.txt";
    }
    static std::string presetKey(const PresetEntry& e)
    {
        return e.user ? "user:" + e.path.substr(e.path.find_last_of("/\\") + 1) : "factory:" + e.name;
    }
    int ratingOf(const PresetEntry& e) const
    {
        const std::string k = presetKey(e);
        for (const auto& r : fRatings) if (r.first == k) return r.second;
        return 0;
    }
    void loadRatings()
    {
        fRatings.clear();
        std::ifstream in(ratingFile());
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            const size_t sp = line.find_last_of(' ');
            if (sp == std::string::npos || sp == 0) continue;
            const int v = std::atoi(line.c_str() + sp + 1);
            if (v >= 1 && v <= 5) fRatings.emplace_back(line.substr(0, sp), v);
        }
    }
    void saveRatings()
    {
        const std::string dir = userPresetDir();
        if (dir.empty()) return;
        makeDirs(dir);
        std::ofstream out(ratingFile());
        for (const auto& r : fRatings) out << r.first << " " << r.second << "\n";
        if (!out) fLibMsg = "Could not write " + ratingFile();
    }
    void setRatingKey(const std::string& k, int v) // v 0 clears
    {
        fRatings.erase(std::remove_if(fRatings.begin(), fRatings.end(), [&](const auto& r) { return r.first == k; }), fRatings.end());
        if (v > 0) fRatings.emplace_back(k, v);
        saveRatings();
    }
    void setRating(int idx, int v) // clicking the current rating again clears it
    {
        if (idx < 0 || idx >= int(fPresets.size())) return;
        const PresetEntry& e = fPresets[size_t(idx)];
        setRatingKey(presetKey(e), ratingOf(e) == v ? 0 : v);
    }
    // categories.txt: "#all" then the full list (older files: the user's own, after the plugin's);
    // tags.txt: the full tag list; labelmap.txt: "cat|tag <TAB> old <TAB> new" renames / deletions
    // applied to the factory presets (user preset files are rewritten instead)
    std::string libFile(const char* name) const
    {
        const std::string dir = userPresetDir();
        return dir.empty() ? dir : dir + "/" + name;
    }
    static std::vector<std::string> readLines(const std::string& path)
    {
        std::vector<std::string> v;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) v.push_back(line);
        }
        return v;
    }
    void loadUserCats()
    {
        fCats.clear();
        const std::vector<std::string> lines = readLines(catFile());
        const bool full = !lines.empty() && lines[0] == "#all";
        if (!full) {
            const maquina::VoiceCategories* vc = maquina::categoriesOf(voiceDef().id);
            for (int i = 0; i < numVoiceCats(); ++i) fCats.push_back(vc->names[i]);
        }
        for (size_t k = full ? 1 : 0; k < lines.size(); ++k)
            if (std::find(fCats.begin(), fCats.end(), lines[k]) == fCats.end()) fCats.push_back(lines[k]);
        fTagList.clear();
        const std::vector<std::string> tl = readLines(libFile("tags.txt"));
        if (tl.empty() || tl[0] != "#all")
            for (int t = 0; t < maquina::kNumPresetTags; ++t) fTagList.push_back(maquina::kPresetTags[t]);
        for (size_t k = (!tl.empty() && tl[0] == "#all") ? 1 : 0; k < tl.size() && fTagList.size() < 32; ++k)
            if (std::find(fTagList.begin(), fTagList.end(), tl[k]) == fTagList.end()) fTagList.push_back(tl[k]);
        fLabelMap.clear();
        for (const std::string& l : readLines(libFile("labelmap.txt"))) {
            const size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            fLabelMap.push_back({ l.substr(0, t1), l.substr(t1 + 1, t2 - t1 - 1), l.substr(t2 + 1) });
        }
    }
    bool saveUserCats()
    {
        const std::string dir = userPresetDir();
        if (dir.empty()) return false;
        makeDirs(dir);
        std::ofstream out(catFile());
        out << "#all\n";
        for (const auto& c : fCats) out << c << "\n";
        std::ofstream tout(libFile("tags.txt"));
        tout << "#all\n";
        for (const auto& t : fTagList) tout << t << "\n";
        std::ofstream mout(libFile("labelmap.txt"));
        for (const auto& m : fLabelMap) mout << m.kind << "\t" << m.from << "\t" << m.to << "\n";
        return bool(out) && bool(tout) && bool(mout);
    }
    // a factory preset's category / tag after the user's renames and deletions ("" = tag deleted)
    std::string mapLabel(const char* kind, std::string v) const
    {
        for (int hop = 0; hop < 16; ++hop) {
            bool moved = false;
            for (const auto& m : fLabelMap)
                if (m.kind == kind && m.from == v) { v = m.to; moved = true; break; }
            if (!moved || v.empty()) break;
        }
        return v;
    }
    void addLabelMap(const std::string& kind, const std::string& from, const std::string& to)
    {
        fLabelMap.erase(std::remove_if(fLabelMap.begin(), fLabelMap.end(),
                                       [&](const LabelMap& m) { return m.kind == kind && m.from == from; }), fLabelMap.end());
        if (from != to) fLabelMap.push_back({ kind, from, to });
    }
    // rewrites every user preset file whose category / tags change; returns how many
    int rewriteUserPresets(const std::function<bool(std::string&, std::vector<std::string>&)>& change)
    {
        int n = 0;
        for (const PresetEntry& e : fPresets) {
            if (!e.user || e.path.empty()) continue;
            std::string cat = e.category;
            std::vector<std::string> tags = e.tags;
            if (!change(cat, tags)) continue;
            const std::string t = withHeader(readFile(e.path), e.name, cat, tags);
            std::ofstream out(e.path);
            out << t;
            n += bool(out);
        }
        return n;
    }
    static std::string trimmed(std::string n)
    {
        while (!n.empty() && n.back() == ' ') n.pop_back();
        while (!n.empty() && n.front() == ' ') n.erase(0, 1);
        return n;
    }
    void commitCatInput()
    {
        const std::string n = trimmed(fCatName);
        const int idx = fCatEditIdx;
        fCatInput = false;
        if (n.empty() || (idx >= 0 && n == fCats[size_t(idx)])) { repaint(); return; }
        bool clash = lower(n) == "all" || lower(n) == "user" || lower(n) == "rated";
        for (int i = 0; i < numCats(); ++i) clash |= i != idx && lower(n) == lower(saveCatName(i));
        if (clash) { fLibMsg = "\"" + n + "\" exists already."; repaint(); return; }
        if (idx < 0) {
            fCats.push_back(n);
            fBrowseCat = numCats(); // the new one
        } else { // rename
            const std::string old = fCats[size_t(idx)];
            fCats[size_t(idx)] = n;
            for (auto& m : fLabelMap) if (m.kind == "cat" && m.to == old) m.to = n;
            addLabelMap("cat", old, n);
            rewriteUserPresets([&](std::string& c, std::vector<std::string>&) { if (c != old) return false; c = n; return true; });
            fLibMsg = "Category \"" + old + "\" is now \"" + n + "\".";
        }
        if (!saveUserCats()) fLibMsg = "Could not write " + catFile();
        loadPresetList();
        repaint();
    }
    void commitTagInput()
    {
        std::string n = trimmed(fTagName);
        for (auto& c : n) if (c == ',' ) c = ' '; // tags are comma separated in the files
        n = trimmed(n);
        const int idx = fTagEditIdx;
        fTagInput = false;
        if (n.empty() || (idx >= 0 && n == fTagList[size_t(idx)])) { repaint(); return; }
        for (int t = 0; t < numTags(); ++t)
            if (t != idx && lower(n) == lower(fTagList[size_t(t)])) { fLibMsg = "Tag \"" + n + "\" exists already."; repaint(); return; }
        if (idx < 0) {
            if (numTags() >= 32) { fLibMsg = "32 tags at most."; repaint(); return; }
            fTagList.push_back(n);
        } else {
            const std::string old = fTagList[size_t(idx)];
            fTagList[size_t(idx)] = n;
            for (auto& m : fLabelMap) if (m.kind == "tag" && m.to == old) m.to = n;
            addLabelMap("tag", old, n);
            rewriteUserPresets([&](std::string&, std::vector<std::string>& tg) {
                auto it = std::find(tg.begin(), tg.end(), old);
                if (it == tg.end()) return false;
                *it = n;
                return true;
            });
            fLibMsg = "Tag \"" + old + "\" is now \"" + n + "\".";
        }
        if (!saveUserCats()) fLibMsg = "Could not write " + libFile("tags.txt");
        loadPresetList();
        repaint();
    }
    void deleteTag(int idx)
    {
        if (idx < 0 || idx >= numTags()) return;
        const std::string old = fTagList[size_t(idx)];
        fTagList.erase(fTagList.begin() + idx);
        for (auto& m : fLabelMap) if (m.kind == "tag" && m.to == old) m.to.clear();
        addLabelMap("tag", old, "");
        const int n = rewriteUserPresets([&](std::string&, std::vector<std::string>& tg) {
            auto it = std::find(tg.begin(), tg.end(), old);
            if (it == tg.end()) return false;
            tg.erase(it);
            return true;
        });
        fTagDelArm = -1;
        auto dropBit = [idx](uint32_t m) { return (m & ((1u << idx) - 1)) | ((m >> (idx + 1)) << idx); };
        fTagMask = dropBit(fTagMask);
        fSaveTags = dropBit(fSaveTags);
        saveUserCats();
        fLibMsg = "Tag \"" + old + "\" deleted" + (n ? ", removed from " + std::to_string(n) + " of your presets." : ".");
        loadPresetList();
        repaint();
    }
    static std::string lower(std::string t)
    {
        for (auto& c : t) c = char(std::tolower(uint8_t(c)));
        return t;
    }
    // the preset file's name / category / tags lines replaced, its values kept
    static std::string withHeader(const std::string& text, const std::string& name, const std::string& cat,
                                  const std::vector<std::string>& tags)
    {
        std::stringstream in(text);
        std::string out, line;
        while (std::getline(in, line)) {
            std::string key = line.substr(0, line.find('='));
            while (!key.empty() && key.back() == ' ') key.pop_back();
            if (line.find('=') != std::string::npos && key == "name") line = "name = " + name;
            else if (line.find('=') != std::string::npos && key == "category") line = "category = " + cat;
            else if (line.find('=') != std::string::npos && key == "tags") line = "tags = " + maquina::joinTags(tags);
            out += line + "\n";
        }
        return out;
    }
    static std::string readFile(const std::string& path)
    {
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    void deleteCat(int ci) // any category; its presets move to User
    {
        if (ci < 0 || ci >= numCats()) return;
        const std::string name = fCats[size_t(ci)];
        fCats.erase(fCats.begin() + ci);
        if (fSaveCat == ci) fSaveCat = numCats(); // User
        else if (fSaveCat > ci) --fSaveCat;
        for (auto& m : fLabelMap) if (m.kind == "cat" && m.to == name) m.to = "User";
        addLabelMap("cat", name, "User");
        const int moved = rewriteUserPresets([&](std::string& c, std::vector<std::string>&) {
            if (c != name) return false;
            c = "User";
            return true;
        });
        saveUserCats();
        fCatDelArm = -1;
        fBrowseCat = 0;
        fLibMsg = "Category \"" + name + "\" deleted" + (moved ? ", " + std::to_string(moved) + " of your presets now under User." : ".");
        loadPresetList();
        repaint();
    }
    void deletePreset(int idx)
    {
        if (idx < 0 || idx >= int(fPresets.size())) return;
        const PresetEntry e = fPresets[size_t(idx)];
        if (!e.user) {
            if (!hideFactory(e.name)) { fLibMsg = "Could not write " + hiddenFile(); repaint(); return; }
        } else if (std::remove(e.path.c_str()) != 0) { fLibMsg = "Could not delete " + e.path; repaint(); return; }
        if (ratingOf(e)) setRatingKey(presetKey(e), 0);
        fDelArm = -1;
        fLibMsg = e.user ? "Deleted \"" + e.name + "\"." : "Hid factory preset \"" + e.name + "\" (RESTORE brings it back).";
        const bool loaded = idx == fPreset;
        loadPresetList();
        if (loaded) { fPreset = -1; fPresetDirty = true; savePresetState(); }
        repaint();
    }
    static std::string slugOf(const std::string& name)
    {
        std::string slug;
        for (char c : name) slug += std::isalnum(uint8_t(c)) ? char(std::tolower(c)) : '-';
        if (slug.find_first_not_of('-') == std::string::npos) slug = "preset";
        return slug;
    }
    // EDIT: the save dialog for an existing user preset (its name, category, tags)
    void openEditDialog(int idx)
    {
        if (idx < 0 || idx >= int(fPresets.size())) return;
        closeSaveDialog();
        const PresetEntry& e = fPresets[size_t(idx)];
        fSaveDlg = true;
        fEditIdx = idx;
        fSaveMsg.clear();
        fSaveError = false;
        fSaveName = e.name;
        fSaveCat = numCats();
        for (int i = 0; i < numCats(); ++i)
            if (e.category == saveCatName(i)) fSaveCat = i;
        fSaveTags = 0;
        for (int t = 0; t < numTags(); ++t)
            if (std::find(e.tags.begin(), e.tags.end(), fTagList[size_t(t)]) != e.tags.end()) fSaveTags |= 1u << t;
        repaint();
    }
    void doEdit()
    {
        const PresetEntry e = fPresets[size_t(fEditIdx)];
        std::string name = fSaveName;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name.front() == ' ') name.erase(0, 1);
        fSaveError = true;
        if (name.empty()) { fSaveMsg = "Please enter a name."; repaint(); return; }
        for (size_t k = 0; k < fPresets.size(); ++k)
            if (const auto& o = fPresets[k]; int(k) != fEditIdx && o.name == name) {
                fSaveMsg = o.user ? "Another preset has that name." : "A factory preset has that name - choose another.";
                repaint();
                return;
            }
        std::vector<std::string> tags;
        for (int t = 0; t < numTags(); ++t)
            if ((fSaveTags >> t) & 1u) tags.push_back(fTagList[size_t(t)]);
        std::string text, path = e.path;
        if (e.user) text = withHeader(readFile(e.path), name, saveCatName(fSaveCat), tags);
        else { // a factory preset: becomes a user preset, the original is hidden
            std::vector<float> v = fValues;
            maquina::applyPreset(voiceDef(), e.values.data(), uint32_t(e.values.size()), v.data());
            text = maquina::writePreset(voiceDef(), name, saveCatName(fSaveCat), tags, v.data());
        }
        if (name != e.name || !e.user) { // renamed: the file follows the name
            const std::string dir = e.user ? e.path.substr(0, e.path.find_last_of('/')) : userPresetDir();
            if (dir.empty()) { fSaveMsg = "No config folder (HOME not set)."; repaint(); return; }
            makeDirs(dir);
            const std::string base = dir + "/" + slugOf(name);
            path = base + maquina::kPresetExt;
            for (int n = 2; n < 100 && path != e.path; ++n) {
                std::ifstream in(path);
                if (!in) break;
                path = base + "-" + std::to_string(n) + maquina::kPresetExt;
            }
        }
        std::ofstream out(path);
        out << text;
        if (!out) { fSaveMsg = "Could not write " + path; repaint(); return; }
        out.close();
        if (!e.user) hideFactory(e.name);
        if (path != e.path) {
            if (e.user) std::remove(e.path.c_str());
            PresetEntry moved = e;
            moved.user = true;
            moved.path = path;
            if (const int r = ratingOf(e)) { setRatingKey(presetKey(e), 0); setRatingKey(presetKey(moved), r); }
        }
        if (fPresetName == e.name) { fPresetName = name; savePresetState(); }
        fSaveError = false;
        fSaveMsg.clear();
        fSaveDlg = false;
        fEditIdx = -1;
        fLibMsg = "Saved \"" + name + "\".";
        loadPresetList();
        repaint();
    }

    void doSave()
    {
        if (fEditIdx >= 0) { doEdit(); return; }
        std::string name = fSaveName;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name.front() == ' ') name.erase(0, 1);
        fSaveError = true;
        if (name.empty()) { fSaveMsg = "Please enter a name."; repaint(); return; }
        for (const auto& e : fPresets)
            if (e.name == name && !e.user) { fSaveMsg = "A factory preset has that name - choose another."; repaint(); return; }
        const std::string dir = userPresetDir();
        if (dir.empty()) { fSaveMsg = "No config folder (HOME not set)."; repaint(); return; }
        std::string slug;
        for (char c : name) slug += std::isalnum(uint8_t(c)) ? char(std::tolower(c)) : '-';
        if (slug.find_first_not_of('-') == std::string::npos) slug = "preset";
        std::vector<std::string> tags;
        for (int t = 0; t < numTags(); ++t)
            if ((fSaveTags >> t) & 1u) tags.push_back(fTagList[size_t(t)]);
        makeDirs(dir);
#ifdef _WIN32
        const std::string path = dir + "\\" + slug + maquina::kPresetExt;
#else
        const std::string path = dir + "/" + slug + maquina::kPresetExt;
#endif
        // a different preset may own this file name: keep both
        std::string finalPath = path;
        for (int n = 2; n < 100; ++n) {
            std::ifstream in(finalPath);
            if (!in) break;
            std::stringstream ss; ss << in.rdbuf();
            if (maquina::parsePreset(voiceDef(), ss.str()).name == name) break;
            finalPath = path.substr(0, path.size() - maquina::kPresetExtLen) + "-" + std::to_string(n) + maquina::kPresetExt;
        }
        std::ofstream out(finalPath);
        out << maquina::writePreset(voiceDef(), name, saveCatName(fSaveCat), tags,
                                   fValues.data());
        if (!out) { fSaveMsg = "Could not write " + finalPath; repaint(); return; }
        out.close();
        fSaveError = false;
        fSaveMsg.clear();
        fPresetName = name;
        fPresetDirty = false;
        loadPresetList();
        savePresetState();
        fSaveDlg = false;
        repaint();
    }

    bool dialogClick(float x, float y)
    {
        bool hit = false;
        fLibMsg.clear();
        dialogItems([&](int kind, int i, float bx, float by, float w, float h) {
            if (hit || x < bx || x >= bx + w || y < by || y >= by + h) return;
            hit = true;
            const bool keepCat = (kind == 1 && fCatInput && fCatEditIdx == i) || (kind == 5 && fCatInput && fCatEditIdx < 0);
            const bool keepTag = (kind == 2 && fTagInput && fTagEditIdx == i) || (kind == 6 && fTagInput && fTagEditIdx < 0);
            if (!keepCat) fCatInput = false;
            if (!keepTag) fTagInput = false;
            if (kind != 8) fCatDelArm = -1;
            if (kind != 9) fTagDelArm = -1;
            switch (kind) {
            case 1:
                if (keepCat) break;
                if (fDlgEdit && i < numCats()) { fCatInput = true; fCatEditIdx = i; fCatName = saveCatName(i); }
                else fSaveCat = i;
                break;
            case 2:
                if (keepTag) break;
                if (fDlgEdit) { fTagInput = true; fTagEditIdx = i; fTagName = fTagList[size_t(i)]; }
                else fSaveTags ^= 1u << i;
                break;
            case 3: doSave(); return;
            case 4: closeSaveDialog(); break;
            case 5: if (!keepCat) { fCatInput = true; fCatEditIdx = -1; fCatName.clear(); } break;
            case 6: if (!keepTag) { fTagInput = true; fTagEditIdx = -1; fTagName.clear(); } break;
            case 7: fDlgEdit = !fDlgEdit; break;
            case 8:
                if (fCatDelArm == i) deleteCat(i);
                else fCatDelArm = i;
                break;
            case 9:
                if (fTagDelArm == i) deleteTag(i);
                else fTagDelArm = i;
                break;
            }
        });
        if (!hit) { fCatInput = fTagInput = false; fCatDelArm = fTagDelArm = -1; }
        fSaveMsg = fLibMsg;
        fSaveError = false;
        repaint();
        return true;
    }
    void closeSaveDialog()
    {
        fSaveDlg = false;
        fEditIdx = -1;
        fDlgEdit = fCatInput = fTagInput = false;
        fCatDelArm = fTagDelArm = -1;
    }
    void dialogHover(float x, float y)
    {
        int kind = -1, idx = -1;
        dialogItems([&](int k, int i, float bx, float by, float w, float h) {
            if (kind < 0 && (k == 8 || k == 9) && x >= bx && x < bx + w && y >= by && y < by + h) { kind = k; idx = i; }
        });
        if (kind != fDlgHoverKind || idx != fDlgHover) { fDlgHoverKind = kind; fDlgHover = idx; repaint(); }
    }

    // ---- keyboard (save name, browser search)
    static void popUtf8(std::string& s)
    {
        while (!s.empty() && (uint8_t(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty()) s.pop_back();
    }
    static std::string utf8(uint cp)
    {
        std::string o;
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
        return o;
    }
    std::string* textTarget()
    {
        return fSaveDlg ? (fCatInput ? &fCatName : fTagInput ? &fTagName : &fSaveName) : (fBrowser && fCatInput) ? &fCatName : (fBrowser && fTagInput) ? &fTagName : (fBrowser && fSearchFocus) ? &fSearch : nullptr;
    }

    // Text entry works from key presses alone (hosts/desktops without an X
    // input method send no character events); a character event, when one
    // follows, replaces the guess (shifted symbols, other keyboard layouts).
    bool typeKey(const KeyboardEvent& ev)
    {
        std::string* t = textTarget();
        if (!t || (ev.mod & (kModifierControl | kModifierAlt | kModifierSuper))) return false;
        uint cp = ev.key;
        if (cp < 32 || cp == 127 || cp == '=' || cp >= 0xE000) return false;
        if ((ev.mod & kModifierShift) && cp >= 'a' && cp <= 'z') cp -= 'a' - 'A';
        if (t->size() >= 40) return true;
        const std::string add = utf8(cp);
        *t += add;
        fTypedKeycode = ev.keycode;
        fTypedLen = add.size();
        fSaveMsg.clear();
        repaint();
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!ev.press || !(fSaveDlg || (fBrowser))) return false;
        if (ev.key == kKeyEscape) {
            if (fSaveDlg && (fCatInput || fTagInput)) fCatInput = fTagInput = false;
            else if (fSaveDlg) closeSaveDialog();
            else if (fCatInput) fCatInput = false;
            else if (fTagInput) fTagInput = false;
            else if (fTagEdit) fTagEdit = false;
            else if (fSearchFocus) fSearchFocus = false;
            else fBrowser = false;
            repaint();
            return true;
        }
        if (fSaveDlg) {
            std::string* t = textTarget();
            if (ev.key == kKeyBackspace) { popUtf8(*t); fTypedLen = 0; fSaveMsg.clear(); repaint(); return true; }
            if (ev.key == kKeyEnter) {
                fLibMsg.clear();
                if (fCatInput) {
                    const bool adding = fCatEditIdx < 0;
                    commitCatInput();
                    if (adding && fLibMsg.empty()) fSaveCat = numCats() - 1;
                } else if (fTagInput) {
                    const bool adding = fTagEditIdx < 0;
                    const int before = numTags();
                    commitTagInput();
                    if (adding && numTags() > before) fSaveTags |= 1u << (numTags() - 1);
                } else { doSave(); return true; }
                fSaveMsg = fLibMsg;
                fSaveError = false;
                repaint();
                return true;
            }
            typeKey(ev);
            return true;
        }
        if (fCatInput) {
            if (ev.key == kKeyBackspace) { popUtf8(fCatName); fTypedLen = 0; repaint(); return true; }
            if (ev.key == kKeyEnter) { commitCatInput(); return true; }
            typeKey(ev);
            return true;
        }
        if (fTagInput) {
            if (ev.key == kKeyBackspace) { popUtf8(fTagName); fTypedLen = 0; repaint(); return true; }
            if (ev.key == kKeyEnter) { commitTagInput(); return true; }
            typeKey(ev);
            return true;
        }
        if (fSearchFocus) {
            if (ev.key == kKeyBackspace) { popUtf8(fSearch); fTypedLen = 0; repaint(); return true; }
            if (ev.key == kKeyEnter) { fSearchFocus = false; repaint(); return true; }
            typeKey(ev);
            return true;
        }
        return false;
    }
    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        std::string* target = textTarget();
        if (!target) return false;
        if (fTypedLen && ev.keycode == fTypedKeycode && target->size() >= fTypedLen) // replace the guess
            target->resize(target->size() - fTypedLen);
        fTypedLen = 0;
        if (ev.character >= 32 && ev.character != 127 && ev.character != '=' && target->size() < 40)
            *target += ev.string;
        fSaveMsg.clear();
        repaint();
        return true;
    }

    void drawLabel(const Ctl& c)
    {
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(kLabel));
        text(c.x + c.w / 2, c.y + 7, c.label, nullptr);
    }

    void drawKnob(const Ctl& c)
    {
        drawLabel(c);
        const float cx = c.x + c.w / 2, cy = c.y + 30, r = 13.f;
        const float a0 = float(0.75 * M_PI), a1 = float(2.25 * M_PI);
        const float lo = minOf(c.param), hi = maxOf(c.param);
        const float t = (fValues[c.param] - lo) / (hi - lo);
        const float av = a0 + t * (a1 - a0);
        const bool bipolar = lo < 0.f;
        const float ac = bipolar ? a0 + (0.f - lo) / (hi - lo) * (a1 - a0) : a0;
        const bool active = fDrag == int(c.param);

        beginPath();
        arc(cx, cy, r, a0, a1, CW);
        strokeColor(col(kTrack));
        strokeWidth(3);
        lineCap(BUTT);
        stroke();
        if (std::fabs(av - ac) > 0.001f) {
            beginPath();
            arc(cx, cy, r, std::min(ac, av), std::max(ac, av), CW);
            strokeColor(col(kAccent));
            strokeWidth(3);
            stroke();
        }
        beginPath();
        moveTo(cx + std::cos(av) * (r - 7), cy + std::sin(av) * (r - 7));
        lineTo(cx + std::cos(av) * (r + 1), cy + std::sin(av) * (r + 1));
        strokeColor(col(kText));
        strokeWidth(2);
        lineCap(ROUND);
        stroke();

        char buf[32];
        valueText(c.param, buf, sizeof(buf));
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(active ? kAccent : kText));
        text(cx, c.y + c.h - 9, buf, nullptr);
    }

    void selectBox(const Ctl& c, float& bx, float& by, float& bw, float& bh) const
    {
        if (c.size > 0) { bx = c.ax - c.bw / 2; bw = c.bw; by = c.ay - c.bh / 2; bh = c.bh; return; }
        bx = c.x + 2; bw = c.w - 4; by = c.y + 18; bh = 22;
    }

    void drawSelect(const Ctl& c)
    {
        drawLabel(c);
        float bx, by, bw, bh;
        selectBox(c, bx, by, bw, bh);
        beginPath();
        rect(bx + 0.5f, by + 0.5f, bw - 1, bh - 1);
        fillColor(col(kWell));
        fill();
        strokeColor(col(fPopup == int(c.param) || fDrag == int(c.param) ? kAccent : kBorder));
        strokeWidth(1);
        stroke();
        // arrows
        fillColor(col(kDim));
        const float my = by + bh / 2;
        beginPath();
        moveTo(bx + 5, my); lineTo(bx + 10, my - 4); lineTo(bx + 10, my + 4); closePath();
        fill();
        beginPath();
        moveTo(bx + bw - 5, my); lineTo(bx + bw - 10, my - 4); lineTo(bx + bw - 10, my + 4); closePath();
        fill();

        char buf[48];
        valueText(c.param, buf, sizeof(buf));
        fontSize(11);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(kText));
        scissor(bx + 12, by, bw - 24, bh);
        text(bx + bw / 2, my + 1, buf, nullptr);
        resetScissor();
    }

    void drawToggle(const Ctl& c)
    {
        drawLabel(c);
        const bool on = fValues[c.param] >= 0.5f;
        const float bw = std::min(40.f, c.w - 6), bx = c.x + (c.w - bw) / 2, by = c.y + 18, bh = 22;
        beginPath();
        rect(bx + 0.5f, by + 0.5f, bw - 1, bh - 1);
        fillColor(on ? col(kAccent) : col(kWell));
        fill();
        strokeColor(col(on ? kAccent : kBorder));
        strokeWidth(1);
        stroke();
        fontSize(11);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(on ? col(kBg) : col(kDim));
        text(bx + bw / 2, by + bh / 2 + 1, on ? "On" : "Off", nullptr);
    }

    // ---- Maquina LA-50: the PG-1000 field
    int la50Param(const La50Slot& sl) const
    {
        if (!sl.field[0] && !sl.field[1]) return sl.param[6]; // KEY MODE, SPLIT: patch only, always
        return fLaMode == 0 ? sl.param[fLaPartial] : fLaMode == 1 ? sl.param[4 + fLaTone] : sl.param[6];
    }
    Ctl la50Ctl(const La50Slot& sl, int param) const
    {
        return { uint32_t(param), CtlType::Slider, "", sl.box.x, sl.box.y - 14, sl.box.w, sl.box.h + 24 };
    }
    int la50MuteParam() const { return findParam(fLaTone ? "l_partial_mute" : "u_partial_mute"); }

    void la50Button(const Box& b, const char* label, bool on)
    {
        frame(b.x, b.y, b.w, b.h, on ? col(kAccent) : col(kWell), on ? col(kAccent) : col(kBorder));
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(on ? col(kBg) : col(kText));
        text(b.x + b.w / 2, b.y + b.h / 2 + 1, label, nullptr);
    }
    void la50Caption(const Box& b, const char* label)
    {
        fontSize(9);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        fillColor(col(kLabel));
        text(b.x, b.y - 3, label, nullptr);
    }

    // one LCD line, cut with an ellipsis to fit w
    void lcdText(float x, float y, float w, std::string s)
    {
        if (textWidth(s.c_str()) > w) {
            while (!s.empty() && textWidth((s + "\xe2\x80\xa6").c_str()) > w) s.pop_back();
            while (!s.empty() && (s.back() & 0xc0) == 0x80) s.pop_back(); // not inside a UTF-8 character
            if (!s.empty() && (s.back() & 0x80)) s.pop_back();
            s += "\xe2\x80\xa6";
        }
        text(x, y, s.c_str(), nullptr);
    }

    // the PCM wave of the selected partial (in COMMON / PATCH: the selected tone's partial)
    int la50PcmPart() const { return fLaMode == 0 ? fLaPartial : fLaTone * 2 + (fLaPartial & 1); }
    int la50PcmParam() const
    {
        static const char* const kSym[4] = { "u_p1_wg_pcm_wave", "u_p2_wg_pcm_wave", "l_p1_wg_pcm_wave", "l_p2_wg_pcm_wave" };
        return findParam(kSym[la50PcmPart()]);
    }
    void drawPcmSelector()
    {
        const La50Panel& P = layout().la50;
        static const char* const kPartName[4] = { "U1", "U2", "L1", "L2" };
        const int p = la50PcmParam();
        if (p < 0) return;
        char cap[32];
        std::snprintf(cap, sizeof cap, "PCM WAVE  %s", kPartName[la50PcmPart()]);
        fontSize(9);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(col(kLabel));
        text(P.pcmPrev.x + 2, P.pcmPrev.y + P.pcmPrev.h + 3, cap, nullptr); // the label below
        for (int k = 0; k < 2; ++k) {
            const Box& b = k ? P.pcmNext : P.pcmPrev;
            frame(b.x, b.y, b.w, b.h, col(kWell), col(kBorder));
            const float cx = b.x + b.w / 2, cy = b.y + b.h / 2;
            beginPath();
            if (k) { moveTo(cx - 2, cy - 4); lineTo(cx + 3, cy); lineTo(cx - 2, cy + 4); }
            else { moveTo(cx + 2, cy - 4); lineTo(cx - 3, cy); lineTo(cx + 2, cy + 4); }
            closePath();
            fillColor(col(kText));
            fill();
        }
        const Box& n = P.pcmName;
        frame(n.x, n.y, n.w, n.h, col(kWell), col(fPopup == p ? kAccent : kBorder));
        const int v = intOf(p);
        const VoiceParam& vp = paramAt(voiceDef(), uint32_t(p));
        char num[16];
        std::snprintf(num, sizeof num, "%d", v + 1);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kAccent));
        text(n.x + 6, n.y + n.h / 2 + 1, num, nullptr);
        fontSize(10);
        fillColor(col(kText));
        text(n.x + 28, n.y + n.h / 2 + 1, v >= 0 && v < int(vp.numLabels) ? vp.labels[v] : "", nullptr);
        // the list mark
        fillColor(col(kDim));
        beginPath();
        const float mx = n.x + n.w - 12, my = n.y + n.h / 2;
        moveTo(mx - 4, my - 2); lineTo(mx + 4, my - 2); lineTo(mx, my + 3); closePath();
        fill();
    }

    // envelope shapes: the selected partial's TVF and TVA, its tone's pitch envelope; each
    // point has a handle: drag sideways for its segment's time, up / down for its level
    struct EnvHandle { float x, y; int tParam, lParam; };
    struct EnvGeo {
        Box b;
        float yTop = 0, yBot = 0;
        bool centre = false;
        std::vector<EnvHandle> pts; // the curve's points in order (the first: the start)
        int susAfter = -1;          // the sustain follows this point (index into pts)
        float susW = 0;             // the sustain's width
        std::string title;
    };
    EnvGeo la50EnvGeo(int e) const // 0 TVF, 1 TVA, 2 P-ENV
    {
        const La50Panel& P = layout().la50;
        static const char* const kPart[4] = { "u_p1_", "u_p2_", "l_p1_", "l_p2_" };
        static const char* const kPartName[4] = { "U1", "U2", "L1", "L2" };
        const int part = fLaMode == 0 ? fLaPartial : fLaTone * 2 + (fLaPartial & 1);
        EnvGeo g;
        g.b = P.env[e];
        const float x0 = g.b.x + 8, x1 = g.b.x + g.b.w - 8;
        g.yTop = g.b.y + 18;
        g.yBot = g.b.y + g.b.h - 6;
        std::vector<int> tP, lP;
        int startL = -1;
        if (e < 2) {
            const std::string pre = std::string(kPart[part]) + (e ? "tva_" : "tvf_");
            for (const char* t : { "t1", "t2", "t3", "t4", "t5" }) tP.push_back(findParam((pre + t).c_str()));
            for (const char* l : { "l1", "l2", "l3", "sus", "end" }) lP.push_back(findParam((pre + l).c_str()));
            g.susAfter = 4;
            g.title = std::string(e ? "TVA ENV  " : "TVF ENV  ") + kPartName[part];
        } else {
            const std::string pre = fLaTone ? "l_penv_" : "u_penv_";
            for (const char* t : { "t1", "t2", "t3", "t4" }) tP.push_back(findParam((pre + t).c_str()));
            for (const char* l : { "l1", "l2", "sus", "end" }) lP.push_back(findParam((pre + l).c_str()));
            startL = findParam((pre + "l0").c_str());
            g.susAfter = 3;
            g.centre = true;
            g.title = std::string("PITCH ENV  ") + (fLaTone ? "LOWER" : "UPPER");
        }
        auto frac = [&](int prm) {
            if (prm < 0) return 0.f;
            const float lo = minOf(uint32_t(prm)), hi = maxOf(uint32_t(prm));
            return hi > lo ? (fValues[uint32_t(prm)] - lo) / (hi - lo) : 0.f;
        };
        auto ly = [&](float f) { return g.yBot - (g.yBot - g.yTop) * std::clamp(f, 0.f, 1.f); };
        // segment widths: exponential in the time value; the sustain a fixed share
        std::vector<float> w;
        float total = 0;
        for (int t : tP) {
            w.push_back(std::pow(2.f, frac(t) * 100.f / 14.f));
            total += w.back();
        }
        const float sus = 0.16f, span = (x1 - x0) * (1 - sus);
        g.susW = (x1 - x0) * sus;
        float x = x0;
        g.pts.push_back({ x, startL >= 0 ? ly(frac(startL)) : g.yBot, -1, startL });
        for (size_t k = 0; k < tP.size(); ++k) {
            x += span * w[k] / total;
            g.pts.push_back({ x, ly(frac(lP[k])), tP[k], lP[k] });
            if (int(k) + 1 == g.susAfter) x += (x1 - x0) * sus;
        }
        return g;
    }
    void drawEnvShape(const EnvGeo& g, bool hot)
    {
        frame(g.b.x, g.b.y, g.b.w, g.b.h, col(kWell), col(kBorder));
        if (g.centre) {
            beginPath();
            moveTo(g.b.x + 4, (g.yTop + g.yBot) / 2); lineTo(g.b.x + g.b.w - 4, (g.yTop + g.yBot) / 2);
            strokeColor(col(kBorder));
            strokeWidth(1);
            stroke();
        }
        // the curve, the sustain held flat
        beginPath();
        moveTo(g.pts[0].x, g.pts[0].y);
        for (size_t k = 1; k < g.pts.size(); ++k) {
            lineTo(g.pts[k].x, g.pts[k].y);
            if (int(k) == g.susAfter) lineTo(g.pts[k].x + g.susW, g.pts[k].y); // held
        }
        strokeColor(col(kAccent, hot ? 255 : 130));
        strokeWidth(1.5f);
        stroke();
        // the key release: a mark at the sustain's end
        if (g.susAfter > 0) {
            const float rx = g.pts[size_t(g.susAfter)].x + g.susW;
            fillColor(col(kDim));
            for (float yy = g.yTop; yy < g.yBot; yy += 6) {
                beginPath();
                rect(rx, yy, 1, 3);
                fill();
            }
        }
        // handles
        for (const EnvHandle& h : g.pts) {
            if (h.tParam < 0 && h.lParam < 0) continue;
            const bool on = fEnvDrag && (h.tParam == fEnvT && h.lParam == fEnvL);
            beginPath();
            rect(h.x - 3, h.y - 3, 6, 6);
            fillColor(on ? col(kAccent) : col(kText, hot ? 230 : 140));
            fill();
        }
        fontSize(9);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(col(hot ? kText : kDim));
        text(g.b.x + 5, g.b.y + 3, g.title.c_str(), nullptr);
    }
    void drawLa50Envs()
    {
        for (int e = 0; e < 3; ++e) drawEnvShape(la50EnvGeo(e), e < 2 ? fLaMode == 0 : fLaMode == 1);
    }
    // a press on an envelope graph: grab the nearest handle
    bool la50EnvPress(float x, float y)
    {
        const La50Panel& P = layout().la50;
        for (int e = 0; e < 3; ++e) {
            if (!P.env[e].hit(x, y)) continue;
            const EnvGeo g = la50EnvGeo(e);
            const EnvHandle* best = nullptr;
            float bd = 14.f * 14.f;
            for (const EnvHandle& h : g.pts) {
                if (h.tParam < 0 && h.lParam < 0) continue;
                const float d = (h.x - x) * (h.x - x) + (h.y - y) * (h.y - y);
                if (d < bd) { bd = d; best = &h; }
            }
            if (best) {
                fEnvDrag = true;
                fEnvT = best->tParam;
                fEnvL = best->lParam;
                fEnvX = x;
                fEnvY = y;
                fEnvT0 = fEnvT >= 0 ? fValues[uint32_t(fEnvT)] : 0;
                fEnvL0 = fEnvL >= 0 ? fValues[uint32_t(fEnvL)] : 0;
                fEnvH = g.yBot - g.yTop;
                fLaLast = fEnvL >= 0 ? fEnvL : fEnvT;
                fLaMsg.clear();
                if (fEnvT >= 0) editParameter(uint32_t(fEnvT), true);
                if (fEnvL >= 0) editParameter(uint32_t(fEnvL), true);
                repaint();
            }
            return true;
        }
        return false;
    }
    void la50EnvMotion(float x, float y)
    {
        if (fEnvT >= 0) { // sideways: the time (the whole range over ~200 px)
            const float range = maxOf(uint32_t(fEnvT)) - minOf(uint32_t(fEnvT));
            setEnvValue(fEnvT, fEnvT0 + (x - fEnvX) * range / 200.f);
        }
        if (fEnvL >= 0) { // up / down: the level (the graph's height = the whole range)
            const float range = maxOf(uint32_t(fEnvL)) - minOf(uint32_t(fEnvL));
            setEnvValue(fEnvL, fEnvL0 + (fEnvY - y) / std::max(fEnvH, 1.f) * range);
            fLaLast = fEnvL;
        }
        repaint();
    }
    void setEnvValue(int prm, float v) // inside a gesture (begun at the press)
    {
        const uint32_t i = uint32_t(prm);
        v = std::clamp(float(std::lround(v)), minOf(i), maxOf(i));
        if (v == fValues[i]) return;
        endRun();
        if (maquina::isSoundParam(voiceDef(), i)) fPresetDirty = true;
        fValues[i] = v;
        setParameterValue(i, v);
    }
    void la50EnvRelease()
    {
        if (fEnvT >= 0) editParameter(uint32_t(fEnvT), false);
        if (fEnvL >= 0) editParameter(uint32_t(fEnvL), false);
        fEnvDrag = false;
        fEnvT = fEnvL = -1;
        repaint();
    }

    void drawLa50()
    {
        const La50Panel& P = layout().la50;
        static const Rgb kPatchRgb { 0xe0, 0x5a, 0x4a };
        static const char* const kPartialNames[4] = { "U1", "U2", "L1", "L2" };
        // LCD
        frame(P.lcd.x, P.lcd.y, P.lcd.w, P.lcd.h, Color(0x2c, 0x34, 0x24), col(kBorder));
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(Color(0xb8, 0xd8, 0x90));
        const char* modeName = fLaMode == 0 ? kPartialNames[fLaPartial] : fLaMode == 1 ? (fLaTone ? "LOWER COM" : "UPPER COM")
                                                                                       : "PATCH";
        // imported presets are "NN Name (bank file)": the name, then the bank on its own line
        std::string name = fPresetName, bank;
        const size_t paren = name.find(" (");
        if (paren != std::string::npos && name.back() == ')') {
            bank = name.substr(paren + 2, name.size() - paren - 3);
            name.resize(paren);
        }
        if (fPresetDirty) name += " *";
        const float lw = P.lcd.w - 12;
        lcdText(P.lcd.x + 6, P.lcd.y + 11, lw, name);
        fillColor(Color(0x88, 0xa8, 0x68));
        fontSize(9);
        if (!bank.empty()) lcdText(P.lcd.x + 6, P.lcd.y + 26, lw, bank);
        fontSize(11);
        fillColor(Color(0xb8, 0xd8, 0x90));
        lcdText(P.lcd.x + 6, P.lcd.y + 43, lw, std::string("EDIT ") + modeName);
        if (!fLaMsg.empty()) {
            lcdText(P.lcd.x + 6, P.lcd.y + 60, lw, fLaMsg);
        } else if (fLaLast >= 0) {
            char v[48];
            valueText(uint32_t(fLaLast), v, sizeof v);
            lcdText(P.lcd.x + 6, P.lcd.y + 60, lw, std::string(paramAt(voiceDef(), uint32_t(fLaLast)).name) + " " + v);
        }
        // select buttons
        la50Caption(P.patch, "PATCH");
        la50Button(P.patch, "PATCH", fLaMode == 2);
        la50Caption(P.common[0], "COMMON SELECT");
        la50Button(P.common[0], "UPPER", fLaMode == 1 && fLaTone == 0);
        la50Button(P.common[1], "LOWER", fLaMode == 1 && fLaTone == 1);
        la50Caption(P.partial[0], "PARTIAL SELECT");
        for (int k = 0; k < 4; ++k) la50Button(P.partial[k], kPartialNames[k], fLaMode == 0 && fLaPartial == k);
        const int mp = la50MuteParam(), mute = mp >= 0 ? intOf(mp) : 3;
        la50Caption(P.mute[0], fLaTone ? "LOWER PARTIALS ON" : "UPPER PARTIALS ON");
        la50Button(P.mute[0], "P1", (mute & 1) != 0);
        la50Button(P.mute[1], "P2", (mute & 2) != 0);
        la50Button(P.init, "INIT", false);
        la50Button(P.import, "IMPORT .SYX", false);
        fontSize(9);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(col(kDim));
        text(P.import.x, P.import.y + P.import.h + 6, "PCM: built-in stand-ins", nullptr);
        la50Caption(Box { P.gainBox.x + 4, P.gainBox.y + 10, P.gainBox.w, 0 }, "OUTPUT");
        drawLa50Envs();
        drawPcmSelector();
        // group brackets
        for (const La50Group& g : P.groups) {
            const Color c = g.kind == 0 ? col(fLaMode == 0 ? kText : kDim)
                            : g.kind == 1 ? col(kAccent, fLaMode == 1 ? 255 : 110)
                                          : col(kPatchRgb, fLaMode == 2 ? 255 : 110);
            beginPath();
            rect(g.x0, g.y + 4, g.x1 - g.x0, 1);
            fillColor(c);
            fill();
            fontSize(9);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            const float tw = textWidth(g.title) + 8;
            beginPath();
            rect((g.x0 + g.x1) / 2 - tw / 2, g.y - 2, tw, 12);
            fillColor(col(kPanel));
            fill();
            fillColor(c);
            text((g.x0 + g.x1) / 2, g.y + 4, g.title, nullptr);
        }
        // sliders with their three labels
        for (const La50Slot& sl : P.slots) {
            const float cx = sl.box.x + sl.box.w / 2;
            fontSize(8);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            if (sl.label[0]) {
                fillColor(col(fLaMode == 0 ? kText : kDim));
                text(cx, sl.box.y - 6, sl.label[0], nullptr);
            }
            const float by = sl.box.y + sl.box.h + 18;
            if (sl.label[1]) {
                fillColor(col(kAccent, fLaMode == 1 ? 255 : 110));
                text(cx, by, sl.label[1], nullptr);
            }
            if (sl.label[2]) {
                fillColor(col(kPatchRgb, fLaMode == 2 ? 255 : 110));
                text(cx, by + (sl.label[1] ? 10 : 0), sl.label[2], nullptr);
            }
            const int p = la50Param(sl);
            if (p >= 0) {
                drawSlider(la50Ctl(sl, p));
            } else { // nothing to edit in this mode: the empty slot
                beginPath();
                roundedRect(cx - 2, sl.box.y + 10, 4, sl.box.h - 30, 2);
                fillColor(col(kWell, 120));
                fill();
            }
        }
    }

    bool la50Click(float x, float y, bool dbl, uint time)
    {
        const La50Panel& P = layout().la50;
        (void)time;
        if (P.patch.hit(x, y)) { fLaMode = 2; fLaMsg.clear(); repaint(); return true; }
        for (int k = 0; k < 2; ++k)
            if (P.common[k].hit(x, y)) { fLaMode = 1; fLaTone = k; fLaMsg.clear(); repaint(); return true; }
        for (int k = 0; k < 4; ++k)
            if (P.partial[k].hit(x, y)) {
                fLaMode = 0;
                fLaPartial = k;
                fLaTone = k / 2;
                fLaMsg.clear();
                repaint();
                return true;
            }
        for (int k = 0; k < 2; ++k)
            if (P.mute[k].hit(x, y)) {
                const int mp = la50MuteParam();
                if (mp >= 0) setValue(uint32_t(mp), float(intOf(mp) ^ (1 << k)), true);
                return true;
            }
        if (P.init.hit(x, y)) { initPatch(); return true; }
        if (la50EnvPress(x, y)) return true;
        if (const int pw = la50PcmParam(); pw >= 0) { // the PCM wave: < previous, > next, the name: the list
            if (P.pcmPrev.hit(x, y) || P.pcmNext.hit(x, y)) {
                const float d = P.pcmNext.hit(x, y) ? 1.f : -1.f;
                setValue(uint32_t(pw), std::clamp(fValues[uint32_t(pw)] + d, 0.f, 99.f), true);
                fLaLast = pw;
                repaint();
                return true;
            }
            if (P.pcmName.hit(x, y)) {
                fPopup = pw;
                fPopupHover = -1;
                fPopupAX = P.pcmName.x + P.pcmName.w - 2 * kItemW;
                fPopupAY = P.pcmName.y + P.pcmName.h + 2;
                fLaLast = pw;
                repaint();
                return true;
            }
        }
        if (P.import.hit(x, y)) {
#if DISTRHO_UI_FILE_BROWSER
            FileBrowserOptions opts;
            opts.title = "Import D-50 patches (.syx)";
            openFileBrowser(opts);
#endif
            return true;
        }
        for (const La50Slot& sl : P.slots) {
            if (!sl.box.hit(x, y)) continue;
            const int p = la50Param(sl);
            if (p < 0) return true;
            fLaLast = p;
            fLaMsg.clear();
            if (dbl) { setValue(uint32_t(p), defOf(uint32_t(p)), true); return true; }
            fLaDragPixels = sl.box.h - 40;
            beginDrag(p, x, y, 1);
            repaint();
            return true;
        }
        return false;
    }

#if DISTRHO_UI_FILE_BROWSER && MAQUINA_LA50
    // IMPORT: every patch of a .syx file -> a user preset ("Imported")
    void uiFileBrowserSelected(const char* filename) override
    {
        if (!filename) return;
        std::ifstream in(filename, std::ios::binary);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const la50::SysexResult r = la50::parseSysex(data.data(), data.size());
        std::string stem = filename;
        stem = stem.substr(stem.find_last_of("/\\") + 1);
        if (stem.size() > 4 && (stem.compare(stem.size() - 4, 4, ".syx") == 0 || stem.compare(stem.size() - 4, 4, ".SYX") == 0))
            stem.resize(stem.size() - 4);
        const std::string dir = userPresetDir();
        if (r.patches.empty() || dir.empty()) {
            fLaMsg = r.patches.empty() ? "no D-50 patches found" : "no preset folder";
            repaint();
            return;
        }
        makeDirs(dir);
        int written = 0;
        std::string lastName;
        for (const la50::SysexPatch& sp : r.patches) {
            std::vector<float> v(maquina::numAllParams(voiceDef()), 0.f);
            for (uint32_t i = 0; i < voiceDef().numParams; ++i) {
                const VoiceParam& p = voiceDef().params[i];
                const int b = p.fwPar < la50::kPatchBytes ? sp.data[p.fwPar] : 0;
                v[i] = float(p.kind == ParamKind::Int ? b + p.offset : b);
            }
            char num[8];
            std::snprintf(num, sizeof num, "%02d", sp.number);
            const std::string name = std::string(num) + " " + (sp.name.empty() ? "Patch" : sp.name) + " (" + stem + ")";
            std::string slug;
            for (char c : stem + "-" + num) slug += std::isalnum((unsigned char)c) ? char(std::tolower(c)) : '-';
            std::ofstream out(dir + "/" + slug + maquina::kPresetExt);
            out << maquina::writePreset(voiceDef(), name, "Imported", {}, v.data());
            if (out) { ++written; lastName = name; }
        }
        loadPresetList();
        fLaMsg = "imported " + std::to_string(written) + " patches";
        if (r.badChecksums) fLaMsg += " (" + std::to_string(r.badChecksums) + " bad)";
        if (written == 1)
            for (size_t i = 0; i < fPresets.size(); ++i)
                if (fPresets[i].name == lastName) loadPreset(int(i));
        repaint();
    }
#endif

    // ---- Maquina BL-101: the panel's sliders and slide switches
    // slider geometry: the slot from top to bottom (panel units)
    void sliderSlot(const Ctl& c, float& x, float& y0, float& y1) const
    {
        x = c.x + c.w / 2;
        y0 = c.y + 24;
        y1 = c.y + c.h - 22;
    }

    void drawSlider(const Ctl& c)
    {
        drawLabel(c);
        float x, y0, y1;
        sliderSlot(c, x, y0, y1);
        const float lo = minOf(c.param), hi = maxOf(c.param);
        const float t = (fValues[c.param] - lo) / (hi - lo);
        const bool bipolar = lo < 0.f;
        const float yv = y1 - t * (y1 - y0);
        const float yc = bipolar ? y1 - (0.f - lo) / (hi - lo) * (y1 - y0) : y1;
        const bool active = fDrag == int(c.param);

        // scale ticks 0..10, as printed on the panel
        for (int k = 0; k <= 10; ++k) {
            const float ty = y1 - float(k) / 10.f * (y1 - y0);
            beginPath();
            moveTo(x - 11, ty);
            lineTo(x - (k % 5 == 0 ? 6.5f : 8.f), ty);
            strokeColor(col(kTrack));
            strokeWidth(1);
            stroke();
        }
        // slot
        beginPath();
        roundedRect(x - 2, y0 - 4, 4, y1 - y0 + 8, 2);
        fillColor(col(kWell));
        fill();
        strokeColor(col(kBorder));
        strokeWidth(1);
        stroke();
        if (std::fabs(yv - yc) > 0.5f) {
            beginPath();
            rect(x - 1, std::min(yv, yc), 2, std::fabs(yv - yc));
            fillColor(col(kAccent));
            fill();
        }
        // cap
        const float capW = std::min(20.f, c.w - 4), capH = 10.f;
        beginPath();
        roundedRect(x - capW / 2, yv - capH / 2, capW, capH, 1.5f);
        fillColor(col(active ? kAccent : kText));
        fill();
        beginPath();
        moveTo(x - capW / 2 + 2, yv);
        lineTo(x + capW / 2 - 2, yv);
        strokeColor(col(active ? kBg : kPanel));
        strokeWidth(1.5f);
        stroke();

        char buf[32];
        valueText(c.param, buf, sizeof(buf));
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(active ? kAccent : kText));
        text(x, c.y + c.h - 8, buf, nullptr);
    }

    // slide switch geometry: position k's y (0 = bottom, as the panel's
    // switches read upward), the lever's x
    struct SwitchGeo { float x, w, yTop, yBot; int n; };
    SwitchGeo switchGeo(const Ctl& c) const
    {
        SwitchGeo g;
        g.n = int(maxOf(c.param)) + 1;
        g.x = c.x + 4;
        g.w = 10;
        const float mid = c.y + 24 + (c.h - 46) / 2;
        const float span = 22.f * float(g.n - 1);
        g.yTop = mid - span / 2;
        g.yBot = mid + span / 2;
        return g;
    }
    float switchPosY(const SwitchGeo& g, int k) const
    {
        return g.n > 1 ? g.yBot - float(k) * (g.yBot - g.yTop) / float(g.n - 1) : g.yBot;
    }

    void drawSwitch(const Ctl& c)
    {
        drawLabel(c);
        const SwitchGeo g = switchGeo(c);
        const VoiceParam& p = paramAt(voiceDef(), c.param);
        const int cur = intOf(int(c.param));
        // the slot
        beginPath();
        roundedRect(g.x, g.yTop - 8, g.w, g.yBot - g.yTop + 16, 2);
        fillColor(col(kWell));
        fill();
        strokeColor(col(fDrag == int(c.param) ? kAccent : kBorder));
        strokeWidth(1);
        stroke();
        // the lever
        const float ly = switchPosY(g, cur);
        beginPath();
        roundedRect(g.x + 1.5f, ly - 6, g.w - 3, 12, 1.5f);
        fillColor(col(kText));
        fill();
        // position labels
        fontSize(9);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        for (int k = 0; k < g.n; ++k) {
            const float py = switchPosY(g, k);
            beginPath();
            moveTo(g.x + g.w + 1, py);
            lineTo(g.x + g.w + 4, py);
            strokeColor(col(kTrack));
            stroke();
            fillColor(k == cur ? col(kAccent) : col(kDim));
            text(g.x + g.w + 6, py + 1, p.labels && k < p.numLabels ? p.labels[k] : "", nullptr);
        }
    }

    // the position a click at y picks
    int switchPosAt(const Ctl& c, float y) const
    {
        if (c.size > 0) { // the new look: nearest lever position
            const int n = std::max<int>(1, paramAt(voiceDef(), c.param).numLabels);
            int best = 0;
            for (int k = 1; k < n; ++k)
                if (std::fabs(slideSwitchPosY(c.ay - c.bh / 2, c.bh, n, k) - y) <
                    std::fabs(slideSwitchPosY(c.ay - c.bh / 2, c.bh, n, best) - y))
                    best = k;
            return best;
        }
        const SwitchGeo g = switchGeo(c);
        int best = 0;
        for (int k = 1; k < g.n; ++k)
            if (std::fabs(switchPosY(g, k) - y) < std::fabs(switchPosY(g, best) - y)) best = k;
        return best;
    }

    // ---- Maquina BL-101: the OSC DRAW table (16 bars) and the CHOP masks
    static constexpr float kChopLabelW = 36.f, kChopNameW = 48.f;

    void drawDrawGraph(const Graph& g)
    {
        frame(g.x, g.y, g.w, g.h, col(kWell), col(fGraphDrag == 1 ? kAccent : kBorder));
        if (g.first < 0) return;
        const bool on = g.mode >= 0 && intOf(g.mode) != 0;
        const float pad = 4, bw = (g.w - 2 * pad) / 16.f, mid = g.y + g.h / 2, half = g.h / 2 - pad;
        beginPath();
        moveTo(g.x + pad, mid);
        lineTo(g.x + g.w - pad, mid);
        strokeColor(col(kBorder));
        strokeWidth(1);
        stroke();
        float py[16];
        for (int k = 0; k < 16; ++k) {
            const float v = float(intOf(g.first + k)) / 128.f; // -1 .. +0.99
            const float x = g.x + pad + k * bw;
            const float y = mid - v * half;
            py[k] = y;
            beginPath();
            rect(x + 1, std::min(y, mid), bw - 2, std::max(1.f, std::fabs(y - mid)));
            fillColor(col(kAccent, on ? 170 : 60));
            fill();
        }
        // SLOPE: the smoothed wave through the steps
        if (on && intOf(g.mode) == 2) {
            beginPath();
            for (int k = 0; k <= 16 * 8; ++k) {
                const int i = (k / 8) % 16, j = (i + 1) % 16;
                const float t = float(k % 8) / 8.f, st = t * t * (3 - 2 * t);
                const float x = g.x + pad + float(k) / 8.f * bw;
                const float y = py[i] + (py[j] - py[i]) * st;
                if (k == 0) moveTo(x, y); else lineTo(x, y);
            }
            strokeColor(col(kText, 200));
            strokeWidth(1.2f);
            stroke();
        }
        if (!on) {
            fontSize(9);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(col(kDim));
            text(g.x + 5, g.y + 3, "DRAW OFF", nullptr);
        }
    }

    uint16_t chopMaskOf(const Graph& g, int s) const
    {
        const int sel = g.mask[s] >= 0 ? intOf(g.mask[s]) : 1;
        static const uint16_t kPresets[6] = { 0xFFFF, 0x5555, 0xAAAA, 0x1111, 0x8888, 0x0000 }; // Bl101Dsp.hpp
        if (sel >= 1 && sel <= 6) return kPresets[sel - 1];
        const int lo = g.bitsLo[s] >= 0 ? intOf(g.bitsLo[s]) : 255, hi = g.bitsHi[s] >= 0 ? intOf(g.bitsHi[s]) : 255;
        return uint16_t((hi & 255) << 8 | (lo & 255));
    }
    struct ChopGeo { float x0, cellW, rowH, y0, nameX; };
    ChopGeo chopGeo(const Graph& g) const
    {
        ChopGeo c;
        c.x0 = g.x + kChopLabelW;
        c.nameX = g.x + g.w - kChopNameW;
        c.cellW = (c.nameX - 4 - c.x0) / 16.f;
        c.y0 = g.y + 16;
        c.rowH = (g.h - 20) / 4.f;
        return c;
    }

    void drawChopGraph(const Graph& g)
    {
        frame(g.x, g.y, g.w, g.h, col(kWell), col(fGraphDrag == 2 ? kAccent : kBorder));
        const ChopGeo c = chopGeo(g);
        const bool on = g.amount >= 0 && intOf(g.amount) > 0;
        static const char* const kRows[4] = { "PULSE", "SAW", "SUB", "NOISE" };
        fontSize(8);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(col(kDim));
        for (int k = 0; k < 16; k += 4) {
            char n[4];
            std::snprintf(n, sizeof n, "%d", k + 1);
            text(c.x0 + (k + 0.5f) * c.cellW, g.y + 8, n, nullptr);
        }
        for (int s = 0; s < 4; ++s) {
            const float y = c.y0 + s * c.rowH;
            const uint16_t m = chopMaskOf(g, s);
            fontSize(9);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(col(kLabel));
            text(g.x + 5, y + c.rowH / 2 + 1, kRows[s], nullptr);
            for (int k = 0; k < 16; ++k) {
                const bool lit = m >> k & 1u;
                beginPath();
                rect(c.x0 + k * c.cellW + 1, y + 1.5f, c.cellW - 2, c.rowH - 3);
                fillColor(lit ? col(kAccent, on ? 220 : 80) : col(kTrack, on ? 255 : 120));
                fill();
            }
            // the mask's name: click / wheel steps through the firmware's masks
            const VoiceParam* mp = g.mask[s] >= 0 ? &paramAt(voiceDef(), uint32_t(g.mask[s])) : nullptr;
            const int sel = g.mask[s] >= 0 ? intOf(g.mask[s]) : 1;
            frame(c.nameX, y + 1, kChopNameW - 4, c.rowH - 2, col(kPanel), col(kBorder));
            fontSize(9);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(col(sel == 0 ? kAccent : kText));
            text(c.nameX + (kChopNameW - 4) / 2, y + c.rowH / 2 + 1,
                 mp && mp->labels && sel >= 0 && sel < mp->numLabels ? mp->labels[sel] : "", nullptr);
        }
        if (!on) { // overtone 0: the masks have no effect
            fontSize(8);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(col(kDim));
            text(c.nameX + (kChopNameW - 4) / 2, g.y + 8, "OVTN 0", nullptr);
        }
    }

    // envelope geometry in panel coordinates
    struct EgPoints { float x0, xPeak, xEnd, yTop, yBot; };
    EgPoints egPoints(const Graph& g) const
    {
        EgPoints e;
        const float pad = 6.f;
        const float gx = g.x + pad, gw = g.w - 2 * pad;
        e.yTop = g.y + 6; e.yBot = g.y + g.h - 6;
        e.x0 = gx;
        if (g.kind == GraphKind::Amp) {
            const double ta = g.attack >= 0 ? egSeconds(fwOf(g.attack), kAttackK) : 0.0;
            const double td = egSeconds(fwOf(g.decay), kDecayK);
            e.xPeak = gx + timeToX(ta) * gw * 0.35f;
            e.xEnd = e.xPeak + timeToX(td) * (gx + gw - e.xPeak);
        } else {
            e.xPeak = gx;
            e.xEnd = gx + timeToX(egSeconds(fwOf(g.decay), kPitchK)) * gw;
        }
        return e;
    }

    void egCurve(float xs, float xe, float yTop, float yBot, int slope, double scale, bool rising, float height)
    {
        const int n = 48;
        for (int i = 0; i <= n; ++i) {
            const double t = double(i) / n;
            // firmware: linear ramp through the slope shaper
            const double val = rising ? t : 1.0 - t;
            const double out = std::clamp(slopeShape(val, slope, scale), 0.0, 1.0);
            const float x = xs + float(t) * (xe - xs);
            const float y = yBot - float(out) * (yBot - yTop) * height;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
    }

    void drawGraph(const Graph& g)
    {
        if (g.kind == GraphKind::Draw) { drawDrawGraph(g); return; }
        if (g.kind == GraphKind::Chop) { drawChopGraph(g); return; }
        beginPath();
        rect(g.x + 0.5f, g.y + 0.5f, g.w - 1, g.h - 1);
        fillColor(col(kWell));
        fill();
        strokeColor(col(kBorder));
        strokeWidth(1);
        stroke();
        const EgPoints e = egPoints(g);
        const int slope = g.slope >= 0 ? fwOf(g.slope) : 64;

        // time grid: 10 ms, 100 ms, 1 s
        for (double t : { 0.01, 0.1, 1.0 }) {
            const float x = (g.kind == GraphKind::Amp ? e.xPeak : e.x0) +
                            timeToX(t) * ((g.x + g.w - 6) - (g.kind == GraphKind::Amp ? e.xPeak : e.x0));
            beginPath();
            moveTo(x, g.y + 3); lineTo(x, g.y + g.h - 3);
            strokeColor(col(kBorder));
            strokeWidth(1);
            stroke();
        }

        float height = 1.f;
        if (g.kind == GraphKind::Pitch && g.amount >= 0)
            height = 0.08f + 0.92f * float(fwOf(g.amount)) / 127.f;

        // filled contour
        beginPath();
        moveTo(e.x0, e.yBot);
        if (g.kind == GraphKind::Amp) {
            egCurve(e.x0, e.xPeak, e.yTop, e.yBot, 127 - slope, 1.999, true, 1.f); // attack uses invSlope
            egCurve(e.xPeak, e.xEnd, e.yTop, e.yBot, slope, 1.999, false, 1.f);
        } else {
            egCurve(e.x0, e.xEnd, e.yTop, e.yBot, slope, 2.0, false, height);
        }
        lineTo(e.xEnd, e.yBot);
        closePath();
        fillColor(col(kAccent, 40));
        fill();
        beginPath();
        if (g.kind == GraphKind::Amp) {
            egCurve(e.x0, e.xPeak, e.yTop, e.yBot, 127 - slope, 1.999, true, 1.f);
            egCurve(e.xPeak, e.xEnd, e.yTop, e.yBot, slope, 1.999, false, 1.f);
        } else {
            egCurve(e.x0, e.xEnd, e.yTop, e.yBot, slope, 2.0, false, height);
        }
        strokeColor(col(kAccent));
        strokeWidth(1.5f);
        stroke();

        // handles
        auto handle = [&](float x, float y, bool hot) {
            beginPath();
            rect(x - 3, y - 3, 6, 6);
            fillColor(hot ? col(kAccent) : col(kText));
            fill();
        };
        const float yPitchTop = e.yBot - (e.yBot - e.yTop) * height;
        if (g.kind == GraphKind::Amp) {
            if (g.attack >= 0) handle(e.xPeak, e.yTop, fDrag == g.attack);
            handle(e.xEnd, e.yBot, fDrag == g.decay);
        } else {
            handle(e.x0, yPitchTop, fDrag == g.amount);
            handle(e.xEnd, e.yBot, fDrag == g.decay);
        }

        // time readout
        char buf[64], t1[16], t2[16];
        fontSize(9);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(col(kDim));
        if (g.kind == GraphKind::Amp) {
            fmtTime(t1, sizeof t1, g.attack >= 0 ? egSeconds(fwOf(g.attack), kAttackK) : 0.0);
            fmtTime(t2, sizeof t2, egSeconds(fwOf(g.decay), kDecayK));
            std::snprintf(buf, sizeof buf, "A %s  D %s", t1, t2);
        } else {
            fmtTime(t2, sizeof t2, egSeconds(fwOf(g.decay), kPitchK));
            std::snprintf(buf, sizeof buf, "D %s", t2);
        }
        text(g.x + g.w - 5, g.y + 4, buf, nullptr);
    }

    // list popup for selectors
    static constexpr float kItemH = 16.f, kItemW = 112.f;
    void popupGeometry(float& px, float& py, int& cols, int& rows) const
    {
        const Ctl* c = ctlFor(uint32_t(fPopup));
        const VoiceParam& p = paramAt(voiceDef(), uint32_t(fPopup));
        const int n = p.numLabels;
        const int maxRows = int((fHeight - kHeaderH - 2 * kMargin) / kItemH);
        cols = (n + maxRows - 1) / maxRows;
        rows = (n + cols - 1) / cols;
        const bool fixed = c && c->size > 0;
        px = std::clamp(c ? (fixed ? c->ax - c->bw / 2 : c->x) : fPopupAX, kMargin, kWidth - kMargin - cols * kItemW);
        py = c ? (fixed ? c->ay + c->bh / 2 + 4 : c->y + 40) : fPopupAY;
        if (py + rows * kItemH > fHeight - kMargin)
            py = std::max(kHeaderH, fHeight - kMargin - rows * kItemH);
    }
    void drawPopup()
    {
        float px, py; int cols, rows;
        popupGeometry(px, py, cols, rows);
        const VoiceParam& p = paramAt(voiceDef(), uint32_t(fPopup));
        beginPath();
        rect(px - 0.5f, py - 0.5f, cols * kItemW + 1, rows * kItemH + 1);
        fillColor(col(kPanel));
        fill();
        strokeColor(col(kAccent));
        strokeWidth(1);
        stroke();
        const int cur = intOf(fPopup);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < p.numLabels; ++i) {
            const float x = px + (i / rows) * kItemW, y = py + (i % rows) * kItemH;
            if (i == cur || i == fPopupHover) {
                beginPath();
                rect(x, y, kItemW, kItemH);
                fillColor(i == cur ? col(kAccent) : col(kTrack));
                fill();
            }
            fillColor(i == cur ? col(kBg) : col(kText));
            text(x + 6, y + kItemH / 2 + 1, p.labels[i], nullptr);
        }
    }
    int popupItemAt(float x, float y) const
    {
        float px, py; int cols, rows;
        popupGeometry(px, py, cols, rows);
        if (x < px || y < py || x >= px + cols * kItemW || y >= py + rows * kItemH) return -1;
        const int i = int((x - px) / kItemW) * rows + int((y - py) / kItemH);
        return i < paramAt(voiceDef(), uint32_t(fPopup)).numLabels ? i : -1;
    }

    // ---- Maquina BL-101 editors: paint the DRAW bars / the CHOP cells
    void paintDraw(const Graph& g, float x, float y)
    {
        const float pad = 4, bw = (g.w - 2 * pad) / 16.f, mid = g.y + g.h / 2, half = g.h / 2 - pad;
        const int k = std::clamp(int((x - g.x - pad) / bw), 0, 15);
        const int v = std::clamp(int(std::lround((mid - y) / half * 128.f)), -128, 127);
        // fill the bars between the last point and this one (fast strokes)
        const int k0 = fPaintLast < 0 ? k : fPaintLast, v0 = fPaintLast < 0 ? v : fPaintLastV;
        const int n = std::abs(k - k0);
        for (int j = 0; j <= n; ++j) {
            const int kk = k0 + (k > k0 ? j : -j);
            const int vv = n ? v0 + (v - v0) * j / n : v;
            setValue(uint32_t(g.first + kk), float(vv), true);
        }
        fPaintLast = k;
        fPaintLastV = v;
    }

    void setChopMask(const Graph& g, int src, uint16_t m)
    {
        if (g.mask[src] < 0 || g.bitsLo[src] < 0 || g.bitsHi[src] < 0) return;
        setValue(uint32_t(g.bitsLo[src]), float(m & 255), true);
        setValue(uint32_t(g.bitsHi[src]), float(m >> 8), true);
        setValue(uint32_t(g.mask[src]), 0.f, true); // Custom
    }

    // a CHOP cell (source * 16 + slice) at x, y; -1 outside
    int chopCellAt(const Graph& g, float x, float y) const
    {
        const ChopGeo c = chopGeo(g);
        if (x < c.x0 || x >= c.x0 + 16 * c.cellW || y < c.y0 || y >= c.y0 + 4 * c.rowH) return -1;
        return int((y - c.y0) / c.rowH) * 16 + int((x - c.x0) / c.cellW);
    }
    // a CHOP mask name (the source) at x, y; -1 outside
    int chopNameAt(const Graph& g, float x, float y) const
    {
        const ChopGeo c = chopGeo(g);
        if (x < c.nameX || y < c.y0 || y >= c.y0 + 4 * c.rowH) return -1;
        return int((y - c.y0) / c.rowH);
    }
    void stepChopName(const Graph& g, int src, int step)
    {
        if (g.mask[src] < 0) return;
        const int n = int(maxOf(uint32_t(g.mask[src]))) + 1;
        setValue(uint32_t(g.mask[src]), float((intOf(g.mask[src]) + step + n) % n), true);
    }
    void paintChop(const Graph& g, float x, float y)
    {
        const int cell = chopCellAt(g, x, y);
        if (cell < 0) return;
        const int src = cell / 16, bit = cell % 16;
        uint16_t m = chopMaskOf(g, src);
        const uint16_t want = fChopPaint ? uint16_t(m | 1u << bit) : uint16_t(m & ~(1u << bit));
        if (want != m || intOf(g.mask[src]) != 0) setChopMask(g, src, want);
    }

    // ------------------------------------------------------------- input
    const Ctl* ctlFor(uint32_t param) const
    {
        for (const Ctl& c : fCtls)
            if (c.param == param) return &c;
        return nullptr;
    }
    const Ctl* ctlAt(float x, float y) const
    {
        for (const Ctl& c : fCtls)
            if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return &c;
        return nullptr;
    }
    const Graph* graphAt(float x, float y) const
    {
        for (const Graph& g : fGraphs)
            if (x >= g.x && x < g.x + g.w && y >= g.y && y < g.y + g.h) return &g;
        return nullptr;
    }

    void beginDrag(int param, float x, float y, int axis)
    {
        endRun();
        fDrag = param;
        fDragAxis = axis;
        fDragStartX = x;
        fDragStartY = y;
        fDragStartValue = fValues[uint32_t(param)];
        editParameter(uint32_t(param), true);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float s = getWidth() / kWidth;
        const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;

        if (!ev.press) {
            if (fEnvDrag) { la50EnvRelease(); return true; }
            if (fMomentary >= 0) { // Oneiroi's CLEAR: a button
                setValue(uint32_t(fMomentary), 0.f, true);
                fMomentary = -1;
                return true;
            }
            if (fRangeDrag >= 0) {
                fRangeDrag = -1;
                saveRand();
                repaint();
                return true;
            }
            if (fDrag >= 0) {
                editParameter(uint32_t(fDrag), false);
                fDrag = -1;
                fLaDragPixels = 0;
                repaint();
                return true;
            }
            if (fGraphDrag) {
                fGraphDrag = 0;
                fPaintGraph = nullptr;
                repaint();
                return true;
            }
            return false;
        }
        if (fSaveDlg) return ev.button == kMouseButtonLeft ? dialogClick(x, y) : true;
        const HeaderGeo& H = headerGeo();
        if (ev.button == kMouseButtonLeft) {
            if (H.prev.hit(x, y)) {
                if (!fPresets.empty()) loadPreset(fPreset < 0 ? int(fPresets.size()) - 1 : (fPreset + int(fPresets.size()) - 1) % int(fPresets.size()));
                return true;
            }
            if (H.next.hit(x, y)) {
                if (!fPresets.empty()) loadPreset(fPreset < 0 ? 0 : (fPreset + 1) % int(fPresets.size()));
                return true;
            }
            if (H.name.hit(x, y)) {
                fBrowser = !fBrowser;
                fSearchFocus = false;
                fPopup = -1;
                if (fBrowser) loadPresetList(); // pick up files added meanwhile
                repaint();
                return true;
            }
            if (H.save.hit(x, y)) { openSaveDialog(); return true; }
            if (H.init.hit(x, y)) { initPatch(); return true; }
        }
        if (fBrowser) return ev.button == kMouseButtonLeft && y >= kHeaderH ? browserClick(x, y) : true;
        if (fPopup < 0 && ev.button == kMouseButtonLeft) {
            const int g = randCellAt(x, y);
            if (g >= 0) {
                const Box& c = layout().rand.cell[g];
                if (titleOf(c).hit(x, y)) {
                    fRand.lock[g] = !fRand.lock[g];
                    saveRand();
                    repaint();
                } else if (!fRand.lock[g]) { // grab the nearer handle
                    const Bar b = barOf(c);
                    const float x0 = b.x + b.w * fRand.min[g] / 100.f, x1 = b.x + b.w * fRand.max[g] / 100.f;
                    const bool useMax = std::fabs(x - x1) < std::fabs(x - x0) || (x0 == x1 && x > x1);
                    fRangeDrag = g * 2 + (useMax ? 1 : 0);
                    setRangeFromX(x);
                }
                return true;
            }
        }
        if (ev.button != kMouseButtonLeft)
            return false;

        if (fPopup >= 0) {
            const int item = popupItemAt(x, y);
            if (item >= 0) setValue(uint32_t(fPopup), float(item), true);
            fPopup = -1;
            repaint();
            return true;
        }

        if (layout().rand.random.hit(x, y)) { doRandom(); return true; }
        if (maquina::isOneiroi(voiceDef().voice)) {
            const RandPanel& R = layout().rand;
            for (int k = 0; k < 2; ++k) {
                const Box& b = k ? R.oAmount : R.oTarget;
                if (!b.hit(x, y)) continue;
                const int n = k ? 3 : 4;
                uint8_t& v = k ? fRand.oAmount : fRand.oTarget;
                v = uint8_t((v + (x < b.x + b.w / 2 ? n - 1 : 1)) % n); // left half: back, right half: on
                if (!k && v == maquina::kOneiroiOsc && voiceDef().voice == maquina::kOneiroiFxVoice) // FX: no oscillators
                    v = uint8_t((v + (x < b.x + b.w / 2 ? n - 1 : 1)) % n);
                saveRand();
                repaint();
                return true;
            }
        }
        for (const Section& sec : fSections) {
            if (sec.osc < 0) continue;
            if (sec.roll.hit(x, y)) { doRandom(sec.osc); return true; }
            if (sec.randOn.hit(x, y)) {
                fRand.oscOn[sec.osc] = !fRand.oscOn[sec.osc];
                saveRand();
                repaint();
                return true;
            }
        }
        if (layout().rand.back.hit(x, y)) { doBack(); return true; }
        if (H.minus.hit(x, y) || H.plus.hit(x, y)) {
            const int z = H.minus.hit(x, y) ? std::max(0, fZoom - 1) : std::min(kNumZooms - 1, fZoom + 1);
            if (z != fZoom) {
                fZoom = z;
                saveZoom(fZoom);
                applyZoom();
            }
            return true;
        }

        if (layout().la50.used) {
            const bool dblLa = (ev.time - fLastClickTime < 350) && fLastClickParam == UINT32_MAX - 1;
            fLastClickParam = UINT32_MAX - 1;
            fLastClickTime = ev.time;
            if (la50Click(x, y, dblLa, ev.time)) return true;
        }
        if (const Graph* g = graphAt(x, y); g && g->kind == GraphKind::Draw) {
            if (g->first >= 0) {
                endRun();
                fGraphDrag = 1;
                fPaintGraph = g;
                fPaintLast = -1;
                paintDraw(*g, x, y);
            }
            return true;
        }
        if (const Graph* g = graphAt(x, y); g && g->kind == GraphKind::Chop) {
            const int name = chopNameAt(*g, x, y);
            if (name >= 0) { stepChopName(*g, name, 1); return true; }
            const int cell = chopCellAt(*g, x, y);
            if (cell >= 0) {
                endRun();
                fChopPaint = !(chopMaskOf(*g, cell / 16) >> (cell % 16) & 1u);
                fGraphDrag = 2;
                fPaintGraph = g;
                paintChop(*g, x, y);
            }
            return true;
        }
        if (const Graph* g = graphAt(x, y)) {
            const EgPoints e = egPoints(*g);
            const float height = (g->kind == GraphKind::Pitch && g->amount >= 0)
                                     ? 0.08f + 0.92f * float(fwOf(g->amount)) / 127.f : 1.f;
            auto near = [&](float hx, float hy) { return std::fabs(x - hx) < 8 && std::fabs(y - hy) < 10; };
            if (g->kind == GraphKind::Amp) {
                if (near(e.xEnd, e.yBot)) beginDrag(g->decay, x, y, 0);
                else if (g->attack >= 0 && near(e.xPeak, e.yTop)) beginDrag(g->attack, x, y, 0);
                else if (g->slope >= 0) beginDrag(g->slope, x, y, 1);
            } else {
                if (near(e.x0, e.yBot - (e.yBot - e.yTop) * height) && g->amount >= 0) beginDrag(g->amount, x, y, 1);
                else if (near(e.xEnd, e.yBot)) beginDrag(g->decay, x, y, 0);
                else if (g->slope >= 0) beginDrag(g->slope, x, y, 1);
            }
            repaint();
            return true;
        }

        const Ctl* c = ctlAt(x, y);
        if (c == nullptr)
            return false;

        const bool dbl = (c->param == fLastClickParam) && (ev.time - fLastClickTime < 350);
        fLastClickParam = c->param;
        fLastClickTime = ev.time;

        switch (c->type) {
        case CtlType::Knob:
        case CtlType::Slider:
            if (dbl) { setValue(c->param, defOf(c->param), true); return true; }
            beginDrag(int(c->param), x, y, 1);
            break;
        case CtlType::Switch:
            setValue(c->param, float(switchPosAt(*c, y)), true);
            break;
        case CtlType::Toggle:
            if (!std::strcmp(paramAt(voiceDef(), c->param).symbol, "loop_clear")) { // momentary
                setValue(c->param, 1.f, true);
                fMomentary = int(c->param);
                break;
            }
            setValue(c->param, fValues[c->param] >= 0.5f ? 0.f : 1.f, true);
            break;
        case CtlType::Select: {
            float bx, by, bw, bh;
            selectBox(*c, bx, by, bw, bh);
            if (x < bx + 16) setValue(c->param, fValues[c->param] - 1, true);
            else if (x > bx + bw - 16) setValue(c->param, fValues[c->param] + 1, true);
            else if (dbl) setValue(c->param, defOf(c->param), true);
            else if (paramAt(voiceDef(), c->param).numLabels <= 48) { fPopup = int(c->param); fPopupHover = -1; }
            else beginDrag(int(c->param), x, y, 1);
            break;
        }
        }
        repaint();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float s = getWidth() / kWidth;
        const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
        if (fSaveDlg) { dialogHover(x, y); return true; }
        if (fEnvDrag) { la50EnvMotion(x, y); return true; }
        if (fBrowser) { browserHover(x, y); return true; }
        if (fPopup >= 0) {
            const int h = popupItemAt(x, y);
            if (h != fPopupHover) { fPopupHover = h; repaint(); }
            return true;
        }
        if (kNew) {
            const int h = randCellAt(x, y);
            if (h != fRandHover) { fRandHover = h; repaint(); }
        }
        if (fRangeDrag >= 0) {
            setRangeFromX(x);
            return true;
        }
        if (fGraphDrag && fPaintGraph) {
            if (fGraphDrag == 1) paintDraw(*fPaintGraph, x, y);
            else paintChop(*fPaintGraph, x, y);
            return true;
        }
        if (fDrag < 0)
            return false;
        const uint32_t i = uint32_t(fDrag);
        const float range = maxOf(i) - minOf(i);
        const bool fine = (ev.mod & kModifierShift) != 0;
        float v;
        if (fDragAxis == 1) {
            float pixels = typeOf(i) == CtlType::Select ? 8.f * range : 160.f; // selects: 8 px per step
            if (fLaDragPixels > 0) pixels = fLaDragPixels; // LA-50: the slot's travel
            else if (typeOf(i) == CtlType::Slider) // a slider: its cap follows the mouse
                if (const Ctl* c = ctlFor(i)) {
                    float sx, y0, y1;
                    sliderSlot(*c, sx, y0, y1);
                    pixels = y1 - y0;
                }
            v = fDragStartValue + (fDragStartY - y) * range / (fine ? pixels * 6 : pixels);
        } else {
            v = fDragStartValue + (x - fDragStartX) * range / (fine ? 900.f : 150.f);
        }
        v = std::clamp(float(std::lround(v)), minOf(i), maxOf(i));
        if (v != fValues[i]) {
            fValues[i] = v;
            if (maquina::isSoundParam(voiceDef(), i)) fPresetDirty = true;
            setParameterValue(i, v);
            repaint();
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float s = getWidth() / kWidth;
        const float x = float(ev.pos.getX()) / s, y = float(ev.pos.getY()) / s;
        if (fSaveDlg || fBrowser) return true;
        if (fPopup >= 0) return false;
        if (randCellAt(x, y) >= 0) return true;
        const float step = ev.delta.getY() > 0 ? 1.f : ev.delta.getY() < 0 ? -1.f : 0.f;
        if (layout().la50.used)
            for (const La50Slot& sl : layout().la50.slots)
                if (sl.box.hit(x, y)) {
                    const int p = la50Param(sl);
                    if (p >= 0 && step != 0.f) {
                        fLaLast = p;
                        fLaMsg.clear();
                        setValue(uint32_t(p), fValues[uint32_t(p)] + step, true);
                    }
                    return true;
                }
        if (const Graph* g = graphAt(x, y)) {
            if (g->kind == GraphKind::Chop) {
                const int name = chopNameAt(*g, x, y);
                if (name >= 0 && step != 0.f) stepChopName(*g, name, int(step));
            } else if (g->kind == GraphKind::Draw && g->first >= 0 && step != 0.f) {
                const int k = std::clamp(int((x - g->x - 4) / ((g->w - 8) / 16.f)), 0, 15);
                setValue(uint32_t(g->first + k), fValues[uint32_t(g->first + k)] + 4 * step, true);
            }
            return true;
        }
        const Ctl* c = ctlAt(x, y);
        if (c == nullptr) return false;
        if (step != 0.f) setValue(c->param, fValues[c->param] + step, true);
        return true;
    }

private:
    std::vector<float> fValues;
    const std::vector<Section>& fSections;
    const std::vector<Ctl>& fCtls;
    const std::vector<Graph>& fGraphs;
    const float fHeight;

    int fDrag = -1;
    int fDragAxis = 1; // 1 = vertical, 0 = horizontal
    // Maquina LA-50: edit mode (0 partial, 1 common, 2 patch), selected partial / tone,
    // the last touched parameter, a message for the LCD, a slider drag's travel
    int fLaMode = 0, fLaPartial = 0, fLaTone = 0, fLaLast = -1;
    std::string fLaMsg;
    float fLaDragPixels = 0;
    // Maquina BL-101 editors: 1 painting DRAW bars, 2 painting CHOP cells
    int fGraphDrag = 0;
    const Graph* fPaintGraph = nullptr;
    int fPaintLast = -1, fPaintLastV = 0;
    bool fChopPaint = true;
    float fDragStartX = 0, fDragStartY = 0, fDragStartValue = 0;
    uint32_t fLastClickParam = UINT32_MAX;
    uint fLastClickTime = 0;
    int fPopup = -1, fPopupHover = -1;
    bool fEnvDrag = false; // LA-50: an envelope handle is held
    int fEnvT = -1, fEnvL = -1;
    float fEnvX = 0, fEnvY = 0, fEnvT0 = 0, fEnvL0 = 0, fEnvH = 1;
    float fPopupAX = 0, fPopupAY = 0; // the popup's place when its control isn't a layout control
    int fZoom = 1;

    maquina::RandSettings fRand;
    int fRangeDrag = -1;          // group * 2 + (0 min / 1 max) being dragged
    int fRandHover = -1;          // the new look: the drawer group under the mouse (handles shown)
    bool fSnapshotDirty = false;

    // presets
    std::vector<PresetEntry> fPresets; // factory, then user (sorted)
    int fPreset = -1;                  // loaded entry, -1 = Init / unknown
    std::string fPresetName { "Init" };
    bool fPresetDirty = false;         // edited since loading
    // test hook: MAQUINA_UI_BROWSER=1 starts with the browser open, =edit also in tag edit mode,
    // =dialog with the edit dialog of the first user preset (edit mode)
    static bool testHook(const char* v = nullptr)
    {
        const char* e = std::getenv("MAQUINA_UI_BROWSER");
        return e && (!v || std::strcmp(e, v) == 0);
    }
    bool fBrowser = testHook();
    bool fPresetHookDone = false;
    int fBrowseCat = testHook("edit") ? 1 : 0;                // 0 All, 1..n categories (the plugin's, then the user's), n+1 User, n+2 Rated
    std::vector<std::string> fHidden;  // hidden factory presets (hidden.txt)
    std::vector<std::pair<std::string, int>> fRatings; // "factory:<name>" / "user:<file>" -> 1..5 (ratings.txt)
    std::vector<std::string> fCats;    // the categories, in order (categories.txt)
    std::vector<std::string> fTagList; // the tags (tags.txt; at most 32: bit masks)
    struct LabelMap { std::string kind, from, to; };
    std::vector<LabelMap> fLabelMap;   // renamed / deleted categories and tags, for the factory presets
    bool fCatInput = false;            // the + CATEGORY / rename field is open
    int fCatEditIdx = -1;              // renaming this category (-1: adding one)
    std::string fCatName;
    bool fTagEdit = testHook("edit"), fTagInput = false; // tag edit mode / its name field is open
    int fTagEditIdx = -1, fTagDelArm = -1;   // renaming this tag (-1: adding) / a tag delete waiting for its confirm
    std::string fTagName;
    bool fDlgEdit = false;             // the save dialog's categories / tags in edit mode
    float fDlgH = 250, fDlgContentH = 0, fDlgTagsY = 0;
    int fDlgHover = -1, fDlgHoverKind = -1;
    int fCatDelArm = -1, fDelArm = -1; // a category / preset delete waiting for its confirm click
    int fEditIdx = -1;                 // the save dialog edits this user preset (-1: saves a new one)
    std::string fLibMsg;               // the browser's action bar note
    uint32_t fTagMask = 0;
    std::string fSearch;
    bool fSearchFocus = false;
    int fBrowseHover = -1, fBrowseHoverKind = -1;
    int fListHidden = 0, fListShown = 0;
    bool fSaveDlg = false;
    std::string fSaveName, fSaveMsg;
    bool fSaveError = false;
    int fSaveCat = 0;
    uint32_t fSaveTags = 0;
    uint fTypedKeycode = 0;  // last key typed as text, and its byte count
    size_t fTypedLen = 0;
    std::vector<float> fSnapshot; // the sound before the first RANDOM of a run
    bool fRunActive = false;      // BACK available
    bool fRedo = false;           // Oneiroi: the next UNDO press is a REDO
    int fMomentary = -1;          // Oneiroi CLEAR held down
    bool fApplying = false;       // our own randomize/back edits in flight
    std::mt19937 fRng { std::random_device {}() };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumUI)
};

UI* createUI() { return new DrumUI(); }

END_NAMESPACE_DISTRHO
