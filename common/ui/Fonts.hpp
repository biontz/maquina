// Fonts.hpp - the embedded fonts of the plugin's theme (common/ui/fonts/README.md).
//
// Faces, by name: "mono" (IBM Plex Mono 500), "mono-sb" (600): the chrome, all themes;
// 303 silver / graphene: "print" (Inter 700), "print-xb" (Inter 800), "logo" (Inter 800
// italic); Mutable: "print" / "logo" (Mulish 800), "print-sm" (Mulish 700); LXR: "print"
// (Questrial), "logo" (Michroma), "lcd" (VT323); Befaco: "print" (Oswald 400), "print-m" (500).
//
// Included by DrumUI.cpp inside namespace maquina_ui, after Theme.hpp.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#if MAQUINA_THEME != MAQUINA_THEME_CLASSIC
#include "fonts/IBMPlexMono/IBMPlexMono-Medium.ttf.hpp"
#include "fonts/IBMPlexMono/IBMPlexMono-SemiBold.ttf.hpp"
#endif
#if MAQUINA_THEME == MAQUINA_THEME_SILVER303 || MAQUINA_THEME == MAQUINA_THEME_GRAPHENE
#include "fonts/Inter/Inter-Bold.subset.ttf.hpp"
#include "fonts/Inter/Inter-ExtraBold.subset.ttf.hpp"
#include "fonts/Inter/Inter-ExtraBoldItalic.subset.ttf.hpp"
#endif
#if MAQUINA_THEME == MAQUINA_THEME_BEFACO
#include "fonts/Oswald/Oswald-Regular.subset.ttf.hpp"
#include "fonts/Oswald/Oswald-Medium.subset.ttf.hpp"
#endif
#if MAQUINA_THEME == MAQUINA_THEME_LXR
#include "fonts/Michroma/Michroma-Regular.subset.ttf.hpp"
#include "fonts/Questrial/Questrial-Regular.subset.ttf.hpp"
#include "fonts/VT323/VT323-Regular.subset.ttf.hpp"
#endif
#if MAQUINA_THEME == MAQUINA_THEME_MUTABLE
#include "fonts/Mulish/Mulish-Bold.subset.ttf.hpp"
#include "fonts/Mulish/Mulish-ExtraBold.subset.ttf.hpp"
#endif

// fontstash sizes a font so that ascent - descent = size; CSS sizes the em. The new look
// works in CSS px: fontPx() scales by the face's (ascent - descent) / unitsPerEm (hhea, head).
inline float ttfEmScale(const unsigned char* d, unsigned size)
{
    auto u16 = [&](unsigned o) { return o + 2 <= size ? unsigned(d[o] << 8 | d[o + 1]) : 0u; };
    auto s16 = [&](unsigned o) { return int(int16_t(uint16_t(u16(o)))); };
    unsigned head = 0, hhea = 0;
    for (unsigned t = 0, n = u16(4); t < n; ++t) {
        const unsigned r = 12 + 16 * t;
        if (r + 16 > size) break;
        const unsigned off = unsigned(u16(r + 8) << 16 | u16(r + 10));
        if (!std::memcmp(d + r, "head", 4)) head = off;
        if (!std::memcmp(d + r, "hhea", 4)) hhea = off;
    }
    const unsigned upm = head ? u16(head + 18) : 0;
    const int span = hhea ? s16(hhea + 4) - s16(hhea + 6) : 0;
    return upm && span > 0 ? float(span) / float(upm) : 1.f;
}

struct FaceScale { const char* name; float scale; };
inline FaceScale gFaces[8];
inline int gNumFaces = 0;
inline float gFaceScale = 1.f;

inline void addFace(NanoVG& v, const char* name, const unsigned char* d, unsigned size)
{
    v.createFontFromMemory(name, d, size, false);
    if (gNumFaces < 8) gFaces[gNumFaces++] = { name, ttfEmScale(d, size) };
}
// select a face (the new look): its em scale follows
inline void useFace(NanoVG& v, const char* name)
{
    v.fontFace(name);
    for (int i = 0; i < gNumFaces; ++i)
        if (!std::strcmp(gFaces[i].name, name)) { gFaceScale = gFaces[i].scale; return; }
    gFaceScale = 1.f;
}
// font size in CSS px of the current face
inline void fontPx(NanoVG& v, float px) { v.fontSize(px * gFaceScale); }

inline void loadThemeFonts(NanoVG& v)
{
    (void)v;
    gNumFaces = 0;
#if MAQUINA_THEME != MAQUINA_THEME_CLASSIC
    addFace(v, "mono", k_font_ibmplexmono_medium, k_font_ibmplexmono_medium_size);
    addFace(v, "mono-sb", k_font_ibmplexmono_semibold, k_font_ibmplexmono_semibold_size);
#endif
#if MAQUINA_THEME == MAQUINA_THEME_SILVER303 || MAQUINA_THEME == MAQUINA_THEME_GRAPHENE
    addFace(v, "print", k_font_inter_bold, k_font_inter_bold_size);
    addFace(v, "print-xb", k_font_inter_extrabold, k_font_inter_extrabold_size);
    addFace(v, "logo", k_font_inter_extrabolditalic, k_font_inter_extrabolditalic_size);
#endif
#if MAQUINA_THEME == MAQUINA_THEME_BEFACO
    addFace(v, "print", k_font_oswald_regular, k_font_oswald_regular_size);
    addFace(v, "print-m", k_font_oswald_medium, k_font_oswald_medium_size);
#endif
#if MAQUINA_THEME == MAQUINA_THEME_LXR
    addFace(v, "print", k_font_questrial_regular, k_font_questrial_regular_size);
    addFace(v, "logo", k_font_michroma_regular, k_font_michroma_regular_size);
    addFace(v, "lcd", k_font_vt323_regular, k_font_vt323_regular_size);
#endif
#if MAQUINA_THEME == MAQUINA_THEME_MUTABLE
    addFace(v, "print", k_font_mulish_extrabold, k_font_mulish_extrabold_size);
    addFace(v, "print-sm", k_font_mulish_bold, k_font_mulish_bold_size);
    addFace(v, "logo", k_font_mulish_extrabold, k_font_mulish_extrabold_size);
#endif
}
