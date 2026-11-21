// Theme.hpp - the editor's look per machine family (docs/ui-redesign.md).
//
// Picked at compile time from MAQUINA_VOICE_INDEX (or a MAQUINA_THEME define). Plugins
// whose family is not redesigned yet keep the classic look (kTheme == Theme::Classic):
// they are moved over step by step.
//
// Included by DrumUI.cpp inside namespace maquina_ui.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#define MAQUINA_THEME_CLASSIC 0
#define MAQUINA_THEME_SILVER303 1 // Acida, Acida FX
#define MAQUINA_THEME_GRAPHENE 2  // BL-101, BL-101 FX, LA-50
#define MAQUINA_THEME_LXR 3       // Kick, Tom, Snare, Cymbal, HiHat
#define MAQUINA_THEME_BEFACO 4    // Oneiroi, Oneiroi FX, Plethora
#define MAQUINA_THEME_MUTABLE 5   // Klang, Klong, Klong FX
#define MAQUINA_THEME_ERICA 6     // Dredrum

#ifndef MAQUINA_THEME
#if defined(MAQUINA_VOICE_INDEX) && MAQUINA_VOICE_INDEX >= 0 && MAQUINA_VOICE_INDEX <= 4 // Kick, Tom, Snare, Cymbal, HiHat
#define MAQUINA_THEME MAQUINA_THEME_LXR
#elif MAQUINA_VOICE_INDEX >= 14 && MAQUINA_VOICE_INDEX <= 16 // BL-101, BL-101 FX, LA-50
#define MAQUINA_THEME MAQUINA_THEME_GRAPHENE
#elif MAQUINA_VOICE_INDEX == 12 // Acida
#define MAQUINA_THEME MAQUINA_THEME_SILVER303
#elif MAQUINA_VOICE_INDEX >= 9 && MAQUINA_VOICE_INDEX <= 11 // Klang, Klong, Klong FX
#define MAQUINA_THEME MAQUINA_THEME_MUTABLE
#else
#define MAQUINA_THEME MAQUINA_THEME_CLASSIC
#endif
#endif

struct Rgb { int r, g, b; };

enum class ThemeId { Classic, Silver303, Graphene, Lxr, Befaco, Mutable, Erica };

constexpr ThemeId kTheme = MAQUINA_THEME == MAQUINA_THEME_SILVER303 ? ThemeId::Silver303
                           : MAQUINA_THEME == MAQUINA_THEME_GRAPHENE ? ThemeId::Graphene
                           : MAQUINA_THEME == MAQUINA_THEME_LXR      ? ThemeId::Lxr
                           : MAQUINA_THEME == MAQUINA_THEME_BEFACO   ? ThemeId::Befaco
                           : MAQUINA_THEME == MAQUINA_THEME_MUTABLE  ? ThemeId::Mutable
                           : MAQUINA_THEME == MAQUINA_THEME_ERICA    ? ThemeId::Erica
                                                                     : ThemeId::Classic;
// the new look: 46 px header chrome, the machine panel, the random drawer
constexpr bool kNew = kTheme != ThemeId::Classic;

// a knob: value ring (inside the knob, or around it), skirt (a ring round the cap, or a
// knurled body), cap (radial stops), pointer
struct KnobStyle {
    Rgb arc;              // value arc
    Rgb rest; int restA;  // the rest of the 270° ring, alpha 0..255
    float ringW;          // ring stroke width
    float ringOut, ringOutSmall; // ring's outer edge beyond size / 2 (small knobs: size < 45)
    bool knurl;           // skirt = the whole body, ridged (skirt / skirt2 every 6°); else a
    Rgb skirt, skirt2;    // ring of skirtW round the cap
    float skirtW;
    float capInset;       // cap radius = size * (0.5 - capInset)
    int capN;             // cap radial stops
    float capStop[4];
    Rgb cap[4];
    float capGx, capGy;   // the cap's light centre, fraction of its box
    Rgb capLine; float capLineW;
    Rgb pointer;
    float ptrFrom, ptrLen; // pointer: from / length, fractions of the cap's diameter from its edge
    int shadowA; float shadowBlur, shadowDy;
};

struct Theme {
    float width;          // window width
    Rgb accent;           // RANDOM, range bars, value arcs
    Rgb accentText;       // plugin name in the header, locked group titles
    Rgb onAccent;         // text on an accent fill
    Rgb hatchA, hatchB;   // a locked range bar's stripes
    Rgb window;           // around the panel
    float panelRadius;
    Rgb print;            // panel print
    Rgb value;            // value text on the panel
    KnobStyle knob;
};

// shared chrome tokens (DESIGN-GUIDE §2)
constexpr Rgb kChromeBg { 0x1b, 0x1d, 0x21 };
constexpr Rgb kChromeLine { 0x2a, 0x2f, 0x37 };
constexpr Rgb kChromeBtn { 0x23, 0x28, 0x30 };
constexpr Rgb kChromeBtnLine { 0x2e, 0x34, 0x3d };
constexpr Rgb kChromeWell { 0x0f, 0x11, 0x14 };
constexpr Rgb kChromeText { 0xe9, 0xec, 0xf1 };
constexpr Rgb kChromeText2 { 0x9a, 0xa4, 0xb1 };
constexpr Rgb kChromeDisabled { 0x6b, 0x74, 0x80 };
constexpr float kChromeHeaderH = 46.f, kChromeDrawerH = 96.f, kChromeDrawerFoldH = 46.f, kChromeInset = 10.f;

constexpr Theme kSilver303 {
    1100.f,
    { 0xd2, 0x38, 0x2a }, { 0xff, 0x6a, 0x55 }, { 0xff, 0xff, 0xff },
    { 0x5b, 0x3a, 0x35 }, { 0x3a, 0x2a, 0x28 },
    { 0x14, 0x15, 0x18 }, 6.f,
    { 0x1d, 0x1d, 0x1b }, { 0x3a, 0x3a, 0x37 },
    { { 0xd2, 0x38, 0x2a }, { 0x1d, 0x1d, 0x1b }, 72, 3.5f, 0.f, 0.f,
      false, { 0x1f, 0x1f, 0x1e }, { 0x1f, 0x1f, 0x1e }, 3.f,
      0.14f, 4, { 0.f, 0.38f, 0.78f, 1.f },
      { { 0xfb, 0xfb, 0xf9 }, { 0xd9, 0xd9, 0xd6 }, { 0xa3, 0xa3, 0x9f }, { 0x7c, 0x7c, 0x78 } }, 0.38f, 0.32f,
      { 0, 0, 0 }, 0.f,
      { 0x1d, 0x1d, 0x1b }, 0.06f, 0.30f,
      100, 8.f, 4.f },
};

// Mutable light grey (Klang, Klong, Klong FX): knurled black skirt, white cap, ink arcs
constexpr Theme kMutable {
    880.f,
    { 0x3f, 0xd3, 0x4a }, { 0x58, 0xd8, 0x63 }, { 0x10, 0x22, 0x0f },
    { 0x2e, 0x5a, 0x34 }, { 0x24, 0x38, 0x2a },
    { 0x14, 0x15, 0x18 }, 4.f,
    { 0x1a, 0x1a, 0x1a }, { 0x55, 0x55, 0x4f },
    { { 0x1a, 0x1a, 0x1a }, { 0x1a, 0x1a, 0x1a }, 38, 3.f, 8.f, 5.f,
      true, { 0x15, 0x15, 0x15 }, { 0x26, 0x26, 0x26 }, 0.f,
      0.16f, 3, { 0.f, 0.55f, 1.f, 1.f },
      { { 0xff, 0xff, 0xff }, { 0xef, 0xef, 0xed }, { 0xcf, 0xcf, 0xcb }, { 0xcf, 0xcf, 0xcb } }, 0.42f, 0.34f,
      { 0x0e, 0x0e, 0x0e }, 2.f,
      { 0x1a, 0x1a, 0x1a }, 0.05f, 0.32f,
      90, 10.f, 4.f },
};

// graphene (BL-101, BL-101 FX, LA-50): dark grey sections with title strips, sliders with
// coloured cap stripes, black knobs with orange rings (mockups/bl101.html)
constexpr Theme kGraphene {
    1100.f,
    { 0xf0, 0x8a, 0x24 }, { 0xf0, 0x8a, 0x24 }, { 0x1b, 0x1d, 0x21 },
    { 0x5a, 0x44, 0x30 }, { 0x3a, 0x2e, 0x24 },
    { 0x14, 0x15, 0x18 }, 6.f,
    { 0xf2, 0xf2, 0xee }, { 0xa9, 0xab, 0xb0 },
    { { 0xf0, 0x8a, 0x24 }, { 0xff, 0xff, 0xff }, 31, 2.f, -1.f, -1.f,
      false, { 0x1a, 0x19, 0x1b }, { 0x1a, 0x19, 0x1b }, 0.f,
      0.1458f, 2, { 0.f, 0.7f, 1.f, 1.f },
      { { 0x4a, 0x48, 0x4c }, { 0x1a, 0x19, 0x1b }, { 0x1a, 0x19, 0x1b }, { 0x1a, 0x19, 0x1b } }, 0.38f, 0.30f,
      { 0, 0, 0 }, 0.f,
      { 0xf0, 0x8a, 0x24 }, 0.0588f, 0.3235f,
      150, 6.f, 3.f },
};

constexpr Theme withWidth(Theme t, float w) { t.width = w; return t; }
constexpr Theme kGrapheneLa50 = withWidth(kGraphene, 1240.f); // the PG-1000 field is wider

// LXR black (Kick, Tom, Snare, Cymbal, HiHat): matte black, thin red rings, grey caps,
// red display digits (mockups/kick-lxr.html)
constexpr Theme kLxr {
    1100.f,
    { 0xe0, 0x29, 0x4a }, { 0xff, 0x5a, 0x72 }, { 0xff, 0xff, 0xff },
    { 0x5a, 0x2a, 0x33 }, { 0x3a, 0x22, 0x28 },
    { 0x14, 0x15, 0x18 }, 10.f,
    { 0xf4, 0xf4, 0xf4 }, { 0xff, 0x3b, 0x30 },
    { { 0xff, 0x3d, 0x5a }, { 0xff, 0xff, 0xff }, 26, 2.f, -1.f, -1.f,
      false, { 0x16, 0x16, 0x17 }, { 0x16, 0x16, 0x17 }, 7.f,
      0.2308f, 3, { 0.f, 0.55f, 1.f, 1.f },
      { { 0xe6, 0xe6, 0xe6 }, { 0xb4, 0xb4, 0xb4 }, { 0x8a, 0x8a, 0x8a }, { 0x8a, 0x8a, 0x8a } }, 0.40f, 0.32f,
      { 0, 0, 0 }, 0.f,
      { 0xf4, 0xf4, 0xf4 }, -0.214f, 0.214f,
      200, 7.f, 3.f },
};

// the classic look's values where a Theme is asked for (never drawn with)
constexpr Theme kClassicTheme {
    1080.f,
    { 0xff, 0x9f, 0x2e }, { 0xff, 0x9f, 0x2e }, { 0x1c, 0x1c, 0x1c },
    { 0x45, 0x45, 0x45 }, { 0x26, 0x26, 0x26 },
    { 0x1c, 0x1c, 0x1c }, 0.f,
    { 0xc8, 0xc8, 0xc8 }, { 0xc8, 0xc8, 0xc8 },
    {},
};

constexpr const Theme& theme()
{
    return kTheme == ThemeId::Silver303 ? kSilver303
         : kTheme == ThemeId::Mutable   ? kMutable
         : kTheme == ThemeId::Lxr       ? kLxr
         : kTheme == ThemeId::Graphene  ? (MAQUINA_VOICE_INDEX == 16 ? kGrapheneLa50 : kGraphene)
                                        : kClassicTheme;
}

// header credit lines: the long one, and a shorter one when the header is tight
struct Credit { const char* longText; const char* shortText; };
