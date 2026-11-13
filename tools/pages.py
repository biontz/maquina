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
                        "trans_freq", "trigger_note"]),
    ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                           "velo_amount", "velo_volume", "note_pitch"]),
    ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
             "volume"]),
]
METAL_TAIL = [
    ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                           "velo_amount", "velo_volume", "note_pitch"]),
    ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
             "volume"]),
]
PAGES = {
    "kick": DRUM_PAGES,
    "tom": DRUM_PAGES,
    "snare": [
        ("OSCILLATOR", ["@bank", "@patch", "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "noise_freq", "noise_mix"]),
        ("ENVELOPES", ["amp_attack", "amp_decay", "amp_slope", "amp_repeat", "pitch_decay", "pitch_slope",
                       "pitch_amount", "volume"]),
        ("TRANSIENT / MIX", ["trans_wave", "trans_volume", "trans_freq", "pan", "drive", "decimation",
                             "trigger_note", "note_pitch"]),
        ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                               "velo_amount", "velo_volume", "volume"]),
        ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
                 "volume"]),
    ],
    "cymbal": [
        ("OSCILLATOR", ["@bank", "@patch", "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "volume", "pan"]),
        ("MOD OSC", ["mod1_freq", "mod2_freq", "mod1_gain", "mod2_gain", "mod1_wave", "mod2_wave", "drive",
                     "decimation"]),
        ("ENVELOPE / TRANSIENT", ["amp_attack", "amp_decay", "amp_repeat", "amp_slope", "trans_wave",
                                  "trans_volume", "trans_freq", "trigger_note"]),
    ] + METAL_TAIL,
    "hihat": [
        ("OSCILLATOR", ["@bank", "@patch", "osc_coarse", "osc_fine", "osc_wave", "osc_pwm", "volume", "pan"]),
        ("MOD OSC", ["mod1_freq", "mod2_freq", "mod1_gain", "mod2_gain", "mod1_wave", "mod2_wave", "drive",
                     "decimation"]),
        ("ENVELOPE / TRANSIENT", ["amp_attack", "amp_decay", "amp_slope", "hat_mode", "trans_wave",
                                  "trans_volume", "trans_freq", "note_pitch"]),
        ("FILTER / VELOCITY", ["filter_freq", "filter_reso", "filter_type", "filter_drive", "velo_dest",
                               "velo_amount", "velo_volume", "hat_mode"]),
        ("LFO", ["lfo_rate", "lfo_sync", "lfo_amount", "lfo_wave", "lfo_retrigger", "lfo_offset", "lfo_target",
                 "trigger_note"]),
    ],
}

# parameters taking the "@bank"/"@patch" slots on CLAP pages (hosts browse presets themselves)
CLAP_FILL = {
    "kick": ["drive", "decimation"],
    "tom": ["drive", "decimation"],
    "snare": ["volume", "pan"],
    "cymbal": ["drive", "decimation"],
    "hihat": ["drive", "decimation"],
}


def clap_pages(voice):
    """PAGES[voice] with the "@" slots dropped and CLAP_FILL[voice] appended to their page."""
    pages = []
    for title, controls in PAGES[voice]:
        if any(c.startswith("@") for c in controls):
            controls = [c for c in controls if not c.startswith("@")] + CLAP_FILL[voice]
        pages.append((title, list(controls)))
    return pages
