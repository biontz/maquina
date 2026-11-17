"""
la50_layout.py - the D-50 patch format, as Maquina LA-50 uses it: one source for
tools/gen_params.py (the plugin's parameters), tools/d50_syx.py (SysEx import /
export) and, through the generated tables, the C++ engine.

A D-50 patch is 7 blocks of 64 bytes (D-50 MIDI Implementation 2.00, *4-3..*4-6):

  block 0  Upper Partial-1   block 3  Lower Partial-1
  block 1  Upper Partial-2   block 4  Lower Partial-2
  block 2  Upper Common      block 5  Lower Common
                             block 6  Patch

Each plugin parameter is one of these bytes, at its D-50 value (0..max); its
VoiceParam::fwPar is the byte's index in the 448-byte patch (block * 64 +
offset). Names (tone names, patch name), the MIDI bytes of the patch block
and the "extension" bytes are not parameters.

SPDX-License-Identifier: GPL-3.0-only
"""

PATCH_BYTES = 7 * 64
BLOCKS = [("u_p1", "U1", "partial"), ("u_p2", "U2", "partial"), ("u", "U", "common"),
          ("l_p1", "L1", "partial"), ("l_p2", "L2", "partial"), ("l", "L", "common"), ("patch", "", "patch")]

# the D-50's character set for names (0..63)
CHARSET = " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890-"
assert len(CHARSET) == 64

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(midi):
    return f"{NOTES[midi % 12]}{midi // 12 - 1}"


KF16 = ["-1", "-1/2", "-1/4", "0", "1/8", "1/4", "3/8", "1/2", "5/8", "3/4", "7/8", "1", "5/4", "3/2", "2"]
LFO_SEL = ["+1", "-1", "+2", "-2", "+3", "-3"]
# bias point: 0..63 = "<A1".."<C7" (below the key), 64..127 = ">A1".."<C7" (above); A1 = MIDI 33
BIAS_POINT = [f"<{note_name(33 + i)}" for i in range(64)] + [f">{note_name(33 + i)}" for i in range(64)]
LFO_WAVES = ["Tri", "Saw", "Square", "Random"]
LOW_EQ_HZ = ["63", "75", "88", "105", "125", "150", "175", "210", "250", "300", "350", "420", "500", "600", "700",
             "840"]
HIGH_EQ_HZ = ["250", "300", "350", "420", "500", "600", "700", "840", "1.0k", "1.2k", "1.4k", "1.7k", "2.0k", "2.4k",
              "2.8k", "3.4k", "4.0k", "4.8k", "5.7k", "6.7k", "8.0k", "9.5k"]
HIGH_EQ_Q = ["0.3", "0.5", "0.7", "1.0", "1.4", "2.0", "3.0", "4.2", "6.0"]
# the D-50's PCM waves (Advanced Owner's Manual p. 34): 1-47 one-shot attacks, 48-76 loops,
# 77-100 loops combined from them
PCM_WAVES = ['Marimba', 'Vibraphone', 'Xylophone 1', 'Xylophone 2', 'Log Bass', 'Hammer', 'Japanese Drum', 'Kalimba', 'Pluck 1', 'Chink', 'Agogo', 'Triangle', 'Bells', 'Nail File', 'Pick', 'Low Piano', 'Mid Piano', 'High Piano', 'Harpsichord', 'Harp', 'Organ Percussion', 'Steel Strings', 'Nylon Strings', 'Electric Guitar 1', 'Electric Guitar 2', 'Dirty Guitar', 'Pick Bass', 'Pop Bass', 'Thump', 'Upright Bass', 'Clarinet', 'Breath', 'Steamer', 'High Flute', 'Low Flute', 'Guiro', 'Indian Flute', 'Flute Harmonics', 'Lips 1', 'Lips 2', 'Trumpet', 'Trombones', 'Contrabass', 'Cello', 'Violin Bow', 'Violins', 'Pizzicato', 'Drawbars Lp', 'High Organ Lp', 'Low Organ Lp', 'E.Piano Lp 1', 'E.Piano Lp 2', 'Clavi Lp', 'Harpsichord Lp', 'E.Bass Lp 1', 'Ac.Bass Lp', 'E.Bass Lp 2', 'E.Bass Lp 3', 'E.Guitar Lp', 'Cello Lp', 'Violin Lp', 'Reed Lp', 'Sax Lp 1', 'Sax Lp 2', 'Aah Lp', 'Ooh Lp', 'Male Lp 1', 'Spectrum 1', 'Spectrum 2', 'Spectrum 3', 'Spectrum 4', 'Spectrum 5', 'Spectrum 6', 'Spectrum 7', 'Male Lp 2', 'Noise Lp', 'Loop 01', 'Loop 02', 'Loop 03', 'Loop 04', 'Loop 05', 'Loop 06', 'Loop 07', 'Loop 08', 'Loop 09', 'Loop 10', 'Loop 11', 'Loop 12', 'Loop 13', 'Loop 14', 'Loop 15', 'Loop 16', 'Loop 17', 'Loop 18', 'Loop 19', 'Loop 20', 'Loop 21', 'Loop 22', 'Loop 23', 'Loop 24']
assert len(PCM_WAVES) == 100

CHORUS_TYPES = ["Chorus 1", "Chorus 2", "Flanger 1", "Flanger 2", "FB Chorus", "Tremolo", "Chor Trem", "Dimension"]
KEY_MODES = ["Whole", "Dual", "Split", "Separate", "Whole-S", "Dual-S", "Split-US", "Split-LS", "Separate-S"]
REVERB_TYPES = ["Small Hall", "Medium Hall", "Large Hall", "Chapel", "Box", "Small Metal Room", "Small Room",
                "Medium Room", "Medium Large Room", "Large Room", "Single Delay 102ms", "Cross Delay 180ms",
                "Cross Delay 224ms", "Cross Delay 148-296ms", "Short Gate 200ms", "Long Gate 480ms", "Bright Hall",
                "Large Cave", "Steel Pan", "Delay 248ms", "Delay 338ms", "Cross Delay 157ms", "Cross Delay 252ms",
                "Cross Delay 274-137ms", "Gate Reverb", "Reverse Gate 360ms", "Reverse Gate 480ms",
                "Short Slap Back", "Medium Slap Back", "Long Slap Back", "Twisted Space", "Space"]
assert len(REVERB_TYPES) == 32


def _f(off, sym, short, mx, default, kind="Int", labels=None, shown=0):
    """offset, symbol (without block prefix), short name (without prefix),
    D-50 maximum, default (D-50 value), kind, labels, display offset (Int)"""
    if labels is not None and kind == "Int":
        kind = "Enum"
    if labels is not None:
        assert len(labels) == mx + 1, (sym, len(labels), mx)
    return dict(off=off, sym=sym, short=short, max=mx, default=default, kind=kind, labels=labels, shown=shown)


# *4-4: each partial block
PARTIAL = [
    _f(0, "wg_coarse", "Coarse", 72, 36, shown=-36),        # C1..C7, C4 = 0
    _f(1, "wg_fine", "Fine", 100, 50, shown=-50),
    _f(2, "wg_keyfollow", "PchKF", 16, 11, labels=KF16 + ["s1", "s2"]),
    _f(3, "wg_lfo_mode", "LFOMd", 3, 1, labels=["Off", "+", "-", "A&L"]),
    _f(4, "wg_penv_mode", "PEnvMd", 2, 1, labels=["Off", "+", "-"]),
    _f(5, "wg_bend_mode", "BendMd", 2, 2, labels=["Off", "KF", "Normal"]),
    _f(6, "wg_waveform", "Wave", 1, 1, labels=["Square", "Saw"]),
    _f(7, "wg_pcm_wave", "PCM", 99, 0, labels=PCM_WAVES),
    _f(8, "wg_pw", "PW", 100, 0),
    _f(9, "wg_pw_velo", "PWVelo", 14, 7, shown=-7),
    _f(10, "wg_pw_lfo", "PWLFO", 5, 0, labels=LFO_SEL),
    _f(11, "wg_pw_lfo_depth", "PWLFOD", 100, 0),
    _f(12, "wg_pw_after", "PWAftr", 14, 7, shown=-7),
    _f(13, "tvf_cutoff", "Cutoff", 100, 60),
    _f(14, "tvf_reso", "Reso", 30, 0),
    _f(15, "tvf_keyfollow", "FKF", 14, 11, labels=KF16),
    _f(16, "tvf_bias_point", "FBiasP", 127, 64, labels=BIAS_POINT),
    _f(17, "tvf_bias_level", "FBiasL", 14, 7, shown=-7),
    _f(18, "tvf_env_depth", "FEnvD", 100, 40),
    _f(19, "tvf_env_velo", "FEnvVe", 100, 30),
    _f(20, "tvf_env_dkf", "FEDKF", 4, 0),
    _f(21, "tvf_env_tkf", "FETKF", 4, 0),
    _f(22, "tvf_t1", "FT1", 100, 0),
    _f(23, "tvf_t2", "FT2", 100, 40),
    _f(24, "tvf_t3", "FT3", 100, 50),
    _f(25, "tvf_t4", "FT4", 100, 50),
    _f(26, "tvf_t5", "FT5", 100, 40),
    _f(27, "tvf_l1", "FL1", 100, 100),
    _f(28, "tvf_l2", "FL2", 100, 60),
    _f(29, "tvf_l3", "FL3", 100, 40),
    _f(30, "tvf_sus", "FSusL", 100, 30),
    _f(31, "tvf_end", "FEndL", 1, 0, labels=["0", "100"]),
    _f(32, "tvf_lfo", "FLFO", 5, 0, labels=LFO_SEL),
    _f(33, "tvf_lfo_depth", "FLFOD", 100, 0),
    _f(34, "tvf_after", "FAftr", 14, 7, shown=-7),
    _f(35, "tva_level", "Level", 100, 90),
    _f(36, "tva_velo", "AVelo", 100, 60, shown=-50),
    _f(37, "tva_bias_point", "ABiasP", 127, 64, labels=BIAS_POINT),
    _f(38, "tva_bias_level", "ABiasL", 12, 12, shown=-12),
    _f(39, "tva_t1", "AT1", 100, 0),
    _f(40, "tva_t2", "AT2", 100, 40),
    _f(41, "tva_t3", "AT3", 100, 50),
    _f(42, "tva_t4", "AT4", 100, 50),
    _f(43, "tva_t5", "AT5", 100, 40),
    _f(44, "tva_l1", "AL1", 100, 100),
    _f(45, "tva_l2", "AL2", 100, 90),
    _f(46, "tva_l3", "AL3", 100, 85),
    _f(47, "tva_sus", "ASusL", 100, 80),
    _f(48, "tva_end", "AEndL", 1, 0, labels=["0", "100"]),
    _f(49, "tva_velo_follow", "AVeloF", 4, 0),
    _f(50, "tva_tkf", "ATKF", 4, 0),
    _f(51, "tva_lfo", "ALFO", 5, 0, labels=LFO_SEL),
    _f(52, "tva_lfo_depth", "ALFOD", 100, 0),
    _f(53, "tva_after", "AAftr", 14, 7, shown=-7),
]

# *4-5: each common block (0..9: the tone name)
COMMON = [
    _f(10, "structure", "Struct", 6, 0, labels=[str(i) for i in range(1, 8)]),
    _f(11, "penv_velo", "PEnvVe", 2, 0),
    _f(12, "penv_tkf", "PEnvTKF", 4, 0),
    _f(13, "penv_t1", "PT1", 50, 0),
    _f(14, "penv_t2", "PT2", 50, 0),
    _f(15, "penv_t3", "PT3", 50, 0),
    _f(16, "penv_t4", "PT4", 50, 0),
    _f(17, "penv_l0", "PL0", 100, 50, shown=-50),
    _f(18, "penv_l1", "PL1", 100, 50, shown=-50),
    _f(19, "penv_l2", "PL2", 100, 50, shown=-50),
    _f(20, "penv_sus", "PSusL", 100, 50, shown=-50),
    _f(21, "penv_end", "PEndL", 100, 50, shown=-50),
    _f(22, "pmod_lfo", "PModLFO", 100, 0),
    _f(23, "pmod_lever", "PModLev", 100, 40),
    _f(24, "pmod_after", "PModAft", 100, 0),
    _f(25, "lfo1_wave", "L1Wave", 3, 0, labels=LFO_WAVES),
    _f(26, "lfo1_rate", "L1Rate", 100, 50),
    _f(27, "lfo1_delay", "L1Dly", 100, 0),
    _f(28, "lfo1_sync", "L1Sync", 2, 0, labels=["Off", "On", "Key"]),
    _f(29, "lfo2_wave", "L2Wave", 3, 0, labels=LFO_WAVES),
    _f(30, "lfo2_rate", "L2Rate", 100, 50),
    _f(31, "lfo2_delay", "L2Dly", 100, 0),
    _f(32, "lfo2_sync", "L2Sync", 1, 0, labels=["Off", "On"]),
    _f(33, "lfo3_wave", "L3Wave", 3, 0, labels=LFO_WAVES),
    _f(34, "lfo3_rate", "L3Rate", 100, 50),
    _f(35, "lfo3_delay", "L3Dly", 100, 0),
    _f(36, "lfo3_sync", "L3Sync", 1, 0, labels=["Off", "On"]),
    _f(37, "eq_low_freq", "LoEQF", 15, 0, labels=LOW_EQ_HZ),
    _f(38, "eq_low_gain", "LoEQG", 24, 12, shown=-12),
    _f(39, "eq_high_freq", "HiEQF", 21, 21, labels=HIGH_EQ_HZ),
    _f(40, "eq_high_q", "HiEQQ", 8, 3, labels=HIGH_EQ_Q),
    _f(41, "eq_high_gain", "HiEQG", 24, 12, shown=-12),
    _f(42, "chorus_type", "ChType", 7, 0, labels=CHORUS_TYPES),
    _f(43, "chorus_rate", "ChRate", 100, 30),
    _f(44, "chorus_depth", "ChDepth", 100, 40),
    _f(45, "chorus_balance", "ChBal", 100, 0),
    _f(46, "partial_mute", "PMute", 3, 3, labels=["Both Off", "P1 On", "P2 On", "Both On"]),
    _f(47, "partial_balance", "PBal", 100, 50),
]

# *4-6: the patch block (0..17: the patch name; 37..39 MIDI settings)
PATCH = [
    _f(18, "key_mode", "KeyMode", 8, 0, labels=KEY_MODES),
    _f(19, "split_point", "Split", 60, 24, labels=[note_name(36 + i) for i in range(61)]),
    _f(20, "porta_mode", "PortaMd", 2, 2, labels=["U", "L", "UL"]),
    _f(21, "hold_mode", "HoldMd", 2, 2, labels=["U", "L", "UL"]),
    _f(22, "u_key_shift", "UKeySh", 48, 24, shown=-24),
    _f(23, "l_key_shift", "LKeySh", 48, 24, shown=-24),
    _f(24, "u_fine", "UFine", 100, 50, shown=-50),
    _f(25, "l_fine", "LFine", 100, 50, shown=-50),
    _f(26, "bender_range", "BendRng", 12, 2),
    _f(27, "after_bend", "AftBend", 24, 12, shown=-12),
    _f(28, "porta_time", "PortaT", 100, 0),
    _f(29, "output_mode", "OutMode", 3, 0, labels=["1", "2", "3", "4"]),
    _f(30, "reverb_type", "RevType", 31, 1, labels=REVERB_TYPES),
    _f(31, "reverb_balance", "RevBal", 100, 30),
    _f(32, "total_volume", "Volume", 100, 80),
    _f(33, "tone_balance", "ToneBal", 100, 50),
    _f(34, "chase_mode", "ChaseMd", 2, 0, labels=["UL", "ULL", "ULU"]),
    _f(35, "chase_level", "ChaseL", 100, 0),
    _f(36, "chase_time", "ChaseT", 100, 0),
]

FIELDS = {"partial": PARTIAL, "common": COMMON, "patch": PATCH}

# the plugin's INIT sound differs from the field defaults here (block prefix, symbol): D-50 value
INIT_CHANGES = {
    ("u_p2", "wg_waveform"): 0,   # Upper: saw + square
    ("u_p2", "tvf_cutoff"): 55,
    ("u_p2", "tva_level"): 75,
    ("u", "chorus_balance"): 40,
    ("l", "partial_mute"): 0,     # Lower muted (Whole mode plays Upper only anyway)
}


def params():
    """every parameter: dict(symbol, short, block, block_index, off, byte (0..447), max, default, kind,
    labels, shown)"""
    out = []
    for b, (prefix, tag, kind) in enumerate(BLOCKS):
        for f in FIELDS[kind]:
            sym = f"{prefix}_{f['sym']}" if kind != "patch" else f"patch_{f['sym']}"
            short = f"{tag} {f['short']}" if tag else f["short"]
            default = INIT_CHANGES.get((prefix, f["sym"]), f["default"])
            out.append(dict(f, symbol=sym, short=short, block=prefix, block_kind=kind, block_index=b,
                            byte=b * 64 + f["off"], default=default))
    return out


def page_of(block_index):
    """port group page: 0 Patch, 1 Upper Common, 2 U Partial 1, 3 U Partial 2, 4 Lower Common, 5 L P1, 6 L P2"""
    return {6: 0, 2: 1, 0: 2, 1: 3, 5: 4, 3: 5, 4: 6}[block_index]


if __name__ == "__main__":
    ps = params()
    print(len(ps), "parameters;", max(len(p["short"]) for p in ps), "chars longest short name")


def write_cpp_header(path):
    """lasynth/la50/engine/La50Layout.generated.hpp: the field offsets for the engine"""
    o = ["// GENERATED by tools/la50_layout.py. Do not edit.", "// SPDX-License-Identifier: GPL-3.0-only",
         "#pragma once", "", "namespace la50 {", "",
         f"constexpr int kPatchBytes = {PATCH_BYTES};",
         "// block starts (block * 64)",
         "constexpr int kUpperP1 = 0, kUpperP2 = 64, kUpperCommon = 128, kLowerP1 = 192, kLowerP2 = 256,",
         "              kLowerCommon = 320, kPatch = 384;", ""]
    for kind, fields, pre in (("partial", PARTIAL, "P_"), ("common", COMMON, "C_"), ("patch", PATCH, "K_")):
        o.append(f"// {kind} block offsets")
        o.append(f"enum {kind.capitalize()}Field : int {{")
        for f in fields:
            o.append(f"    {pre}{f['sym'].upper()} = {f['off']},")
        o.append("};")
        o.append("")
    o.append("// the D-50 character set (names)")
    o.append(f'constexpr char kCharset[] = "{CHARSET}";')
    o.append("")
    o.append("// field maxima, by byte of the patch (0 = not a parameter: names, MIDI, extension)")
    mx = [0] * PATCH_BYTES
    for p in params():
        mx[p["byte"]] = p["max"]
    o.append("constexpr unsigned char kFieldMax[kPatchBytes] = {")
    for k in range(0, PATCH_BYTES, 32):
        o.append("    " + ", ".join(str(v) for v in mx[k:k + 32]) + ",")
    o.append("};")
    o.append("")
    o.append("// the INIT patch: every field's default, named \"INIT\"")
    raw = [0] * PATCH_BYTES
    for p in params():
        raw[p["byte"]] = p["default"]
    for k, ch in enumerate("INIT".ljust(18)):
        raw[6 * 64 + k] = CHARSET.index(ch)
    for blk in (2, 5):
        raw[blk * 64:blk * 64 + 10] = raw[6 * 64:6 * 64 + 10]
    o.append("constexpr unsigned char kInitPatch[kPatchBytes] = {")
    for k in range(0, PATCH_BYTES, 32):
        o.append("    " + ", ".join(str(v) for v in raw[k:k + 32]) + ",")
    o.append("};")
    o.append("")
    o.append("} // namespace la50")
    with open(path, "w") as f:
        f.write("\n".join(o) + "\n")
