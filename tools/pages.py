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
}


def clap_pages(voice):
    """PAGES[voice] with the "@" slots dropped and CLAP_FILL[voice] appended to their page."""
    pages = []
    for title, controls in PAGES[voice]:
        if any(c.startswith("@") for c in controls):
            controls = [c for c in controls if not c.startswith("@")] + CLAP_FILL[voice]
        pages.append((title, list(controls)))
    return pages
