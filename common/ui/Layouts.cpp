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
    if (maquina::isStrings(v))
        return { "after jpcima string-machine \xc2\xb7 MI Plaits \xc2\xb7 unofficial",
                 "string-machine, Plaits \xc2\xb7 unofficial" };
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


const HeaderGeo& headerGeo()
{
    static const HeaderGeo g = makeHeaderGeo();
    return g;
}

// ---------------------------------------------------------------------------
// random drawer: [RANDOM]/[BACK] | one group per random target

void makeDrawer(Layout& L, float panelBottom, bool channels = false)
{
    const float y = panelBottom + kChromeInset;
    L.drawer = { 0, y, kWidth, kChromeDrawerH };
    const float cy = y + kChromeDrawerH / 2;
    RandPanel& R = L.rand;
    R.random = { 16, cy - 33, 104, 32 };
    R.back = { 16, cy + 7, 104, 26 };
    int n = 0;
    // a group shows when it holds a parameter of its own: one whose first matching group it
    // is (BL-101: PATCH matches only patch_level, which LEVEL holds first)
    for (int i = 0; i < maquina::kNumRandGroups; ++i) {
        R.used[i] = false;
        for (uint32_t k = 0; k < voiceDef().numParams && !R.used[i]; ++k)
            R.used[i] = maquina::randGroupOf(voiceDef().params[k].symbol, voiceDef().voice) == i;
        n += R.used[i];
    }
    float x = 16 + 104 + (n > 8 ? 18 : 26); // many groups (LXR: 10): tighter
    // open: [▴] at the right end; folded: one bar [RANDOM] [BACK] ... [RANGES ▾]
    R.openToggle = { kWidth - 16 - 26, cy - 13, 26, 26 };
    R.foldRandom = { 16, y + 10, 104, 26 };
    R.foldBack = { 16 + 104 + 10, y + 10, 104, 26 };
    R.foldToggle = { kWidth - 16 - 112, y + 10, 112, 26 };
    float right = R.openToggle.x - 22;
    if (channels) { // per channel: [A] [RND ON] [ROLL], three rows, a rule before them
        const float rollX = right - 50, onX = rollX - 6 - 64, lw = voiceDef().voice == maquina::kDredrumVoice ? 44.f : 20.f;
        for (int k = 0; k < 3; ++k) {
            Section sec { "", onX - lw, y + 8 + 28.f * float(k), lw + 64 + 6 + 50, 24 };
            sec.osc = k;
            sec.randOn = { onX, sec.y, 64, 24 };
            sec.roll = { rollX, sec.y, 50, 24 };
            L.sections.push_back(sec);
        }
        right = onX - lw - 24;
    }
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

// the knob rings' LED colours on the Mutable boards (Strings' scheme, used by Klang and Klong
// too): amber = the sound source, cyan = its modulation / resonator / filter, green = level,
// envelope, output
constexpr Rgb kLedAmber { 0xf0, 0xa0, 0x30 }, kLedCyan { 0x3f, 0xc1, 0xc9 }, kLedGreen { 0x3f, 0xd3, 0x4a };
inline void ledRing(Ctl& c, Rgb rgb)
{
    c.led = true;
    c.ledRgb = rgb;
}

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
    // ring colours: the oscillator amber, the envelope amounts cyan, the low-pass gate and gain green
    static const struct { const char* sym; const char* label; float size; const char* sub; const char* subLabel; bool win;
                          Rgb ring, subRing; } kCols[7] = {
        { "coarse", "Frequency", 96, "fine", "Fine", false, kLedAmber, kLedAmber },
        { "harmonics", "Harmonics", 96, "fm_env", "Env \xe2\x80\xba FM", false, kLedAmber, kLedCyan },
        { "timbre", "Timbre", 70, "timbre_env", "Env \xe2\x80\xba Timbre", false, kLedAmber, kLedCyan },
        { "morph", "Morph", 70, "morph_env", "Env \xe2\x80\xba Morph", false, kLedAmber, kLedCyan },
        { "lpg_decay", "Decay", 70, "lpg_mode", "Mode", true, kLedGreen, kLedGreen },
        { "lpg_colour", "Colour", 70, nullptr, nullptr, false, kLedGreen, kLedGreen },
        { "gain", "Gain", 56, "output", "Output", true, kLedGreen, kLedGreen },
    };
    for (int k = 0; k < 7; ++k) {
        const float x = colX(k);
        ledRing(addKnob(L, kCols[k].sym, kCols[k].label, x, ly, ky, vy, kCols[k].size, colW), kCols[k].ring);
        if (!kCols[k].sub) continue;
        L.decos.push_back({ Deco::Dotted, { x, dy0, 0, dy1 - dy0 } });
        if (kCols[k].win) addWindow(L, kCols[k].sub, kCols[k].subLabel, x, sy, 104, 28, sly);
        else ledRing(addKnob(L, kCols[k].sub, kCols[k].subLabel, x, sly, sy, svy, 36, colW), kCols[k].subRing);
    }
    // LOW-PASS GATE over Decay and Colour, its LED in the gate's green
    {
        Deco d { Deco::RuleText, { colX(4) - colW / 2, gridY + 2.5f, 2 * colW + gap, 0 }, "Low-pass gate" };
        d.led = true;
        d.rgb = kLedGreen;
        L.decos.push_back(d);
    }
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
    // ring colours: the pitch amber, the resonator cyan, gain green
    ledRing(addKnob(L, "coarse", "Frequency", ox + 110, oy + 140 + 7.8f, oy + 80, oy + 140 + 24.4f, 96, 140), kLedAmber);
    {
        Ctl& f = addKnob(L, "fine", "Fine", ox + 182, oy + 126 + 6.5f, oy + 138, oy + 126 + 21.8f, 36, 40);
        f.labelX = ox + 182 + 28;
        f.w = ox + 182 + 28 + 50 - f.x;
        ledRing(f, kLedAmber);
    }
    ledRing(addKnob(L, "structure", "Structure", ox + 370, oy + 140 + 7.8f, oy + 80, oy + 140 + 24.4f, 96, 140), kLedCyan);
    ledRing(addKnob(L, "brightness", "Brightness", ox + 75, oy + 305 + 7.8f, oy + 258, oy + 305 + 24.4f, 70, 140), kLedCyan);
    ledRing(addKnob(L, "damping", "Damping", ox + 240, oy + 305 + 7.8f, oy + 258, oy + 305 + 24.4f, 70, 140), kLedCyan);
    ledRing(addKnob(L, "position", "Position", ox + 405, oy + 305 + 7.8f, oy + 258, oy + 305 + 24.4f, 70, 140), kLedCyan);
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
    ledRing(addKnob(L, "gain", "Gain", ox + 640, gy + 40 + 7.8f, gy, gy + 40 + 24.4f, 52, 140), kLedGreen);
    makeDrawer(L, py + ph);
}

// ---------------------------------------------------------------------------
// Maquina Strings: the Mutable grey at 1100 px (the approved mockup, Plugin Design "Strings
// panel"): three rows, the strings on the left (5/12), the wave layer on the right (7/12).
// Knob rings glow in their layer's colour: amber STRINGS, cyan WAVE + FILTER, the theme's green
// BLEND, AMP, FX.

constexpr Rgb kStrAmber = kLedAmber, kStrCyan = kLedCyan, kStrGreen = kLedGreen;

void makeStrings(Layout& L)
{
    // ph 684 (the mockup's 683 + 1): the window stays an even number of pixels tall at 100 %
    // (845 px drew garbage over the panel on a 2x XWayland desktop)
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 684;
    L.panel = { px, py, pw, ph };
    auto Y = [&](float ry) { return py + ry; }; // panel rows (mockup y - 62)
    const float lx0 = 44, lx1 = 450, rx0 = 476, rx1 = 1056; // the two columns

    auto title = [&](float x0, float x1, float ry, const char* t, bool led, Rgb c) {
        Deco d { Deco::Title, { x0, Y(ry), x1 - x0, 0 }, t };
        d.led = led;
        d.rgb = c;
        L.decos.push_back(d);
    };
    // a knob with its layer's LED ring; label and value under it
    auto knob = [&](const char* sym, const char* label, float cx, float ky, float size, Rgb ring) -> Ctl& {
        const bool small = size < 45;
        const float ly = ky + size / 2 + (small ? 11 : 16), vy = ly + 16;
        Ctl& c = addKnob(L, sym, label, cx, Y(ly), Y(ky), Y(vy), size, small ? 70.f : 80.f);
        c.led = true;
        c.ledRgb = ring;
        c.bigLabel = true;
        return c;
    };
    auto seg = [&](const char* sym, Box b, int cols, const char* const* labels) -> Ctl* {
        const int p = findParam(sym);
        if (p < 0) return nullptr;
        Ctl c { uint32_t(p), CtlType::Select, nullptr, b.x, b.y, b.w, b.h };
        c.ax = b.x + b.w / 2; c.ay = b.y + b.h / 2; c.size = 1; c.bw = b.w; c.bh = b.h;
        c.look = Ctl::Seg; c.cols = uint8_t(cols); c.segLabels = labels;
        L.ctls.push_back(c);
        return &L.ctls.back();
    };
    auto graph = [&](GraphKind k, Box b, const char* a, const char* bb, const char* cc) {
        Graph g { k, b.x, b.y, b.w, b.h };
        g.p[0] = findParam(a); g.p[1] = findParam(bb); g.p[2] = findParam(cc);
        L.graphs.push_back(g);
    };

    // title row: the logo, KEYS [LAYER | WAVE LOW | WAVE HIGH]
    L.decos.push_back({ Deco::Logo, { lx0, Y(39) - 17, 200, 34 } });
    { Deco d { Deco::Title, { 782, Y(39), 0, 0 }, "Keys" }; L.decos.push_back(d); }
    static const char* const kKeys[] = { "Layer", "Wave Low", "Wave High" };
    seg("keys", { 835, Y(24), 220, 30 }, 0, kKeys);

    // ---- row 1: STRINGS | WAVE
    title(lx0, lx1, 74, "Strings", true, kStrAmber);
    title(rx0, rx1, 74, "Wave", true, kStrCyan);
    {
        Ctl& r = knob("register", "Register", 92, 168, 72, kStrAmber);
        r.labelY = Y(223); r.valueY = 0; // the station list says where it is
        r.y = Y(118); r.h = Y(232) - r.y;
        const int p = findParam("register");
        if (p >= 0) { // the stations: click = jump there
            Ctl c { uint32_t(p), CtlType::Select, nullptr, 154, Y(110), 112, 94 };
            c.ay = Y(119); c.ledX = 162; c.ledRgb = kStrAmber; c.look = Ctl::Stations;
            L.ctls.push_back(c);
        }
    }
    seg("shape", { 158, Y(208), 117, 26 }, 0, nullptr);
    knob("detune", "Detune", 329, 110, 46, kStrAmber);
    knob("tone", "Tone", 409, 110, 46, kStrAmber);
    knob("vibrato", "Vibrato", 329, 201, 46, kStrAmber);
    knob("vib_delay", "Vib Delay", 409, 201, 46, kStrAmber);

    graph(GraphKind::WaveTerrain, { rx0, Y(101), 236, 112 }, "wave_bank", "wave_x", "wave_y");
    if (const int p = findParam("wave_bank"); p >= 0) { // BANK (A) (B) (C) (D)
        Ctl c { uint32_t(p), CtlType::Select, "Bank", rx0, Y(231) - 14, 170, 28 };
        c.ay = Y(231); c.labelX = rx0; c.n = 4; c.look = Ctl::RoundSel;
        for (int k = 0; k < 4; ++k) c.xs[k] = 536 + 30 * k;
        L.ctls.push_back(c);
    }
    static const struct { const char* sym; const char* label; } kWave[2][4] = {
        { { "wave_x", "Pos X" }, { "wave_y", "Morph Y" }, { "wave_octave", "Octave" }, { "wave_unison", "Unison" } },
        { { "wave_lfo_rate", "LFO Rate" }, { "wave_lfo_depth", "LFO Depth" }, { "wave_env_pos", "Env \xe2\x80\xba Pos" },
          { "wave_spread", "Spread" } },
    };
    for (int row = 0; row < 2; ++row)
        for (int k = 0; k < 4; ++k)
            knob(kWave[row][k].sym, kWave[row][k].label, 771 + 82 * k, row ? 201 : 110, 46, kStrCyan);

    // ---- row 2: BLEND | FILTER
    // BLEND, the mixer: [● STRINGS] (level) ═══ blend ═══ (level) [WAVE ●]; the LEDs switch the
    // layers on / off (off: not computed), the levels act before the blend
    title(lx0, lx1, 277, "Blend", false, {});
    {
        auto onOff = [&](const char* sym, const char* label, float ledX, float labelX, Box hit, Rgb rgb) {
            const int p = findParam(sym);
            if (p < 0) return;
            Ctl c { uint32_t(p), CtlType::Toggle, label, hit.x, hit.y, hit.w, hit.h };
            c.ay = Y(318); c.ledX = ledX; c.labelX = labelX; c.ledRgb = rgb; c.look = Ctl::LedLabel;
            L.ctls.push_back(c);
        };
        onOff("strings_on", "Strings", 52, 62, { 44, Y(306), 80, 24 }, kStrAmber);
        onOff("wave_on", "Wave", 446, -1, { 392, Y(306), 58, 24 }, kStrCyan);
        knob("strings_level", "Level", 146, 318, 28, kStrAmber);
        knob("wave_level", "Level", 366, 318, 28, kStrCyan);
        if (const int p = findParam("blend"); p >= 0) {
            Ctl c { uint32_t(p), CtlType::Knob, nullptr, 168, Y(304), 176, 28 };
            c.ax = 176; c.ay = Y(318); c.bw = 160; c.look = Ctl::HFader;
            L.ctls.push_back(c);
        }
    }
    graph(GraphKind::BlendCurve, { lx0, Y(383), 112, 62 }, "blend", "blend_shift", "blend_time");
    knob("blend_shift", "Shift", 201, 400, 34, kStrGreen);
    knob("blend_time", "Time", 273, 400, 34, kStrGreen);
    knob("blend_velo", "Velo", 343, 400, 34, kStrGreen);
    knob("layer_detune", "Detune", 413, 400, 34, kStrGreen);

    title(rx0, rx1, 277, "Filter", true, kStrCyan);
    seg("filter_mode", { rx0, Y(311), 236, 30 }, 0, nullptr);
    graph(GraphKind::FilterResp, { rx0, Y(348), 236, 92 }, "filter_mode", "filter_cutoff", "filter_reso");
    knob("filter_cutoff", "Cutoff", 766, 358, 46, kStrCyan);
    knob("filter_reso", "Reso", 849, 358, 46, kStrCyan);
    knob("filter_env", "Env", 931, 358, 46, kStrCyan);
    knob("filter_keytrack", "Keytrack", 1015, 358, 46, kStrCyan);

    // ---- row 3: AMP | ENSEMBLE / FX
    title(lx0, lx1, 479, "Amp", false, {});
    static const struct { const char* sym; const char* label; } kAmp[5] = {
        { "attack", "Attack" }, { "decay", "Decay" }, { "sustain", "Sustain" }, { "release", "Release" },
        { "velocity", "Velo" },
    };
    for (int k = 0; k < 5; ++k) knob(kAmp[k].sym, kAmp[k].label, 84 + 81 * k, 560, 46, kStrGreen);

    title(rx0, rx1, 479, "Ensemble / FX", false, {});
    seg("fx_mode", { rx0, Y(547), 178, 61 }, 2, nullptr);
    static const struct { const char* sym; const char* label; } kFx[5] = {
        { "fx_amount", "Amount" }, { "fx_slow", "Slow" }, { "fx_fast", "Fast" }, { "reverb", "Reverb" },
        { "gain", "Gain" },
    };
    for (int k = 0; k < 5; ++k) knob(kFx[k].sym, kFx[k].label, 706 + 77.75f * k, 560, 46, kStrGreen);

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
    { 0, "OSC", 3, { LXR_OSC } }, { 0, "KEY", 0.8f, { { kTg, "note_pitch", "Track" } } }, { 0, "PITCH EG", 4, { LXR_PITCH } },
    { 0, "FM", 3, { { kKn, "fm_amount", "Amount" }, { kKn, "fm_freq", "Freq" }, { LxrKind::Stack, "fm_wave", nullptr, "fm_mode" } } },
    { 1, "CLICK", 2.4f, { LXR_CLICK } }, { 1, "LFO", 5, { LXR_LFO } }, { 1, "MOD", 2.3f, { LXR_MOD } },
    { 2, "AEG", 4.4f, { LXR_AEG } }, { 2, "FILTER", 3, { LXR_FILTER } }, { 2, "MIX", 3.3f, { LXR_MIX } },
};
const LxrBoxSpec kLxrSnareBoxes[] = {
    { 0, "OSC", 3, { LXR_OSC } }, { 0, "KEY", 0.8f, { { kTg, "note_pitch", "Track" } } },
    { 0, "NOISE", 1.6f, { { kKn, "noise_freq", "Freq" }, { kKn, "noise_mix", "Mix" } } }, { 0, "PITCH EG", 4, { LXR_PITCH } },
    { 1, "CLICK", 2.4f, { LXR_CLICK } }, { 1, "LFO", 5, { LXR_LFO } }, { 1, "MOD", 2.3f, { LXR_MOD } },
    { 2, "AEG", 4.4f, { LXR_AEG } }, { 2, "FILTER", 3, { LXR_FILTER } }, { 2, "MIX", 3.3f, { LXR_MIX } },
};
const LxrBoxSpec kLxrMetalBoxes[] = { // Cymbal, HiHat
    { 0, "OSC", 3, { LXR_OSC } }, { 0, "KEY", 0.8f, { { kTg, "note_pitch", "Track" } } }, { 0, "AEG", 4.2f, { LXR_AEG } },
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

// ---------------------------------------------------------------------------
// graphene: BL-101, BL-101 FX (mockups/bl101.html, bl101-fx.html). Sections with a title
// strip in rows; fixed sections keep the mockup's width (or their natural one if wider),
// the others share the rest; items spread like the mockups' flex rows (space-around).

// a slide switch's option as printed on the panel
std::string grOption(const char* l)
{
    static const char* const kMap[][2] = {
        { "Inv Saw", "INV" }, { "Triangle", "TRI" }, { "Square", "SQR" }, { "Random", "S/H" }, { "Manual", "MAN" },
        { "-2 Pulse", "-2 PW" }, { "Gate+Trig", "G+T" },
    };
    for (const auto& e : kMap)
        if (!std::strcmp(e[0], l)) return e[1];
    std::string t = l;
    for (char& ch : t) ch = char(std::toupper(ch));
    return t;
}
// Inter 700 10 px upper case, 0.06 em: about 7 px a character; options (9.5 px): 6.6
inline float grLabelW(const char* l) { return l ? float(utf8Len(l)) * 7.f : 0.f; }
float grSwitchW(int p)
{
    const VoiceParam& vp = paramAt(voiceDef(), uint32_t(p));
    float w = 0;
    for (uint8_t k = 0; vp.labels && k < vp.numLabels; ++k) w = std::max(w, float(utf8Len(grOption(vp.labels[k]).c_str())) * 6.6f);
    return 14 + 5 + w;
}
// a stepper's window: the longest value (IBM Plex Mono 12 px, 7.2 px a character)
float grWindowW(int p)
{
    const VoiceParam& vp = paramAt(voiceDef(), uint32_t(p));
    int n = 3;
    for (uint8_t k = 0; vp.labels && k < vp.numLabels; ++k) n = std::max(n, utf8Len(vp.labels[k]));
    return std::max(52.f, float(n) * 7.2f + 20.f);
}

enum class GrKind : uint8_t { Slider, Switch, Knob, Led, Step };
// join: sits right after the previous item (12 px), one unit with it (CHORD's voice + shift);
// sym2 (a slider): a small chip under its value toggling that parameter (NOISE: PINK / WHITE)
struct GrItem { GrKind kind; const char* sym; const char* label; bool join = false; const char* sym2 = nullptr; };
struct GrSecSpec { int row; const char* title; float width; bool green; GrItem items[10]; };

constexpr GrKind kGSl = GrKind::Slider, kGSw = GrKind::Switch, kGKn = GrKind::Knob, kGLed = GrKind::Led, kGSt = GrKind::Step;
const GrSecSpec kBl101Secs[] = {
    { 0, "MODULATOR", 118, true, { { kGSl, "modulator_rate", "Rate" }, { kGSw, "modulator_wave", "Wave" } } },
    { 0, "VCO", 190, false, { { kGSl, "vco_mod", "Mod" }, { kGSl, "vco_pw", "PW" }, { kGSw, "vco_range", "Range" },
                              { kGSw, "vco_pwm_src", "PWM" } } },
    { 0, "SOURCE MIXER", 258, true, { { kGSl, "mix_pulse", "Pulse" }, { kGSl, "mix_saw", "Saw" }, { kGSl, "mix_sub", "Sub" },
                                       { kGSl, "mix_noise", "Noise", false, "mix_noise_type" }, { kGSw, "mix_sub_type", "Sub" } } },
    { 0, "VCF", 206, false, { { kGSl, "vcf_freq", "Freq" }, { kGSl, "vcf_res", "Res" }, { kGSl, "vcf_env", "Env" },
                              { kGSl, "vcf_mod", "Mod" }, { kGSl, "vcf_kybd", "Kybd" } } },
    { 0, "VCA", 62, false, { { kGSw, "vca_mode", nullptr } } },
    { 0, "ENV", 0, false, { { kGSw, "env_trigger", "Trig" }, { kGSl, "adsr_attack", "A" }, { kGSl, "adsr_decay", "D" },
                            { kGSl, "adsr_sustain", "S" }, { kGSl, "adsr_release", "R" } } },
    { 1, "LFO MODE", 200, false, { { kGLed, "modulator_sync", "Sync" }, { kGLed, "modulator_fast", "Fast" },
                                   { kGLed, "modulator_trig", "Key trig" } } },
    { 1, "VOICE", 380, false, { { kGSt, "voice_mode", "Voicing" }, { kGSt, "porta_mode", "Porta" }, { kGKn, "porta_time", "Time" },
                                { kGKn, "transpose", "Transpose" } } },
    { 1, "CHORD", 0, false, { { kGLed, "chord2_on", "Voice 2" }, { kGKn, "chord2_shift", nullptr, true },
                              { kGLed, "chord3_on", "Voice 3" }, { kGKn, "chord3_shift", nullptr, true },
                              { kGLed, "chord4_on", "Voice 4" }, { kGKn, "chord4_shift", nullptr, true } } },
    { 2, "CONTROL", 0, false, { { kGKn, "vco_fine", "Fine" }, { kGKn, "bend_vco", "Bend VCO" }, { kGKn, "bend_vcf", "Bend VCF" },
                                { kGKn, "stereo_spread", "Spread" }, { kGKn, "unison_detune", "Detune" } } },
    { 2, "MIX", 240, false, { { kGKn, "patch_level", "Level" }, { kGKn, "gain", "Gain" } } },
    { 3, "OSC DRAW", 330, false, {} },
    { 3, "OSC CHOP", 0, false, {} },
};
const GrSecSpec kBl101FxSecs[] = {
    { 0, "CHORUS", 170, false, { { kGSt, "chorus_mode", "Mode" } } },
    { 0, "DELAY", 0, false, { { kGSl, "dly_level", "Level" }, { kGSl, "dly_time", "Time" }, { kGSl, "dly_feedback", "Feedback" },
                              { kGLed, "dly_sync", "Sync" } } },
    { 0, "REVERB", 0, true, { { kGSt, "rev_type", "Type" }, { kGSl, "rev_level", "Level" }, { kGSl, "rev_time", "Time" },
                              { kGSl, "rev_predelay", "Pre-delay" } } },
    { 1, "LO-FI", 0, false, { { kGSt, "lofi_mode", "Mode" }, { kGKn, "lofi_amount", "Amount" }, { kGKn, "lofi_mix", "Mix" } } },
    { 1, "MIX", 150, false, { { kGKn, "gain", "Gain" } } },
};

constexpr float kGrTitleH = 26.f; // title strip 24 + its 2 px bottom line
constexpr float kGrSliderH = 104.f, kGrKnobD = 48.f;

void makeGraphene(Layout& L)
{
    const bool fx = voiceDef().voice == maquina::kBl101FxVoice;
    const GrSecSpec* specs = fx ? kBl101FxSecs : kBl101Secs;
    const size_t nSpecs = fx ? std::size(kBl101FxSecs) : std::size(kBl101Secs);
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset;
    const float ph = fx ? 358 : 698;
    L.panel = { px, py, pw, ph };
    const float x0 = px + 8, x1 = px + pw - 8;
    std::vector<float> rowH = fx ? std::vector<float> { 212 } : std::vector<float> { 212, 118, 112 };
    float used = 0;
    for (float h : rowH) used += h + 6;
    rowH.push_back(ph - 16 - used);
    float ry = py + 8;
    for (size_t r = 0; r < rowH.size(); ++r) {
        struct Unit { std::vector<std::pair<const GrItem*, int>> items; float w; };
        struct SecW { const GrSecSpec* spec; std::vector<Unit> units; float natural; };
        std::vector<SecW> secs;
        for (size_t b = 0; b < nSpecs; ++b) {
            if (specs[b].row != int(r)) continue;
            SecW sw { &specs[b], {}, 12 };
            for (const GrItem& it : specs[b].items) {
                if (!it.sym) break;
                const int p = findParam(it.sym);
                if (p < 0) continue;
                float w = 0;
                switch (it.kind) {
                case GrKind::Slider: w = std::max({ 26.f, grLabelW(it.label), it.sym2 ? 40.f : 0.f }); break;
                case GrKind::Switch: w = std::max(grSwitchW(p), grLabelW(it.label)); break;
                case GrKind::Knob: w = std::max(kGrKnobD, grLabelW(it.label)); break;
                case GrKind::Led: w = std::max(40.f, grLabelW(it.label)); break;
                case GrKind::Step: w = std::max(26 + 4 + grWindowW(p) + 4 + 26, grLabelW(it.label)); break;
                }
                if (it.join && !sw.units.empty()) {
                    sw.units.back().items.push_back({ &it, p });
                    sw.units.back().w += 12 + w;
                } else {
                    sw.units.push_back({ { { &it, p } }, w });
                }
            }
            for (size_t u = 0; u < sw.units.size(); ++u) sw.natural += sw.units[u].w + (u ? 8.f : 0.f);
            secs.push_back(std::move(sw));
        }
        // widths: fixed sections first, the flexible ones share the rest
        float fixedW = 6.f * float(secs.size() - 1), flexNat = 0;
        int nFlex = 0;
        for (const SecW& s : secs) {
            if (s.spec->width > 0) fixedW += std::max(s.spec->width, s.natural);
            else { flexNat += s.natural; ++nFlex; }
        }
        const float spare = std::max(0.f, (x1 - x0) - fixedW - flexNat);
        float sx = x0;
        const float h = rowH[r];
        for (const SecW& s : secs) {
            const float w = s.spec->width > 0 ? std::max(s.spec->width, s.natural) : s.natural + spare / float(std::max(1, nFlex));
            L.decos.push_back({ Deco::GrSection, { sx, ry, w, h }, s.spec->title });
            const float cy = ry + kGrTitleH + (h - kGrTitleH) / 2;
            // space-around between units
            float unitsW = 8.f * float(s.units.size() ? s.units.size() - 1 : 0);
            for (const Unit& u : s.units) unitsW += u.w;
            const float share = s.units.empty() ? 0.f : (w - unitsW) / float(s.units.size());
            float ux = sx + share / 2;
            for (const Unit& u : s.units) {
                float ix = ux;
                for (const auto& [itp, p] : u.items) {
                    const GrItem& it = *itp;
                    float iw = 0;
                    switch (it.kind) {
                    case GrKind::Slider: iw = std::max({ 26.f, grLabelW(it.label), it.sym2 ? 40.f : 0.f }); break;
                    case GrKind::Switch: iw = std::max(grSwitchW(p), grLabelW(it.label)); break;
                    case GrKind::Knob: iw = std::max(kGrKnobD, grLabelW(it.label)); break;
                    case GrKind::Led: iw = std::max(40.f, grLabelW(it.label)); break;
                    case GrKind::Step: iw = std::max(26 + 4 + grWindowW(p) + 4 + 26, grLabelW(it.label)); break;
                    }
                    const float icx = ix + iw / 2;
                    const float lh = it.label ? 12.f : 0.f;
                    switch (it.kind) {
                    case GrKind::Slider: { // label, 6, slider 104, 6, value
                        const float t = cy - (12 + 6 + kGrSliderH + 6 + 13) / 2;
                        Ctl c { uint32_t(p), CtlType::Slider, it.label, ix, t, iw, 12 + 6 + kGrSliderH + 6 + 13 };
                        c.ax = icx; c.ay = t + 18 + kGrSliderH / 2; c.size = 1; c.bw = 26; c.bh = kGrSliderH;
                        c.labelY = t + 6; c.valueY = t + 18 + kGrSliderH + 6 + 6.5f; c.look = Ctl::GrSlider;
                        c.n = s.spec->green ? 1 : 0; // cap stripe: green / orange
                        L.ctls.push_back(c);
                        if (it.sym2)
                            if (const int q = findParam(it.sym2); q >= 0) {
                                Ctl k { uint32_t(q), CtlType::Toggle, nullptr, icx - 20, c.valueY + 9, 40, 16 };
                                k.ax = icx; k.ay = c.valueY + 17; k.size = 1; k.bw = 40; k.bh = 16; k.look = Ctl::GrChip;
                                L.ctls.push_back(k);
                            }
                        break;
                    }
                    case GrKind::Switch: { // label, 6, switch (20 px a position) + options
                        const int n = std::max<int>(1, paramAt(voiceDef(), uint32_t(p)).numLabels);
                        const float sh = 20.f * float(n), t = cy - (lh + (lh ? 6 : 0) + sh) / 2;
                        Ctl c { uint32_t(p), CtlType::Switch, it.label, ix, t, iw, lh + (lh ? 6 : 0) + sh };
                        const float swx = icx - grSwitchW(p) / 2;
                        c.ax = swx + 7; c.ay = t + lh + (lh ? 6 : 0) + sh / 2; c.size = 1; c.bw = 14; c.bh = sh;
                        c.labelY = t + 6; c.labelX = swx + 19; c.look = Ctl::GrSwitch;
                        L.ctls.push_back(c);
                        break;
                    }
                    case GrKind::Knob: { // label, 4, knob, 4, value
                        const float t = cy - (lh + 4 + kGrKnobD + 4 + 13) / 2;
                        Ctl c { uint32_t(p), CtlType::Knob, it.label, ix, t, iw, lh + 4 + kGrKnobD + 4 + 13 };
                        c.ax = icx; c.ay = t + lh + 4 + kGrKnobD / 2; c.size = kGrKnobD;
                        c.labelY = t + 6; c.valueY = t + lh + 4 + kGrKnobD + 4 + 6.5f;
                        L.ctls.push_back(c);
                        break;
                    }
                    case GrKind::Led: { // LED 16 x 5, 6, button 40 x 22, 6, label
                        const float t = cy - (5 + 6 + 22 + 6 + 12) / 2;
                        Ctl c { uint32_t(p), CtlType::Toggle, it.label, ix, t, iw, 51 };
                        c.ax = icx; c.ay = t + 11 + 11; c.size = 1; c.bw = 40; c.bh = 22;
                        c.labelY = t + 39 + 6; c.look = Ctl::GrLed;
                        L.ctls.push_back(c);
                        break;
                    }
                    case GrKind::Step: { // label, 8, [‹] window [›]
                        const float t = cy - (lh + 8 + 26) / 2;
                        Ctl c { uint32_t(p), CtlType::Select, it.label, ix, t, iw, lh + 8 + 26 };
                        c.ax = icx; c.ay = t + lh + 8 + 13; c.size = 1; c.bw = grWindowW(p); c.bh = 26;
                        c.labelY = t + 6; c.look = Ctl::GrStep;
                        L.ctls.push_back(c);
                        break;
                    }
                    }
                    ix += iw + 12;
                }
                ux += u.w + 8 + share;
            }
            // OSC DRAW: the bar display, MODE stepper, MULTIPLY window
            if (!std::strcmp(s.spec->title, "OSC DRAW")) {
                Graph g { GraphKind::Draw, sx + 12, ry + kGrTitleH + 10, w - 24, 96 };
                g.mode = findParam("draw_mode");
                g.first = findParam("draw_1");
                L.graphs.push_back(g);
                const float rowY = g.y + 96 + 8 + 13;
                if (g.mode >= 0) {
                    const float ww = 64, cx = sx + 12 + 42 + 30 + ww / 2;
                    Ctl c { uint32_t(g.mode), CtlType::Select, "Mode", sx + 12, rowY - 13, 42 + 60 + ww + 4, 26 };
                    c.ax = cx; c.ay = rowY; c.size = 1; c.bw = ww; c.bh = 26; c.labelX = sx + 12; c.labelY = rowY;
                    c.look = Ctl::GrStep;
                    L.ctls.push_back(c);
                }
                if (const int m = findParam("draw_mult"); m >= 0) {
                    const float ww = 36, wx = sx + w - 12 - ww;
                    Ctl c { uint32_t(m), CtlType::Knob, "Multiply", wx - 72, rowY - 13, 72 + ww, 26 };
                    c.ax = wx + ww / 2; c.ay = rowY; c.size = 1; c.bw = ww; c.bh = 26; c.labelX = wx - 72; c.labelY = rowY;
                    c.look = Ctl::GrWin;
                    L.ctls.push_back(c);
                }
            }
            // OSC CHOP: 4 rows of 16 pads (+ the mask window), OVERTONE and COMB knobs
            if (!std::strcmp(s.spec->title, "OSC CHOP")) {
                const float gw = 52 + 16 * 26 + 4 + 60, gh = 4 * 24 - 6, kw = 48 + 8 + 70;
                const float gx = sx + (w - gw - 22 - kw) / 2, gy = cy - gh / 2;
                Graph g { GraphKind::Chop, gx, gy, gw, gh };
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
                L.graphs.push_back(g);
                const float kx = gx + gw + 22 + 24;
                const char* const kKn[2][2] = { { "chop_overtone", "Overtone" }, { "chop_comb", "Comb" } };
                for (int k = 0; k < 2; ++k) {
                    const int p = findParam(kKn[k][0]);
                    if (p < 0) continue;
                    const float ky = cy + (k ? 29.f : -29.f);
                    Ctl c { uint32_t(p), CtlType::Knob, kKn[k][1], kx - 24, ky - 24, kw, 48 };
                    c.ax = kx; c.ay = ky; c.size = kGrKnobD; c.labelX = kx + 24 + 8; c.labelY = ky - 7; c.valueY = ky + 8;
                    L.ctls.push_back(c);
                }
            }
            sx += w + 6;
        }
        ry += h + 6;
    }
    makeDrawer(L, py + ph);
}

// ---------------------------------------------------------------------------
// LA-50 (mockups/la50.html): the PATCH column, then the PG-1000's three slider rows split
// into titled sections; 42 px columns. Fills the La50Panel the editor's LA-50 code uses, so
// the edit modes, drags and envelopes work unchanged. A slot's box: the slider (top + 13,
// height - 27) with 13 px above and 14 below (la50Click: cap travel = box.h - 40).

constexpr float kLaColW = 42.f, kLaSliderH = 84.f;

void makeLa50New(Layout& L)
{
    La50Panel& P = L.la50;
    P.used = true;
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 638;
    L.panel = { px, py, pw, ph };
    const float x0 = px + 8, y0 = py + 8, colPatch = 186;
    // PATCH column
    L.decos.push_back({ Deco::GrSection, { x0, y0, colPatch, ph - 16 }, "PATCH" });
    const float lx = x0 + 10, lw = colPatch - 20;
    float y = y0 + kGrTitleH + 8;
    P.lcd = { lx, y, lw, 56 };
    y += 56 + 10;
    // a row of LED buttons: caption, LED + button (36 x 20), name under it
    auto buttons = [&](Box* out, int n, bool spread) {
        const float bw = 40, bh = 22;
        for (int k = 0; k < n; ++k) {
            const float cx = n == 1 ? lx + bw / 2 : spread ? lx + bw / 2 + (lw - bw) * float(k) / float(n - 1) : 0;
            out[k] = { cx - bw / 2, y + 14 + 9 + 11, bw, bh };
        }
        y += 14 + 9 + 11 + bh + 14 + 12;
    };
    buttons(&P.patch, 1, true);
    buttons(P.common, 2, true);
    buttons(P.partial, 4, true);
    buttons(P.mute, 2, true);
    P.init = { lx, y, 60, 26 };
    P.import = { lx + 66, y, lw - 66, 26 };
    y += 26 + 10;
    {
        const float bottom = y0 + ph - 16 - 10, gap = 8, eh = (bottom - y - 2 * gap) / 3;
        for (int k = 0; k < 3; ++k) P.env[k] = { lx, y + k * (eh + gap), lw, eh };
    }
    // the rows: sections (first column, last column, title, width, slots: 0 centred,
    // 1 right-aligned (WG WAVEFORM: the PCM selector left of them), 2 left (TVA ENV: OUTPUT))
    struct Sec { int row, c0, c1; const char* title; float w; int align; };
    static const Sec kSecs[] = {
        { 0, 0, 5, "WG PITCH", 309, 0 }, { 0, 6, 12, "WG WAVEFORM", 567, 1 }, { 0, 13, 14, "KEY", 0, 0 },
        { 1, 0, 9, "TVF", 456, 0 }, { 1, 10, 15, "TVF ENV", 273, 0 }, { 1, 16, 21, "TVA ENV", 0, 0 },
        { 2, 0, 6, "TVA", 334, 0 }, { 2, 7, 12, "TVF ENV", 285, 0 }, { 2, 13, 18, "TVA ENV", 0, 2 },
    };
    const float rx0 = x0 + colPatch + 6, rx1 = px + pw - 8;
    const float rowY[3] = { y0, y0 + 202 + 6, y0 + 2 * (202 + 6) };
    const float rowH[3] = { 202, 202, y0 + ph - 16 - (y0 + 2 * (202 + 6)) };
    const La50SlotSpec* rows[3] = { kLa50Row1, kLa50Row2, kLa50Row3 };
    const int counts[3] = { int(std::size(kLa50Row1)), int(std::size(kLa50Row2)), int(std::size(kLa50Row3)) };
    static const char* const kPartial[4] = { "u_p1_", "u_p2_", "l_p1_", "l_p2_" };
    static const char* const kCommon[2] = { "u_", "l_" };
    std::vector<float> colCx[3];
    for (int r = 0; r < 3; ++r) colCx[r].assign(size_t(counts[r]), 0.f);
    float sx = rx0;
    int lastRow = -1;
    for (const Sec& sc : kSecs) {
        if (sc.row != lastRow) { sx = rx0; lastRow = sc.row; }
        const float w = sc.w > 0 ? sc.w : rx1 - sx;
        L.decos.push_back({ Deco::GrSection, { sx, rowY[sc.row], w, rowH[sc.row] }, sc.title });
        const int n = sc.c1 - sc.c0 + 1;
        const float left = sc.align == 1 ? sx + w - 12 - n * kLaColW : sc.align == 2 ? sx + 12 : sx + (w - n * kLaColW) / 2;
        for (int c = sc.c0; c <= sc.c1 && c < counts[sc.row]; ++c) colCx[sc.row][size_t(c)] = left + (c - sc.c0 + 0.5f) * kLaColW;
        if (sc.row == 0 && sc.align == 1) { // the PCM wave selector, centred in the room left of the slots
            const float fw = 26 + 4 + 150 + 4 + 26, fx = sx + (left - sx - fw) / 2, fy = rowY[0] + kGrTitleH + (rowH[0] - kGrTitleH) / 2 - 20;
            P.pcmPrev = { fx, fy, 26, 26 };
            P.pcmName = { fx + 30, fy, 150, 26 };
            P.pcmNext = { fx + fw - 26, fy, 26, 26 };
        }
        if (sc.align == 2) { // OUTPUT: the gain knob right of the slots
            const float gx = left + n * kLaColW, gwid = sx + w - gx;
            P.gainBox = { gx, rowY[sc.row] + kGrTitleH, gwid, rowH[sc.row] - kGrTitleH };
            if (const int gain = findParam("gain"); gain >= 0) {
                const float cx = gx + gwid / 2, cy = rowY[sc.row] + kGrTitleH + (rowH[sc.row] - kGrTitleH) / 2 + 6;
                Ctl c { uint32_t(gain), CtlType::Knob, "Gain", cx - 40, cy - 40, 80, 90 };
                c.ax = cx; c.ay = cy; c.size = kGrKnobD; c.labelY = cy + 24 + 12; c.valueY = cy + 24 + 26;
                L.ctls.push_back(c);
            }
        }
        sx += w + 6;
    }
    for (int r = 0; r < 3; ++r) {
        const float top = rowY[r] + kGrTitleH + 54; // the slider's top
        for (int c = 0; c < counts[r]; ++c) {
            const La50SlotSpec& sp = rows[r][c];
            La50Slot sl;
            sl.box = { colCx[r][size_t(c)] - kLaColW / 2, top - 13, kLaColW, kLaSliderH + 27 };
            sl.green = (r == 1 && c >= 10) || (r == 2 && c >= 7); // TVF ENV, TVA ENV
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
    // group brackets under the lower labels (the section titles are the kind-0 groups)
    for (const La50GroupSpec& g : kLa50Groups) {
        if (g.kind == 0 || g.row == 0) continue;
        P.groups.push_back({ colCx[g.row][size_t(g.c0)] - kLaColW / 2 + 4, colCx[g.row][size_t(g.c1)] + kLaColW / 2 - 4,
                             rowY[g.row] + rowH[g.row] - 10, g.title, g.kind });
    }
    makeDrawer(L, py + ph);
}

// ---------------------------------------------------------------------------
// Befaco Oneiroi, Oneiroi FX (mockups/oneiroi.html, oneiroi-fx.html): the module 1:1 (1090 x
// 830) and the expander (204 x 830); positions in module coordinates from the mockups. The
// randomizer is the module's own, on the panel: no drawer.

void makeOneiroi(Layout& L)
{
    BfPanel& B = L.bf;
    B.used = true;
    B.fx = voiceDef().voice == maquina::kOneiroiFxVoice;
    const float ox = 10, oy = kChromeHeaderH + 10, ex = ox + 1090 + 6;
    B.ox = ox; B.oy = oy; B.ex = ex;
    L.panel = { ox, oy, 1090, 830 };
    L.noDrawer = true;
    L.height = oy + 830 + 10;
    // knobs: centre, scale radius, cap, main and SHIFT parameter, label, SHIFT label
    struct K { float x, y, R; int kind; const char* sym; const char* alt; const char* label; const char* altLabel; };
    static const K kKnobs[] = {
        { 178, 150, 64, 0, "loop_speed", nullptr, "VARISPEED", nullptr },
        { 322, 82, 42, 1, "loop_start", "loop_sos", "START", "SOS" },
        { 322, 222, 42, 1, "loop_length", "loop_filter", "LENGTH", "FILTER" },
        { 91, 380, 64, 0, "osc_pitch", "osc_octave", "PITCH", "OCTAVE" },
        { 322, 360, 42, 1, "osc_detune", "osc_unison", "DETUNE", "UNISON" },
        { 537, 212, 42, 2, "mod_level", nullptr, "MOD LEVEL", nullptr },
        { 537, 372, 42, 2, "mod_speed", "mod_type", "SPEED", "TYPE" },
        { 656, 146, 64, 0, "flt_cutoff", "flt_mode", "CUTOFF", "MODE" },
        { 774, 222, 42, 1, "flt_reso", "flt_pos", "RESO", "POS" },
        { 897, 146, 64, 0, "res_tune", "res_dissonance", "TUNE", "DISSONANCE" },
        { 1017, 222, 42, 1, "res_feedback", nullptr, "FEEDBACK", nullptr },
        { 774, 410, 64, 0, "echo_density", "echo_filter", "DENSITY", "FILTER" },
        { 656, 456, 42, 1, "echo_repeats", nullptr, "REPEATS", nullptr },
        { 1017, 410, 64, 0, "amb_spacetime", "amb_autopan", "SPACETIME", "AUTOPAN" },
        { 897, 456, 42, 1, "amb_decay", nullptr, "DECAY", nullptr },
    };
    auto knob = [&](float x, float y, float R, int kind, const char* sym, const char* alt, const char* label,
                    const char* altLabel, float bx) {
        const int p = findParam(sym);
        if (p < 0) return;
        const float cx = bx + x, cy = oy + y;
        Ctl c { uint32_t(p), CtlType::Knob, label, cx - R, cy - R, 2 * R, 2 * R };
        c.ax = cx; c.ay = cy; c.size = 2 * R; c.n = uint8_t(kind); c.look = Ctl::BfKnob;
        // label block (Oswald 17 px, then the SHIFT box, then the value)
        const float top = cy + (kind == 0 ? 70.f : 46.f);
        c.labelY = top + 9;
        const int a = alt ? findParam(alt) : -1;
        c.alt = a;
        c.valueY = top + (a >= 0 ? 18 + 3 + 17 + 3 + 6.5f : 18 + 3 + 6.5f);
        L.ctls.push_back(c);
        if (a >= 0) { // the white SHIFT box: drag / wheel / double-click edit the SHIFT function
            const float bw = float(utf8Len(altLabel)) * 7.2f + 12, by = top + 18 + 3 + 8.5f;
            Ctl b { uint32_t(a), CtlType::Knob, altLabel, cx - bw / 2, by - 9, bw, 18 };
            b.ax = cx; b.ay = by; b.size = 1; b.bw = bw; b.bh = 17; b.look = Ctl::BfShift;
            L.ctls.push_back(b);
        }
    };
    for (const K& k : kKnobs) knob(k.x, k.y, k.R, k.kind, k.sym, k.alt, k.label, k.altLabel, ox);
    // filter mode / reso position badges
    struct Bd { float x, y; const char* sym; int v; const char* t; };
    static const Bd kBadges[] = {
        { 590, 190, "flt_mode", 0, "LP" }, { 590, 102, "flt_mode", 1, "BP" }, { 722, 102, "flt_mode", 2, "HP" },
        { 722, 190, "flt_mode", 3, "CF" }, { 722, 238, "flt_pos", 0, "I" }, { 740, 170, "flt_pos", 1, "II" },
        { 812, 170, "flt_pos", 2, "III" }, { 826, 238, "flt_pos", 3, "IV" },
    };
    for (const Bd& b : kBadges) {
        const int p = findParam(b.sym);
        if (p < 0) continue;
        const float w = float(utf8Len(b.t)) * 7.f + 8, cx = ox + b.x, cy = oy + b.y;
        Ctl c { uint32_t(p), CtlType::Select, b.t, cx - w / 2, cy - 9, w, 18 };
        c.ax = cx; c.ay = cy; c.size = 1; c.bw = w; c.bh = 18; c.n = uint8_t(b.v); c.look = Ctl::BfBadge;
        L.ctls.push_back(c);
    }
    // SS / [WT] switch (Oneiroi)
    if (const int p = findParam("osc2_mode"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Switch, nullptr, ox + 196, oy + 389, 74, 58 };
        c.ax = ox + 256; c.ay = oy + 418; c.size = 1; c.bw = 26; c.bh = 58; c.look = Ctl::BfSwitch;
        L.ctls.push_back(c);
    }
    // SOURCE stepper (FX)
    if (const int p = findParam("loop_source"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Select, "SOURCE", ox + 178 - 92, oy + 398 - 15, 184, 30 };
        c.ax = ox + 178; c.ay = oy + 398; c.size = 1; c.bw = 110; c.bh = 30; c.labelY = oy + 360; c.look = Ctl::GrStep;
        L.ctls.push_back(c);
    }
    // faders
    struct F { float x; const char* sym; const char* label; };
    static const F kFaders[] = { { 182, "vol_looper", "LOOPER" }, { 276, "vol_sine", "SINE" }, { 276, "vol_input", "INPUT" },
                                 { 372, "vol_osc2", "SS/WT" }, { 755, "mix_filter", "FILTER" },
                                 { 851, "mix_resonator", "RESONATOR" }, { 945, "mix_echo", "ECHO" },
                                 { 1041, "mix_ambience", "AMBIENCE" } };
    for (const F& f : kFaders) {
        const int p = findParam(f.sym);
        if (p < 0) continue;
        const float cx = ox + f.x;
        Ctl c { uint32_t(p), CtlType::Slider, f.label, cx - 20, oy + 552, 40, 176 };
        c.ax = cx; c.ay = oy + 560 + 80; c.size = 1; c.bw = 14; c.bh = 160; c.labelY = oy + 740; c.valueY = oy + 762;
        c.look = Ctl::BfFader;
        L.ctls.push_back(c);
    }
    // RECORD (SHIFT: CLEAR) and its CLEAR box
    if (const int p = findParam("loop_record"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Toggle, "RECORD", ox + 71 - 33, oy + 700 - 33, 66, 66 };
        c.ax = ox + 71; c.ay = oy + 700; c.size = 58; c.labelY = oy + 750; c.look = Ctl::BfButton;
        c.alt = findParam("loop_clear");
        L.ctls.push_back(c);
        if (c.alt >= 0) {
            Ctl b { uint32_t(c.alt), CtlType::Toggle, "CLEAR", ox + 71 - 22, oy + 772 - 9, 44, 18 };
            b.ax = ox + 71; b.ay = oy + 772; b.size = 1; b.bw = 44; b.bh = 17; b.look = Ctl::BfShift;
            L.ctls.push_back(b);
        }
    }
    // the module's randomizer and SHIFT (drawn and clicked by the editor)
    B.shift = { ox + 430 - 33, oy + 415 - 33, 66, 66 };
    B.random = { ox + 640 - 33, oy + 710 - 33, 66, 66 };
    B.target = { ox + 584 - 13, oy + 602 - 59, 26, 118 };
    B.amount = { ox + 664 - 13, oy + 598 - 36, 26, 72 };
    B.prev = { ox + 512 - 11, oy + 680 - 11, 22, 22 };
    B.next = { ox + 576 - 11, oy + 680 - 11, 22, 22 };
    for (int k = 0; k < 4; ++k) B.rowY[k] = oy + 560 + 28.f * float(k);
    // the expander
    const float eo = B.fx ? -60.f : 0.f; // FX: no ENVELOPE / DRONE; SYNC and OUT move up
    if (!B.fx) {
        knob(58, 150, 42, 1, "env_attack", nullptr, "ATTACK", nullptr, ex);
        knob(146, 150, 42, 1, "env_decay", nullptr, "DECAY", nullptr, ex);
        knob(58, 296, 42, 1, "env_sustain", nullptr, "SUSTAIN", nullptr, ex);
        knob(146, 296, 42, 1, "env_release", nullptr, "RELEASE", nullptr, ex);
        if (const int p = findParam("env_drone"); p >= 0) {
            Ctl c { uint32_t(p), CtlType::Toggle, "DRONE", ex + 102 - 28, oy + 416 - 28, 56, 56 };
            c.ax = ex + 102; c.ay = oy + 416; c.size = 46; c.labelY = oy + 458; c.look = Ctl::BfButton;
            c.ledX = ex + 140; c.prevX = oy + 394;
            L.ctls.push_back(c);
        }
    }
    const float sy = B.fx ? 128 + 13.f : 568 + 13.f, gy = B.fx ? 302.f : 722.f;
    (void)eo;
    if (const int p = findParam("sync"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Select, nullptr, ex + 102 - 68, oy + sy - 13, 136, 26 };
        c.ax = ex + 102; c.ay = oy + sy; c.size = 1; c.bw = 60; c.bh = 26; c.look = Ctl::GrStep;
        L.ctls.push_back(c);
    }
    if (const int p = findParam("gain"); p >= 0) {
        const float cx = ex + 102, cy = oy + gy;
        Ctl c { uint32_t(p), CtlType::Knob, nullptr, cx - 42, cy - 42, 84, 84 };
        c.ax = cx; c.ay = cy; c.size = 84; c.n = 1; c.look = Ctl::BfKnob; c.valueY = cy + 68;
        L.ctls.push_back(c);
    }
}

// Befaco Plethora (mockups/plethora.html): A left, the program display centre, B mirrored
// right, C under the rules; the expander holds the envelopes, levels and mutes
void makePlethora(Layout& L)
{
    BfPanel& B = L.bf;
    B.used = true;
    B.plethora = true;
    const float ox = 10, oy = kChromeHeaderH + 10, ex = ox + 994 + 6;
    B.ox = ox; B.oy = oy; B.ex = ex;
    B.ew = 300;
    L.panel = { ox, oy, 994, 580 };
    // knob: centre, scale radius, cap radius, cap kind, label top offset, label px
    auto knob = [&](float bx, float x, float y, float R, float C, int kind, const char* sym, const char* label, float top, float px) {
        const int p = findParam(sym);
        if (p < 0) return;
        const float cx = bx + x, cy = oy + y;
        Ctl c { uint32_t(p), CtlType::Knob, label, cx - R, cy - R, 2 * R, 2 * R };
        c.ax = cx; c.ay = cy; c.size = 2 * R; c.n = uint8_t(kind); c.bw = C; c.bh = px; c.look = Ctl::BfKnob;
        c.labelY = cy + top + 9;
        c.valueY = cy + top + 18 + 1 + 6.5f;
        L.ctls.push_back(c);
    };
    knob(ox, 70, 100, 40, 29, 3, "a_x", "X A", 46, 17);
    knob(ox, 70, 230, 40, 29, 3, "a_y", "Y A", 46, 17);
    knob(ox, 215, 170, 78, 60, 6, "a_cutoff", "CUTOFF", 84, 17);
    knob(ox, 355, 100, 40, 29, 3, "a_reso", "RES", 46, 17);
    knob(ox, 924, 100, 40, 29, 4, "b_x", "X B", 46, 17);
    knob(ox, 924, 230, 40, 29, 4, "b_y", "Y B", 46, 17);
    knob(ox, 779, 170, 78, 60, 7, "b_cutoff", "CUTOFF", 84, 17);
    knob(ox, 639, 100, 40, 29, 4, "b_reso", "RES", 46, 17);
    knob(ox, 80, 460, 40, 29, 5, "c_grit", "GRIT QTY", 46, 17);
    knob(ox, 220, 460, 40, 29, 5, "c_reso", "RES", 46, 17);
    knob(ox, 497, 452, 50, 38, 6, "c_cutoff", "CUTOFF", 56, 17);
    // HP / BP / LP switches (HP on top), labels left of them
    auto ftype = [&](float x, float y, const char* sym) {
        const int p = findParam(sym);
        if (p < 0) return;
        Ctl c { uint32_t(p), CtlType::Switch, nullptr, ox + x - 42, oy + y - 33, 56, 66 };
        c.ax = ox + x; c.ay = oy + y; c.size = 1; c.bw = 22; c.bh = 66; c.n = 3; c.labelX = ox + x - 28;
        c.look = Ctl::BfSwitch;
        L.ctls.push_back(c);
    };
    ftype(362, 248, "a_ftype");
    ftype(660, 248, "b_ftype");
    ftype(366, 450, "c_ftype");
    // SOURCE: GRITTY | WHITE (decision 9: two positions)
    if (const int p = findParam("c_source"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Switch, nullptr, ox + 740, oy + 440, 180, 100 };
        c.ax = ox + 820; c.ay = oy + 458; c.size = 1; c.bw = 96; c.bh = 24; c.n = 2; c.look = Ctl::BfSwitch;
        L.ctls.push_back(c);
    }
    // algorithm readouts (click = the list)
    for (int k = 0; k < 2; ++k)
        if (const int p = findParam(k ? "b_algo" : "a_algo"); p >= 0) {
            const float cx = ox + (k ? 779.f : 215.f), cy = oy + 330;
            Ctl c { uint32_t(p), CtlType::Select, nullptr, cx - 110, cy - 13, 220, 26 };
            c.ax = cx; c.ay = cy; c.size = 1; c.bw = 220; c.bh = 26; c.look = Ctl::Window;
            L.ctls.push_back(c);
        }
    B.digit[0] = { ox + 461 - 29, oy + 128 - 42, 58, 84 };
    B.digit[1] = { ox + 533 - 29, oy + 128 - 42, 58, 84 };
    B.program = { ox + 497 - 35, oy + 290 - 35, 70, 70 };
    // the expander: A (silver), B (white), C (black) columns
    static const char* const kCh[3] = { "a_", "b_", "c_" };
    for (int k = 0; k < 3; ++k) {
        const float x = 56 + 94.f * float(k);
        char sym[24];
        static const char* const kRows[3][2] = { { "attack", "ATTACK" }, { "decay", "DECAY" }, { "level", "LEVEL" } };
        for (int r = 0; r < 3; ++r) {
            std::snprintf(sym, sizeof sym, "%s%s", kCh[k], kRows[r][0]);
            knob(ex, x, 126 + 96.f * float(r), 30, 21, 3 + k, sym, kRows[r][1], 34, 14);
        }
        std::snprintf(sym, sizeof sym, "%smute", kCh[k]);
        if (const int p = findParam(sym); p >= 0) {
            Ctl c { uint32_t(p), CtlType::Toggle, "MUTE", ex + x - 24, oy + 392, 48, 88 };
            c.ax = ex + x; c.ay = oy + 436; c.size = 36; c.bh = 14; c.labelY = oy + 470;
            c.ledX = ex + x; c.prevX = oy + 404; c.look = Ctl::BfButton;
            L.ctls.push_back(c);
        }
    }
    if (const int p = findParam("env_sync"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Toggle, "ENV SYNC", ex + 48 - 30, oy + 508, 60, 66 };
        c.ax = ex + 48; c.ay = oy + 542; c.size = 24; c.bh = 12; c.labelY = oy + 566;
        c.ledX = ex + 48; c.prevX = oy + 518; c.look = Ctl::BfButton;
        L.ctls.push_back(c);
    }
    if (const int p = findParam("trig_mode"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Select, "MODE", ex + 148 - 34, oy + 538 - 13, 68, 26 };
        c.ax = ex + 148; c.ay = oy + 538; c.size = 1; c.bw = 68; c.bh = 26; c.labelY = oy + 566; c.look = Ctl::Window;
        L.ctls.push_back(c);
    }
    if (const int p = findParam("gain"); p >= 0) {
        Ctl c { uint32_t(p), CtlType::Knob, nullptr, ex + 248 - 22, oy + 534 - 22, 44, 44 };
        c.ax = ex + 248; c.ay = oy + 534; c.size = 44; c.n = 5; c.bw = 15; c.bh = 12; c.valueY = oy + 566;
        c.look = Ctl::BfKnob;
        L.ctls.push_back(c);
    }
    makeDrawer(L, oy + 580, true);
}

// Acida FX (DESIGN-GUIDE §3.1a; mockups/acida-fx.html): the silver panel in two rows split by
// the seam; DISTORTION | PCF FILTER | logo, COMPRESSOR | TEMPO DELAY | GAIN; items spread
// edge to edge in each section (space-between), each centred vertically
void makeAcidaFx(Layout& L)
{
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 358;
    L.panel = { px, py, pw, ph };
    const float x0 = px + 20, x1 = px + pw - 20, col3 = 165, gap = 6;
    const float x3 = x1 - col3, vr2 = x3 - gap, avail = vr2 - gap - x0 - 2 * gap - 1.5f;
    const float w1 = avail * 2.6f / 9.6f, vr1 = x0 + w1 + gap, x2 = vr1 + 1.5f + gap, w2 = vr2 - gap - x2;
    const float rowH = ph / 2;
    L.decos.push_back({ Deco::Seam, { px, py + rowH, pw, 1 } });
    enum K { Kn, On, Mode };
    struct It { K k; const char* sym; const char* label; float size; };
    struct Sec { int row; const char* title; float x, w; It items[8]; };
    const Sec kSecs[] = {
        { 0, "DISTORTION", x0, w1, { { On, "dist_on", "On", 0 }, { Kn, "dist_amount", "Amount", 54 }, { Kn, "dist_shape", "Shape", 54 } } },
        { 0, "PCF FILTER", x2, w2, { { On, "pcf_on", "On", 0 }, { Mode, "pcf_mode", "Mode", 0 }, { Kn, "pcf_wave", "Wave", 54 },
                                     { Kn, "pcf_freq", "Freq", 70 }, { Kn, "pcf_reso", "Reso", 54 }, { Kn, "pcf_amount", "Amount", 54 },
                                     { Kn, "pcf_decay", "Decay", 54 } } },
        { 1, "COMPRESSOR", x0, w1, { { On, "comp_on", "On", 0 }, { Kn, "comp_amount", "Amount", 54 }, { Kn, "comp_threshold", "Thresh", 54 } } },
        { 1, "TEMPO DELAY", x2, w2, { { On, "delay_on", "On", 0 }, { Mode, "delay_mode", "Mode", 0 }, { Kn, "delay_steps", "Steps", 54 },
                                      { Kn, "delay_send", "Send", 54 }, { Kn, "delay_feedback", "Feedback", 54 },
                                      { Kn, "delay_pan", "Pan", 54 } } },
    };
    for (int r = 0; r < 2; ++r)
        for (float vx : { vr1, vr2 }) L.decos.push_back({ Deco::VRule, { vx, py + r * rowH + 22, 1.5f, rowH - 44 } });
    for (const Sec& sc : kSecs) {
        const float ry = py + sc.row * rowH, cy = ry + 44 + (rowH - 44 - 10) / 2, ix0 = sc.x + 4, ix1 = sc.x + sc.w - 4;
        L.decos.push_back({ Deco::Heading, { ix0, ry + 14, 0, 0 }, sc.title });
        // item widths, then space-between
        int n = 0;
        float ws[8], total = 0;
        for (const It& it : sc.items) {
            if (!it.sym) break;
            ws[n] = it.k == Kn ? std::max(it.size, 70.f) : it.k == On ? 56.f : 160.f;
            total += ws[n++];
        }
        const float between = n > 1 ? (ix1 - ix0 - total) / float(n - 1) : 0.f;
        float x = ix0;
        for (int i = 0; i < n; ++i) {
            const It& it = sc.items[i];
            const float cx = x + ws[i] / 2;
            const int p = findParam(it.sym);
            if (p >= 0) switch (it.k) {
            case Kn: {
                const float t = cy - (13 + 10 + it.size + 10 + 13) / 2;
                addKnob(L, it.sym, it.label, cx, t + 6.5f, t + 23 + it.size / 2, t + 23 + it.size + 10 + 6.5f, it.size, ws[i]);
                break;
            }
            case On: { // label, red LED, silver key
                const float t = cy - 30;
                Ctl c { uint32_t(p), CtlType::Toggle, it.label, x, t, ws[i], 60 };
                c.ax = cx; c.ay = t + 13 + 10 + 9 + 8 + 10; c.size = 1; c.bw = 30; c.bh = 20; c.labelY = t + 6.5f;
                c.prevX = t + 13 + 10 + 4.5f; // the LED's centre
                L.ctls.push_back(c);
                break;
            }
            case Mode: { // label, [‹] LED window [›]
                const float t = cy - 26.5f;
                Ctl c { uint32_t(p), CtlType::Select, it.label, x, t, ws[i], 53 };
                c.ax = cx; c.ay = t + 13 + 10 + 15; c.size = 1; c.bw = ws[i]; c.bh = 30; c.labelY = t + 6.5f;
                L.ctls.push_back(c);
                break;
            }
            }
            x += ws[i] + between;
        }
    }
    L.decos.push_back({ Deco::Logo, { x3, py, col3, rowH } });
    const float gy = py + rowH + (rowH - 13 - 10 - 46 - 10 - 13) / 2;
    addKnob(L, "gain", "Gain", x3 + col3 / 2, gy + 6.5f, gy + 23 + 23, gy + 23 + 46 + 10 + 6.5f, 46, 80);
    makeDrawer(L, py + ph);
}

// Erica Dredrum (DESIGN-GUIDE §3.6; mockups/dredrum.html): wordmark, OLED, MODE, GAIN on top;
// three oscillator columns under white rules; pixel decorations
void makeDredrum(Layout& L)
{
    const float px = kChromeInset, py = kChromeHeaderH + kChromeInset, pw = kWidth - 2 * kChromeInset, ph = 478;
    L.panel = { px, py, pw, ph };
    static const float kPix[][2] = { { 262, 18 }, { 271, 18 }, { 271, 27 }, { 736, 14 }, { 736, 23 }, { 745, 23 }, { 520, 160 },
                                     { 529, 160 }, { 529, 169 }, { 538, 169 }, { 1000, 165 }, { 1009, 156 }, { 1009, 165 } };
    for (const auto& q : kPix) L.decos.push_back({ Deco::Pixel, { px + q[0], py + q[1], 7, 7 } });
    const float x0 = px + 28, x1 = px + pw - 28, y0 = py + 20;
    L.decos.push_back({ Deco::Logo, { x0, y0, 220, 150 } });
    L.oled = { x0 + 220 + 30, y0 + 7, 460, 136 };
    { // MODE: TRIG / GATE bat toggle
        const int p = findParam("trig_mode");
        const float cx = px + 845, top = y0 + (150 - 67) / 2;
        if (p >= 0) {
            Ctl c { uint32_t(p), CtlType::Switch, "Mode", cx - 40, top, 80, 67 };
            c.ax = cx + 19; c.ay = top + 21 + 23; c.size = 1; c.bw = 18; c.bh = 46; c.labelY = top + 6.5f; c.labelX = cx + 2;
            c.look = Ctl::ErBat;
            L.ctls.push_back(c);
        }
    }
    if (const int p = findParam("gain"); p >= 0) {
        const float cx = px + 987, top = y0 + (150 - 94) / 2;
        Ctl c { uint32_t(p), CtlType::Knob, "Gain", cx - 40, top, 80, 94 };
        c.ax = cx; c.ay = top + 19 + 29; c.size = 58; c.labelY = top + 6.5f; c.valueY = top + 19 + 58 + 6 + 5.5f;
        c.look = Ctl::ErKnob;
        L.ctls.push_back(c);
    }
    // the oscillator columns
    const float cy0 = y0 + 150 + 16, colW = (x1 - x0 - 44) / 3;
    for (int k = 0; k < 3; ++k) {
        const float cx0 = x0 + k * (colW + 22);
        L.decos.push_back({ Deco::HRule, { cx0, cy0, colW, 1.5f } });
        char name[8];
        std::snprintf(name, sizeof name, "OSC %d", k + 1);
        static char names[3][8];
        std::memcpy(names[k], name, sizeof name);
        L.decos.push_back({ Deco::RuleText, { cx0, cy0 + 12 + 13, 0, 0 }, names[k] });
        char sym[24];
        auto P = [&](const char* f) { std::snprintf(sym, sizeof sym, "osc%d_%s", k + 1, f); return findParam(sym); };
        if (const int p = P("wave"); p >= 0) { // [o] OLED window [o]
            const float wcx = cx0 + colW - 13 - 6 - 48;
            Ctl c { uint32_t(p), CtlType::Select, nullptr, wcx - 48 - 19, cy0 + 12, 96 + 38, 26 };
            c.ax = wcx; c.ay = cy0 + 12 + 13; c.size = 1; c.bw = 96; c.bh = 26; c.look = Ctl::LxrSel;
            L.ctls.push_back(c);
        }
        static const char* const kBig[4][2] = { { "pitch", "Pitch" }, { "drop", "Drop" }, { "decay", "Decay" }, { "level", "Level" } };
        const float ky = cy0 + 12 + 26 + 12;
        for (int i = 0; i < 4; ++i)
            if (const int p = P(kBig[i][0]); p >= 0) {
                const float cx = cx0 + (i + 0.5f) * colW / 4;
                Ctl c { uint32_t(p), CtlType::Knob, kBig[i][1], cx - colW / 8, ky, colW / 4, 106 };
                c.ax = cx; c.ay = ky + 19 + 35; c.size = 70; c.labelY = ky + 6.5f; c.valueY = ky + 19 + 70 + 6 + 5.5f;
                c.look = Ctl::ErKnob;
                L.ctls.push_back(c);
            }
        // bottom row: Octave, Semi (knobs), [Click], Track, Mute (buttons); bottoms aligned
        struct B { const char* f; const char* label; bool knob; };
        std::vector<B> row = { { "octave", "Octave", true }, { "semi", "Semi", true } };
        if (k == 2) row.push_back({ nullptr, "Click", false });
        row.push_back({ "track", "Track", false });
        row.push_back({ "mute", "Mute", false });
        const float by = ky + 106 + 12, bottom = by + 76;
        for (size_t i = 0; i < row.size(); ++i) {
            const int p = row[i].f ? P(row[i].f) : findParam("click");
            if (p < 0) continue;
            const float cx = cx0 + (float(i) + 0.5f) * colW / float(row.size());
            if (row[i].knob) {
                Ctl c { uint32_t(p), CtlType::Knob, row[i].label, cx - 30, by, 60, 76 };
                c.ax = cx; c.ay = by + 22; c.size = 44; c.labelY = by + 48 + 6.5f; c.valueY = by + 48 + 13 + 4 + 5.5f;
                c.n = 1; c.look = Ctl::ErKnob; // n = 1: label under the knob
                L.ctls.push_back(c);
            } else {
                const float top = bottom - 63;
                Ctl c { uint32_t(p), CtlType::Toggle, row[i].label, cx - 28, top, 56, 50 };
                c.ax = cx; c.ay = top + 13; c.size = 26; c.labelY = top + 26 + 8 + 6.5f; c.look = Ctl::ErButton;
                L.ctls.push_back(c);
            }
        }
    }
    makeDrawer(L, py + ph, true);
}

void makeNewLayout(Layout& L)
{
    switch (voiceDef().voice) {
    case maquina::kAcidaVoice: makeAcida(L); break;
    case maquina::kAcidaFxVoice: makeAcidaFx(L); break;
    case maquina::kKlangVoice: makeKlang(L); break;
    case maquina::kKlongVoice:
    case maquina::kKlongFxVoice: makeKlong(L); break;
    case maquina::kBl101Voice:
    case maquina::kBl101FxVoice: makeGraphene(L); break;
    case maquina::kLa50Voice: makeLa50New(L); break;
    case maquina::kStringsVoice: makeStrings(L); break;
    case maquina::kOneiroiVoice:
    case maquina::kOneiroiFxVoice: makeOneiroi(L); break;
    case maquina::kPlethoraVoice: makePlethora(L); break;
    case maquina::kDredrumVoice: makeDredrum(L); break;
    default:
        if (maquina::isLxrVoice(voiceDef().voice)) makeLxr(L);
        break;
    }
}
