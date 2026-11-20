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
    float x = 14 + monoWidth(headerTitle().c_str(), 13, 0.12f) + gap + 28 + gap;
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
    // the preset field gets 250 px, less when the credit line would not fit
    const Credit cr = creditOf(voiceDef().voice);
    const float fixed = sq + gap + gap + sq + gap + saveW + gap + initW;
    float nameW = 250;
    g.credit = cr.longText;
    const float room = r - x - fixed - 2 * gap;
    if (nameW + monoWidth(cr.longText, 11) > room) {
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
    const float gap = 26, cw = (kWidth - 16 - x - gap * float(n - 1)) / float(std::max(1, n));
    for (int i = 0; i < maquina::kNumRandGroups; ++i) {
        R.cell[i] = { x, cy - 25, R.used[i] ? cw : 0.f, R.used[i] ? 50.f : 0.f };
        if (R.used[i]) x += cw + gap;
    }
    L.height = y + kChromeDrawerH;
}

// ---------------------------------------------------------------------------
// fixed boards

// a knob column: label bottom line, knob centre, value centre; hit area = the column
void addKnob(Layout& L, const char* sym, const char* label, float cx, float labelY, float ky, float valueY, float size,
             float colW)
{
    const int p = findParam(sym);
    if (p < 0) return;
    Ctl c { uint32_t(p), CtlType::Knob, label, cx - colW / 2, labelY - 28, colW, valueY + 10 - (labelY - 28) };
    c.ax = cx; c.ay = ky; c.size = size; c.labelY = labelY; c.valueY = valueY;
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

    const float labelY = py + 65, ky = py + 115, valueY = py + 172;
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

void makeNewLayout(Layout& L)
{
    switch (voiceDef().voice) {
    case maquina::kAcidaVoice: makeAcida(L); break;
    default: break;
    }
}
