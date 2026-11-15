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
//
// SPDX-License-Identifier: GPL-3.0-only
#include "DistrhoUI.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
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

constexpr float kWidth = 1080.f;
constexpr float kHeaderH = 30.f;
constexpr float kMargin = 8.f;
constexpr float kGap = 6.f;
constexpr float kTitleH = 18.f;
constexpr float kCellH = 64.f;
constexpr float kSectionPad = 8.f;
constexpr float kKnobW = 54.f;
constexpr float kSelectW = 96.f;
constexpr float kToggleW = 54.f;
constexpr float kGraphW = 168.f;

struct Rgb { int r, g, b; };
constexpr Rgb kBg { 0x1c, 0x1c, 0x1c };
constexpr Rgb kPanel { 0x26, 0x26, 0x26 };
constexpr Rgb kBorder { 0x36, 0x36, 0x36 };
constexpr Rgb kWell { 0x1a, 0x1a, 0x1a };
constexpr Rgb kTrack { 0x45, 0x45, 0x45 };
constexpr Rgb kText { 0xc8, 0xc8, 0xc8 };
constexpr Rgb kDim { 0x80, 0x80, 0x80 };
constexpr Rgb kLabel { 0xa8, 0xa8, 0xa8 }; // parameter labels + section titles: lighter than kDim, below kText
constexpr Rgb kAccent { 0xff, 0x9f, 0x2e };

Color col(Rgb c, int a = 255) { return Color(c.r, c.g, c.b, a); }

// ---------------------------------------------------------------------------
// firmware envelope models (SlopeEg2.c / Decay.c), for drawing only

constexpr double kBlockSec = 64.0 / 44140.0;

double egRate(int v, double k) // value change per 64-sample block
{
    const double x = v / 127.0;
    const double r = (1.0 - ((1.0 + k) * x / (1.0 + k * std::fabs(x)))) * 2.0;
    return std::min(1.0, r);
}
double egSeconds(int v, double k)
{
    const double r = egRate(v, k);
    return r <= 1e-9 ? INFINITY : kBlockSec / r;
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
    if (std::isinf(t)) std::snprintf(buf, n, "inf");
    else if (t < 1.0) std::snprintf(buf, n, "%.0f ms", t * 1000.0);
    else std::snprintf(buf, n, "%.2f s", t);
}

// ---------------------------------------------------------------------------
// panel description: sections in display order, items by LV2 symbol

enum class GraphKind { None, Amp, Pitch };
struct ItemSpec { const char* symbol; const char* label; };
struct SectionSpec {
    const char* title;
    GraphKind graph;
    ItemSpec items[10];
};

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
    // + the output gain (ExtraParams.hpp, all voices; Dredrum/Plethora: Gain only)
    { "MIX", GraphKind::None,
      { { "volume", "Volume" }, { "pan", "Pan" }, { "drive", "Drive" }, { "decimation", "Rate" },
        { "gain", "Gain" } } },
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
    { "GLOBAL", GraphKind::None, { { "env_sync", "Env Sync" } } }, // + MODE (trig_mode), shared with Dredrum
};

enum class CtlType { Knob, Select, Toggle };

struct Ctl {
    uint32_t param;
    CtlType type;
    const char* label;
    float x, y, w, h;   // cell
};
struct Graph {
    GraphKind kind;
    float x, y, w, h;
    int attack = -1, decay = -1, slope = -1, amount = -1;
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
struct RandPanel {
    Box random, back;
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

CtlType typeOf(uint32_t i)
{
    switch (paramAt(voiceDef(), i).kind) {
    case ParamKind::Int: return CtlType::Knob;
    case ParamKind::Toggle:
    case ParamKind::Retrigger: return CtlType::Toggle;
    default: return CtlType::Select;
    }
}

float cellWidth(uint32_t i)
{
    switch (typeOf(i)) {
    case CtlType::Knob: return kKnobW;
    case CtlType::Toggle: return kToggleW;
    default: return kSelectW;
    }
}

struct Layout {
    std::vector<Section> sections;
    std::vector<Ctl> ctls;
    std::vector<Graph> graphs;
    RandPanel rand;
    float height = 400.f;
};

Layout makeLayout()
{
    Layout L;
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
        float w = 2 * kSectionPad + (s.graph != GraphKind::None ? kGraphW + kGap : 0.f);
        for (auto& it : p.items) w += cellWidth(it.first);
        w += kGap * float(p.items.size() - 1);
        p.natural = w;
        secs.push_back(std::move(p));
    }

    // Rows per voice family, chosen so each row is nearly full (3 rows).
    // Sections a voice does not have are skipped; anything not listed (or a
    // row that would overflow) falls back to greedy packing.
    // (MIX with Gain is too wide to share a row with LFO and VELOCITY)
    static const char* const kDrumRows[3][5] = {
        { "OSCILLATOR", "AMP EG", "PITCH EG" },
        { "TRANSIENT", "LFO", "VELOCITY" },
        { "FM", "FILTER", "KEY", "MIX" } };
    static const char* const kSnareRows[3][5] = {
        { "OSCILLATOR", "NOISE", "PITCH EG", "KEY" },
        { "TRANSIENT", "LFO", "VELOCITY" },
        { "AMP EG", "FILTER", "MIX" } };
    static const char* const kMetalRows[3][5] = {
        { "OSCILLATOR", "AMP EG", "KEY", "VELOCITY" },
        { "MOD OSCILLATORS", "TRANSIENT", "FILTER" },
        { "LFO", "MIX" } };
    static const char* const kDredrumRows[3][5] = {
        { "OSC 1" },
        { "OSC 2" },
        { "OSC 3", "MODE", "MIX" } };
    static const char* const kPlethoraRows[3][5] = {
        { "CHANNEL A" },
        { "CHANNEL B" },
        { "CHANNEL C", "GLOBAL", "MODE", "MIX" } };
    const uint8_t v = voiceDef().voice;
    const char* const (*plan)[5] = v == maquina::kPlethoraVoice ? kPlethoraRows
                                   : v == maquina::kDredrumVoice ? kDredrumRows
                                   : v <= 2                    ? kDrumRows
                                   : v == 3                    ? kSnareRows
                                                               : kMetalRows;

    const float avail = kWidth - 2 * kMargin;
    std::vector<std::vector<Pending*>> rows;
    std::vector<bool> placed(secs.size(), false);
    for (int r = 0; r < 3; ++r) {
        std::vector<Pending*> row;
        float used = 0.f;
        for (int k = 0; k < 5 && plan[r][k]; ++k)
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
    const float rowH = kTitleH + kCellH + kSectionPad;
    for (auto& row : rows) {
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
                const float gw = kGraphW + extra * 0.5f;
                g.x = cx; g.y = cy + 2; g.w = gw; g.h = kCellH - 6;
                if (g.kind == GraphKind::Amp) {
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
                const float cw = cellWidth(it.first) + itemExtra;
                L.ctls.push_back({ it.first, typeOf(it.first), it.second, cx, cy, cw, kCellH });
                cx += cw + kGap;
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

std::string configDir() // ~/.config/maquina (no trailing slash), empty if unknown
{
#ifdef _WIN32
    const char* base = std::getenv("APPDATA");
    return base ? std::string(base) + "\\Maquina" : std::string();
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
    std::string dir;
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) dir = x;
    else if (const char* h = std::getenv("HOME")) dir = std::string(h) + "/.config";
    else return std::string();
    return dir + "/maquina/ui.conf";
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
        if (p.kind == ParamKind::Int) {
            if (p.offset != 0) std::snprintf(buf, n, "%+d", iv);
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
        const float s = getWidth() / kWidth;
        scale(s, s);

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
            }
        }
        if (fPopup >= 0)
            drawPopup();
        drawPresetBar();
        if (fBrowser) drawBrowser();
        if (fSaveDlg) drawSaveDialog();
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
    static Bar barOf(const Box& c) { return { c.x + 8, c.y + 34, c.w - 16 }; }
    static Box titleOf(const Box& c) { return { c.x, c.y, c.w, 20 }; }

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

    void drawRandPanel()
    {
        const RandPanel& R = layout().rand;
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
        fPresets.clear();
        const maquina::VoicePresets& fp = maquina::k_factory_presets[MAQUINA_VOICE_INDEX];
        for (uint32_t k = 0; k < fp.count; ++k) {
            PresetEntry e;
            e.name = fp.presets[k].name;
            e.category = fp.presets[k].category;
            e.tags = maquina::splitTags(fp.presets[k].tags);
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
    }

    void savePresetState()
    {
        setState("preset", ((fPresetDirty ? "*" : "") + fPresetName).c_str());
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
        const maquina::VoiceCategories* vc = maquina::categoriesOf(voiceDef().id);
        int nc = 0;
        while (vc && nc < 10 && vc->names[nc]) ++nc;
        if (fBrowseCat == nc + 1 && !e.user) return false;
        if (fBrowseCat >= 1 && fBrowseCat <= nc && e.category != vc->names[fBrowseCat - 1]) return false;
        for (int t = 0; t < maquina::kNumPresetTags; ++t)
            if (fTagMask & (1u << t))
                if (std::find(e.tags.begin(), e.tags.end(), maquina::kPresetTags[t]) == e.tags.end()) return false;
        if (!fSearch.empty()) {
            std::string a = e.name, b = fSearch;
            for (auto& c : a) c = char(std::tolower(c));
            for (auto& c : b) c = char(std::tolower(c));
            if (a.find(b) == std::string::npos) return false;
        }
        return true;
    }

    // ---- small drawing helpers
    float textWidth(const char* t)
    {
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
        scissor(kNameX + 4, y, kPresetNameW - 8, h);
        text(kNameX + 7, my, name.c_str(), nullptr);
        if (fPreset >= 0) {
            const PresetEntry& e = fPresets[size_t(fPreset)];
            fontSize(9);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fillColor(col(kDim));
            text(kNameX + kPresetNameW - 16, my, (e.user ? "User \xc2\xb7 " + e.category : e.category).c_str(), nullptr);
        }
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
    static constexpr float kCatRowH = 20.f, kListRowH = 19.f, kListColW = 300.f;

    // lays out search, tag chips and the list; calls fn(kind, index, x, y, w, h)
    // kind: 0 category, 1 search box, 2 tag chip, 3 preset row, 4 close, 5 clear tags
    template <class F> void browserItems(F&& fn)
    {
        const BrowserGeo g = browserGeo();
        const maquina::VoiceCategories* vc = maquina::categoriesOf(voiceDef().id);
        int nc = 0;
        while (vc && nc < 10 && vc->names[nc]) ++nc;
        for (int i = 0; i < nc + 2; ++i)
            fn(0, i, g.catX, g.y + 30 + i * kCatRowH, g.catW, kCatRowH - 2);
        fn(4, 0, g.x + g.w - 26, g.y + 6, 20, 18);
        float ty = g.y + 30;
        fn(1, 0, g.rightX, ty, 180, 20);
        float tx = g.rightX + 190;
        for (int t = 0; t < maquina::kNumPresetTags; ++t) {
            const float w = chipWidth(maquina::kPresetTags[t]);
            if (tx + w > g.rightX + g.rightW - 50) { tx = g.rightX + 190; ty += 24; }
            fn(2, t, tx, ty + 1, w, 18);
            tx += w + 5;
        }
        fn(5, 0, g.rightX + g.rightW - 44, g.y + 31, 44, 18);
        const float ly = ty + 32;
        const int rows = std::max(1, int((g.y + g.h - 8 - ly) / kListRowH));
        const int cols = std::max(1, int(g.rightW / kListColW));
        const float cw = g.rightW / float(cols);
        int n = 0;
        for (size_t i = 0; i < fPresets.size(); ++i) {
            if (!presetVisible(fPresets[i])) continue;
            if (n < rows * cols) fn(3, int(i), g.rightX + (n / rows) * cw, ly + (n % rows) * kListRowH, cw - 8, kListRowH - 1);
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
        const maquina::VoiceCategories* vc = maquina::categoriesOf(voiceDef().id);
        int nc = 0;
        while (vc && nc < 10 && vc->names[nc]) ++nc;

        browserItems([&](int kind, int i, float x, float y, float w, float h) {
            switch (kind) {
            case 0: {
                const char* label = i == 0 ? "All" : i <= nc ? vc->names[i - 1] : "User";
                const bool on = fBrowseCat == i;
                if (on || fBrowseHoverKind == 0 && fBrowseHover == i) {
                    beginPath(); rect(x, y, w, h);
                    fillColor(on ? col(kAccent) : col(kTrack)); fill();
                }
                fontSize(11);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(on ? col(kBg) : col(kText));
                text(x + 8, y + h / 2 + 1, label, nullptr);
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
            case 2: chip(x, y, maquina::kPresetTags[i], (fTagMask >> i) & 1u); break;
            case 3: {
                const PresetEntry& e = fPresets[size_t(i)];
                const bool cur = i == fPreset, hot = fBrowseHoverKind == 3 && fBrowseHover == i;
                if (cur || hot) {
                    beginPath(); rect(x, y, w, h);
                    fillColor(cur ? col(kAccent) : col(kTrack)); fill();
                }
                fontSize(11);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(cur ? col(kBg) : col(kText));
                scissor(x, y, w, h);
                text(x + 6, y + h / 2 + 1, e.name.c_str(), nullptr);
                fontSize(9);
                textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
                fillColor(cur ? col(kBg) : col(kDim));
                std::string info = maquina::joinTags(e.tags);
                info = (e.user ? "U \xc2\xb7 " : "") + e.category + (info.empty() ? "" : "  " + info);
                text(x + w - 6, y + h / 2 + 1, info.c_str(), nullptr);
                resetScissor();
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
            }
        });
        if (fListShown == 0 || fListHidden > 0) {
            char buf[64];
            if (fListShown == 0) std::snprintf(buf, sizeof buf, "No presets match.");
            else std::snprintf(buf, sizeof buf, "+%d more - narrow the filter", fListHidden);
            fontSize(10);
            textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
            fillColor(col(kDim));
            text(g.x + g.w - 10, g.y + g.h - 4, buf, nullptr);
        }
    }

    bool browserClick(float x, float y)
    {
        bool hit = false;
        browserItems([&](int kind, int i, float bx, float by, float w, float h) {
            if (hit || x < bx || x >= bx + w || y < by || y >= by + h) return;
            hit = true;
            fSearchFocus = kind == 1;
            switch (kind) {
            case 0: fBrowseCat = i; break;
            case 2: fTagMask ^= 1u << i; break;
            case 3: loadPreset(i); break;
            case 4: fBrowser = false; break;
            case 5: fTagMask = 0; break;
            }
        });
        if (!hit) fSearchFocus = false;
        repaint();
        return true;
    }
    void browserHover(float x, float y)
    {
        int kind = -1, idx = -1;
        browserItems([&](int k, int i, float bx, float by, float w, float h) {
            if ((k == 0 || k == 3) && x >= bx && x < bx + w && y >= by && y < by + h) { kind = k; idx = i; }
        });
        if (kind != fBrowseHoverKind || idx != fBrowseHover) {
            fBrowseHoverKind = kind;
            fBrowseHover = idx;
            repaint();
        }
    }

    // ---- save dialog
    struct DlgGeo { float x, y, w, h; };
    DlgGeo dlgGeo() const { return { (kWidth - 560) / 2, kHeaderH + 20, 560, 250 }; }

    // kind: 0 name box, 1 category chip, 2 tag chip, 3 save, 4 cancel
    template <class F> void dialogItems(F&& fn)
    {
        const DlgGeo g = dlgGeo();
        const float lx = g.x + 16, fx = g.x + 96, fw = g.w - 112;
        fn(0, 0, fx, g.y + 34, fw, 22);
        const maquina::VoiceCategories* vc = maquina::categoriesOf(voiceDef().id);
        float cx = fx, cy = g.y + 66;
        for (int i = 0; vc && i < 10 && vc->names[i]; ++i) {
            const float w = chipWidth(vc->names[i]);
            if (cx + w > fx + fw) { cx = fx; cy += 24; }
            fn(1, i, cx, cy, w, 18);
            cx += w + 5;
        }
        float tx = fx, ty = cy + 32;
        for (int t = 0; t < maquina::kNumPresetTags; ++t) {
            const float w = chipWidth(maquina::kPresetTags[t]);
            if (tx + w > fx + fw) { tx = fx; ty += 24; }
            fn(2, t, tx, ty, w, 18);
            tx += w + 5;
        }
        fn(3, 0, g.x + g.w - 16 - 80 - 8 - 80, g.y + g.h - 38, 80, 24);
        fn(4, 0, g.x + g.w - 16 - 80, g.y + g.h - 38, 80, 24);
        (void)lx;
    }

    void drawSaveDialog()
    {
        const DlgGeo g = dlgGeo();
        beginPath();
        rect(0, 0, kWidth, fHeight);
        fillColor(col(kBg, 170));
        fill();
        frame(g.x, g.y, g.w, g.h, col(kPanel), col(kAccent));
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col(kText));
        text(g.x + 16, g.y + 16, "SAVE PRESET", nullptr);
        fontSize(10);
        fillColor(col(kDim));
        text(g.x + 16, g.y + 45, "Name", nullptr);
        text(g.x + 16, g.y + 75, "Category", nullptr);
        bool tagLabel = false;
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
            case 1: chip(x, y, maquina::categoriesOf(voiceDef().id)->names[i], fSaveCat == i); break;
            case 2:
                if (!tagLabel) {
                    fontSize(10); textAlign(ALIGN_LEFT | ALIGN_MIDDLE); fillColor(col(kDim));
                    text(g.x + 16, y + 9, "Tags", nullptr);
                    tagLabel = true;
                }
                chip(x, y, maquina::kPresetTags[i], (fSaveTags >> i) & 1u);
                break;
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
        const std::string msg = !fSaveMsg.empty() ? fSaveMsg : saveHint();
        text(g.x + 16, g.y + g.h - 26, msg.c_str(), nullptr);
    }

    std::string saveHint() const
    {
        for (const auto& e : fPresets)
            if (e.name == fSaveName) return e.user ? "Replaces your preset of that name." : "Factory name - choose another.";
        return "Saved to " + userPresetDir();
    }

    void openSaveDialog()
    {
        fBrowser = false;
        fSaveDlg = true;
        fSaveMsg.clear();
        fSaveError = false;
        fSaveName = fPresetName == "Init" ? std::string("My ") + voiceDef().name : fPresetName;
        fSaveCat = 0;
        fSaveTags = 0;
        if (fPreset >= 0) {
            const PresetEntry& e = fPresets[size_t(fPreset)];
            const maquina::VoiceCategories* vc = maquina::categoriesOf(voiceDef().id);
            for (int i = 0; vc && i < 10 && vc->names[i]; ++i)
                if (e.category == vc->names[i]) fSaveCat = i;
            for (int t = 0; t < maquina::kNumPresetTags; ++t)
                if (std::find(e.tags.begin(), e.tags.end(), maquina::kPresetTags[t]) != e.tags.end()) fSaveTags |= 1u << t;
        }
        repaint();
    }

    void doSave()
    {
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
        for (int t = 0; t < maquina::kNumPresetTags; ++t)
            if ((fSaveTags >> t) & 1u) tags.push_back(maquina::kPresetTags[t]);
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
        out << maquina::writePreset(voiceDef(), name, maquina::categoriesOf(voiceDef().id)->names[fSaveCat], tags,
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
        dialogItems([&](int kind, int i, float bx, float by, float w, float h) {
            if (x < bx || x >= bx + w || y < by || y >= by + h) return;
            switch (kind) {
            case 1: fSaveCat = i; break;
            case 2: fSaveTags ^= 1u << i; break;
            case 3: doSave(); return;
            case 4: fSaveDlg = false; break;
            }
        });
        fSaveMsg.clear();
        repaint();
        return true;
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
    std::string* textTarget() { return fSaveDlg ? &fSaveName : (fBrowser && fSearchFocus) ? &fSearch : nullptr; }

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
            if (fSaveDlg) fSaveDlg = false;
            else if (fSearchFocus) fSearchFocus = false;
            else fBrowser = false;
            repaint();
            return true;
        }
        if (fSaveDlg) {
            if (ev.key == kKeyBackspace) { popUtf8(fSaveName); fTypedLen = 0; fSaveMsg.clear(); repaint(); return true; }
            if (ev.key == kKeyEnter) { doSave(); return true; }
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
        const Ctl& c = *ctlFor(uint32_t(fPopup));
        const VoiceParam& p = paramAt(voiceDef(), uint32_t(fPopup));
        const int n = p.numLabels;
        const int maxRows = int((fHeight - kHeaderH - 2 * kMargin) / kItemH);
        cols = (n + maxRows - 1) / maxRows;
        rows = (n + cols - 1) / cols;
        px = std::clamp(c.x, kMargin, kWidth - kMargin - cols * kItemW);
        py = c.y + 40;
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
            if (fRangeDrag >= 0) {
                fRangeDrag = -1;
                saveRand();
                repaint();
                return true;
            }
            if (fDrag >= 0) {
                editParameter(uint32_t(fDrag), false);
                fDrag = -1;
                repaint();
                return true;
            }
            return false;
        }
        if (fSaveDlg) return ev.button == kMouseButtonLeft ? dialogClick(x, y) : true;
        if (ev.button == kMouseButtonLeft && y >= kZoomY && y < kZoomY + kZoomH) {
            if (x >= kPrevX && x < kPrevX + kArrowW) {
                if (!fPresets.empty()) loadPreset(fPreset < 0 ? int(fPresets.size()) - 1 : (fPreset + int(fPresets.size()) - 1) % int(fPresets.size()));
                return true;
            }
            if (x >= kNextX && x < kNextX + kArrowW) {
                if (!fPresets.empty()) loadPreset(fPreset < 0 ? 0 : (fPreset + 1) % int(fPresets.size()));
                return true;
            }
            if (x >= kNameX && x < kNameX + kPresetNameW) {
                fBrowser = !fBrowser;
                fSearchFocus = false;
                fPopup = -1;
                if (fBrowser) loadPresetList(); // pick up files added meanwhile
                repaint();
                return true;
            }
            if (x >= kSaveX && x < kSaveX + kSaveW) { openSaveDialog(); return true; }
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
        if (y >= kZoomY && y < kZoomY + kZoomH && x >= kZoomX && x < kZoomX + kZoomW) {
            const int z = x < kZoomX + kZoomW / 2 ? std::max(0, fZoom - 1) : std::min(kNumZooms - 1, fZoom + 1);
            if (z != fZoom) {
                fZoom = z;
                saveZoom(fZoom);
                applyZoom();
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
            if (dbl) { setValue(c->param, defOf(c->param), true); return true; }
            beginDrag(int(c->param), x, y, 1);
            break;
        case CtlType::Toggle:
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
        if (fSaveDlg) return true;
        if (fBrowser) { browserHover(x, y); return true; }
        if (fPopup >= 0) {
            const int h = popupItemAt(x, y);
            if (h != fPopupHover) { fPopupHover = h; repaint(); }
            return true;
        }
        if (fRangeDrag >= 0) {
            setRangeFromX(x);
            return true;
        }
        if (fDrag < 0)
            return false;
        const uint32_t i = uint32_t(fDrag);
        const float range = maxOf(i) - minOf(i);
        const bool fine = (ev.mod & kModifierShift) != 0;
        float v;
        if (fDragAxis == 1) {
            const float pixels = typeOf(i) == CtlType::Select ? 8.f * range : 160.f; // selects: 8 px per step
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
        const Ctl* c = ctlAt(x, y);
        if (c == nullptr) return false;
        const float step = ev.delta.getY() > 0 ? 1.f : ev.delta.getY() < 0 ? -1.f : 0.f;
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
    float fDragStartX = 0, fDragStartY = 0, fDragStartValue = 0;
    uint32_t fLastClickParam = UINT32_MAX;
    uint fLastClickTime = 0;
    int fPopup = -1, fPopupHover = -1;
    int fZoom = 1;

    maquina::RandSettings fRand;
    int fRangeDrag = -1;          // group * 2 + (0 min / 1 max) being dragged
    bool fSnapshotDirty = false;

    // presets
    std::vector<PresetEntry> fPresets; // factory, then user (sorted)
    int fPreset = -1;                  // loaded entry, -1 = Init / unknown
    std::string fPresetName { "Init" };
    bool fPresetDirty = false;         // edited since loading
    bool fBrowser = false;
    int fBrowseCat = 0;                // 0 All, 1..n categories, n+1 User
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
    bool fApplying = false;       // our own randomize/back edits in flight
    std::mt19937 fRng { std::random_device {}() };

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumUI)
};

UI* createUI() { return new DrumUI(); }

END_NAMESPACE_DISTRHO
