// Fonts.hpp - the embedded fonts of the plugin's theme (common/ui/fonts/README.md).
//
// Faces, by name: "mono" (IBM Plex Mono 500), "mono-sb" (600): the chrome, all themes;
// 303 silver / graphene: "print" (Inter 700), "print-xb" (Inter 800), "logo" (Inter 800
// italic).
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
}
