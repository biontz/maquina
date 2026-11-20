// Controls.cpp - drawing of the new-look controls and chrome parts (NanoVG only, no
// bitmaps). No hit-testing here: DrumUI.cpp keeps that.
//
// Included by DrumUI.cpp inside namespace maquina_ui, after Theme.hpp.
// SPDX-License-Identifier: GPL-3.0-only

inline Color rgba(Rgb c, int a = 255) { return Color(c.r, c.g, c.b, float(a) / 255.f); }
inline Rgb mix(Rgb a, Rgb b, float t) // a -> b
{
    return { int(std::lround(a.r + (b.r - a.r) * t)), int(std::lround(a.g + (b.g - a.g) * t)),
             int(std::lround(a.b + (b.b - a.b) * t)) };
}

// text that tolerates empty strings (DPF asserts on them)
inline void vText(NanoVG& v, float x, float y, const char* s)
{
    if (s && *s) v.text(x, y, s, nullptr);
}
inline float vTextWidth(NanoVG& v, const char* s)
{
    if (!s || !*s) return 0.f;
    Rectangle<float> b;
    v.textBounds(0, 0, s, nullptr, b);
    return b.getWidth();
}

constexpr float kKnobA0 = float(0.75 * M_PI), kKnobA1 = float(2.25 * M_PI); // 270°, 7:30 .. 4:30

// UTF-8 code points in s (the monospace chrome font: width = count * advance)
inline int utf8Len(const char* s)
{
    int n = 0;
    for (; *s; ++s) n += (uint8_t(*s) & 0xc0) != 0x80;
    return n;
}

// Fill a shape with a radial gradient of several stops (CSS radial-gradient). NanoVG
// gradients have two colours: the shape is filled with the last stop, then each band is
// laid over it from the outside in, fading out to the stop below it.
template <class Shape>
void fillRadialStops(NanoVG& v, Shape shape, float gx, float gy, float r, const float* stops, const Rgb* cols,
                     int n)
{
    v.beginPath();
    shape();
    v.fillColor(rgba(cols[n - 1]));
    v.fill();
    for (int i = n - 2; i >= 0; --i) {
        v.beginPath();
        shape();
        v.fillPaint(v.radialGradient(gx, gy, stops[i] * r, stops[i + 1] * r, rgba(cols[i]), rgba(cols[i], 0)));
        v.fill();
    }
}

// a soft round shadow under a circle of radius r
inline void circleShadow(NanoVG& v, float cx, float cy, float r, float blur, int alpha)
{
    v.beginPath();
    v.circle(cx, cy, r + blur);
    v.fillPaint(v.radialGradient(cx, cy, r - blur * 0.5f, r + blur, Color(0, 0, 0, alpha / 255.f), Color(0, 0, 0, 0.f)));
    v.fill();
}

// soft rectangular shadow (CSS box-shadow 0 dy blur)
inline void boxShadow(NanoVG& v, float x, float y, float w, float h, float r, float dy, float blur, int alpha)
{
    v.beginPath();
    v.rect(x - blur, y + dy - blur, w + 2 * blur, h + 2 * blur);
    v.fillPaint(v.boxGradient(x, y + dy, w, h, r + blur * 0.5f, blur, Color(0, 0, 0, alpha / 255.f), Color(0, 0, 0, 0.f)));
    v.fill();
}

// ---------------------------------------------------------------------------
// knob: ring (rest + value arc), skirt, cap, pointer. t = 0..1, size = outer diameter.

void drawKnobStyled(NanoVG& v, const KnobStyle& k, float cx, float cy, float size, float t, bool hot)
{
    const float rr = size / 2 - k.ringW / 2;
    const float av = kKnobA0 + std::clamp(t, 0.f, 1.f) * (kKnobA1 - kKnobA0);
    v.lineCap(NanoVG::BUTT);
    v.beginPath();
    v.arc(cx, cy, rr, kKnobA0, kKnobA1, NanoVG::CW);
    v.strokeColor(rgba(k.rest, k.restA));
    v.strokeWidth(k.ringW);
    v.stroke();
    if (av > kKnobA0 + 0.001f) {
        v.beginPath();
        v.arc(cx, cy, rr, kKnobA0, av, NanoVG::CW);
        v.strokeColor(rgba(hot ? mix(k.arc, { 0xff, 0xff, 0xff }, 0.25f) : k.arc));
        v.stroke();
    }
    const float rc = size * (0.5f - k.capInset);
    circleShadow(v, cx, cy + 4, rc + k.skirtW, 8, 100);
    v.beginPath();
    v.circle(cx, cy, rc + k.skirtW);
    v.fillColor(rgba(k.skirt));
    v.fill();
    // cap lit from 38 % / 32 % of its box (CSS: farthest corner = radius)
    const float gx = cx - 0.24f * rc, gy = cy - 0.36f * rc, gr = 1.84f * rc;
    static const float kStops[4] = { 0.f, 0.38f, 0.78f, 1.f };
    fillRadialStops(v, [&] { v.circle(cx, cy, rc); }, gx, gy, gr, kStops, k.cap, 4);
    // pointer: from 6 % to 36 % of the cap's diameter, from its edge
    const float ca = std::cos(av), sa = std::sin(av);
    const float r0 = rc * 0.88f, r1 = rc * 0.28f;
    v.beginPath();
    v.moveTo(cx + ca * r0, cy + sa * r0);
    v.lineTo(cx + ca * r1, cy + sa * r1);
    v.strokeColor(rgba(k.pointer));
    v.strokeWidth(std::max(2.f, size * 0.05f));
    v.lineCap(NanoVG::ROUND);
    v.stroke();
}

// ---------------------------------------------------------------------------
// 303 silver

// brushed silver: vertical gradient + fine horizontal hairlines, top highlight, drop shadow
void drawSilverPanel(NanoVG& v, float x, float y, float w, float h, float r)
{
    boxShadow(v, x, y, w, h, r, 6, 18, 128);
    v.beginPath();
    v.roundedRect(x, y, w, h, r);
    v.fillPaint(v.linearGradient(x, y, x, y + h, rgba({ 0xde, 0xde, 0xda }), rgba({ 0xc6, 0xc6, 0xc2 })));
    v.fill();
    v.save();
    v.scissor(x, y + 1, w, h - 2);
    for (float ly = y + 1; ly < y + h - 1; ly += 2) {
        v.beginPath();
        v.rect(x, ly, w, 1);
        v.fillColor(Color(255, 255, 255, 0.05f));
        v.fill();
        v.beginPath();
        v.rect(x, ly + 1, w, 1);
        v.fillColor(Color(0, 0, 0, 0.025f));
        v.fill();
    }
    v.restore();
    v.beginPath(); // inset top highlight
    v.moveTo(x + r, y + 0.5f);
    v.lineTo(x + w - r, y + 0.5f);
    v.strokeColor(Color(255, 255, 255, 0.7f));
    v.strokeWidth(1);
    v.stroke();
}

// vertical slide switch: dark slot, light lever at position pos of n (0 = top)
void drawSlideSwitchV(NanoVG& v, float x, float y, float w, float h, int n, int pos, bool hot)
{
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillColor(rgba({ 0x1d, 0x1d, 0x1b }));
    v.fill();
    v.beginPath(); // inset shadow
    v.roundedRect(x, y, w, h, 3);
    v.fillPaint(v.boxGradient(x, y + 2, w, h, 3, 3, Color(0, 0, 0, 0.f), Color(0, 0, 0, 0.6f)));
    v.fill();
    const float lh = 16, travel = h - 6 - lh;
    const float ly = y + 3 + (n > 1 ? travel * float(pos) / float(n - 1) : 0.f);
    v.beginPath();
    v.roundedRect(x + 2, ly, w - 4, lh, 2);
    v.fillPaint(v.linearGradient(x, ly, x, ly + lh, rgba(hot ? Rgb { 0xff, 0xff, 0xff } : Rgb { 0xf4, 0xf4, 0xf2 }),
                                 rgba({ 0x9e, 0x9e, 0x9a })));
    v.fill();
}
// the y of position k's lever centre (for labels and hit-testing)
inline float slideSwitchPosY(float y, float h, int n, int k)
{
    const float lh = 16, travel = h - 6 - lh;
    return y + 3 + (n > 1 ? travel * float(k) / float(n - 1) : 0.f) + lh / 2;
}

// small dark step button with a chevron (‹ or ›) on the panel
void drawStepButton(NanoVG& v, float x, float y, float w, float h, bool right, Rgb fillC, Rgb lineC, Rgb glyph)
{
    v.beginPath();
    v.roundedRect(x + 0.5f, y + 0.5f, w - 1, h - 1, 5);
    v.fillColor(rgba(fillC));
    v.fill();
    v.strokeColor(rgba(lineC));
    v.strokeWidth(1);
    v.stroke();
    const float mx = x + w / 2, my = y + h / 2, d = right ? 1.f : -1.f;
    v.beginPath();
    v.moveTo(mx - 1.5f * d, my - 3.5f);
    v.lineTo(mx + 1.5f * d, my);
    v.lineTo(mx - 1.5f * d, my + 3.5f);
    v.strokeColor(rgba(glyph));
    v.strokeWidth(1.5f);
    v.lineCap(NanoVG::ROUND);
    v.lineJoin(NanoVG::ROUND);
    v.stroke();
}

// LED display window: dark inset, glowing text (font set by the caller)
void drawLedWindow(NanoVG& v, float x, float y, float w, float h, const char* s, Rgb led, float sizePx)
{
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillColor(rgba({ 0x1a, 0x1d, 0x1a }));
    v.fill();
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillPaint(v.boxGradient(x, y + 2, w, h, 3, 4, Color(0, 0, 0, 0.f), Color(0, 0, 0, 0.7f)));
    v.fill();
    // fit: shrink the font until the text fits
    float px = sizePx;
    fontPx(v, px);
    while (px > 8 && vTextWidth(v, s) > w - 8) fontPx(v, px -= 0.5f);
    v.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
    v.fontBlur(3);
    v.fillColor(rgba(led, 150));
    vText(v, x + w / 2, y + h / 2 + 1, s);
    v.fontBlur(0);
    v.fillColor(rgba(led));
    vText(v, x + w / 2, y + h / 2 + 1, s);
}

// the generic acid-house smiley in Maquina's own drawing (mockup SVG, viewBox 0 0 100 100)
void drawSmiley(NanoVG& v, float cx, float cy, float size, Rgb ink)
{
    const float s = size / 100.f, ox = cx - size / 2, oy = cy - size / 2;
    auto X = [&](float u) { return ox + u * s; };
    auto Y = [&](float u) { return oy + u * s; };
    v.beginPath();
    v.circle(cx, cy, 46 * s);
    v.strokeColor(rgba(ink));
    v.strokeWidth(5 * s);
    v.stroke();
    v.beginPath();
    v.ellipse(X(36.5f), Y(38), 6.2f * s, 10.5f * s);
    v.ellipse(X(63.5f), Y(38), 6.2f * s, 10.5f * s);
    v.fillColor(rgba(ink));
    v.fill();
    v.beginPath(); // the smile: a crescent
    v.moveTo(X(19), Y(57));
    v.bezierTo(X(26), Y(78), X(74), Y(78), X(81), Y(57));
    v.bezierTo(X(80), Y(56), X(79), Y(56), X(78), Y(57));
    v.bezierTo(X(70), Y(71), X(30), Y(71), X(22), Y(57));
    v.bezierTo(X(21), Y(56), X(20), Y(56), X(19), Y(57));
    v.closePath();
    v.fill();
    v.beginPath(); // the curled ends
    v.moveTo(X(16.5f), Y(54));
    v.lineTo(X(22.5f), Y(59.5f));
    v.moveTo(X(83.5f), Y(54));
    v.lineTo(X(77.5f), Y(59.5f));
    v.strokeWidth(3.2f * s);
    v.lineCap(NanoVG::ROUND);
    v.stroke();
}

// ---------------------------------------------------------------------------
// chrome (header + random drawer)

// a chrome button: dark fill, thin border, radius 6
void drawChromeButton(NanoVG& v, float x, float y, float w, float h, const char* label, bool active, bool enabled = true)
{
    v.beginPath();
    v.roundedRect(x + 0.5f, y + 0.5f, w - 1, h - 1, 6);
    v.fillColor(rgba(kChromeBtn));
    v.fill();
    v.strokeColor(rgba(active ? theme().accent : kChromeBtnLine));
    v.strokeWidth(1);
    v.stroke();
    if (!label || !*label) return;
    fontPx(v, 12);
    v.textAlign(NanoVG::ALIGN_CENTER | NanoVG::ALIGN_MIDDLE);
    v.fillColor(rgba(enabled ? kChromeText : kChromeDisabled));
    v.text(x + w / 2, y + h / 2 + 1, label, nullptr);
}

// padlock, 12 x 14 (mockup SVG); open: the shackle swings off to the right
void drawPadlock12(NanoVG& v, float x, float y, bool closed, Color c)
{
    v.beginPath();
    v.roundedRect(x + 1, y + 6, 10, 7.5f, 1.5f);
    v.fillColor(c);
    v.fill();
    v.beginPath();
    v.moveTo(x + 3.2f, y + 6);
    v.lineTo(x + 3.2f, y + 4.2f);
    if (closed) {
        v.arc(x + 6, y + 4.2f, 2.8f, float(M_PI), float(2 * M_PI), NanoVG::CW);
        v.lineTo(x + 8.8f, y + 6);
    } else {
        v.arc(x + 6, y + 4.2f, 2.8f, float(M_PI), float(1.72 * M_PI), NanoVG::CW);
    }
    v.strokeColor(c);
    v.strokeWidth(1.6f);
    v.lineCap(NanoVG::BUTT);
    v.stroke();
}

// range bar: track 6 px, accent fill min..max; locked = diagonal hatch over the whole bar
void drawRangeBar(NanoVG& v, float x, float yc, float w, int lo, int hi, bool locked, bool handles, int hotHandle)
{
    const float h = 6, y = yc - h / 2;
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillColor(rgba(kChromeBtnLine));
    v.fill();
    if (locked) {
        v.save();
        v.beginPath();
        v.roundedRect(x, y, w, h, 3);
        v.fillColor(rgba(theme().hatchB));
        v.fill();
        v.scissor(x, y, w, h);
        v.beginPath();
        for (float sx = x - h; sx < x + w + h; sx += 8) { // 135° stripes, 4 px on / 4 off
            v.moveTo(sx, y + h);
            v.lineTo(sx + 4, y + h);
            v.lineTo(sx + 4 + h, y);
            v.lineTo(sx + h, y);
            v.closePath();
        }
        v.fillColor(rgba(theme().hatchA));
        v.fill();
        v.restore();
        return;
    }
    const float x0 = x + w * lo / 100.f, x1 = x + w * hi / 100.f;
    v.beginPath();
    v.roundedRect(x0, y, std::max(h, x1 - x0), h, 3);
    v.fillColor(rgba(theme().accent));
    v.fill();
    if (!handles) return;
    for (int k = 0; k < 2; ++k) {
        const float hx = std::clamp(k ? x1 : x0, x + 3, x + w - 3);
        v.beginPath();
        v.circle(hx, yc, 5);
        v.fillColor(rgba(hotHandle == k ? theme().accentText : kChromeText));
        v.fill();
        v.strokeColor(rgba(kChromeBg));
        v.strokeWidth(1.5f);
        v.stroke();
    }
}
