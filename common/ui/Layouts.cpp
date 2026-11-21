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
    float x = 16 + 104 + 26;
    int n = 0;
    for (int i = 0; i < maquina::kNumRandGroups; ++i)
        n += (R.used[i] = maquina::randGroupUsed(voiceDef(), i));
    // open: [▴] at the right end; folded: one bar [RANDOM] [BACK] ... [RANGES ▾]
    R.openToggle = { kWidth - 16 - 26, cy - 13, 26, 26 };
    R.foldRandom = { 16, y + 10, 104, 26 };
    R.foldBack = { 16 + 104 + 10, y + 10, 104, 26 };
    R.foldToggle = { kWidth - 16 - 112, y + 10, 112, 26 };
    const float right = R.openToggle.x - 22;
    const float gap = 26, cw = (right - x - gap * float(n - 1)) / float(std::max(1, n));
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

void makeNewLayout(Layout& L)
{
    switch (voiceDef().voice) {
    case maquina::kAcidaVoice: makeAcida(L); break;
    case maquina::kKlangVoice: makeKlang(L); break;
    case maquina::kKlongVoice:
    case maquina::kKlongFxVoice: makeKlong(L); break;
    default: break;
    }
}
