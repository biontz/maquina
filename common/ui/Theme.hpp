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
#if MAQUINA_VOICE_INDEX == 12 // Acida
#define MAQUINA_THEME MAQUINA_THEME_SILVER303
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

// a knob: printed / value ring, skirt, cap, pointer
struct KnobStyle {
    Rgb arc;            // value arc (the machine accent)
    Rgb rest; int restA; // the rest of the 270° ring, alpha 0..255
    float ringW;        // ring stroke width
    Rgb skirt;          // ring around the cap
    float skirtW;
    Rgb cap[4];         // cap radial stops 0, .38, .78, 1 (lit from the top left)
    Rgb pointer;
    float capInset;     // cap radius = size * (0.5 - capInset)
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
constexpr float kChromeHeaderH = 46.f, kChromeDrawerH = 96.f, kChromeInset = 10.f;

constexpr Theme kSilver303 {
    1100.f,
    { 0xd2, 0x38, 0x2a }, { 0xff, 0x6a, 0x55 }, { 0xff, 0xff, 0xff },
    { 0x5b, 0x3a, 0x35 }, { 0x3a, 0x2a, 0x28 },
    { 0x14, 0x15, 0x18 }, 6.f,
    { 0x1d, 0x1d, 0x1b }, { 0x3a, 0x3a, 0x37 },
    { { 0xd2, 0x38, 0x2a }, { 0x1d, 0x1d, 0x1b }, 72, 3.5f, { 0x1f, 0x1f, 0x1e }, 3.f,
      { { 0xfb, 0xfb, 0xf9 }, { 0xd9, 0xd9, 0xd6 }, { 0xa3, 0xa3, 0x9f }, { 0x7c, 0x7c, 0x78 } },
      { 0x1d, 0x1d, 0x1b }, 0.14f },
};

// the classic look's values where a Theme is asked for (never drawn with)
constexpr Theme kClassicTheme {
    1080.f,
    { 0xff, 0x9f, 0x2e }, { 0xff, 0x9f, 0x2e }, { 0x1c, 0x1c, 0x1c },
    { 0x45, 0x45, 0x45 }, { 0x26, 0x26, 0x26 },
    { 0x1c, 0x1c, 0x1c }, 0.f,
    { 0xc8, 0xc8, 0xc8 }, { 0xc8, 0xc8, 0xc8 },
    { { 0xff, 0x9f, 0x2e }, { 0x45, 0x45, 0x45 }, 255, 3.f, { 0x45, 0x45, 0x45 }, 0.f,
      { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } }, { 0xc8, 0xc8, 0xc8 }, 0.f },
};

constexpr const Theme& theme() { return kTheme == ThemeId::Silver303 ? kSilver303 : kClassicTheme; }

// header credit lines: the long one, and a shorter one when the header is tight
struct Credit { const char* longText; const char* shortText; };
