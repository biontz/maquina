# Editor fonts

All fonts are under the SIL Open Font License 1.1 (`LICENSE-OFL.txt` in each folder). Each
plugin embeds only the fonts of its theme (`common/ui/Theme.hpp`), as the
`*.ttf.hpp` headers made by `tools/gen_fonts.py`.

| Folder | Files | Source | Changes |
|---|---|---|---|
| `IBMPlexMono/` | Medium (500), SemiBold (600) | github.com/google/fonts `ofl/ibmplexmono` | none (Reserved Font Name "Plex": shipped unmodified) |
| `Mulish/` | Bold (700), ExtraBold (800) | github.com/google/fonts `ofl/mulish` (variable) | static instances (`fonttools varLib.instancer wght=700 / 800`), subset like Inter (no reserved name) |
| `Inter/` | Bold (700), ExtraBold (800), ExtraBold Italic | github.com/rsms/inter release 4.1, `extras/ttf` | subset to Latin-1, punctuation and arrows (`*.subset.ttf`; no reserved name) |

Subsetting (fontTools):

    pyftsubset Inter-Bold.ttf --layout-features=kern \
      --unicodes="U+0020-007E,U+00A0-00FF,U+2010-2027,U+2030-203A,U+2190-2195,U+2212,U+25B2-25C0" \
      --output-file=Inter-Bold.subset.ttf
