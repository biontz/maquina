"""
pages.py - encoder/remote-control page layouts per voice, shared by
gen_machinista_profiles.py (Machinista controller profiles) and
gen_remote_pages.py (CLAP remote-controls pages, e.g. Bitwig).

Each page has exactly 8 slots of LV2/DPF parameter symbols. Slots starting
with "@" are Machinista pseudo-controls (preset bank/patch browsing); on CLAP
pages they are dropped and CLAP_FILL[voice] is appended to that page.

The layouts are hand-arranged: the LXR's own menu pages hold 3-8 controls,
which would leave encoders dead, so related firmware pages share a page.

SPDX-License-Identifier: GPL-3.0-only
"""

DRUM_PAGES = [
    ("OSCILLATOR", ["@bank", "@patch", "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "volume", "pan"]),
    ("ENVELOPES", ["amp_attack", "amp_decay", "amp_slope", "pitch_decay", "pitch_slope", "pitch_amount",
                   "drive", "decimation"]),
    ("FM / TRANSIENT", ["fm_amount", "fm_freq", "fm_wave", "fm_mode", "trans_wave", "trans_volume",
                        "trans_freq", "volume"]),
    ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                           "velo_amount", "velo_volume", "note_pitch"]),
    ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
             "gain"]),
]
METAL_TAIL = [
    ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                           "velo_amount", "velo_volume", "note_pitch"]),
    ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
             "gain"]),
]
PAGES = {
    "kick": DRUM_PAGES,
    "tom": DRUM_PAGES,
    "snare": [
        ("OSCILLATOR", ["@bank", "@patch", "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "noise_freq", "noise_mix"]),
        ("ENVELOPES", ["amp_attack", "amp_decay", "amp_slope", "amp_repeat", "pitch_decay", "pitch_slope",
                       "pitch_amount", "volume"]),
        ("TRANSIENT / MIX", ["trans_wave", "trans_volume", "trans_freq", "pan", "drive", "decimation",
                             "volume", "note_pitch"]),
        ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                               "velo_amount", "velo_volume", "volume"]),
        ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
                 "gain"]),
    ],
    "cymbal": [
        ("OSCILLATOR", ["@bank", "@patch", "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "volume", "pan"]),
        ("MOD OSC", ["mod1_freq", "mod2_freq", "mod1_gain", "mod2_gain", "mod1_wave", "mod2_wave", "drive",
                     "decimation"]),
        ("ENVELOPE / TRANSIENT", ["amp_attack", "amp_decay", "amp_repeat", "amp_slope", "trans_wave",
                                  "trans_volume", "trans_freq", "volume"]),
    ] + METAL_TAIL,
    "hihat": [
        ("OSCILLATOR", ["@bank", "@patch", "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "volume", "pan"]),
        ("MOD OSC", ["mod1_freq", "mod2_freq", "mod1_gain", "mod2_gain", "mod1_wave", "mod2_wave", "drive",
                     "decimation"]),
        ("ENVELOPE / TRANSIENT", ["amp_attack", "amp_decay", "amp_slope", "trans_wave", "trans_volume",
                                  "trans_freq", "volume", "pan"]),
        ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                               "velo_amount", "velo_volume", "note_pitch"]),
        ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
                 "gain"]),
    ],
    # Maquina Dredrum: levels/decays first, then one page per oscillator
    # (its 8 controls; mute only on SWITCHES), then the switches
    "dredrum": [
        ("MIX", ["@bank", "@patch", "osc1_level", "osc2_level", "osc3_level", "osc1_decay", "osc2_decay",
                 "osc3_decay"]),
    ] + [
        (f"OSC {n}", [f"osc{n}_wave", f"osc{n}_pitch", f"osc{n}_drop", f"osc{n}_decay", f"osc{n}_level",
                      f"osc{n}_track", f"osc{n}_octave", f"osc{n}_semi"]) for n in (1, 2, 3)
    ] + [
        ("SWITCHES", ["trig_mode", "click", "osc1_track", "osc2_track", "osc3_track", "osc1_mute",
                      "osc2_mute", "osc3_mute"]),
        ("MIX / OUT", ["gain", "osc1_level", "osc2_level", "osc3_level", "osc1_mute", "osc2_mute", "osc3_mute",
                       "trig_mode"]),
    ],
}

# Maquina Plethora on CLAP hosts (Bitwig): the whole module, one page per channel
PAGES["plethora"] = [
    ("A", ["@bank", "@patch", "a_algo", "a_x", "a_y", "a_ftype", "a_cutoff", "a_reso"]),
    ("A ENV / MIX", ["a_attack", "a_decay", "a_level", "a_mute", "b_level", "c_level", "env_sync", "trig_mode"]),
    ("B", ["b_algo", "b_x", "b_y", "b_ftype", "b_cutoff", "b_reso", "b_attack", "b_decay"]),
    ("C", ["c_source", "c_grit", "c_ftype", "c_cutoff", "c_reso", "c_attack", "c_decay", "c_level"]),
    ("MUTES", ["a_mute", "b_mute", "c_mute", "gain", "trig_mode", "a_level", "b_level", "c_level"]),
]

# Maquina Oneiroi / Oneiroi FX: the module's sections, SHIFT functions next
# to their knob, each effect with its dry/wet fader
_ONEIROI_FX_PAGES = [
    ("FILTER / RESO", ["flt_cutoff", "flt_reso", "flt_mode", "flt_pos", "res_tune", "res_feedback",
                       "res_dissonance", "mix_resonator"]),
    ("ECHO / AMBIENCE", ["echo_density", "echo_repeats", "echo_filter", "mix_echo", "amb_spacetime", "amb_decay",
                         "amb_autopan", "mix_ambience"]),
]
PAGES["oneiroi"] = [
    ("OSC / MIX", ["@bank", "@patch", "osc_pitch", "osc_detune", "vol_sine", "vol_osc2", "vol_looper", "env_drone"]),
    ("LOOPER", ["loop_speed", "loop_start", "loop_length", "loop_sos", "loop_filter", "loop_record", "loop_clear",
                "vol_looper"]),
] + _ONEIROI_FX_PAGES + [
    ("MOD / OSC", ["mod_level", "mod_speed", "mod_type", "osc_octave", "osc_unison", "osc2_mode", "mix_filter",
                   "sync"]),
    ("ENVELOPE", ["env_attack", "env_decay", "env_sustain", "env_release", "env_drone", "gain", "osc_pitch",
                  "osc_octave"]),
]
PAGES["oneiroi_fx"] = [
    ("INPUT / LOOPER", ["@bank", "@patch", "vol_input", "vol_looper", "loop_record", "loop_source", "loop_clear",
                        "sync"]),
    ("LOOPER", ["loop_speed", "loop_start", "loop_length", "loop_sos", "loop_filter", "loop_record", "vol_looper",
                "vol_input"]),
] + _ONEIROI_FX_PAGES + [
    ("MOD / MIX", ["mod_level", "mod_speed", "mod_type", "mix_filter", "mix_resonator", "mix_echo", "mix_ambience",
                   "gain"]),
]

# Maquina Klang: the big knobs + LPG first, then the envelope amounts and tuning
PAGES["klang"] = [
    ("SOUND", ["@bank", "@patch", "model", "harmonics", "timbre", "morph", "lpg_decay", "lpg_colour"]),
    ("ENV / TUNE", ["fm_env", "timbre_env", "morph_env", "lpg_mode", "coarse", "fine", "key_track", "gain"]),
    ("MIX", ["output", "gain", "model", "harmonics", "timbre", "morph", "lpg_decay", "coarse"]),
]
# Maquina Klong / Klong FX: the resonator's knobs, then tuning and output
PAGES["klong"] = [
    ("RESONATOR", ["@bank", "@patch", "model", "polyphony", "structure", "brightness", "damping", "position"]),
    ("TUNE / MIX", ["coarse", "fine", "output", "gain", "structure", "brightness", "damping", "position"]),
]
PAGES["klong_fx"] = [
    ("RESONATOR", ["@bank", "@patch", "model", "polyphony", "structure", "brightness", "damping", "position"]),
    ("TUNE / MIX", ["coarse", "fine", "note_source", "output", "gain", "structure", "damping", "position"]),
]
# Maquina Acida: the 303's knobs on one page (the panel's order), then the switches
PAGES["acida"] = [
    ("SYNTH", ["@bank", "@patch", "tune", "cutoff", "resonance", "env_mod", "decay", "accent"]),
    ("WAVE / MIX", ["waveform", "engine", "accent_vel", "gain", "cutoff", "resonance", "env_mod", "decay"]),
]
# Maquina Acida FX: the inserts, then the delay
PAGES["acida_fx"] = [
    ("DIST / PCF", ["@bank", "@patch", "dist_amount", "dist_shape", "pcf_freq", "pcf_reso", "pcf_amount",
                    "pcf_decay"]),
    ("PCF / COMP", ["dist_on", "pcf_on", "pcf_wave", "pcf_mode", "comp_on", "comp_amount", "comp_threshold",
                    "gain"]),
    ("DELAY", ["delay_on", "delay_send", "delay_steps", "delay_mode", "delay_feedback", "delay_pan", "dist_amount",
               "pcf_freq"]),
]
# Maquina BL-101: the panel's sections in its order (MODULATOR, VCO, SOURCE
# MIXER, VCF, VCA, ENV), then voicing, chord memory, the DRAW steps (one
# encoder per step) and CHOP (a mask selector per source)
PAGES["bl101"] = [
    ("MOD / VCO", ["@bank", "@patch", "modulator_rate", "modulator_wave", "vco_mod", "vco_range", "vco_pw",
                   "vco_pwm_src"]),
    ("MIXER", ["mix_pulse", "mix_saw", "mix_sub", "mix_sub_type", "mix_noise", "mix_noise_type", "vco_fine",
               "patch_level"]),
    ("VCF / VCA", ["vcf_freq", "vcf_res", "vcf_env", "vcf_mod", "vcf_kybd", "vca_mode", "env_trigger", "gain"]),
    ("ENV / LFO", ["adsr_attack", "adsr_decay", "adsr_sustain", "adsr_release", "modulator_sync",
                   "modulator_fast", "modulator_trig", "porta_time"]),
    ("VOICE", ["voice_mode", "porta_mode", "porta_time", "transpose", "bend_vco", "bend_vcf", "stereo_spread",
               "unison_detune"]),
    ("CHORD", ["chord2_on", "chord2_shift", "chord3_on", "chord3_shift", "chord4_on", "chord4_shift", "voice_mode",
               "transpose"]),
    ("DRAW 1-8", [f"draw_{k}" for k in range(1, 9)]),
    ("DRAW 9-16", [f"draw_{k}" for k in range(9, 17)]),
    ("CHOP", ["chop_overtone", "chop_comb", "chop_mask_sqr", "chop_mask_saw", "chop_mask_sub", "chop_mask_noise",
              "draw_mode", "draw_mult"]),
]
# Maquina BL-101 FX
PAGES["bl101_fx"] = [
    ("CHORUS / DELAY", ["@bank", "@patch", "chorus_mode", "dly_level", "dly_time", "dly_feedback", "dly_sync",
                        "lofi_mode"]),
    ("REVERB / LO-FI", ["rev_level", "rev_time", "rev_type", "rev_predelay", "lofi_amount", "lofi_mix", "lofi_mode",
                        "gain"]),
]

# Maquina LA-50: per tone the sound's main knobs (partial 1 / 2 side by side), then the patch;
# everything else (D-50 detail) is edited in the editor: OFF_PAGE["la50"]
def _la50_tone(t, name):
    return [
        (f"{name} TVF", [f"{t}_p1_tvf_cutoff", f"{t}_p1_tvf_reso", f"{t}_p1_tvf_env_depth", f"{t}_p1_tvf_lfo_depth",
                         f"{t}_p2_tvf_cutoff", f"{t}_p2_tvf_reso", f"{t}_p2_tvf_env_depth", f"{t}_p2_tvf_lfo_depth"]),
        (f"{name} TVF ENV", [f"{t}_p1_tvf_t1", f"{t}_p1_tvf_t2", f"{t}_p1_tvf_sus", f"{t}_p1_tvf_t5",
                             f"{t}_p2_tvf_t1", f"{t}_p2_tvf_t2", f"{t}_p2_tvf_sus", f"{t}_p2_tvf_t5"]),
        (f"{name} TVA ENV", [f"{t}_p1_tva_t1", f"{t}_p1_tva_t2", f"{t}_p1_tva_sus", f"{t}_p1_tva_t5",
                             f"{t}_p2_tva_t1", f"{t}_p2_tva_t2", f"{t}_p2_tva_sus", f"{t}_p2_tva_t5"]),
        (f"{name} WG", [f"{t}_p1_wg_waveform", f"{t}_p1_wg_pcm_wave", f"{t}_p1_wg_pw", f"{t}_p1_tva_level",
                        f"{t}_p2_wg_waveform", f"{t}_p2_wg_pcm_wave", f"{t}_p2_wg_pw", f"{t}_p2_tva_level"]),
        (f"{name} COMMON", [f"{t}_structure", f"{t}_partial_balance", f"{t}_partial_mute", f"{t}_lfo1_rate",
                            f"{t}_pmod_lfo", f"{t}_chorus_type", f"{t}_chorus_balance", f"{t}_lfo1_wave"]),
    ]


PAGES["la50"] = [
    ("PATCH", ["@bank", "@patch", "patch_key_mode", "patch_tone_balance", "patch_reverb_type", "patch_reverb_balance",
               "patch_total_volume", "gain"]),
] + _la50_tone("u", "UPPER") + _la50_tone("l", "LOWER") + [
    ("PLAY", ["patch_split_point", "patch_bender_range", "patch_after_bend", "patch_porta_time", "patch_porta_mode",
              "patch_hold_mode", "patch_u_key_shift", "patch_l_key_shift"]),
]

# Maquina Strings: the strings, the wave layer, filter + blend motion, amp + effects, then vibrato,
# unison and the rest (provisional until the engine is in)
PAGES["strings"] = [
    ("STRINGS", ["@bank", "@patch", "register", "shape", "detune", "tone", "blend", "keys"]),
    ("WAVE", ["wave_bank", "wave_x", "wave_y", "wave_octave", "wave_lfo_rate", "wave_lfo_depth", "wave_env_pos",
              "layer_detune"]),
    ("FILTER / BLEND", ["filter_mode", "filter_cutoff", "filter_reso", "filter_env", "filter_keytrack",
                        "blend_shift", "blend_time", "blend_velo"]),
    ("AMP / FX", ["attack", "decay", "sustain", "release", "fx_mode", "fx_amount", "reverb", "gain"]),
    ("VIBRATO / MORE", ["vibrato", "vib_depth", "vib_delay", "velocity", "fx_slow", "fx_fast", "wave_unison",
                        "wave_spread"]),
    ("MIXER", ["strings_on", "strings_level", "wave_on", "wave_level", "blend", "keys", "layer_detune", "gain"]),
]

# Machinista profiles that differ from PAGES: only these pages, and the
# profile (and its preset banks) only holds the parameters on them; the rest
# keep their defaults. Maquina Plethora on Machinista = channel A only (B and
# C default to Level 0, so they are silent and not computed).
MACHINISTA_PAGES = {
    "plethora": [
        ("A", ["@bank", "@patch", "a_algo", "a_x", "a_y", "a_ftype", "a_cutoff", "a_reso"]),
        ("A ENV", ["a_attack", "a_decay", "a_level", "a_mute", "trig_mode", "a_x", "a_y", "gain"]),
    ],
}

# parameters deliberately on no page: Maquina BL-101's Custom CHOP mask bytes
# (presets carry them; the editor's grid edits them, an encoder can't)
OFF_PAGE = {
    "bl101": {f"chop_bits_{s}_{h}" for s in ("sqr", "saw", "sub", "noise") for h in ("lo", "hi")},
    "la50": None,  # filled below: everything not on PAGES["la50"]
}

# parameters taking the "@bank"/"@patch" slots on CLAP pages (hosts browse presets themselves)
CLAP_FILL = {
    "kick": ["drive", "decimation"],
    "tom": ["drive", "decimation"],
    "snare": ["volume", "pan"],
    "cymbal": ["drive", "decimation"],
    "hihat": ["drive", "decimation"],
    "dredrum": ["trig_mode", "click"],
    "plethora": ["a_level", "a_decay"],
    "oneiroi": ["mix_echo", "mix_ambience"],
    "oneiroi_fx": ["mix_echo", "mix_ambience"],
    "klang": ["coarse", "fine"],
    "klong": ["coarse", "fine"],
    "klong_fx": ["coarse", "note_source"],
    "acida": ["waveform", "gain"],
    "acida_fx": ["dist_on", "pcf_on"],
    "bl101": ["modulator_sync", "modulator_fast"],
    "bl101_fx": ["rev_level", "rev_type"],
    "la50": ["u_p1_tvf_cutoff", "l_p1_tvf_cutoff"],
    "strings": ["vibrato", "vib_delay"],
}


def _la50_off_page():
    import la50_layout
    on = {c for _, controls in PAGES["la50"] for c in controls}
    return {p["symbol"] for p in la50_layout.params()} - on


OFF_PAGE["la50"] = _la50_off_page()


def clap_pages(voice):
    """PAGES[voice] with the "@" slots dropped and CLAP_FILL[voice] appended to their page."""
    pages = []
    for title, controls in PAGES[voice]:
        if any(c.startswith("@") for c in controls):
            controls = [c for c in controls if not c.startswith("@")] + CLAP_FILL[voice]
        pages.append((title, list(controls)))
    return pages
