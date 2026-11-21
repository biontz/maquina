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
    const float out = size < 45 ? k.ringOutSmall : k.ringOut;
    const float rr = size / 2 + out - k.ringW / 2;
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
    const float body = k.knurl ? size / 2 : rc + k.skirtW;
    circleShadow(v, cx, cy + k.shadowDy, body, k.shadowBlur, k.shadowA);
    v.beginPath();
    v.circle(cx, cy, body);
    v.fillColor(rgba(k.skirt));
    v.fill();
    if (k.knurl) { // ridges: every other 6° wedge
        v.beginPath();
        for (int i = 0; i < 30; ++i) {
            const float a0 = float(i) * float(M_PI) / 15.f, a1 = a0 + float(M_PI) / 30.f;
            v.moveTo(cx, cy);
            v.arc(cx, cy, body, a0, a1, NanoVG::CW);
            v.closePath();
        }
        v.fillColor(rgba(k.skirt2));
        v.fill();
    }
    if (k.capLineW > 0) {
        v.beginPath();
        v.circle(cx, cy, rc + k.capLineW);
        v.fillColor(rgba(k.capLine));
        v.fill();
    }
    // cap lit from (capGx, capGy) of its box; CSS radius: the farthest corner
    const float gx = cx - rc + 2 * rc * k.capGx, gy = cy - rc + 2 * rc * k.capGy;
    const float dx = 2 * rc * std::max(k.capGx, 1 - k.capGx), dy = 2 * rc * std::max(k.capGy, 1 - k.capGy);
    fillRadialStops(v, [&] { v.circle(cx, cy, rc); }, gx, gy, std::sqrt(dx * dx + dy * dy), k.capStop, k.cap, k.capN);
    const float ca = std::cos(av), sa = std::sin(av);
    const float r0 = rc - k.ptrFrom * 2 * rc, r1 = r0 - k.ptrLen * 2 * rc;
    v.beginPath();
    v.moveTo(cx + ca * r0, cy + sa * r0);
    v.lineTo(cx + ca * r1, cy + sa * r1);
    v.strokeColor(rgba(k.pointer));
    v.strokeWidth(std::max(2.f, std::min(3.f, size * 0.05f)));
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

// ---------------------------------------------------------------------------
// Mutable light grey

void drawMutablePanel(NanoVG& v, float x, float y, float w, float h, float r)
{
    boxShadow(v, x, y, w, h, r, 6, 18, 128);
    v.beginPath();
    v.roundedRect(x, y, w, h, r);
    v.fillPaint(v.linearGradient(x, y, x, y + h, rgba({ 0xec, 0xec, 0xea }), rgba({ 0xdf, 0xdf, 0xdc })));
    v.fill();
    v.beginPath();
    v.moveTo(x + r, y + 0.5f);
    v.lineTo(x + w - r, y + 0.5f);
    v.strokeColor(rgba({ 0xff, 0xff, 0xff }));
    v.strokeWidth(1);
    v.stroke();
    // black screws in the corners
    v.beginPath();
    for (float sx : { x + 16.5f, x + w - 16.5f })
        for (float sy : { y + 16.5f, y + h - 16.5f }) v.circle(sx, sy, 4.5f);
    v.fillColor(rgba({ 0x1a, 0x1a, 0x1a }));
    v.fill();
}

// round black push button with a light bezel (30 px)
void drawRoundButton(NanoVG& v, float cx, float cy, bool hot)
{
    circleShadow(v, cx, cy + 2, 18, 4, 90);
    v.beginPath();
    v.circle(cx, cy, 18);
    v.fillColor(rgba({ 0xbd, 0xbd, 0xb8 }));
    v.fill();
    v.beginPath();
    v.circle(cx, cy, 15);
    v.fillPaint(v.radialGradient(cx - 3, cy - 5, 0, 18, rgba(hot ? Rgb { 0x55, 0x55, 0x55 } : Rgb { 0x3a, 0x3a, 0x3a }),
                                 rgba({ 0x0e, 0x0e, 0x0e })));
    v.fill();
}

// LED, 12 px: off = grey, on = lit in its colour with a glow
void drawLed(NanoVG& v, float cx, float cy, bool on, Rgb c)
{
    if (on) {
        v.beginPath();
        v.circle(cx, cy, 15);
        v.fillPaint(v.radialGradient(cx, cy, 4, 15, rgba(c, 150), rgba(c, 0)));
        v.fill();
        static const float kStops[3] = { 0.f, 0.55f, 1.f };
        const Rgb cols[3] = { mix(c, { 0xff, 0xff, 0xff }, 0.7f), c, mix(c, { 0, 0, 0 }, 0.35f) };
        fillRadialStops(v, [&] { v.circle(cx, cy, 6); }, cx - 1.2f, cy - 1.8f, 7.5f, kStops, cols, 3);
    } else {
        v.beginPath();
        v.circle(cx, cy, 6);
        v.fillColor(rgba({ 0xa3, 0xa3, 0x9e }));
        v.fill();
        v.beginPath();
        v.circle(cx, cy, 6);
        v.fillPaint(v.radialGradient(cx, cy + 1, 3, 6.5f, Color(0, 0, 0, 0.f), Color(0, 0, 0, 0.35f)));
        v.fill();
    }
}

// colour dot (14 px, white border); selected: an ink ring around it, alt: a thinner one
void drawColourDot(NanoVG& v, float cx, float cy, Rgb c, bool selected, bool alt)
{
    if (selected || alt) {
        v.beginPath();
        v.circle(cx, cy, alt && !selected ? 9 : 10);
        v.fillColor(rgba({ 0x1a, 0x1a, 0x1a }));
        v.fill();
    }
    v.beginPath();
    v.circle(cx, cy, 8);
    v.fillColor(Color(0, 0, 0, 0.15f));
    v.fill();
    v.beginPath();
    v.circle(cx, cy, 7);
    v.fillColor(rgba({ 0xff, 0xff, 0xff }));
    v.fill();
    v.beginPath();
    v.circle(cx, cy, 5);
    v.fillColor(rgba(c));
    v.fill();
}

// dark window (a selector's value, click = list): text + ▾
void drawDarkWindow(NanoVG& v, float x, float y, float w, float h, const char* s, bool hot)
{
    v.beginPath();
    v.roundedRect(x, y, w, h, 4);
    v.fillColor(rgba(hot ? Rgb { 0x2a, 0x2a, 0x2a } : Rgb { 0x1a, 0x1a, 0x1a }));
    v.fill();
    // a long name: a slightly smaller font first, then cut with an ellipsis (small zooms
    // measure a little wider than 100 %, so a name that fits there can overflow here)
    const float room = w - 34;
    float px = 12.5f;
    fontPx(v, px);
    for (; px > 11.f && vTextWidth(v, s) > room; px -= 0.5f) fontPx(v, px - 0.5f);
    std::string fit = s ? s : "";
    if (vTextWidth(v, fit.c_str()) > room) {
        while (!fit.empty() && vTextWidth(v, (fit + "\xe2\x80\xa6").c_str()) > room) {
            fit.pop_back();
            while (!fit.empty() && (uint8_t(fit.back()) & 0xc0) == 0x80) fit.pop_back(); // whole UTF-8 chars
            if (!fit.empty() && (uint8_t(fit.back()) & 0xc0) == 0xc0) fit.pop_back();
        }
        while (!fit.empty() && fit.back() == ' ') fit.pop_back();
        fit += "\xe2\x80\xa6";
    }
    s = fit.c_str();
    const float tw = std::min(vTextWidth(v, s), room);
    const float tx = x + (w - tw - 18) / 2;
    v.save();
    v.scissor(x + 4, y, w - 26, h);
    v.textAlign(NanoVG::ALIGN_LEFT | NanoVG::ALIGN_MIDDLE);
    v.fillColor(rgba({ 0xf2, 0xf2, 0xee }));
    vText(v, tx, y + h / 2 + 1, s);
    v.restore();
    const float ax = tx + tw + 12, ay = y + h / 2;
    v.beginPath();
    v.moveTo(ax - 4, ay - 2);
    v.lineTo(ax + 4, ay - 2);
    v.lineTo(ax, ay + 3);
    v.closePath();
    v.fillColor(rgba({ 0x8c, 0x8c, 0x8c }));
    v.fill();
}

// dotted line (2 px dots, 4 px pitch), vertical or horizontal
void drawDotted(NanoVG& v, float x0, float y0, float x1, float y1, Rgb c)
{
    const float len = std::hypot(x1 - x0, y1 - y0);
    const int n = int(len / 4);
    v.beginPath();
    for (int i = 0; i <= n; ++i) {
        const float t = n ? float(i) / float(n) : 0.f;
        v.rect(x0 + (x1 - x0) * t - 1, y0 + (y1 - y0) * t - 1, 2, 2);
    }
    v.fillColor(rgba(c));
    v.fill();
}

// ---------------------------------------------------------------------------
// LXR black (mockups/kick-lxr.html)

constexpr Rgb kLxrRed { 0xff, 0x2a, 0x22 };     // display digits, envelope curves
constexpr Rgb kLxrValue { 0xff, 0x3b, 0x30 };   // values, selector windows
constexpr Rgb kLxrTime { 0xff, 0x6a, 0x60 };    // envelope time text
constexpr Rgb kLxrLine { 0x6a, 0x6a, 0x6a };    // section outlines, page-button rules
constexpr Rgb kLxrPanelBg { 0x11, 0x11, 0x12 }; // behind section titles

// matte black panel: radial ellipse from the top, top highlight, drop shadow
void drawLxrPanel(NanoVG& v, float x, float y, float w, float h, float r)
{
    boxShadow(v, x, y, w, h, r, 8, 22, 150);
    v.beginPath();
    v.roundedRect(x, y, w, h, r);
    v.fillColor(rgba(kLxrPanelBg));
    v.fill();
    // CSS radial-gradient(ellipse at 50% -20%, #1d1d1e, #111112 60%): squashed circle
    v.save();
    v.scale(1.f, h / w);
    const float cy = (y - 0.2f * h) * w / h;
    v.beginPath();
    v.roundedRect(x, y * w / h, w, h * w / h, r);
    v.fillPaint(v.radialGradient(x + w / 2, cy, 0, 0.6f * w * 0.75f, rgba({ 0x1d, 0x1d, 0x1e }), rgba(kLxrPanelBg)));
    v.fill();
    v.restore();
    v.beginPath();
    v.moveTo(x + r, y + 0.5f);
    v.lineTo(x + w - r, y + 0.5f);
    v.strokeColor(Color(1.f, 1.f, 1.f, 0.07f));
    v.strokeWidth(1);
    v.stroke();
}

// text with a soft glow of its own colour (CSS text-shadow 0 0 blur)
void glowText(NanoVG& v, float x, float y, const char* s, Rgb c, float blur, int glowA)
{
    if (!s || !*s) return;
    v.fontBlur(blur);
    v.fillColor(rgba(c, glowA));
    v.text(x, y, s, nullptr);
    v.text(x, y, s, nullptr);
    v.fontBlur(0);
    v.fillColor(rgba(c));
    v.text(x, y, s, nullptr);
}

// section: 1.5 px outline, radius 7; titleW > 0: the title's room on the top edge (the line
// is cut there; the caller draws the title)
void drawLxrBox(NanoVG& v, float x, float y, float w, float h, float titleW)
{
    v.beginPath();
    v.roundedRect(x + 0.75f, y + 0.75f, w - 1.5f, h - 1.5f, 7);
    v.strokeColor(rgba(kLxrLine));
    v.strokeWidth(1.5f);
    v.stroke();
    if (titleW <= 0) return;
    v.beginPath();
    v.rect(x + w / 2 - titleW / 2 - 8, y - 2, titleW + 16, 4);
    v.fillColor(rgba(kLxrPanelBg));
    v.fill();
}

// dot-matrix window: near-black red, inset line; grid: faint red dots every 6 px
void drawLxrMatrix(NanoVG& v, float x, float y, float w, float h, bool grid)
{
    v.beginPath();
    v.roundedRect(x, y, w, h, 4);
    v.fillColor(rgba({ 0x0a, 0x03, 0x03 }));
    v.fill();
    v.beginPath();
    v.roundedRect(x, y, w, h, 4);
    v.fillPaint(v.boxGradient(x, y + 2, w, h, 4, 6, Color(0, 0, 0, 0.f), Color(0, 0, 0, 0.8f)));
    v.fill();
    if (grid) {
        v.beginPath();
        for (float gy = y + 3; gy < y + h - 1; gy += 6)
            for (float gx = x + 3; gx < x + w - 1; gx += 6) v.circle(gx, gy, 1.f);
        v.fillColor(Color(1.f, 60 / 255.f, 50 / 255.f, 0.16f));
        v.fill();
    }
    v.beginPath();
    v.roundedRect(x + 0.5f, y + 0.5f, w - 1, h - 1, 4);
    v.strokeColor(rgba({ 0x2a, 0x10, 0x10 }));
    v.strokeWidth(1);
    v.stroke();
}

// round grey button (24 px; small: the 14 px step buttons) in a black ring; on: lit pink-red
void drawLxrButton(NanoVG& v, float cx, float cy, bool on, bool small, bool hot)
{
    const float r = small ? 7.f : 12.f, ring = small ? 2.f : 3.f;
    if (on) {
        v.beginPath();
        v.circle(cx, cy, r + ring + 15);
        v.fillPaint(v.radialGradient(cx, cy, r, r + ring + 15, Color(1.f, 61 / 255.f, 90 / 255.f, 0.55f),
                                     Color(1.f, 61 / 255.f, 90 / 255.f, 0.f)));
        v.fill();
    } else {
        circleShadow(v, cx, cy + 2, r + ring, 4, 200);
    }
    v.beginPath();
    v.circle(cx, cy, r + ring);
    v.fillColor(rgba({ 0x05, 0x05, 0x05 }));
    v.fill();
    static const float kStops[3] = { 0.f, 0.45f, 1.f };
    if (on) {
        const Rgb cols[3] = { { 0xff, 0xd6, 0xdc }, { 0xff, 0x3d, 0x5a }, { 0xc0, 0x16, 0x2f } };
        fillRadialStops(v, [&] { v.circle(cx, cy, r); }, cx - r * 0.1f, cy - r * 0.2f, r * 1.6f, kStops, cols, 3);
    } else {
        const Rgb hi = small ? Rgb { 0x8a, 0x8a, 0x8a } : Rgb { 0x9a, 0x9a, 0x9a };
        const Rgb cols[3] = { hot ? mix(hi, { 0xff, 0xff, 0xff }, 0.25f) : hi, small ? Rgb { 0x55, 0x55, 0x55 } : Rgb { 0x5e, 0x5e, 0x5e },
                              small ? Rgb { 0x55, 0x55, 0x55 } : Rgb { 0x5e, 0x5e, 0x5e } };
        static const float kOff[3] = { 0.f, 0.7f, 1.f };
        fillRadialStops(v, [&] { v.circle(cx, cy, r); }, cx - r * 0.2f, cy - r * 0.36f, r * 1.75f, kOff, cols, 3);
    }
}

// VOLUME fader: black well (30 x 72), slot, cap with a white line; t = 0..1 (top = 1)
void drawLxrFader(NanoVG& v, float x, float y, float w, float h, float t, bool hot)
{
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillColor(rgba({ 0x05, 0x05, 0x05 }));
    v.fill();
    v.beginPath();
    v.roundedRect(x + 0.75f, y + 0.75f, w - 1.5f, h - 1.5f, 3);
    v.strokeColor(rgba({ 0x2a, 0x2a, 0x2a }));
    v.strokeWidth(1.5f);
    v.stroke();
    v.beginPath();
    v.rect(x + w / 2 - 2, y + 6, 4, h - 12);
    v.fillColor(rgba({ 0, 0, 0 }));
    v.fill();
    const float capH = 18, top = y + 4 + (1.f - std::clamp(t, 0.f, 1.f)) * (h - 8 - capH);
    boxShadow(v, x + 3, top, w - 6, capH, 2, 2, 3, 255);
    v.beginPath();
    v.roundedRect(x + 3, top, w - 6, capH, 2);
    v.fillPaint(v.linearGradient(0, top, 0, top + capH, rgba(hot ? Rgb { 0x45, 0x45, 0x46 } : Rgb { 0x2e, 0x2e, 0x2f }),
                                 rgba({ 0x0c, 0x0c, 0x0c })));
    v.fill();
    v.beginPath();
    v.rect(x + 5, top + 8, w - 10, 2);
    v.fillColor(rgba({ 0xf4, 0xf4, 0xf4 }));
    v.fill();
}

// ---------------------------------------------------------------------------
// graphene (mockups/bl101.html)

constexpr Rgb kGrSection { 0x2b, 0x2c, 0x2f };
constexpr Rgb kGrStrip { 0x32, 0x33, 0x36 };
constexpr Rgb kGrWindowText { 0xff, 0xb1, 0x5e };
constexpr Rgb kGrGreen { 0x46, 0xb0, 0x7a };
constexpr Rgb kGrOrange { 0xf0, 0x8a, 0x24 };

void drawGrPanel(NanoVG& v, float x, float y, float w, float h, float r)
{
    boxShadow(v, x, y, w, h, r, 6, 18, 140);
    v.beginPath();
    v.roundedRect(x, y, w, h, r);
    v.fillColor(rgba({ 0x22, 0x23, 0x26 }));
    v.fill();
    // radial-gradient(ellipse at 30% 0%, #2f3033, #222326 70%)
    v.save();
    v.scale(1.f, h / w);
    v.beginPath();
    v.roundedRect(x, y * w / h, w, w, r);
    v.fillPaint(v.radialGradient(x + 0.3f * w, y * w / h, 0, 0.7f * w * 0.9f, rgba({ 0x2f, 0x30, 0x33 }), rgba({ 0x22, 0x23, 0x26 })));
    v.fill();
    v.restore();
    v.beginPath();
    v.moveTo(x + r, y + 0.5f);
    v.lineTo(x + w - r, y + 0.5f);
    v.strokeColor(Color(1.f, 1.f, 1.f, 0.08f));
    v.strokeWidth(1);
    v.stroke();
}

// a section: grey body, title strip (24 px, 2 px dark line under it); the caller prints the title
void drawGrSection(NanoVG& v, float x, float y, float w, float h)
{
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillColor(rgba(kGrSection));
    v.fill();
    v.beginPath();
    v.roundedRect(x, y, w, 24, 3);
    v.fillColor(rgba(kGrStrip));
    v.fill();
    v.beginPath();
    v.rect(x, y + 24, w, 2);
    v.fillColor(rgba({ 0x1a, 0x1b, 0x1d }));
    v.fill();
    v.beginPath();
    v.moveTo(x + 3, y + 0.5f);
    v.lineTo(x + w - 3, y + 0.5f);
    v.strokeColor(Color(1.f, 1.f, 1.f, 0.05f));
    v.strokeWidth(1);
    v.stroke();
}

// vertical slider, 26 x h: tick scale (left), slot, black cap with a coloured stripe; t = 0..1
void drawGrSlider(NanoVG& v, float cx, float top, float h, float t, Rgb stripe, bool hot)
{
    const float x = cx - 13;
    v.beginPath();
    for (float ty = top + 6; ty <= top + h - 6 + 0.01f; ty += 9.2f) v.rect(x, std::round(ty), 6, 1);
    v.fillColor(rgba({ 0xd8, 0xd8, 0xd4 }));
    v.fill();
    v.beginPath();
    v.roundedRect(x + 12, top, 5, h, 2.5f);
    v.fillColor(rgba({ 0x0b, 0x0b, 0x0c }));
    v.fill();
    const float capY = top + (1.f - std::clamp(t, 0.f, 1.f)) * (h - 13);
    boxShadow(v, x + 4, capY, 22, 13, 2, 2, 3, 150);
    v.beginPath();
    v.roundedRect(x + 4, capY, 22, 13, 2);
    v.fillPaint(v.linearGradient(0, capY, 0, capY + 13, rgba(hot ? Rgb { 0x55, 0x55, 0x57 } : Rgb { 0x3a, 0x3a, 0x3c }),
                                 rgba({ 0x11, 0x11, 0x12 })));
    v.fill();
    v.beginPath();
    v.roundedRect(x + 7, capY + 5, 16, 2.5f, 1);
    v.fillColor(rgba(stripe));
    v.fill();
}

// slide switch: well 14 x (20 per position), lever with a yellow stripe at screen row k
void drawGrSwitch(NanoVG& v, float x, float y, float h, int row, bool hot)
{
    v.beginPath();
    v.roundedRect(x, y, 14, h, 3);
    v.fillColor(rgba({ 0x0b, 0x0b, 0x0c }));
    v.fill();
    const float ly = y + 20.f * float(row) + 3.5f;
    v.beginPath();
    v.roundedRect(x + 1, ly, 12, 13, 2);
    v.fillPaint(v.linearGradient(0, ly, 0, ly + 13, rgba(hot ? Rgb { 0x55, 0x55, 0x57 } : Rgb { 0x3a, 0x3a, 0x3c }),
                                 rgba({ 0x11, 0x11, 0x11 })));
    v.fill();
    v.beginPath();
    v.rect(x + 3, ly + 5, 8, 2.5f);
    v.fillColor(rgba({ 0xe8, 0xc5, 0x3a }));
    v.fill();
}

// light-grey button (40 x 22) with a red rectangular LED above it
void drawGrLedButton(NanoVG& v, float cx, float cy, bool on, bool hot)
{
    const float ly = cy - 11 - 6 - 5;
    if (on) {
        v.beginPath();
        v.rect(cx - 16, ly - 8, 32, 21);
        v.fillPaint(v.boxGradient(cx - 8, ly, 16, 5, 1, 7, Color(1.f, 59 / 255.f, 34 / 255.f, 0.7f), Color(1.f, 59 / 255.f, 34 / 255.f, 0.f)));
        v.fill();
    }
    v.beginPath();
    v.roundedRect(cx - 8, ly, 16, 5, 1);
    v.fillColor(rgba(on ? Rgb { 0xff, 0x3b, 0x22 } : Rgb { 0x4a, 0x15, 0x12 }));
    v.fill();
    v.beginPath();
    v.roundedRect(cx - 20, cy - 11 + 2, 40, 22, 2);
    v.fillColor(rgba({ 0x6e, 0x6d, 0x69 }));
    v.fill();
    v.beginPath();
    v.roundedRect(cx - 20, cy - 11, 40, 22, 2);
    v.fillPaint(v.linearGradient(0, cy - 11, 0, cy + 11, rgba(hot ? Rgb { 0xe6, 0xe5, 0xe0 } : Rgb { 0xd6, 0xd5, 0xd0 }),
                                 rgba({ 0xa9, 0xa8, 0xa3 })));
    v.fill();
}

// dark display window (steppers, MULTIPLY, the CHOP masks); the caller prints the value
void drawGrWindow(NanoVG& v, float x, float y, float w, float h, bool hot)
{
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillColor(rgba({ 0x10, 0x11, 0x12 }));
    v.fill();
    v.beginPath();
    v.roundedRect(x, y, w, h, 3);
    v.fillPaint(v.boxGradient(x, y + 1, w, h, 3, 3, Color(0, 0, 0, 0.f), Color(0, 0, 0, 0.7f)));
    v.fill();
    if (hot) {
        v.beginPath();
        v.roundedRect(x + 0.5f, y + 0.5f, w - 1, h - 1, 3);
        v.strokeColor(rgba(kGrOrange, 160));
        v.strokeWidth(1);
        v.stroke();
    }
}

// ---------------------------------------------------------------------------
// Befaco (mockups/oneiroi.html)

constexpr Rgb kBfWhite { 0xf2, 0xf2, 0xf2 };
constexpr Rgb kBfRed { 0xe3, 0x26, 0x2a };
constexpr Rgb kBfGrey { 0x8c, 0x8c, 0x8c };

// knob: printed white 270° scale at R with a dot at each end, red value arc inside it (glow);
// cap (radius C, 0 = the Oneiroi default): 0 big knurled (white line), 1 small grey (dark dot),
// 2 black with a white ring (white dot); Plethora: 3 silver (white dot), 4 white (black dot),
// 5 black (white dot), 6 smoky knurled (white line), 7 white knurled (black line)
void drawBfKnob(NanoVG& v, float cx, float cy, float R, int kind, float t, bool hot, float C = 0)
{
    const float a0 = kKnobA0, a1 = kKnobA1, av = a0 + std::clamp(t, 0.f, 1.f) * (a1 - a0);
    v.lineCap(NanoVG::BUTT);
    v.beginPath();
    v.arc(cx, cy, R - 1, a0, a1, NanoVG::CW);
    v.strokeColor(rgba(kBfWhite));
    v.strokeWidth(2);
    v.stroke();
    for (float a : { a0, a1 }) {
        v.beginPath();
        v.circle(cx + std::cos(a) * (R - 0.5f), cy + std::sin(a) * (R - 0.5f), 3.5f);
        v.fillColor(rgba(kBfWhite));
        v.fill();
    }
    if (av > a0 + 0.001f) {
        const Rgb arc = hot ? mix(kBfRed, { 0xff, 0xff, 0xff }, 0.25f) : kBfRed;
        v.beginPath();
        v.arc(cx, cy, R - 6.5f, a0, av, NanoVG::CW);
        v.strokeColor(rgba(arc, 70));
        v.strokeWidth(7);
        v.stroke();
        v.beginPath();
        v.arc(cx, cy, R - 6.5f, a0, av, NanoVG::CW);
        v.strokeColor(rgba(arc));
        v.strokeWidth(3);
        v.stroke();
    }
    if (C <= 0) C = kind == 0 ? R - 14 : R - 12;
    const float ca = std::cos(av), sa = std::sin(av);
    if (kind >= 3) { // Plethora: a dark 4 px ring round every cap
        circleShadow(v, cx, cy + 5, C + 4, 12, 215);
        v.beginPath();
        v.circle(cx, cy, C + 4);
        v.fillColor(rgba({ 0x0c, 0x0c, 0x0c }));
        v.fill();
        if (kind >= 6) { // knurled: smoky / white ridges, a lit centre disk, a line pointer
            const bool w = kind == 7;
            v.beginPath();
            v.circle(cx, cy, C);
            v.fillColor(rgba(w ? Rgb { 0xd9, 0xd9, 0xd6 } : Rgb { 0x5d, 0x5d, 0x5f }));
            v.fill();
            v.beginPath();
            for (int i = 0; i < 20; ++i) {
                const float b0 = float(i) * float(M_PI) / 10.f + 8.f * float(M_PI) / 180.f, b1 = float(i + 1) * float(M_PI) / 10.f;
                v.moveTo(cx, cy);
                v.arc(cx, cy, C, b0 - float(M_PI) / 2, b1 - float(M_PI) / 2, NanoVG::CW);
                v.closePath();
            }
            v.fillColor(rgba(w ? Rgb { 0xf4, 0xf4, 0xf2 } : Rgb { 0x7a, 0x7a, 0x7c }));
            v.fill();
            v.beginPath();
            v.circle(cx, cy, C);
            v.fillPaint(v.radialGradient(cx - 0.16f * C, cy - 0.32f * C, 0, 1.1f * C, Color(1.f, 1.f, 1.f, w ? 0.6f : 0.25f), Color(1.f, 1.f, 1.f, 0.f)));
            v.fill();
            const float ic = 0.66f * C;
            v.beginPath();
            v.circle(cx, cy, ic);
            v.fillPaint(v.radialGradient(cx - 0.2f * ic, cy - 0.36f * ic, 0, 1.6f * ic, rgba(w ? Rgb { 0xff, 0xff, 0xff } : Rgb { 0xa5, 0xa6, 0xa8 }),
                                         rgba(w ? Rgb { 0xd5, 0xd5, 0xd2 } : Rgb { 0x56, 0x57, 0x5a })));
            v.fill();
            v.beginPath();
            v.moveTo(cx + ca * 0.76f * C, cy + sa * 0.76f * C);
            v.lineTo(cx + ca * 0.0f * C, cy + sa * 0.0f * C);
            v.strokeColor(rgba(w ? Rgb { 0x11, 0x11, 0x11 } : Rgb { 0xf7, 0xf7, 0xf7 }));
            v.strokeWidth(5);
            v.lineCap(NanoVG::ROUND);
            v.stroke();
            return;
        }
        static const float kStops[3] = { 0.f, 0.55f, 1.f };
        static const Rgb kCaps[3][3] = { { { 0xf0, 0xf1, 0xf3 }, { 0xa9, 0xab, 0xaf }, { 0x6f, 0x71, 0x75 } },
                                         { { 0xff, 0xff, 0xff }, { 0xe6, 0xe6, 0xe4 }, { 0xb9, 0xb9, 0xb6 } },
                                         { { 0x4a, 0x4a, 0x4a }, { 0x14, 0x14, 0x14 }, { 0x05, 0x05, 0x05 } } };
        fillRadialStops(v, [&] { v.circle(cx, cy, C); }, cx - 0.16f * C, cy - 0.4f * C, 1.6f * C, kStops, kCaps[kind - 3], 3);
        const float dr = 0.74f * C - 4;
        v.beginPath();
        v.circle(cx + ca * dr, cy + sa * dr, 4);
        v.fillColor(rgba(kind == 4 ? Rgb { 0x11, 0x11, 0x11 } : Rgb { 0xf7, 0xf7, 0xf7 }));
        v.fill();
        return;
    }
    circleShadow(v, cx, cy + 4, C + (kind ? 5 : 0), 9, 200);
    if (kind == 0) { // knurled: 24 ridges, light from the top left, a darker centre disk
        v.beginPath();
        v.circle(cx, cy, C);
        v.fillColor(rgba({ 0x2c, 0x2c, 0x2c }));
        v.fill();
        v.beginPath();
        for (int i = 0; i < 24; ++i) {
            const float b0 = float(i) * float(M_PI) / 12.f + 7.f * float(M_PI) / 180.f, b1 = float(i + 1) * float(M_PI) / 12.f;
            v.moveTo(cx, cy);
            v.arc(cx, cy, C, b0 - float(M_PI) / 2, b1 - float(M_PI) / 2, NanoVG::CW);
            v.closePath();
        }
        v.fillColor(rgba({ 0x3b, 0x3b, 0x3b }));
        v.fill();
        v.beginPath();
        v.circle(cx, cy, C);
        v.fillPaint(v.radialGradient(cx - 0.16f * C, cy - 0.32f * C, 0, 1.1f * C, Color(1.f, 1.f, 1.f, 0.16f), Color(1.f, 1.f, 1.f, 0.f)));
        v.fill();
        const float ic = 0.68f * C;
        v.beginPath();
        v.circle(cx, cy, ic);
        v.fillPaint(v.radialGradient(cx - 0.2f * ic, cy - 0.36f * ic, 0, 1.5f * ic, rgba({ 0x4b, 0x4b, 0x4b }), rgba({ 0x26, 0x26, 0x26 })));
        v.fill();
        v.beginPath();
        v.moveTo(cx + ca * 0.72f * C, cy + sa * 0.72f * C);
        v.lineTo(cx + ca * 0.04f * C, cy + sa * 0.04f * C);
        v.strokeColor(rgba(kBfWhite));
        v.strokeWidth(3.5f);
        v.lineCap(NanoVG::ROUND);
        v.stroke();
        return;
    }
    // rings: grey = dark then a thin grey edge; black = a white ring
    v.beginPath();
    v.circle(cx, cy, C + (kind == 1 ? 5.f : 4.f));
    v.fillColor(rgba(kind == 1 ? Rgb { 0x3a, 0x3a, 0x3a } : Rgb { 0xe6, 0xe6, 0xe6 }));
    v.fill();
    if (kind == 1) {
        v.beginPath();
        v.circle(cx, cy, C + 4);
        v.fillColor(rgba({ 0x1d, 0x1d, 0x1d }));
        v.fill();
    }
    static const float kStops[3] = { 0.f, 0.55f, 1.f };
    const Rgb grey[3] = { { 0xe2, 0xe2, 0xe2 }, { 0xb4, 0xb4, 0xb4 }, { 0x8a, 0x8a, 0x8a } };
    const Rgb black[3] = { { 0x3a, 0x3a, 0x3a }, { 0x06, 0x06, 0x06 }, { 0x06, 0x06, 0x06 } };
    static const float kBlackStops[3] = { 0.f, 0.7f, 1.f };
    fillRadialStops(v, [&] { v.circle(cx, cy, C); }, cx - 0.2f * C, cy - 0.36f * C, 1.6f * C, kind == 1 ? kStops : kBlackStops,
                    kind == 1 ? grey : black, 3);
    const float dr = 0.68f * C - 3.5f;
    v.beginPath();
    v.circle(cx + ca * dr, cy + sa * dr, 3.5f);
    v.fillColor(rgba(kind == 1 ? Rgb { 0x1d, 0x1d, 0x1d } : kBfWhite));
    v.fill();
}

// fader: black slot (14 wide) with a thin edge, glossy red pill cap (24 x 44); t = 0..1
void drawBfFader(NanoVG& v, float cx, float top, float h, float t, bool hot)
{
    v.beginPath();
    v.roundedRect(cx - 7, top, 14, h, 7);
    v.fillColor(rgba({ 0, 0, 0 }));
    v.fill();
    v.strokeColor(rgba({ 0x26, 0x26, 0x26 }));
    v.strokeWidth(1);
    v.stroke();
    const float cy = top + (1.f - std::clamp(t, 0.f, 1.f)) * (h - 44);
    v.beginPath();
    v.roundedRect(cx - 14, cy - 4, 28, 56, 13);
    v.fillPaint(v.boxGradient(cx - 12, cy, 24, 44, 11, 10, Color(227 / 255.f, 20 / 255.f, 28 / 255.f, 0.35f), Color(0, 0, 0, 0.f)));
    v.fill();
    static const float kStops[3] = { 0.f, 0.45f, 1.f };
    const Rgb cols[3] = { hot ? Rgb { 0xff, 0xc0, 0xc0 } : Rgb { 0xff, 0x9a, 0x9a }, { 0xe3, 0x14, 0x1c }, { 0x8f, 0x0a, 0x10 } };
    fillRadialStops(v, [&] { v.roundedRect(cx - 12, cy, 24, 44, 11); }, cx - 2.4f, cy + 12.3f, 30.f, kStops, cols, 3);
}

// the tick ladder between faders: 7 white bars, 28 -> 8 px wide, over h
void drawBfLadder(NanoVG& v, float cx, float top, float h)
{
    static const float kW[7] = { 28, 26, 24, 20, 16, 12, 8 };
    v.beginPath();
    for (int k = 0; k < 7; ++k) v.roundedRect(cx - kW[k] / 2, top + (h - 4) * float(k) / 6.f, kW[k], 4, 2);
    v.fillColor(rgba(kBfWhite));
    v.fill();
}

// round push button (d 58 or 46) in a dark ring; on: a red glow round it
void drawBfButton(NanoVG& v, float cx, float cy, float d, bool on, bool hot)
{
    const float r = d / 2;
    if (on) {
        v.beginPath();
        v.circle(cx, cy, r + 16);
        v.fillPaint(v.radialGradient(cx, cy, r, r + 16, Color(1.f, 42 / 255.f, 48 / 255.f, 0.45f), Color(1.f, 42 / 255.f, 48 / 255.f, 0.f)));
        v.fill();
    }
    circleShadow(v, cx, cy + 4, r + 4, 9, 200);
    v.beginPath();
    v.circle(cx, cy, r + 4);
    v.fillColor(rgba({ 0x1d, 0x1d, 0x1d }));
    v.fill();
    v.beginPath();
    v.circle(cx, cy, r);
    v.fillPaint(v.radialGradient(cx - 0.2f * r, cy - 0.36f * r, 0, 1.4f * r, rgba(hot ? Rgb { 0x80, 0x80, 0x80 } : Rgb { 0x6a, 0x6a, 0x6a }),
                                 rgba({ 0x3a, 0x3a, 0x3a })));
    v.fill();
}

// LED (13 px): off dark red (or a given colour), on lit red with a glow
void drawBfLed(NanoVG& v, float cx, float cy, float level, Rgb off = { 0x3a, 0x14, 0x16 })
{
    v.beginPath();
    v.circle(cx, cy, 6.5f);
    v.fillColor(rgba(off));
    v.fill();
    if (level <= 0.01f) return;
    v.beginPath();
    v.circle(cx, cy, 6.5f + 13);
    v.fillPaint(v.radialGradient(cx, cy, 4, 19.5f, Color(1.f, 42 / 255.f, 48 / 255.f, 0.55f * level), Color(1.f, 42 / 255.f, 48 / 255.f, 0.f)));
    v.fill();
    static const float kStops[3] = { 0.f, 0.55f, 1.f };
    const Rgb cols[3] = { { 0xff, 0xb0, 0xb0 }, { 0xff, 0x2a, 0x30 }, { 0xa3, 0x14, 0x1a } };
    v.globalAlpha(level);
    fillRadialStops(v, [&] { v.circle(cx, cy, 6.5f); }, cx - 1.3f, cy - 2, 9.f, kStops, cols, 3);
    v.globalAlpha(1.f);
}

// ribbed slide switch (26 wide): well with a thin edge, a 30 px ribbed lever at row k of n
void drawBfSwitch(NanoVG& v, float cx, float cy, float h, int n, int k, bool hot)
{
    const float x = cx - 13, y = cy - h / 2;
    v.beginPath();
    v.roundedRect(x, y, 26, h, 4);
    v.fillColor(rgba({ 0x05, 0x05, 0x05 }));
    v.fill();
    v.strokeColor(rgba({ 0x33, 0x33, 0x33 }));
    v.strokeWidth(1);
    v.stroke();
    const float ly = y + 3 + (n > 1 ? (h - 36) * float(k) / float(n - 1) : 0.f);
    for (int i = 0; i < 10; ++i) {
        v.beginPath();
        v.rect(x + 3, ly + i * 3, 20, 3);
        v.fillColor(rgba(i % 2 ? Rgb { 0x3a, 0x3a, 0x3a } : hot ? Rgb { 0x70, 0x70, 0x70 } : Rgb { 0x5a, 0x5a, 0x5a }));
        v.fill();
    }
}
