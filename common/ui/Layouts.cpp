// Layouts.cpp - the new look's geometry: header chrome, random drawer and the fixed
// board layouts (positions in window coordinates, from the mockups in the design folder;
// docs/ui-redesign.md). The section-grid families keep makeLayout()'s row packing.
//
// Included by DrumUI.cpp inside namespace maquina_ui, after the Layout types.
// SPDX-License-Identifier: GPL-3.0-only

// ---------------------------------------------------------------------------
// header

struct HeaderGeo {
    float titleX = 14, titleY = 23;
    Box prev, name, next, save, init, minus, pct, plus;
    float creditRight = 0;
    const char* credit = "";
};

Credit creditOf(uint8_t v)
{
    if (v == maquina::kDredrumVoice) return { "based on Dredrum firmware \xc2\xb7 unofficial", "Dredrum \xc2\xb7 unofficial" };
    if (v == maquina::kPlethoraVoice)
        return { "based on Befaco Noise Plethora \xc2\xb7 unofficial", "after Noise Plethora \xc2\xb7 unofficial" };
    if (maquina::isOneiroi(v))
        return { "based on Befaco Oneiroi firmware \xc2\xb7 unofficial", "after Oneiroi \xc2\xb7 unofficial" };
    if (v == maquina::kKlangVoice) return { "after MI Plaits \xc2\xb7 unofficial", "after Plaits \xc2\xb7 unofficial" };
    if (maquina::isKlong(v)) return { "after MI Rings \xc2\xb7 unofficial", "after Rings \xc2\xb7 unofficial" };
    if (maquina::isAcida(v)) return { "based on OpenRB-338 \xc2\xb7 unofficial", "OpenRB-338 \xc2\xb7 unofficial" };
    if (maquina::isBl101(v))
        return { "based on S1Vave by Andrew GG \xc2\xb7 unofficial", "after S1Vave \xc2\xb7 unofficial" };
    if (maquina::isLa50(v))
        return { "LA synthesis \xc2\xb7 reads D-50 patches \xc2\xb7 dsynkant, Munt \xc2\xb7 unofficial",
                 "LA synthesis \xc2\xb7 dsynkant, Munt \xc2\xb7 unofficial" };
    return { "based on LXR-02 firmware \xc2\xb7 unofficial", "after LXR-02 \xc2\xb7 unofficial" };
}

// IBM Plex Mono advances 0.6 em; the title has 0.12 em letter spacing
constexpr float kMonoAdv = 0.6f;
inline float monoWidth(const char* s, float px, float spacingEm = 0.f)
{
    const int n = utf8Len(s);
    return n ? n * px * (kMonoAdv + spacingEm) - px * spacingEm : 0.f;
}

std::string headerTitle()
{
    std::string t = std::string("MAQUINA ") + voiceDef().name;
    for (char& c : t) c = char(std::toupper(c));
    return t;
}

HeaderGeo makeHeaderGeo()
{
    HeaderGeo g;
    const float by = 10, bh = 26, gap = 10, sq = 26;
    const float titleEnd = 14 + monoWidth(headerTitle().c_str(), 13, 0.12f);
    float x = titleEnd + gap + 28 + gap;
    const float saveW = monoWidth("SAVE", 12) + 26, initW = monoWidth("INIT", 12) + 26;
    // right end: [–] 100% [+]
    float r = kWidth - 14;
    g.plus = { r - sq, by, sq, bh };
    r -= sq + gap;
    g.pct = { r - 40, by, 40, bh };
    r -= 40 + gap;
    g.minus = { r - sq, by, sq, bh };
    r -= sq + gap;
    g.creditRight = r;
    // the preset field gets up to 250 px: it narrows (to 170) before the credit line is
    // shortened, then down to 150 with the short credit
    const Credit cr = creditOf(voiceDef().voice);
    const float fixed = sq + gap + gap + sq + gap + saveW + gap + initW;
    float room = r - x - fixed - 2 * gap;
    float nameW = std::min(250.f, room - monoWidth(cr.longText, 11));
    if (nameW < 170) { // tight: the spacer after the title shrinks to 6 px first
        x = titleEnd + gap + 6 + gap;
        room = r - x - fixed - 2 * gap;
        nameW = std::min(250.f, room - monoWidth(cr.longText, 11));
    }
    g.credit = cr.longText;
    if (nameW < 170) {
        g.credit = cr.shortText;
        nameW = std::clamp(room - monoWidth(cr.shortText, 11), 150.f, 250.f);
    }
    g.prev = { x, by, sq, bh };
    x += sq + gap;
    g.name = { x, by, nameW, bh };
    x += nameW + gap;
    g.next = { x, by, sq, bh };
    x += sq + gap;
    g.save = { x, by, saveW, bh };
    x += saveW + gap;
    g.init = { x, by, initW, bh };
    return g;
}

// the classic header (DrumUI's constants)
HeaderGeo classicHeaderGeo()
{
    HeaderGeo g;
    g.prev = { kPrevX, kZoomY, kArrowW, kZoomH };
    g.name = { kNameX, kZoomY, kPresetNameW, kZoomH };
    g.next = { kNextX, kZoomY, kArrowW, kZoomH };
    g.save = { kSaveX, kZoomY, kSaveW, kZoomH };
    g.init = { kInitX, kZoomY, kInitW, kZoomH };
    g.minus = { kZoomX, kZoomY, kZoomW / 2, kZoomH };
    g.plus = { kZoomX + kZoomW / 2, kZoomY, kZoomW / 2, kZoomH };
    g.pct = { kZoomX, kZoomY, 0, 0 };
    return g;
}

const HeaderGeo& headerGeo()
{
    static const HeaderGeo g = kNew ? makeHeaderGeo() : classicHeaderGeo();
    return g;
}

// ---------------------------------------------------------------------------
// random drawer: [RANDOM]/[BACK] | one group per random target

void makeDrawer(Layout& L, float panelBottom)
{
    const float y = panelBottom + kChromeInset;
    L.drawer = { 0, y, kWidth, kChromeDrawerH };
    const float cy = y + kChromeDrawerH / 2;
    RandPanel& R = L.rand;
    R.random = { 16, cy - 33, 104, 32 };
    R.back = { 16, cy + 7, 104, 26 };
    int n = 0;
    for (int i = 0; i < maquina::kNumRandGroups; ++i)
        n += (R.used[i] = maquina::randGroupUsed(voiceDef(), i));
    float x = 16 + 104 + (n > 8 ? 18 : 26); // many groups (LXR: 10): tighter
    // open: [▴] at the right end; folded: one bar [RANDOM] [BACK] ... [RANGES ▾]
    R.openToggle = { kWidth - 16 - 26, cy - 13, 26, 26 };
    R.foldRandom = { 16, y + 10, 104, 26 };
    R.foldBack = { 16 + 104 + 10, y + 10, 104, 26 };
    R.foldToggle = { kWidth - 16 - 112, y + 10, 112, 26 };
    const float right = R.openToggle.x - 22;
    const float gap = n > 8 ? 10.f : 26.f, cw = (right - x - gap * float(n - 1)) / float(std::max(1, n));
    for (int i = 0; i < maquina::kNumRandGroups; ++i) {
        R.cell[i] = { x, cy - 25, R.used[i] ? cw : 0.f, R.used[i] ? 50.f : 0.f };
        if (R.used[i]) x += cw + gap;
    }
    L.height = y + kChromeDrawerH;
}

// ---------------------------------------------------------------------------
// fixed boards

// a knob: label (centre line of its last line), knob centre, value centre line; the hit
// area spans the column from the label to the value
Ctl& addKnob(Layout& L, const char* sym, const char* label, float cx, float labelY, float ky, float valueY, float size,
             float colW)
{
    static Ctl none;
    const int p = findParam(sym);
    if (p < 0) return none = Ctl {};
    const float top = std::min(labelY - 20, ky - size / 2 - 10), bottom = std::max(valueY + 8, ky + size / 2 + 10);
    Ctl c { uint32_t(p), CtlType::Knob, label, cx - colW / 2, top, colW, bottom - top };
    c.ax = cx; c.ay = ky; c.size = size; c.labelY = labelY; c.valueY = valueY;
    L.ctls.push_back(c);
    return L.ctls.back();
}

// a selector drawn as a dark window (click = list), its label under it
void addWindow(Layout& L, const char* sym, const char* label, float cx, float cy, float w, float h, float labelY)
{
    const int p = findParam(sym);
    if (p < 0) return;
    const float bottom = label ? labelY + 8 : cy + h / 2;
    Ctl c { uint32_t(p), CtlType::Select, label, cx - w / 2, cy - h / 2, w, bottom - (cy - h / 2) };
    c.ax = cx; c.ay = cy; c.size = 1; c.bw = w; c.bh = h; c.labelY = labelY; c.look = Ctl::Window;
    L.ctls.push_back(c);
}

// Maquina Acida: one row (DESIGN-GUIDE §3.1; mockups/acida.html)
void makeAcida(Layout& L)
{
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 218;
    L.panel = { px, py, pw, ph };
    // columns: [waveform/engine 124] | tone knobs (flex) | [acc vel, gain 130] | [logo 150+12]
    const float x0 = px + 16, gap = 12, vr = 1.5f;
    const float waveX = x0, waveW = 124;
    const float vr1 = waveX + waveW + gap;
    const float toneX = vr1 + vr + gap;
    const float logoW = 162, accW = 130;
    const float logoX = px + pw - 16 - logoW;
    const float vr3 = logoX - gap - vr;
    const float accX = vr3 - gap - accW;
    const float vr2 = accX - gap - vr;
    const float toneW = vr2 - gap - toneX;
    for (float vx : { vr1, vr2, vr3 }) L.decos.push_back({ Deco::VRule, { vx, py + 22, vr, ph - 44 } });
    L.decos.push_back({ Deco::Logo, { logoX + 12, py, logoW - 12, ph } });

    const float labelY = py + 58.5f, ky = py + 115, valueY = py + 172;
    static const struct { const char* sym; const char* label; float size; } kTone[] = {
        { "tune", "Tuning", 58 }, { "cutoff", "Cut off\nfreq", 82 }, { "resonance", "Resonance", 58 },
        { "env_mod", "Env mod", 58 }, { "decay", "Decay", 58 }, { "accent", "Accent", 58 },
    };
    const float colW = 84, step = (toneW - colW) / 5;
    for (int k = 0; k < 6; ++k)
        addKnob(L, kTone[k].sym, kTone[k].label, toneX + colW / 2 + k * step, labelY, ky, valueY, kTone[k].size, colW);
    addKnob(L, "accent_vel", "Acc vel", accX + 29, labelY, ky, valueY, 44, 58);
    addKnob(L, "gain", "Gain", accX + 29 + 72, labelY, ky, valueY, 44, 58);

    // WAVEFORM: SAW / SQUARE slide switch; ENGINE: stepper with an LED window
    const float wcx = waveX + waveW / 2, top = py + (ph - 128) / 2;
    if (const int p = findParam("waveform"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Switch, "Waveform", wcx - 40, top, 80, 66 };
        c.ax = wcx + 27; c.ay = top + 22 + 22; c.size = 1; c.bw = 16; c.bh = 44; c.labelY = top + 13;
        L.ctls.push_back(c);
    }
    if (const int p = findParam("engine"); p >= 0) {
        const float ey = top + 66 + 16;
        Ctl c { uint32_t(p), CtlType::Select, "Engine", wcx - 55, ey, 110, 46 };
        c.ax = wcx; c.ay = ey + 20 + 13; c.size = 1; c.bw = 110; c.bh = 26; c.labelY = ey + 13;
        L.ctls.push_back(c);
    }
    makeDrawer(L, py + ph);
}

// ---------------------------------------------------------------------------
// Mutable light grey: Klang (DESIGN-GUIDE §3.5; mockups/klang.html)

// a cluster: a row of LEDs / colour dots picking a value, round buttons, an LED
Ctl& addCluster(Layout& L, const char* sym, const char* label, Box hit, float cy)
{
    static Ctl none;
    const int p = findParam(sym);
    if (p < 0) return none = Ctl {};
    Ctl c { uint32_t(p), CtlType::Select, label, hit.x, hit.y, hit.w, hit.h };
    c.ay = cy; c.look = Ctl::Cluster;
    L.ctls.push_back(c);
    return L.ctls.back();
}

void makeKlang(Layout& L)
{
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 378;
    L.panel = { px, py, pw, ph };
    const float x0 = px + 24, x1 = px + pw - 24, cy = py + 22 + 17; // the top bar's centre line
    L.decos.push_back({ Deco::Logo, { x0, cy - 17, 200, 34 } });
    // [‹] 8 model LEDs [›] [model ▾]   (LED) [TRACK]
    if (const int p = findParam("key_track"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Toggle, "Track", x1 - 102, cy - 17, 102, 34 };
        c.ay = cy; c.ledX = x1 - 92; c.stepX = x1 - 63; c.labelX = x1 - 40; c.look = Ctl::LedButton;
        L.ctls.push_back(c);
    }
    addWindow(L, "model", nullptr, x1 - 112 - 75, cy, 150, 28, 0); // first: the list opens here
    {
        // [‹] gap 14, 8 LEDs (12 px, gap 8), gap 14, [›] gap 14, the window (150), gap 14, TRACK
        const float nx = x1 - 112 - 150 - 14 - 15, pr = nx - 15 - 14 - 152 - 14 - 15; // › and ‹ centres
        Ctl& c = addCluster(L, "model", nullptr, { pr - 17, cy - 17, nx + 17 - (pr - 17), 34 }, cy);
        c.n = 8;
        for (int k = 0; k < 8; ++k) c.xs[k] = pr + 35 + 20 * k;
        c.prevX = pr;
        c.stepX = nx;
    }
    // seven columns: main knob row, the small knob / selector that belongs to it below
    const float gap = 8, colW = (x1 - x0 - 6 * gap) / 7, gridY = cy + 17 + 18;
    auto colX = [&](int k) { return x0 + k * (colW + gap) + colW / 2; };
    const float ky = gridY + 58, ly = gridY + 116 + 8, vy = ly + 14.5f;
    const float dy0 = vy + 13, dy1 = dy0 + 18, sy = dy1 + 26, sly = sy + 26 + 6.5f, svy = sly + 13.5f;
    static const struct { const char* sym; const char* label; float size; const char* sub; const char* subLabel; bool win; } kCols[7] = {
        { "coarse", "Frequency", 96, "fine", "Fine", false },
        { "harmonics", "Harmonics", 96, "fm_env", "Env \xe2\x80\xba FM", false },
        { "timbre", "Timbre", 70, "timbre_env", "Env \xe2\x80\xba Timbre", false },
        { "morph", "Morph", 70, "morph_env", "Env \xe2\x80\xba Morph", false },
        { "lpg_decay", "Decay", 70, "lpg_mode", "Mode", true },
        { "lpg_colour", "Colour", 70, nullptr, nullptr, false },
        { "gain", "Gain", 56, "output", "Output", true },
    };
    for (int k = 0; k < 7; ++k) {
        const float x = colX(k);
        addKnob(L, kCols[k].sym, kCols[k].label, x, ly, ky, vy, kCols[k].size, colW);
        if (!kCols[k].sub) continue;
        L.decos.push_back({ Deco::Dotted, { x, dy0, 0, dy1 - dy0 } });
        if (kCols[k].win) addWindow(L, kCols[k].sub, kCols[k].subLabel, x, sy, 104, 28, sly);
        else addKnob(L, kCols[k].sub, kCols[k].subLabel, x, sly, sy, svy, 36, colW);
    }
    // LOW-PASS GATE over Decay and Colour
    L.decos.push_back({ Deco::RuleText, { colX(4) - colW / 2, gridY + 2.5f, 2 * colW + gap, 0 }, "Low-pass gate" });
    makeDrawer(L, py + ph);
}

// Klong, Klong FX: the Rings arrangement (mockups/klong.html, klong-fx.html)
void makeKlong(Layout& L)
{
    const bool fx = voiceDef().voice == maquina::kKlongFxVoice;
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 438;
    L.panel = { px, py, pw, ph };
    const float x0 = px + 36, x1 = px + pw - 36, cy = py + 22 + 17;
    L.decos.push_back({ Deco::Logo, { x0, cy - 17, 200, 34 } });
    // right to left: [model ▾] (button) (LED) 6 colour dots MODEL | POLY 1 2 3 4 (LED) (button)
    addWindow(L, "model", nullptr, x1 - 64, cy, 128, 28, 0);
    {
        const float ab = x1 - 128 - 10 - 15, led = ab - 15 - 10 - 6, d0 = led - 6 - 10 - 114 + 7;
        Ctl& c = addCluster(L, "model", "Model", { d0 - 7 - 10 - 40, cy - 17, ab + 17 - (d0 - 57), 34 }, cy);
        c.n = 6;
        for (int k = 0; k < 6; ++k) c.xs[k] = d0 + 20 * k;
        c.ledX = led; c.stepX = ab; c.labelX = d0 - 7 - 10 - 40;
        const float div = c.labelX - 16 - 1.5f;
        L.decos.push_back({ Deco::Divider, { div, cy - 14, 1.5f, 28 } });
        const float pab = div - 16 - 15, pled = pab - 15 - 10 - 6, p0 = pled - 6 - 10 - 130 + 7;
        Ctl& q = addCluster(L, "polyphony", "Poly", { p0 - 7 - 10 - 32, cy - 17, pab + 17 - (p0 - 49), 34 }, cy);
        q.n = 4;
        for (int k = 0; k < 4; ++k) q.xs[k] = p0 + 35 * k;
        q.ledX = pled; q.stepX = pab; q.labelX = p0 - 7 - 10 - 32;
    }
    // knobs (positions relative to the area under the top bar)
    const float ox = x0, oy = cy + 17 + 18;
    addKnob(L, "coarse", "Frequency", ox + 110, oy + 140 + 7.8f, oy + 80, oy + 140 + 24.4f, 96, 140);
    {
        Ctl& f = addKnob(L, "fine", "Fine", ox + 182, oy + 126 + 6.5f, oy + 138, oy + 126 + 21.8f, 36, 40);
        f.labelX = ox + 182 + 28;
        f.w = ox + 182 + 28 + 50 - f.x;
    }
    addKnob(L, "structure", "Structure", ox + 370, oy + 140 + 7.8f, oy + 80, oy + 140 + 24.4f, 96, 140);
    addKnob(L, "brightness", "Brightness", ox + 75, oy + 305 + 7.8f, oy + 258, oy + 305 + 24.4f, 70, 140);
    addKnob(L, "damping", "Damping", ox + 240, oy + 305 + 7.8f, oy + 258, oy + 305 + 24.4f, 70, 140);
    addKnob(L, "position", "Position", ox + 405, oy + 305 + 7.8f, oy + 258, oy + 305 + 24.4f, 70, 140);
    L.decos.push_back({ Deco::Dotted, { ox + 496, oy + 10, 0, py + ph - 20 - 10 - (oy + 10) } });
    // right column: OUTPUT (ODD | EVEN), NOTES (Klong FX), GAIN
    const float rc = ox + 520 + 120;
    if (const int p = findParam("output"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Select, "Output", rc - 69, oy + 20, 138, 98 };
        c.ax = rc; c.ay = oy + 20 + 24; c.size = 1; c.bw = 138; c.bh = 48;
        c.labelY = oy + 68 + 10 + 7.8f; c.valueY = c.labelY + 7.8f + 10 + 6.8f; c.look = Ctl::Segments;
        L.ctls.push_back(c);
    }
    if (fx) addWindow(L, "note_source", "Notes", rc, oy + 150 + 14, 120, 28, oy + 178 + 8 + 7.8f);
    const float gy = oy + (fx ? 268 : 212);
    addKnob(L, "gain", "Gain", ox + 640, gy + 40 + 7.8f, gy, gy + 40 + 24.4f, 52, 140);
    makeDrawer(L, py + ph);
}

// ---------------------------------------------------------------------------
// LXR drums: Kick, Tom, Snare, Cymbal, HiHat (mockups/kick-lxr.html, tom, snare, cymbal,
// hihat). Sections are outlined boxes in three rows; a box's items are spread like the
// mockups' flex rows (space-around). Each box gets its natural width, the rest of the row is
// shared out by the mockups' flex weights.

// the LXR-02 display's three-letter names (firmware MenuText.h shortNames)
const char* lxrShortName(const char* sym)
{
    static const char* const kMap[][2] = {
        { "osc_coarse", "coa" }, { "osc_fine", "fin" }, { "osc_wave", "wav" }, { "osc_pwm", "pwm" },
        { "noise_freq", "frq" }, { "noise_mix", "mix" }, { "amp_attack", "atk" }, { "amp_decay", "dec" },
        { "amp_slope", "slp" }, { "amp_repeat", "rpt" }, { "pitch_decay", "dec" }, { "pitch_slope", "slp" },
        { "pitch_amount", "amt" }, { "velo_dest", "dst" }, { "velo_amount", "amt" }, { "velo_volume", "vol" },
        { "fm_amount", "amt" }, { "fm_freq", "frq" }, { "fm_wave", "wav" }, { "fm_mode", "mod" },
        { "trans_wave", "wav" }, { "trans_volume", "vol" }, { "trans_freq", "frq" }, { "filter_freq", "frq" },
        { "filter_reso", "res" }, { "filter_type", "typ" }, { "filter_drive", "drv" }, { "lfo_rate", "frq" },
        { "lfo_sync", "snc" }, { "lfo_amount", "amt" }, { "lfo_wave", "wav" }, { "lfo_retrigger", "rtg" },
        { "lfo_offset", "ofs" }, { "lfo_target", "dst" }, { "volume", "vol" }, { "pan", "pan" },
        { "decimation", "srt" }, { "drive", "drv" }, { "note_pitch", "trk" }, { "gain", "gai" },
        { "mod1_freq", "f1" }, { "mod2_freq", "f2" }, { "mod1_gain", "g1" }, { "mod2_gain", "g2" },
        { "mod1_wave", "w1" }, { "mod2_wave", "w2" },
    };
    for (const auto& e : kMap)
        if (!std::strcmp(e[0], sym)) return e[1];
    return "";
}

// a selector value as the small red windows and the display show it (up to 5 characters)
std::string lxrShortLabel(const char* l)
{
    static const char* const kMap[][2] = {
        { "Sine", "Sin" }, { "Triangle", "Tri" }, { "Square", "Sqr" }, { "Noise", "Noi" }, { "Saw Up", "SawU" },
        { "Saw Down", "SawD" }, { "Random", "Rnd" }, { "Exp Up", "ExpU" }, { "Exp Down", "ExpD" },
        { "Lowpass", "LP" }, { "Highpass", "HP" }, { "Bandpass", "BP" }, { "Unity BP", "UBP" },
        { "Notch", "Ntch" }, { "Lowpass 2", "LP2" }, { "Offset", "Ofs" }, { "Click 2", "Clk2" },
        { "Kick 2", "Kck2" }, { "Snap 2", "Snp2" }, { "Snare", "Snr" }, { "Volume", "Vol" },
        { "Sample Rate", "SRt" }, { "Drive", "Drv" },
    };
    if (!l) return {};
    for (const auto& e : kMap)
        if (!std::strcmp(e[0], l)) return e[1];
    // modulation targets: "<Section> <Param>" -> section letter(s) + the param's short name
    static const char* const kSec[][2] = { { "Osc", "O" }, { "Amp", "A" }, { "Pitch", "P" }, { "Velocity", "V" },
                                           { "FM", "FM" }, { "Transient", "T" }, { "Filter", "F" }, { "LFO", "L" },
                                           { "Noise", "N" }, { "Mod1", "M1" }, { "Mod2", "M2" } };
    static const char* const kWord[][2] = {
        { "Coarse", "coa" }, { "Fine", "fin" }, { "Wave", "wav" }, { "PWM", "pwm" }, { "Attack", "atk" },
        { "Decay", "dec" }, { "Slope", "slp" }, { "Repeat", "rpt" }, { "Amount", "amt" }, { "Dest", "dst" },
        { "Freq", "frq" }, { "Mode", "mod" }, { "> Vol", "vol" }, { "Vol", "vol" }, { "Reso", "res" },
        { "Type", "typ" }, { "Drive", "drv" }, { "Rate", "frq" }, { "Sync", "snc" }, { "Retrigger", "rtg" },
        { "Phase", "ofs" }, { "Mix", "mix" }, { "Gain", "gai" },
    };
    if (const char* sp = std::strchr(l, ' '))
        for (const auto& sec : kSec)
            if (size_t(sp - l) == std::strlen(sec[0]) && !std::strncmp(l, sec[0], size_t(sp - l)))
                for (const auto& w : kWord)
                    if (!std::strcmp(sp + 1, w[0])) return std::string(sec[1]) + w[1];
    std::string s = l;
    if (utf8Len(l) > 5) s.resize(5);
    return s;
}

// VT323 advances 0.4 em: a window holding n characters at 21 px, 8 px padding each side
inline float lxrWindowW(int chars) { return std::max(44.f, chars * 8.4f + 16.f); }
// Questrial 11 px upper case, 0.05 em spacing (about 0.62 em a character)
inline float lxrLabelW(const char* l)
{
    float best = 0;
    for (const char* a = l; a && *a;) {
        const char* b = std::strchr(a, '\n');
        const size_t n = b ? size_t(b - a) : std::strlen(a);
        best = std::max(best, float(n) * 6.8f);
        a = b ? b + 1 : nullptr;
    }
    return best;
}

enum class LxrKind : uint8_t { Knob, Sel, Toggle, Amp, Pitch, Stack };
struct LxrItem { LxrKind kind; const char* sym; const char* label; const char* sym2 = nullptr; };
struct LxrBoxSpec { int row; const char* title; float flex; LxrItem items[8]; };
struct LxrPageSpec { const char* name; const char* syms[8]; };

constexpr LxrKind kKn = LxrKind::Knob, kSel = LxrKind::Sel, kTg = LxrKind::Toggle;
#define LXR_OSC { kKn, "osc_coarse", "Coarse" }, { kKn, "osc_fine", "Fine" }, { kSel, "osc_wave", "Wave" }, \
    { kKn, "osc_pwm", "PWM" }
#define LXR_AEG { LxrKind::Amp, nullptr, nullptr }, { kKn, "amp_attack", "Attack" }, { kKn, "amp_decay", "Decay" }, \
    { kKn, "amp_slope", "Slope" }, { kKn, "amp_repeat", "Repeat" }
#define LXR_PITCH { LxrKind::Pitch, nullptr, nullptr }, { kKn, "pitch_amount", "Amount" }, \
    { kKn, "pitch_decay", "Decay" }, { kKn, "pitch_slope", "Slope" }
#define LXR_CLICK { kSel, "trans_wave", "Wave" }, { kKn, "trans_volume", "Volume" }, { kKn, "trans_freq", "Freq" }
#define LXR_LFO { kSel, "lfo_wave", "Wave" }, { kKn, "lfo_rate", "Rate" }, { kKn, "lfo_amount", "Amount" }, \
    { kKn, "lfo_offset", "Phase" }, { kSel, "lfo_target", "Target" }, { kSel, "lfo_sync", "Sync" }, \
    { kTg, "lfo_retrigger", "Retrig" }
#define LXR_MOD { kSel, "velo_dest", "Velo dest" }, { kKn, "velo_amount", "Amount" }, { kTg, "velo_volume", "> Vol" }
#define LXR_FILTER { kSel, "filter_type", "Type" }, { kKn, "filter_freq", "Cutoff" }, { kKn, "filter_reso", "Reso" }, \
    { kKn, "filter_drive", "Drive" }
#define LXR_MIX { kKn, "volume", "Volume" }, { kKn, "pan", "Pan" }, { kKn, "drive", "Drive" }, \
    { kKn, "decimation", "Rate" }, { kKn, "gain", "Gain" }

// Kick, Tom (as tom.html; the Kick mockup is the same rows)
const LxrBoxSpec kLxrDrumBoxes[] = {
    { 0, "OSC", 3, { LXR_OSC } }, { 0, "AEG", 4, { LXR_AEG } }, { 0, "PITCH EG", 4, { LXR_PITCH } },
    { 1, "CLICK", 2.4f, { LXR_CLICK } }, { 1, "LFO", 5, { LXR_LFO } }, { 1, "MOD", 2.3f, { LXR_MOD } },
    { 2, "FM", 3.4f, { { kKn, "fm_amount", "Amount" }, { kKn, "fm_freq", "Freq" }, { LxrKind::Stack, "fm_wave", nullptr, "fm_mode" } } },
    { 2, "FILTER", 3.2f, { LXR_FILTER, { kTg, "note_pitch", "Key\ntrk" } } }, { 2, "MIX", 3.1f, { LXR_MIX } },
};
const LxrBoxSpec kLxrSnareBoxes[] = {
    { 0, "OSC", 3, { LXR_OSC } }, { 0, "NOISE", 1.6f, { { kKn, "noise_freq", "Freq" }, { kKn, "noise_mix", "Mix" } } },
    { 0, "PITCH EG", 4, { LXR_PITCH } }, { 0, "KEY", 0.8f, { { kTg, "note_pitch", "Track" } } },
    { 1, "CLICK", 2.4f, { LXR_CLICK } }, { 1, "LFO", 5, { LXR_LFO } }, { 1, "MOD", 2.3f, { LXR_MOD } },
    { 2, "AEG", 4.4f, { LXR_AEG } }, { 2, "FILTER", 3, { LXR_FILTER } }, { 2, "MIX", 3.3f, { LXR_MIX } },
};
const LxrBoxSpec kLxrMetalBoxes[] = { // Cymbal, HiHat
    { 0, "OSC", 3, { LXR_OSC } }, { 0, "AEG", 4.2f, { LXR_AEG } }, { 0, "KEY", 0.8f, { { kTg, "note_pitch", "Track" } } },
    { 0, "MOD", 2.3f, { LXR_MOD } },
    { 1, "MOD OSC", 5, { { kKn, "mod1_freq", "Freq 1" }, { kKn, "mod1_gain", "Gain 1" }, { kSel, "mod1_wave", "Wave 1" },
                         { kKn, "mod2_freq", "Freq 2" }, { kKn, "mod2_gain", "Gain 2" }, { kSel, "mod2_wave", "Wave 2" } } },
    { 1, "CLICK", 2.4f, { LXR_CLICK } }, { 1, "FILTER", 3, { LXR_FILTER } },
    { 2, "LFO", 5.4f, { LXR_LFO } }, { 2, "MIX", 4, { LXR_MIX } },
};

// the display pages (firmware menuPages order); slots 4-7 are the page's second half
#define LXR_P_OSC { "OSC", { "osc_coarse", "osc_fine", "osc_wave", "osc_pwm" } }
#define LXR_P_AEG { "AEG", { "amp_attack", "amp_decay", "amp_slope", "amp_repeat" } }
#define LXR_P_CLICK { "CLICK", { "trans_wave", "trans_volume", "trans_freq" } }
#define LXR_P_FILTER { "FILTER", { "filter_freq", "filter_reso", "filter_type", "filter_drive" } }
#define LXR_P_LFO { "LFO", { "lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target" } }
#define LXR_P_MIX { "MIX", { "volume", "pan", "decimation", "drive", "note_pitch", "gain" } }
#define LXR_P_MOD { "MOD", { "pitch_decay", "pitch_slope", "pitch_amount", nullptr, "velo_dest", "velo_amount", "velo_volume" } }
const LxrPageSpec kLxrDrumPages[8] = {
    LXR_P_OSC, LXR_P_AEG, LXR_P_MOD, { "FM", { "fm_amount", "fm_freq", "fm_wave", "fm_mode" } },
    LXR_P_CLICK, LXR_P_FILTER, LXR_P_LFO, LXR_P_MIX,
};
const LxrPageSpec kLxrSnarePages[8] = {
    LXR_P_OSC, { "NOISE", { "noise_freq", "noise_mix" } }, LXR_P_AEG, LXR_P_MOD,
    LXR_P_CLICK, LXR_P_FILTER, LXR_P_LFO, LXR_P_MIX,
};
const LxrPageSpec kLxrMetalPages[8] = {
    LXR_P_OSC, LXR_P_AEG, { "MOD", { "velo_dest", "velo_amount", "velo_volume" } },
    { "M.OSC", { "mod1_freq", "mod1_gain", "mod1_wave", nullptr, "mod2_freq", "mod2_gain", "mod2_wave" } },
    LXR_P_CLICK, LXR_P_FILTER, LXR_P_LFO, LXR_P_MIX,
};

// the widest value a selector shows, in characters
int lxrSelChars(int p)
{
    const VoiceParam& vp = paramAt(voiceDef(), uint32_t(p));
    int n = 3;
    for (uint8_t k = 0; vp.labels && k < vp.numLabels; ++k) n = std::max(n, utf8Len(lxrShortLabel(vp.labels[k]).c_str()));
    return n;
}

void makeLxr(Layout& L)
{
    const uint8_t v = voiceDef().voice;
    const bool snare = v == 3, metal = v >= 4;
    const LxrBoxSpec* specs = snare ? kLxrSnareBoxes : metal ? kLxrMetalBoxes : kLxrDrumBoxes;
    const size_t nSpecs = snare ? std::size(kLxrSnareBoxes) : metal ? std::size(kLxrMetalBoxes) : std::size(kLxrDrumBoxes);
    const LxrPageSpec* pages = snare ? kLxrSnarePages : metal ? kLxrMetalPages : kLxrDrumPages;

    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 568;
    L.panel = { px, py, pw, ph };
    const float x0 = px + 16, x1 = px + pw - 16, top = py + 14;

    // top strip: wordmark | display | page buttons | VOLUME
    L.decos.push_back({ Deco::Logo, { x0, top, 170, 96 } });
    LxrStrip& S = L.lxr;
    S.used = true;
    S.display = { x0 + 170 + 24, top + 10, 330, 76 };
    const float volW = 52, bx0 = S.display.x + S.display.w + 24, bx1 = x1 - volW - 24;
    S.pages = 8;
    S.btnY = top + 25 + 12;
    S.labelY = top + 25 + 24 + 8 + 7;
    S.lineX0 = bx0;
    S.lineX1 = bx1;
    for (int k = 0; k < 8; ++k) {
        S.btnX[k] = bx0 + 10 + 12 + k * (bx1 - bx0 - 44) / 7;
        S.name[k] = pages[k].name;
        for (int j = 0; j < 8; ++j) S.slot[k][j] = pages[k].syms[j] ? findParam(pages[k].syms[j]) : -1;
    }
    if (const int p = findParam("volume"); p >= 0) {
        const float cx = x1 - volW / 2, fy = top + 2.5f + 13 + 6;
        Ctl c { uint32_t(p), CtlType::Slider, "Volume", x1 - volW, top, volW, 96 };
        c.ax = cx; c.ay = fy + 36; c.size = 1; c.bw = 30; c.bh = 72; c.labelY = top + 2.5f + 6.5f;
        c.look = Ctl::LxrFader;
        L.ctls.push_back(c);
    }

    // rows
    const float rowY[3] = { top + 96 + 20, top + 96 + 20 + 132 + 20, top + 96 + 20 + 132 + 20 + 124 + 20 };
    const float rowH[3] = { 132, 124, py + ph - 16 - (top + 96 + 20 + 132 + 20 + 124 + 20) };
    constexpr float kKnobD = 52, kMini = 14, kMiniGap = 5, kItemGap = 8, kPadX = 10;
    struct Placed { const LxrItem* it; int p, p2; float w; };
    auto itemWidth = [&](const LxrItem& it, int p, int p2) -> float {
        switch (it.kind) {
        case LxrKind::Knob: return std::max(kKnobD, lxrLabelW(it.label));
        case LxrKind::Sel: return std::max(lxrWindowW(lxrSelChars(p)) + 2 * (kMini + kMiniGap), lxrLabelW(it.label));
        case LxrKind::Toggle: return std::max(30.f, lxrLabelW(it.label));
        case LxrKind::Stack:
            return lxrWindowW(std::max(lxrSelChars(p), p2 >= 0 ? lxrSelChars(p2) : 0)) + 2 * (kMini + kMiniGap);
        default: return 150.f; // envelope graph
        }
    };
    for (int r = 0; r < 3; ++r) {
        struct BoxW { const LxrBoxSpec* spec; std::vector<Placed> items; float natural; };
        std::vector<BoxW> boxes;
        for (size_t b = 0; b < nSpecs; ++b) {
            if (specs[b].row != r) continue;
            BoxW bw { &specs[b], {}, 2 * kPadX };
            for (const LxrItem& it : specs[b].items) {
                if (it.kind == LxrKind::Knob && !it.sym) break; // end of the list (a zero entry)
                int p = -1, p2 = -1;
                if (it.kind == LxrKind::Amp) p = findParam("amp_decay");
                else if (it.kind == LxrKind::Pitch) p = findParam("pitch_decay");
                else p = findParam(it.sym);
                if (it.sym2) p2 = findParam(it.sym2);
                if (p < 0) continue;
                const float w = itemWidth(it, p, p2);
                bw.natural += w + (bw.items.empty() ? 0.f : kItemGap);
                bw.items.push_back({ &it, p, p2, w });
            }
            if (!bw.items.empty()) boxes.push_back(std::move(bw));
        }
        float used = 14.f * float(boxes.size() - 1), flex = 0;
        for (const BoxW& b : boxes) { used += b.natural; flex += b.spec->flex; }
        const float spare = std::max(0.f, (x1 - x0) - used);
        float bx = x0;
        const float by = rowY[r], bh = rowH[r], cy = by + 22 + (bh - 32) / 2; // content: 22 top, 10 bottom
        for (const BoxW& b : boxes) {
            const float w = b.natural + spare * b.spec->flex / flex;
            L.decos.push_back({ Deco::LxrBox, { bx, by, w, bh }, b.spec->title });
            // space-around: the room left after the items and gaps, half a share at each end
            float itemsW = kItemGap * float(b.items.size() - 1);
            for (const Placed& pl : b.items) itemsW += pl.w;
            const float share = (w - 2 * kPadX - itemsW) / float(b.items.size());
            float ix = bx + kPadX + share / 2;
            for (const Placed& pl : b.items) {
                const float icx = ix + pl.w / 2;
                const LxrItem& it = *pl.it;
                const int lines = it.label && std::strchr(it.label, '\n') ? 2 : 1;
                const float lh = 13.f * float(lines);
                switch (it.kind) {
                case LxrKind::Knob: {
                    const float t = cy - (lh + 5 + kKnobD + 5 + 22) / 2;
                    Ctl c { uint32_t(pl.p), CtlType::Knob, it.label, ix, t, pl.w, lh + 5 + kKnobD + 5 + 22 };
                    c.ax = icx; c.ay = t + lh + 5 + kKnobD / 2; c.size = kKnobD;
                    c.labelY = t + lh - 6.5f; c.valueY = t + lh + 5 + kKnobD + 5 + 11;
                    L.ctls.push_back(c);
                    break;
                }
                case LxrKind::Sel: {
                    const float t = cy - (lh + 8 + 30) / 2;
                    Ctl c { uint32_t(pl.p), CtlType::Select, it.label, ix, t, pl.w, lh + 8 + 30 };
                    c.ax = icx; c.ay = t + lh + 8 + 15; c.size = 1; c.bw = lxrWindowW(lxrSelChars(pl.p)); c.bh = 30;
                    c.labelY = t + lh - 6.5f; c.look = Ctl::LxrSel;
                    L.ctls.push_back(c);
                    break;
                }
                case LxrKind::Stack: {
                    const float ww = pl.w - 2 * (kMini + kMiniGap);
                    for (int k = 0; k < 2; ++k) {
                        const int p = k ? pl.p2 : pl.p;
                        if (p < 0) continue;
                        const float wy = cy + (k ? 19.f : -19.f);
                        Ctl c { uint32_t(p), CtlType::Select, nullptr, ix, wy - 15, pl.w, 30 };
                        c.ax = icx; c.ay = wy; c.size = 1; c.bw = ww; c.bh = 30; c.look = Ctl::LxrSel;
                        L.ctls.push_back(c);
                    }
                    break;
                }
                case LxrKind::Toggle: {
                    const float t = cy - (lh + 12 + 24) / 2;
                    Ctl c { uint32_t(pl.p), CtlType::Toggle, it.label, ix, t, pl.w, lh + 12 + 24 + 4 };
                    c.ax = icx; c.ay = t + lh + 12 + 12; c.size = 1; c.labelY = t + lh - 6.5f;
                    c.look = Ctl::LxrToggle;
                    L.ctls.push_back(c);
                    break;
                }
                case LxrKind::Amp:
                case LxrKind::Pitch: {
                    Graph g { it.kind == LxrKind::Amp ? GraphKind::Amp : GraphKind::Pitch, ix, cy - 42, 150, 84 };
                    if (it.kind == LxrKind::Amp) {
                        g.attack = findParam("amp_attack"); g.decay = findParam("amp_decay"); g.slope = findParam("amp_slope");
                    } else {
                        g.decay = findParam("pitch_decay"); g.slope = findParam("pitch_slope"); g.amount = findParam("pitch_amount");
                    }
                    L.graphs.push_back(g);
                    break;
                }
                }
                ix += pl.w + kItemGap + share;
            }
            bx += w + 14;
        }
    }
    makeDrawer(L, py + ph);
}

void makeNewLayout(Layout& L)
{
    switch (voiceDef().voice) {
    case maquina::kAcidaVoice: makeAcida(L); break;
    case maquina::kKlangVoice: makeKlang(L); break;
    case maquina::kKlongVoice:
    case maquina::kKlongFxVoice: makeKlong(L); break;
    default:
        if (maquina::isLxrVoice(voiceDef().voice)) makeLxr(L);
        break;
    }
}
