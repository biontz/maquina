#!/usr/bin/env python3
"""
gen_params.py - build the plugins' parameter tables from the LXR firmware.

Reads the firmware's own UI tables, so every plugin exposes exactly the
parameters, ranges, option lists and page layout of the hardware voice:

  Inc/menu/menuPages.h   per-voice menu pages (8 slots each)  -> params + pages
  Src/menu/menu.c        parameter_dtypes[]                    -> ranges/kinds
  Inc/menu/MenuText.h    option name arrays                     -> enum labels
  Src/menu/Cc2Text.c     modTargets[], modTargetVoiceOffsets    -> mod targets
  Inc/Parameters.h       ParamEnums                             -> numbers (via cc)

Outputs (checked in, regenerate after a firmware update):
  common/VoiceTables.generated.hpp
  machinista/layout.json   (pages per plugin; used by gen_machinista_profiles.py)

LV2 symbols are API: hosts and Machinista profiles store them. SYMBOLS below
pins them; never rename an existing one.

Maquina Dredrum is not an LXR voice: its table is DREDRUM below (the
Dredrum firmware has no UI tables, only 3 x 5 pot/button values plus the
CLICK switch). It is appended after the LXR voices, then Maquina Plethora
(PLETHORA below; algorithm list from befaco/plethora/third_party/noise-plethora Banks_Def.hpp),
then Maquina Oneiroi and Oneiroi FX (oneiroi_params below).
"""
import json
import math
import os
import re
import subprocess
import sys
import tempfile

from layout import COMMON, DRUM02, LXR_FW, PLETHORA, ROOT

FW = os.path.join(LXR_FW, "Core")

PLUGINS = [
    # id, display name, engine voice, menu page
    ("kick", "Kick", 0, 0),
    ("tom", "Tom", 1, 1),
    ("snare", "Snare", 3, 3),
    ("cymbal", "Cymbal", 4, 4),
    ("hihat", "HiHat", 5, 5),
]

PAGE_TITLES = ["OSC", "AMP EG", "MOD", "FM", "TRANSIENT", "FILTER", "LFO", "MIX"]
PAGE_TITLES_METAL = {3: "MOD OSC"}  # cymbal / hihat page 4

# (page, TEXT_ key, occurrence) -> (symbol, name). Occurrence counts repeats of
# the same TEXT_ on one page (the two mod-osc waveforms).
SYMBOLS = {
    (0, "COARSE", 0): ("osc_coarse", "Osc Coarse"),
    (0, "FINE", 0): ("osc_fine", "Osc Fine"),
    (0, "WAVEFORM", 0): ("osc_wave", "Osc Wave"),
    (0, "PWM", 0): ("osc_pwm", "Osc PWM"),
    (0, "NOISE", 0): ("noise_freq", "Noise Freq"),
    (0, "MIX", 0): ("noise_mix", "Noise Mix"),
    (1, "ATTACK", 0): ("amp_attack", "Amp Attack"),
    (1, "DECAY", 0): ("amp_decay", "Amp Decay"),
    # HiHat: one decay (the plugin plays the closed-hat trigger; no Closed/Open)
    (1, "DECAY_CLOSED", 0): ("amp_decay", "Amp Decay"),
    (1, "REPEAT", 0): ("amp_repeat", "Amp Repeat"),
    (1, "EG_SLOPE", 0): ("amp_slope", "Amp Slope"),
    (2, "PITCH_DECAY", 0): ("pitch_decay", "Pitch Decay"),
    (2, "EG_SLOPE", 0): ("pitch_slope", "Pitch Slope"),
    (2, "MOD_AMOUNT", 0): ("pitch_amount", "Pitch Amount"),
    (2, "VEL_DEST", 0): ("velo_dest", "Velocity Dest"),
    (2, "VEL_AMT", 0): ("velo_amount", "Velocity Amount"),
    (2, "VEL_MOD_VOL", 0): ("velo_volume", "Velocity > Vol"),
    (3, "FM_AMOUNT", 0): ("fm_amount", "FM Amount"),
    (3, "FM_FREQ", 0): ("fm_freq", "FM Freq"),
    (3, "WAVEFORM", 0): ("fm_wave", "FM Wave"),
    (3, "MODE", 0): ("fm_mode", "FM Mode"),
    (3, "MOD_OSC1_FREQ", 0): ("mod1_freq", "Mod1 Freq"),
    (3, "MOD_OSC2_FREQ", 0): ("mod2_freq", "Mod2 Freq"),
    (3, "MOD_OSC1_GAIN", 0): ("mod1_gain", "Mod1 Gain"),
    (3, "MOD_OSC2_GAIN", 0): ("mod2_gain", "Mod2 Gain"),
    (3, "WAVEFORM", 1): ("mod2_wave", "Mod2 Wave"),
    (4, "TRANSIENT_WAVE", 0): ("trans_wave", "Transient Wave"),
    (4, "TRANSIENT_VOLUME", 0): ("trans_volume", "Transient Vol"),
    (4, "TRANSIENT_FREQ", 0): ("trans_freq", "Transient Freq"),
    (5, "FILTER_F", 0): ("filter_freq", "Filter Freq"),
    (5, "FILTER_RESO", 0): ("filter_reso", "Filter Reso"),
    (5, "FILTER_TYPE", 0): ("filter_type", "Filter Type"),
    (5, "FILTER_DRIVE", 0): ("filter_drive", "Filter Drive"),
    (6, "FREQ_LFO", 0): ("lfo_rate", "LFO Rate"),
    (6, "SYNC_LFO", 0): ("lfo_sync", "LFO Sync"),
    (6, "MOD_LFO", 0): ("lfo_amount", "LFO Amount"),
    (6, "WAVE_LFO", 0): ("lfo_wave", "LFO Wave"),
    (6, "RETRIGGER_LFO", 0): ("lfo_retrigger", "LFO Retrigger"),
    (6, "OFFSET_LFO", 0): ("lfo_offset", "LFO Phase"),
    (6, "TARGET_LFO", 0): ("lfo_target", "LFO Target"),
    (7, "VOLUME", 0): ("volume", "Volume"),
    (7, "PAN", 0): ("pan", "Pan"),
    (7, "SAMPLE_RATE", 0): ("decimation", "Sample Rate"),
    (7, "DRIVE", 0): ("drive", "Drive"),
}
# Names shown by hosts (Bitwig remote knobs cut at ~9 characters, Machinista
# displays are small). The long names above stay the modulation-target labels,
# which preset files store (lfo_target = Filter Freq). Max 9 characters.
SHORT_NAMES = {
    "osc_coarse": "Coarse", "osc_fine": "Fine", "osc_wave": "Wave", "osc_pwm": "PWM",
    "noise_freq": "Noise Frq", "noise_mix": "Noise Mix",
    "amp_attack": "Attack", "amp_decay": "Decay", "amp_repeat": "Repeat", "amp_slope": "Slope",
    "pitch_decay": "Pitch Dec", "pitch_slope": "Pitch Slp", "pitch_amount": "Pitch Amt",
    "velo_dest": "Velo Dest", "velo_amount": "Velo Amt", "velo_volume": "Velo>Vol",
    "fm_amount": "FM Amt", "fm_freq": "FM Freq", "fm_wave": "FM Wave", "fm_mode": "FM Mode",
    "mod1_freq": "1 Freq", "mod2_freq": "2 Freq", "mod1_gain": "1 Gain", "mod2_gain": "2 Gain",
    "mod1_wave": "1 Wave", "mod2_wave": "2 Wave",
    "trans_wave": "T Wave", "trans_volume": "T Vol", "trans_freq": "T Freq",
    "filter_freq": "Cutoff", "filter_reso": "Reso", "filter_type": "Flt Type", "filter_drive": "Flt Drive",
    "lfo_rate": "LFO Rate", "lfo_sync": "LFO Sync", "lfo_amount": "LFO Amt", "lfo_wave": "LFO Wave",
    "lfo_retrigger": "LFO Retrg", "lfo_offset": "LFO Phase", "lfo_target": "LFO Dest",
    "volume": "Volume", "pan": "Pan", "decimation": "Decimate", "drive": "Drive",
}
MAX_SHORT_NAME = 9

# Mod-osc page of cymbal/hihat: its first WAVEFORM is mod osc 1.
SYMBOLS_METAL = {(3, "WAVEFORM", 0): ("mod1_wave", "Mod1 Wave")}

# Hardware-only entries left out of a single-voice plugin. NOTE is the
# hardware's per-voice MIDI trigger note: in a DAW every note triggers
# (PAR_MIDI_NOTE stays 0 = Any); Key Track (ExtraParams.hpp) sets the pitch.
EXCLUDE_TEXT = {"AUDIO_OUT", "PAT_LENGTH", "MIDI_CHANNEL", "TARGET_VOICE_LFO", "NOTE"}

# Parameters a voice has but its menu pages do not show:
# pid -> [(page, PAR_, (symbol, name), DTYPE_)]. The hihat's PAR_VELOD6_OPEN is
# not one: the plugin only plays the closed-hat trigger, with its Amp Decay.
EXTRA = {}

LONG_LABELS = {
    "waveformNames": ["Sine", "Triangle", "Saw", "Square", "Noise", "PWM"],
    "filterTypes": ["Lowpass", "Highpass", "Bandpass", "Unity BP", "Notch", "Peak", "Lowpass 2", "Off"],
    "lfoWaveNames": ["Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random", "Exp Up", "Exp Down"],
    "syncRateNames": ["Off", "4/1", "2/1", "1/1", "1/2", "1/3", "1/4", "1/6", "1/8", "1/12", "1/16", "1/32"],
    "transientNames": ["Snap", "Offset", "Click", "Click 2", "Tick", "Kick", "Rim", "Drop", "Hat",
                       "Clap", "Kick 2", "Snare", "Tom", "Snap 2"],
    "mixFmNames": ["FM", "Mix"],
}
MENU_ARRAYS = {
    "MENU_WAVEFORM": "waveformNames",
    "MENU_FILTER": "filterTypes",
    "MENU_LFO_WAVES": "lfoWaveNames",
    "MENU_SYNC_RATES": "syncRateNames",
    "MENU_TRANS": "transientNames",
    "MENU_MIX_FM": "mixFmNames",
    "MENU_RETRIGGER": None,  # mapped to Off/On (retrigger on own voice)
}
# PM63 parameters shown as value-60 instead of value-63 (menu.c)
PM60 = re.compile(r"PAR_(COARSE\d|FM_FREQ\d|MOD_OSC_F[12](_CYM)?)$")

# ---------------------------------------------------------------------------
# Maquina Dredrum (euro-modules/dredrum/engine/, euro-modules/dredrum/third_party/dredrum). Engine voice number
# kDredrumVoice in VoiceTypes.hpp. Pages: 0..2 = oscillator 1..3, 3 = mode.
# Values are the firmware's own 0..255 (8-bit ADC) per pot.
DREDRUM_VOICE = 16
DREDRUM_WAVES = ["Sine", "Square", "Saw", "Noise"]
DREDRUM_PAGE_TITLES = ["OSC 1", "OSC 2", "OSC 3", "MODE"]
# Init sound (the module boots with whatever its EEPROM holds): a sine kick
# on oscillator 1; oscillators 2 and 3 set up for a noise / click layer.
DREDRUM_INIT = {
    1: dict(wave=0, pitch=20, drop=110, decay=190, level=255),
    2: dict(wave=3, pitch=128, drop=0, decay=0, level=0),
    3: dict(wave=1, pitch=160, drop=0, decay=0, level=0),
}


# ---------------------------------------------------------------------------
# Maquina Plethora (Befaco Noise Plethora, VCV port in befaco/plethora/third_party/noise-plethora).
# Engine voice number kPlethoraVoice in VoiceTypes.hpp. Pages: 0..2 = channel
# A, B, C, 3 = global. Knobs 0..127 map onto the module's 0..1 controls.
PLETHORA_VOICE = 17
PLETHORA_PAGE_TITLES = ["A", "B", "C", "GLOBAL"]
PLETHORA_BANKS_DEF = os.path.join(PLETHORA, "third_party", "noise-plethora", "src", "noise-plethora", "plugins",
                                  "Banks_Def.hpp")


def plethora_algorithms():
    """["1-01 radioOhNo", ...] in bank/program order (the module's 3 banks + the VCV port's)."""
    s = open(PLETHORA_BANKS_DEF).read()
    out = []
    for bank, body in re.findall(r"#define BANKS_DEF_(\d+) \{(.*?)\n\s*\}", s, re.S):
        names = re.findall(r'\{ "(\w+)", [0-9.]+ \}', body)
        out += [f"{bank}-{i + 1:02d} {n}" for i, n in enumerate(names)]
    if len(out) != 60:
        sys.exit(f"{PLETHORA_BANKS_DEF}: expected 60 algorithms, found {len(out)}")
    return out


def plethora_params():
    algos = plethora_algorithms()
    params = []

    def p(symbol, name, short, page, kind, default, fwmax=127, labels=None):
        params.append(dict(symbol=symbol, name=name, short=short, page=page, kind=kind, fwmax=fwmax,
                           labels=labels, default=default))

    for ch, page in (("a", 0), ("b", 1)):
        C = ch.upper()
        p(f"{ch}_algo", f"{C} Algorithm", f"{C} Algo", page, "Enum", 0 if ch == "a" else 10, len(algos) - 1, algos)
        p(f"{ch}_x", f"{C} X", f"{C} X", page, "Int", 64)
        p(f"{ch}_y", f"{C} Y", f"{C} Y", page, "Int", 64)
    p("c_source", "C Source", "C Source", 2, "Enum", 0, 1, ["Gritty", "White"])
    p("c_grit", "C Grit", "C Grit", 2, "Int", 64)
    for ch, page in (("a", 0), ("b", 1), ("c", 2)):
        C = ch.upper()
        p(f"{ch}_ftype", f"{C} Filter Type", f"{C} Filter", page, "Enum", 0, 2, ["Lowpass", "Bandpass", "Highpass"])
        p(f"{ch}_cutoff", f"{C} Cutoff", f"{C} Cutoff", page, "Int", 127)
        p(f"{ch}_reso", f"{C} Resonance", f"{C} Reso", page, "Int", 0)
        # Maquina additions: envelope, level (A on, B and C off), mute
        p(f"{ch}_attack", f"{C} Attack", f"{C} Attack", page, "Int", 0)
        p(f"{ch}_decay", f"{C} Decay", f"{C} Decay", page, "Int", 70)
        p(f"{ch}_level", f"{C} Level", f"{C} Level", page, "Int", 127 if ch == "a" else 0)
        p(f"{ch}_mute", f"{C} Mute", f"{C} Mute", page, "Toggle", 0, 1, ["Off", "On"])
    p("env_sync", "Envelope Sync", "Env Sync", 3, "Toggle", 0, 1, ["Off", "On"])
    p("trig_mode", "Trigger Mode", "Mode", 3, "Enum", 0, 1, ["Trig", "Gate"])
    # display order: per channel, then global
    order = {s: i for i, s in enumerate(
        [f"{c}_{k}" for c in "ab" for k in ("algo", "x", "y", "ftype", "cutoff", "reso", "attack", "decay", "level",
                                            "mute")] +
        ["c_source", "c_grit"] + [f"c_{k}" for k in ("ftype", "cutoff", "reso", "attack", "decay", "level", "mute")] +
        ["env_sync", "trig_mode"])}
    params.sort(key=lambda q: order[q["symbol"]])
    for q in params:
        q.update(par="0", offset=0, fwmin=0, modtargets=None)
        if len(q["short"]) > MAX_SHORT_NAME:
            sys.exit(f"plethora: '{q['short']}' is longer than {MAX_SHORT_NAME} characters")
    pages = [dict(title=t, controls=[q["symbol"] for q in params if q["page"] == i])
             for i, t in enumerate(PLETHORA_PAGE_TITLES)]
    return params, pages


def dredrum_params():
    params = []
    for n in (1, 2, 3):
        init = DREDRUM_INIT[n]
        params += [
            dict(symbol=f"osc{n}_wave", name=f"Osc {n} Wave", short=f"{n} Wave", page=n - 1, kind="Enum",
                 fwmax=3, labels=DREDRUM_WAVES, default=init["wave"]),
            dict(symbol=f"osc{n}_pitch", name=f"Osc {n} Pitch", short=f"{n} Pitch", page=n - 1, kind="Int",
                 fwmax=255, labels=None, default=init["pitch"]),
            dict(symbol=f"osc{n}_drop", name=f"Osc {n} Pitch Drop", short=f"{n} Drop", page=n - 1, kind="Int",
                 fwmax=255, labels=None, default=init["drop"]),
            dict(symbol=f"osc{n}_decay", name=f"Osc {n} Decay", short=f"{n} Decay", page=n - 1, kind="Int",
                 fwmax=255, labels=None, default=init["decay"]),
            dict(symbol=f"osc{n}_level", name=f"Osc {n} Level", short=f"{n} Level", page=n - 1, kind="Int",
                 fwmax=255, labels=None, default=init["level"]),
            # Maquina additions (not on the module): key tracking (note 60 =
            # the set pitch) and a transpose; all off = the firmware's pitch
            dict(symbol=f"osc{n}_track", name=f"Osc {n} Key Track", short=f"{n} Track", page=n - 1,
                 kind="Toggle", fwmax=1, labels=["Off", "On"], default=0),
            # performance switch: not in presets, never randomized
            dict(symbol=f"osc{n}_mute", name=f"Osc {n} Mute", short=f"{n} Mute", page=n - 1,
                 kind="Toggle", fwmax=1, labels=["Off", "On"], default=0),
            dict(symbol=f"osc{n}_octave", name=f"Osc {n} Octave", short=f"{n} Octave", page=n - 1, kind="Int",
                 fwmax=8, offset=-4, labels=None, default=0),
            dict(symbol=f"osc{n}_semi", name=f"Osc {n} Semitone", short=f"{n} Semi", page=n - 1, kind="Int",
                 fwmax=24, offset=-12, labels=None, default=0),
        ]
    params.append(dict(symbol="click", name="Osc 3 Click", short="Click", page=2, kind="Toggle", fwmax=1,
                       labels=["Off", "On"], default=0))
    params.append(dict(symbol="trig_mode", name="Trigger Mode", short="Mode", page=3, kind="Enum", fwmax=1,
                       labels=["Trig", "Gate"], default=0))
    for p in params:
        p.setdefault("offset", 0)
        p.update(par="0", fwmin=0, modtargets=None)
        if len(p["short"]) > MAX_SHORT_NAME:
            sys.exit(f"dredrum: '{p['short']}' is longer than {MAX_SHORT_NAME} characters")
    pages = []
    for i, title in enumerate(DREDRUM_PAGE_TITLES):
        pages.append(dict(title=title, controls=[p["symbol"] for p in params if p["page"] == i]))
    return params, pages


# ---------------------------------------------------------------------------
# Maquina Oneiroi / Oneiroi FX (Befaco Oneiroi, befaco/oneiroi/third_party/oneiroi). Engine
# voice numbers kOneiroiVoice / kOneiroiFxVoice in VoiceTypes.hpp. Pages are
# the module's sections (port groups): 0 looper, 1 oscillators, 2 filter,
# 3 resonator, 4 echo, 5 ambience, 6 modulation, 7 mix (faders), 8 envelope,
# 9 global. Knobs 0..127 map onto the module's 0..1 controls; "centred"
# knobs are -64..+63 with 0 at the module's centre position (OneiroiHost.cpp).
ONEIROI_VOICE = 18
ONEIROI_FX_VOICE = 19
ONEIROI_PAGE_TITLES = ["LOOPER", "OSCILLATORS", "FILTER", "RESONATOR", "ECHO", "AMBIENCE", "MODULATION", "MIX",
                       "ENVELOPE", "GLOBAL"]


def oneiroi_params(fx):
    params = []

    def p(symbol, name, short, page, kind, default, fwmax=127, offset=0, labels=None):
        params.append(dict(symbol=symbol, name=name, short=short, page=page, kind=kind, fwmax=fwmax,
                           offset=offset, labels=labels, default=default))

    def knob(symbol, name, short, page, default):
        p(symbol, name, short, page, "Int", default)

    def centred(symbol, name, short, page, default):
        p(symbol, name, short, page, "Int", default, 127, -64)

    onoff = ["Off", "On"]
    # LOOPER (+ SOS and Filter: the module's SHIFT functions of START / LENGTH)
    centred("loop_speed", "Looper Speed", "Speed", 0, 32)  # +32 = 1x forward
    knob("loop_start", "Looper Start", "Start", 0, 0)
    knob("loop_length", "Looper Length", "Length", 0, 127)
    knob("loop_sos", "Looper Sound On Sound", "SOS", 0, 0)
    centred("loop_filter", "Looper Filter", "Loop Filt", 0, 0)
    p("loop_record", "Looper Record", "Record", 0, "Toggle", 0, 1, labels=onoff)
    if fx:
        p("loop_source", "Looper Record Source", "Rec Src", 0, "Enum", 0, 1, labels=["Input", "Output"])
    p("loop_clear", "Looper Clear", "Clear", 0, "Toggle", 0, 1, labels=onoff)
    if not fx:
        # OSCILLATORS (+ Octave, Unison: SHIFT of PITCH / DETUNE)
        p("osc_pitch", "Oscillator Pitch", "Pitch", 1, "Int", 0, 120, -60)  # 1/10 semitones
        p("osc_octave", "Oscillator Octave", "Octave", 1, "Int", 0, 8, -4)
        knob("osc_detune", "Oscillator Detune / Offset", "Detune", 1, 0)
        centred("osc_unison", "Oscillator Unison", "Unison", 1, 0)
        p("osc2_mode", "Oscillator 2 Mode", "Osc 2", 1, "Enum", 0, 1, labels=["Supersaw", "Wavetable"])
    # FILTER (+ Mode, Position: SHIFT of CUTOFF / RESO)
    knob("flt_cutoff", "Filter Cutoff", "Cutoff", 2, 127)
    knob("flt_reso", "Filter Resonance", "Reso", 2, 0)
    p("flt_mode", "Filter Mode", "Flt Mode", 2, "Enum", 0, 3, labels=["Lowpass", "Bandpass", "Highpass", "Comb"])
    p("flt_pos", "Filter Position", "Flt Pos", 2, "Enum", 0, 3,
      labels=["I Start", "II Pre-Echo", "III Pre-Amb", "IV End"])
    # RESONATOR (+ Dissonance: SHIFT of TUNE)
    knob("res_tune", "Resonator Tune", "Res Tune", 3, 64)
    knob("res_feedback", "Resonator Feedback", "Res Fdbk", 3, 64)
    knob("res_dissonance", "Resonator Dissonance", "Res Diss", 3, 0)
    # ECHO (+ Filter: SHIFT of DENSITY)
    knob("echo_density", "Echo Density", "Density", 4, 64)
    knob("echo_repeats", "Echo Repeats", "Repeats", 4, 64)
    centred("echo_filter", "Echo Filter", "Echo Filt", 4, 0)
    # AMBIENCE (+ Auto Pan: SHIFT of SPACETIME)
    centred("amb_spacetime", "Ambience Spacetime", "Spacetime", 5, 40)
    knob("amb_decay", "Ambience Decay", "Amb Decay", 5, 64)
    knob("amb_autopan", "Ambience Auto Pan", "Auto Pan", 5, 0)
    # MODULATION (+ Type: SHIFT of SPEED); routed to the filter cutoff, as the
    # module's default modulation mapping
    knob("mod_level", "Modulation Level", "Mod Level", 6, 0)
    knob("mod_speed", "Modulation Speed", "Mod Speed", 6, 64)
    knob("mod_type", "Modulation Type", "Mod Type", 6, 0)
    # MIX: the module's 8 faders (volumes of the sources, dry/wet of the effects)
    if fx:
        knob("vol_input", "Input Volume", "Input", 7, 127)
    knob("vol_looper", "Looper Volume", "Looper", 7, 0)
    if not fx:
        knob("vol_sine", "Sine Volume", "Sine", 7, 100)
        knob("vol_osc2", "Supersaw / Wavetable Volume", "Saw / WT", 7, 0)
    knob("mix_filter", "Filter Dry/Wet", "Filter", 7, 0)
    knob("mix_resonator", "Resonator Dry/Wet", "Resonator", 7, 0)
    knob("mix_echo", "Echo Dry/Wet", "Echo", 7, 0)
    knob("mix_ambience", "Ambience Dry/Wet", "Ambience", 7, 0)
    if not fx:
        # ENVELOPE: Maquina addition (the module drones); Drone holds it open
        knob("env_attack", "Envelope Attack", "Attack", 8, 20)
        knob("env_decay", "Envelope Decay", "Decay", 8, 64)
        knob("env_sustain", "Envelope Sustain", "Sustain", 8, 127)
        knob("env_release", "Envelope Release", "Release", 8, 70)
        p("env_drone", "Drone", "Drone", 8, "Toggle", 0, 1, labels=onoff)
    # GLOBAL: SYNC IN from the host transport (Maquina)
    p("sync", "Sync", "Sync", 9, "Enum", 0, 3, labels=["Off", "Beat", "Bar", "4 Bars"])
    for q in params:
        q.update(par="0", fwmin=0, modtargets=None)
        if len(q["short"]) > MAX_SHORT_NAME:
            sys.exit(f"oneiroi: '{q['short']}' is longer than {MAX_SHORT_NAME} characters")
    pages = [dict(title=t, controls=[q["symbol"] for q in params if q["page"] == i])
             for i, t in enumerate(ONEIROI_PAGE_TITLES)]
    return params, [pg for pg in pages if pg["controls"]]


# Maquina Klang (Plaits), Klong + Klong FX (Rings): mutable-instruments/
KLANG_VOICE = 20
KLONG_VOICE = 21
KLONG_FX_VOICE = 22
# pages 0..3 + 7 (MIX: the output choice + gain, like the LXR mix page);
# page 1 is Klang's SOUND, Klong's RESONATOR (common/DrumPlugin.cpp)
MUTABLE_PAGE_TITLES = {0: "MODEL", 1: "SOUND", 2: "ENVELOPE", 3: "LPG", 7: "MIX"}
# the Model parameter: Plaits' original 16 models, then the 8 of firmware
# 1.1/1.2 (KlangHost::engineOfModel)
KLANG_MODELS = ["Virtual Analog", "Waveshaping", "2-Op FM", "Formant", "Harmonic", "Wavetable", "Chords",
                "Speech", "Swarm", "Filtered Noise", "Particle", "Inharm String", "Modal", "Bass Drum",
                "Snare Drum", "Hi-Hat",
                "VA + VCF", "Phase Dist", "6-Op FM 1", "6-Op FM 2", "6-Op FM 3", "Wave Terrain", "String Mach",
                "Chiptune"]
# Rings' 6 resonator models, then the string synth's 6 effects (the
# "Disastrous Peace" easter egg)
KLONG_MODELS = ["Modal", "Sympathetic", "Inharm String", "FM Voice", "Sympath Quant", "String + Verb",
                "Peace Formant", "Peace Chorus", "Peace Reverb", "Peace Form 2", "Peace Ensemble", "Peace Rev 2"]


def mutable_params(kind):
    """kind: "klang", "klong" or "klong_fx"."""
    params = []

    def p(symbol, name, short, page, kind_, default, fwmax=127, offset=0, labels=None):
        params.append(dict(symbol=symbol, name=name, short=short, page=page, kind=kind_, fwmax=fwmax,
                           offset=offset, labels=labels, default=default))

    def knob(symbol, name, short, page, default):
        p(symbol, name, short, page, "Int", default)

    if kind == "klang":
        # MODEL: the model buttons; Coarse/Fine instead of FREQUENCY's octave ranges
        p("model", "Model", "Model", 0, "Enum", 0, len(KLANG_MODELS) - 1, labels=KLANG_MODELS)
        p("coarse", "Coarse Tune", "Coarse", 0, "Int", 0, 72, -36)   # semitones
        p("fine", "Fine Tune", "Fine", 0, "Int", 0, 200, -100)        # cents
        p("key_track", "Key Track", "Key Track", 0, "Toggle", 1, 1, labels=["Off", "On"])
        # SOUND: the three big knobs
        knob("harmonics", "Harmonics", "Harmonics", 1, 64)
        knob("timbre", "Timbre", "Timbre", 1, 64)
        knob("morph", "Morph", "Morph", 1, 64)
        # ENVELOPE: the attenuverters, which set the internal decay envelope's
        # amount on FM / TIMBRE / MORPH (nothing patched into those inputs)
        p("fm_env", "Envelope to FM", "Env > FM", 2, "Int", 0, 126, -63)
        p("timbre_env", "Envelope to Timbre", "Env > Tmb", 2, "Int", 0, 126, -63)
        p("morph_env", "Envelope to Morph", "Env > Mrp", 2, "Int", 0, 126, -63)
        # LPG: the hidden parameters (Decay, Colour) + how notes drive it
        knob("lpg_decay", "LPG Decay", "Decay", 3, 64)
        knob("lpg_colour", "LPG Colour", "Colour", 3, 64)
        p("lpg_mode", "LPG Mode", "LPG Mode", 3, "Enum", 0, 1, labels=["Ping", "Velocity"])
        p("output", "Output", "Output", 7, "Enum", 0, 3, labels=["Out", "Aux", "Out + Aux", "Mix"])
    else:
        p("model", "Model", "Model", 0, "Enum", 0, len(KLONG_MODELS) - 1, labels=KLONG_MODELS)
        p("polyphony", "Polyphony", "Poly", 0, "Int", 1, 3, 1)
        p("coarse", "Coarse Tune", "Coarse", 0, "Int", 0, 60, -48)    # tonic 12..72, as the FREQUENCY knob
        p("fine", "Fine Tune", "Fine", 0, "Int", 0, 200, -100)
        if kind == "klong_fx":
            p("note_source", "Notes", "Notes", 0, "Enum", 0, 1, labels=["Internal", "MIDI"])
        knob("structure", "Structure", "Structure", 1, 64)
        knob("brightness", "Brightness", "Bright", 1, 64)
        knob("damping", "Damping", "Damping", 1, 64)
        knob("position", "Position", "Position", 1, 64)
        p("output", "Output", "Output", 7, "Enum", 0, 1, labels=["Odd / Even", "Mono"])
    for q in params:
        q.update(par="0", fwmin=0, modtargets=None)
        if len(q["short"]) > MAX_SHORT_NAME:
            sys.exit(f"{kind}: '{q['short']}' is longer than {MAX_SHORT_NAME} characters")
    titles = dict(MUTABLE_PAGE_TITLES)
    if kind != "klang":
        titles[1] = "RESONATOR"
    pages = [dict(title=t, controls=[q["symbol"] for q in params if q["page"] == i])
             for i, t in sorted(titles.items())]
    return params, [pg for pg in pages if pg["controls"]]


# Maquina Acida + Acida FX (OpenRB-338's 303 and effects): openrb/acida/
ACIDA_VOICE = 23
ACIDA_FX_VOICE = 24
# Acida: page 0 SYNTH, 1 PLAY, 7 MIX; Acida FX: 0 DIST, 1 PCF, 2 COMP, 3 DELAY, 7 MIX
# (common/DrumPlugin.cpp kAcidaPageNames)
ACIDA_PAGE_TITLES = {0: "SYNTH", 1: "PLAY", 7: "MIX"}
ACIDA_FX_PAGE_TITLES = {0: "DIST", 1: "PCF", 2: "COMP", 3: "DELAY", 7: "MIX"}


def acida_params(fx):
    """The controllers of OpenRB-338's devices (0..127 bytes; defaults = the
    ctor values of tb303.rs / dist.rs / pcf.rs / comp.rs / delay.rs), as
    openrb/acida/engine/AcidaHost.cpp reads them."""
    params = []
    onoff = ["Off", "On"]

    def p(symbol, name, short, page, kind_, default, fwmax=127, offset=0, labels=None, fwmin=0):
        params.append(dict(symbol=symbol, name=name, short=short, page=page, kind=kind_, fwmax=fwmax,
                           offset=offset, labels=labels, default=default, fwmin=fwmin))

    if not fx:
        p("tune", "Tune", "Tune", 0, "Int", 0, 127, -64)          # byte 64 = centre
        p("cutoff", "Cutoff", "Cutoff", 0, "Int", 20)
        p("resonance", "Resonance", "Resonance", 0, "Int", 90)
        p("env_mod", "Env Mod", "Env Mod", 0, "Int", 20)
        p("decay", "Decay", "Decay", 0, "Int", 100)
        p("accent", "Accent", "Accent", 0, "Int", 64)
        p("waveform", "Waveform", "Wave", 0, "Enum", 0, 1, labels=["Saw", "Square"])
        p("engine", "Sound Engine", "Engine", 0, "Enum", 0, 1, labels=["2.0", "Vintage"])
        # performance: the velocity from which a note is accented (OpenRB: 100)
        p("accent_vel", "Accent Velocity", "Acc Vel", 1, "Int", 100, 127, fwmin=1)
    else:
        p("dist_on", "Distortion", "Dist", 0, "Toggle", 1, 1, labels=onoff)
        p("dist_amount", "Dist Amount", "Dist Amt", 0, "Int", 70)
        p("dist_shape", "Dist Shape", "Dist Shp", 0, "Int", 0)
        p("pcf_on", "PCF", "PCF", 1, "Toggle", 0, 1, labels=onoff)
        p("pcf_freq", "PCF Frequency", "PCF Freq", 1, "Int", 70)
        p("pcf_reso", "PCF Resonance", "PCF Reso", 1, "Int", 64)
        p("pcf_amount", "PCF Amount", "PCF Amt", 1, "Int", 40)
        p("pcf_wave", "PCF Wave", "PCF Wave", 1, "Int", 25, 54, 1)  # the 55 patterns, shown 1..55 (byte 24)
        p("pcf_decay", "PCF Decay", "PCF Decay", 1, "Int", 90)
        p("pcf_mode", "PCF Mode", "PCF Mode", 1, "Enum", 0, 1, labels=["Low Pass", "Band Pass"])
        p("comp_on", "Compressor", "Comp", 2, "Toggle", 0, 1, labels=onoff)
        p("comp_amount", "Comp Amount", "Comp Amt", 2, "Int", 70)
        p("comp_threshold", "Comp Threshold", "Comp Thr", 2, "Int", 64)
        p("delay_on", "Delay", "Delay", 3, "Toggle", 0, 1, labels=onoff)
        p("delay_send", "Delay Send", "Dly Send", 3, "Int", 100)
        p("delay_steps", "Delay Steps", "Dly Steps", 3, "Int", 3, 32, fwmin=1)
        p("delay_mode", "Delay Mode", "Dly Mode", 3, "Enum", 1, 1, labels=["Triplets", "16ths"])
        p("delay_feedback", "Delay Feedback", "Dly Fdbk", 3, "Int", 80)
        p("delay_pan", "Delay Pan", "Dly Pan", 3, "Int", 0, 127, -64)
    for q in params:
        q.update(par="0", modtargets=None)
        if len(q["short"]) > MAX_SHORT_NAME:
            sys.exit(f"acida: '{q['short']}' is longer than {MAX_SHORT_NAME} characters")
    titles = ACIDA_FX_PAGE_TITLES if fx else ACIDA_PAGE_TITLES
    pages = [dict(title=t, controls=[q["symbol"] for q in params if q["page"] == i])
             for i, t in sorted(titles.items())]
    return params, [pg for pg in pages if pg["controls"]]


# Maquina BL-101 + BL-101 FX (S1Vave's engine): s1vave/bl101/
BL101_VOICE = 25
BL101_FX_VOICE = 26
# BL-101: the SH-101-style panel sections; BL-101 FX: the effects
# (common/DrumPlugin.cpp kBl101PageNames / kBl101FxPageNames)
BL101_PAGE_TITLES = {0: "MODULATOR", 1: "VCO", 2: "SOURCE MIXER", 3: "VCF", 4: "ENV", 5: "VOICE", 6: "CHORD",
                     7: "MIX", 8: "DRAW", 9: "CHOP"}
BL101_FX_PAGE_TITLES = {0: "CHORUS", 1: "DELAY", 2: "REVERB", 3: "LO-FI", 7: "MIX"}
BL101_PARAMS_H = os.path.join(ROOT, "s1vave", "bl101", "third_party", "s1vave", "firmware", "src", "obv_params.h")
# VoiceParam::fwPar of the parameters that are not patch bytes (Bl101Host.cpp)
BL101_DRAW_BASE = 100      # + step 0..15: the OSC DRAW table (byte - 128) * 256
BL101_MASK_BASE = 200      # + source 0..3: the chop mask selector
BL101_BITS_LO_BASE = 210   # + source: the Custom mask, low byte
BL101_BITS_HI_BASE = 220   # + source: high byte
BL101_MASK_LABELS = ["Custom", "All", "Odd", "Even", "1 of 4", "4 of 4", "Off"]  # Bl101Dsp.hpp kChopMaskPresets


def bl101_firmware_params():
    """P_* name -> (index, max, default) from the vendored obv_params.h"""
    src = open(BL101_PARAMS_H, encoding="utf-8").read()
    body = src[src.index("enum {"):src.index("NPARAM")]
    names = re.findall(r"^\s*(P_[A-Z0-9_]+),", body, re.M)
    consts = {}
    for grp in re.findall(r"enum\s*\{([^}]*)\}", src):
        items = [x.strip() for x in grp.split(",") if x.strip() and not x.strip().startswith("/")]
        if items and not items[0].startswith("P_") and not items[0].startswith("K_"):
            for i, it in enumerate(items):
                it = it.split("=")[0].strip()
                if re.fullmatch(r"[A-Z_0-9]+", it):
                    consts.setdefault(it, i)
    out = {}
    for m in re.finditer(r"\[(P_[A-Z0-9_]+)\]\s*=\s*\{\"[^\"]*\",\s*([^,]+),\s*([^,]+),", src):
        def val(x):
            x = x.strip()
            if x == "NPOLY_MODE - 1":
                return 3
            return int(x) if re.fullmatch(r"-?\d+", x) else consts[x]
        out[m.group(1)] = (names.index(m.group(1)), val(m.group(2)), val(m.group(3)))
    if len(out) != len(names):
        sys.exit("bl101: could not read every PDEF entry of obv_params.h")
    return out


def bl101_params(fx):
    """Maquina BL-101 / BL-101 FX parameters: S1Vave's patch bytes (fwPar = the
    firmware's P_* index, ranges and defaults from its PDEF table), plus the
    OSC DRAW table and the CHOP masks (fwPar BL101_*_BASE + n)."""
    fw = bl101_firmware_params()
    params = []
    onoff = ["Off", "On"]

    def p(symbol, short, page, kind_, pname=None, labels=None, offset=0, fwmin=0, fwmax=None, default=None,
          par=None):
        if pname is not None:
            idx, mx, df = fw[pname]
            par = idx
            fwmax = mx if fwmax is None else fwmax
            default = df if default is None else default
        if kind_ == "Enum" and labels and len(labels) != fwmax + 1:
            sys.exit(f"bl101: {symbol}: {len(labels)} labels for 0..{fwmax}")
        params.append(dict(symbol=symbol, name=short, short=short, page=page, kind=kind_, fwmax=fwmax,
                           offset=offset, labels=labels, default=default, fwmin=fwmin, par=str(par),
                           modtargets=None))

    if not fx:
        # MODULATOR (the LFO)
        p("modulator_rate", "LFO Rate", 0, "Int", "P_LFO_RATE")
        p("modulator_wave", "LFO Wave", 0, "Enum", "P_LFO_WAVE",
          ["Saw", "Inv Saw", "Triangle", "Square", "Random", "Noise"])
        p("modulator_sync", "LFO Sync", 0, "Toggle", "P_LFO_SYNC", onoff)
        p("modulator_fast", "LFO Fast", 0, "Toggle", "P_LFO_MODE", onoff)
        p("modulator_trig", "Key Trig", 0, "Toggle", "P_LFO_TRIG", onoff)
        # VCO
        p("vco_mod", "VCO Mod", 1, "Int", "P_LFO_PITCH")
        p("vco_range", "Range", 1, "Enum", "P_RANGE", ["64'", "32'", "16'", "8'", "4'", "2'"])
        p("vco_pw", "Pulse W", 1, "Int", "P_PWM_DEPTH")
        p("vco_pwm_src", "PWM Src", 1, "Enum", "P_PWM_SRC", ["Env", "Manual", "LFO"])
        p("vco_fine", "Fine", 1, "Int", "P_FINE_TUNE", offset=-64)
        # SOURCE MIXER
        p("mix_pulse", "Pulse", 2, "Int", "P_PULSE_LVL")
        p("mix_saw", "Saw", 2, "Int", "P_SAW_LVL")
        p("mix_sub", "Sub", 2, "Int", "P_SUB_LVL")
        p("mix_sub_type", "Sub Type", 2, "Enum", "P_SUB_TYPE", ["-2 Pulse", "-2 Oct", "-1 Oct"])
        p("mix_noise", "Noise", 2, "Int", "P_NOISE_LVL")
        p("mix_noise_type", "Noise Typ", 2, "Enum", "P_NOISE_MODE", ["Pink", "White"])
        # VCF
        p("vcf_freq", "Cutoff", 3, "Int", "P_CUTOFF")
        p("vcf_res", "Reso", 3, "Int", "P_RESO")
        p("vcf_env", "VCF Env", 3, "Int", "P_FENV_AMT")
        p("vcf_mod", "VCF Mod", 3, "Int", "P_FLFO_AMT")
        p("vcf_kybd", "Kybd", 3, "Int", "P_KEYTRK")
        # VCA + ENV
        p("vca_mode", "VCA", 4, "Enum", "P_AMP_MODE", ["Gate", "Env"])
        p("env_trigger", "Env Trig", 4, "Enum", "P_ENV_TRG", ["LFO", "Gate", "Gate+Trig"])
        p("adsr_attack", "Attack", 4, "Int", "P_ATTACK")
        p("adsr_decay", "Decay", 4, "Int", "P_DECAY")
        p("adsr_sustain", "Sustain", 4, "Int", "P_SUSTAIN")
        p("adsr_release", "Release", 4, "Int", "P_RELEASE")
        # VOICE
        p("voice_mode", "Voicing", 5, "Enum", "P_POLY_MODE", ["Mono", "Unison", "Poly", "Chord"])
        p("porta_mode", "Porta", 5, "Enum", "P_PORTA_MODE", ["Off", "On", "Auto"])
        p("porta_time", "Porta T", 5, "Int", "P_PORTA_TIME")
        p("transpose", "Transpose", 5, "Int", "P_TRANSPOSE", offset=-64, fwmin=4)
        p("bend_vco", "Bend VCO", 5, "Int", "P_OSC_BEND")
        p("bend_vcf", "Bend VCF", 5, "Int", "P_FLT_BEND")
        p("stereo_spread", "Spread", 5, "Int", "P_SPREAD")
        p("unison_detune", "Detune", 5, "Int", "P_DETUNE")
        # CHORD (voices 2..4 of Chord voicing)
        for v in (2, 3, 4):
            p(f"chord{v}_on", f"Chord {v}", 6, "Toggle", f"P_CHORD_SW{v}", onoff)
            p(f"chord{v}_shift", f"Shift {v}", 6, "Int", f"P_CHORD_KS{v}", offset=-12)
        # MIX
        p("patch_level", "Level", 7, "Int", "P_VOLUME")
        # DRAW: mode, multiply (shown 1..32), the 16 steps (shown -128..127 = int16 / 256;
        # the INIT ramp's last step wraps to -128, as on the firmware)
        p("draw_mode", "Draw", 8, "Enum", "P_DRAW_SW", ["Off", "Step", "Slope"])
        p("draw_mult", "Multiply", 8, "Int", "P_DRAW_MULT", offset=1)
        for k in range(16):
            init = (k - 7) * 16
            init = init - 256 if init > 127 else init
            p(f"draw_{k + 1}", f"Draw {k + 1}", 8, "Int", offset=-128, fwmax=255, default=init + 128,
              par=BL101_DRAW_BASE + k)
        # CHOP: overtone, comb (shown 1..32), a mask per source + its Custom bits
        p("chop_overtone", "Overtone", 9, "Int", "P_CHOP_OVTN")
        p("chop_comb", "Comb", 9, "Int", "P_CHOP_COMB", offset=1)
        for k, (src, short) in enumerate((("sqr", "Pulse"), ("saw", "Saw"), ("sub", "Sub"), ("noise", "Noise"))):
            p(f"chop_mask_{src}", f"{short} Msk", 9, "Enum", labels=BL101_MASK_LABELS, fwmax=6, default=1,
              par=BL101_MASK_BASE + k)
        for k, (src, short) in enumerate((("sqr", "Pls"), ("saw", "Saw"), ("sub", "Sub"), ("noise", "Noi"))):
            p(f"chop_bits_{src}_lo", f"{short} Lo", 9, "Int", fwmax=255, default=255, par=BL101_BITS_LO_BASE + k)
            p(f"chop_bits_{src}_hi", f"{short} Hi", 9, "Int", fwmax=255, default=255, par=BL101_BITS_HI_BASE + k)
    else:
        p("chorus_mode", "Chorus", 0, "Enum", "P_CHO_MODE", ["Off", "Type 1", "Type 2", "Type 3", "Type 4"])
        p("dly_level", "Dly Level", 1, "Int", "P_DLY_LVL")
        p("dly_time", "Dly Time", 1, "Int", "P_DLY_TIME")
        p("dly_feedback", "Dly Fdbk", 1, "Int", "P_DLY_FB")
        p("dly_sync", "Dly Sync", 1, "Toggle", "P_DLY_SYNC", onoff)
        # (the firmware's 0: an inserted BL-101 FX that does nothing; a little reverb instead)
        p("rev_level", "Rev Level", 2, "Int", "P_REV_LVL", default=40)
        p("rev_time", "Rev Time", 2, "Int", "P_REV_TIME")
        p("rev_type", "Rev Type", 2, "Enum", "P_REV_TYPE",
          ["Ambience", "Room", "Hall 1", "Hall 2", "Plate", "Spring", "Modulated"])
        p("rev_predelay", "Pre-Delay", 2, "Int", "P_REV_PRE")
        p("lofi_mode", "Lo-Fi", 3, "Enum", "P_FX_MODE", ["Reduce", "Distort"])
        p("lofi_amount", "Lo-Fi Amt", 3, "Int", "P_FX_AMT")
        p("lofi_mix", "Lo-Fi Mix", 3, "Int", "P_FX_MIX")
    for q in params:
        if len(q["short"]) > MAX_SHORT_NAME:
            sys.exit(f"bl101: '{q['short']}' is longer than {MAX_SHORT_NAME} characters")
        if not (q["fwmin"] <= q["default"] <= q["fwmax"]):
            sys.exit(f"bl101: {q['symbol']}: default {q['default']} outside {q['fwmin']}..{q['fwmax']}")
        if q["kind"] == "Int":
            q["default"] += q["offset"]  # the table's defaults are shown values
    titles = BL101_FX_PAGE_TITLES if fx else BL101_PAGE_TITLES
    pages = [dict(title=t, controls=[q["symbol"] for q in params if q["page"] == i])
             for i, t in sorted(titles.items())]
    return params, [pg for pg in pages if pg["controls"]]


# Maquina LA-50 (an LA synth playing D-50 patches): lasynth/la50/
LA50_VOICE = 27
# port groups (common/DrumPlugin.cpp kLa50PageNames)
LA50_PAGE_TITLES = {0: "PATCH", 1: "UPPER COMMON", 2: "UPPER PARTIAL 1", 3: "UPPER PARTIAL 2", 4: "LOWER COMMON",
                    5: "LOWER PARTIAL 1", 6: "LOWER PARTIAL 2", 7: "MIX"}


def la50_params():
    """Maquina LA-50 parameters: the D-50 patch's bytes (tools/la50_layout.py); fwPar = the byte's index in the
    448-byte patch"""
    import la50_layout
    params = []
    for q in la50_layout.params():
        kind = q["kind"]
        params.append(dict(symbol=q["symbol"], name=q["short"], short=q["short"], page=la50_layout.page_of(q["block_index"]),
                           kind=kind, fwmax=q["max"], fwmin=0, offset=q["shown"] if kind == "Int" else 0,
                           labels=q["labels"], default=q["default"] + (q["shown"] if kind == "Int" else 0),
                           par=str(q["byte"]), modtargets=None))
    pages = [dict(title=t, controls=[q["symbol"] for q in params if q["page"] == i])
             for i, t in sorted(LA50_PAGE_TITLES.items())]
    return params, [pg for pg in pages if pg["controls"]]


# Maquina Strings (string machine + Plaits wavetables): stringmachine/strings/
STRINGS_VOICE = 28
# port groups (LV2 / Zynthian: 8 groups of 4 + MORE; common/DrumPlugin.cpp kStringsPageNames).
# Page 7 holds the extra Gain (ExtraParams.hpp), so FX is page 7.
STRINGS_PAGE_TITLES = {0: "STRINGS", 1: "VIBRATO / BLEND", 2: "WAVE", 3: "WAVE MOD", 4: "FILTER", 5: "MOTION",
                       6: "AMP", 7: "FX", 8: "MORE"}
# log-mapped parameters (stringmachine/strings/engine/StringsUnits.hpp): lo, default, hi; 127 steps
# bending at the default (the knee)
STRINGS_LOG = {"wave_lfo_rate": (0.05, 0.8, 10.0), "filter_cutoff": (20.0, 1800.0, 18000.0),
               "blend_time": (0.05, 2.4, 10.0), "attack": (0.001, 0.42, 10.0), "decay": (0.001, 1.2, 20.0),
               "release": (0.001, 1.8, 20.0)}


def log_knee(lo, default, hi):
    """StringsUnits.hpp logKnee(): the step holding the default (lround)"""
    return int(math.floor(127 * math.log(default / lo) / math.log(hi / lo) + 0.5))


def strings_params():
    """Maquina Strings parameters (the editor-first skeleton: the engine comes later). Values are whole
    steps; StringsUnits.hpp says what they mean (Hz, s, ct)."""
    params = []

    def p(symbol, short, page, kind, default, fwmax=100, offset=0, labels=None):
        params.append(dict(symbol=symbol, name=short, short=short, page=page, kind=kind, fwmax=fwmax,
                           offset=offset, labels=labels, default=default))

    def logp(symbol, short, page):
        p(symbol, short, page, "Int", log_knee(*STRINGS_LOG[symbol]), 127)

    # STRINGS: the register morph (Cello .. Organ), the oscillators, tone
    p("register", "Register", 0, "Int", 42)
    p("shape", "Shape", 0, "Enum", 2, 2, labels=["Saw", "PWM", "Mix"])
    p("detune", "Detune", 0, "Int", 6, 25)             # cents
    p("tone", "Tone", 0, "Int", 50)
    # VIBRATO (delayed) + the layers' blend and key split
    p("vibrato", "Vibrato", 1, "Int", 10, 30)          # 5.0 .. 8.0 Hz
    p("vib_delay", "Vib Delay", 1, "Int", 6, 30)       # 0 .. 3 s
    p("blend", "Blend", 1, "Int", 40)                  # 0 strings .. 100 wave
    p("keys", "Keys", 1, "Enum", 0, 2, labels=["Layer", "Wave Low", "Wave High"])
    # WAVE: Plaits' wavetables (4 banks of 8 x 8)
    p("wave_bank", "Bank", 2, "Enum", 0, 3, labels=["A", "B", "C", "D"])
    p("wave_x", "Pos X", 2, "Int", 35)
    p("wave_y", "Morph Y", 2, "Int", 60)
    p("wave_octave", "Octave", 2, "Int", 0, 4, -2)
    logp("wave_lfo_rate", "LFO Rate", 3)
    p("wave_lfo_depth", "LFO Depth", 3, "Int", 25)
    p("wave_env_pos", "Env > Pos", 3, "Int", 30, 200, -100)
    p("layer_detune", "Layer Det", 3, "Int", 7, 25)    # cents
    # FILTER (the wave layer's): LP / BP / HP / COMB
    p("filter_mode", "Flt Mode", 4, "Enum", 0, 3, labels=["LP", "BP", "HP", "Comb"])
    logp("filter_cutoff", "Cutoff", 4)
    p("filter_reso", "Reso", 4, "Int", 60)
    p("filter_env", "Flt Env", 4, "Int", 36, 200, -100)
    # MOTION: filter key tracking, the blend over note time and by velocity
    p("filter_keytrack", "Keytrack", 5, "Int", 50)     # %
    p("blend_shift", "Shift", 5, "Int", 40, 200, -100)
    logp("blend_time", "Blnd Time", 5)
    p("blend_velo", "Blnd Velo", 5, "Int", 30)
    # AMP
    logp("attack", "Attack", 6)
    logp("decay", "Decay", 6)
    p("sustain", "Sustain", 6, "Int", 100)
    logp("release", "Release", 6)
    # FX (+ the extra Gain on this page)
    p("fx_mode", "FX Mode", 7, "Enum", 0, 3, labels=["Ensemble", "Phaser", "Chorus", "Formant"])
    p("fx_amount", "FX Amount", 7, "Int", 70)
    p("reverb", "Reverb", 7, "Int", 40)
    # MORE: off the Machinista pages' main row
    p("vib_depth", "Vib Depth", 8, "Int", 30)          # 0 = off
    p("velocity", "Velocity", 8, "Int", 0)             # 0 = off
    p("fx_slow", "FX Slow", 8, "Int", 30, 60)          # 0.30 .. 0.90 Hz
    p("fx_fast", "FX Fast", 8, "Int", 30, 60)          # 3.0 .. 9.0 Hz
    # the wave layer's unison: voices per note (1 = off) and their detune spread
    p("wave_unison", "Unison", 8, "Int", 1, 3, 1)      # 1 .. 4 voices
    p("wave_spread", "Spread", 8, "Int", 12, 50)       # cents
    for q in params:
        q.update(par="0", fwmin=0, modtargets=None)
        if len(q["short"]) > MAX_SHORT_NAME:
            sys.exit(f"strings: '{q['short']}' is longer than {MAX_SHORT_NAME} characters")
        lo, hi = q["offset"], q["fwmax"] + q["offset"]
        if q["kind"] == "Enum":
            lo, hi = 0, len(q["labels"]) - 1
        if not lo <= q["default"] <= hi:
            sys.exit(f"strings: {q['symbol']}: default {q['default']} outside {lo}..{hi}")
    pages = [dict(title=t, controls=[q["symbol"] for q in params if q["page"] == i])
             for i, t in sorted(STRINGS_PAGE_TITLES.items())]
    return params, [pg for pg in pages if pg["controls"]]


NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def read(path):
    with open(os.path.join(FW, path), encoding="latin-1") as f:
        return f.read()


def strip_comments(s):
    """Remove C comments, honouring string/char literals and comment order."""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c in "\"'":
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            out.append(" ")
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def brace_groups(s, start):
    """Top-level {...} groups of the initializer that starts at s[start]=='{'."""
    depth, cur, out = 0, "", []
    for ch in s[start:]:
        if ch == "{":
            depth += 1
            if depth == 1:
                continue
            if depth == 2:
                cur = ""
                continue
        if ch == "}":
            depth -= 1
            if depth == 0:
                break
            if depth == 1:
                out.append(cur)
                continue
        if depth >= 2:
            cur += ch
    return out


def parse_menu_pages():
    s = strip_comments(read("Inc/menu/menuPages.h"))
    i = s.index("{", s.index("menuPages[NUM_PAGES][NUM_SUB_PAGES]"))
    pages = brace_groups(s, i)
    result = []
    for p in pages:
        subs = re.findall(r"\{([^{}]*)\}", p)
        page = []
        for sp in subs:
            t = [x.strip() for x in sp.split(",") if x.strip()]
            page.append(list(zip(t[:8], t[8:16])))
        result.append(page)
    return result


def parse_dtypes():
    s = read("Src/menu/menu.c")
    i = s.index("{", s.index("parameter_dtypes[NUM_PARAMS]"))
    j = s.index("};", i)
    body = strip_comments(s[i + 1:j])
    return [e.strip() for e in body.split(",") if e.strip()]


def parse_name_arrays():
    s = read("Inc/menu/MenuText.h")
    out = {}
    for name in [a for a in MENU_ARRAYS.values() if a]:
        m = re.search(name + r"\[\]\[\d+\]\s*=\s*\{(.*?)\};", s, flags=re.S)
        body = strip_comments(m.group(1))
        items = re.findall(r"\{\s*(\"[^\"]*\"|\d+)\s*\}", body)
        count = int(items[0])
        labels = [x.strip('"') for x in items[1:1 + count]]
        if len(labels) != count:
            sys.exit(f"{name}: expected {count} entries, got {len(labels)}")
        out[name] = labels
    return out


def parse_mod_targets():
    s = strip_comments(read("Src/menu/Cc2Text.c"))
    i = s.index("{", s.index("modTargets[]"))
    ents = re.findall(r"\{\s*(TEXT_\w+)\s*,\s*(PAR_\w+)\s*\}", s[i:s.index("};", i)])
    m = re.search(r"modTargetVoiceOffsets\[6\]\s*=\s*\{(.*?)\};", s, flags=re.S)
    offs = [tuple(map(int, x)) for x in re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*\}", m.group(1))]
    return ents, offs


def enum_values(names):
    """Resolve PAR_*/TEXT_* identifiers by compiling against the firmware headers."""
    names = sorted(set(names))
    src = '#include <stdio.h>\n#include "Parameters.h"\n#include "menu/menu.h"\nint main(void){\n'
    src += "".join(f'printf("%s %d\\n", "{n}", (int)({n}));\n' for n in names)
    src += "return 0;}\n"
    with tempfile.TemporaryDirectory() as d:
        c, exe = os.path.join(d, "e.c"), os.path.join(d, "e")
        with open(c, "w") as f:
            f.write(src)
        subprocess.check_call(["cc", "-w", "-I" + os.path.join(DRUM02, "engine", "fw_shim"),
                               "-I" + os.path.join(FW, "Inc"), c, "-o", exe])
        out = subprocess.check_output([exe], text=True)
    return {k: int(v) for k, v in (l.split() for l in out.splitlines())}


# Defaults that differ from the Initkit (mirrors EngineHost's constructor).
DEFAULT_OVERRIDES = {}


def parse_init_kit():
    s = strip_comments(read("Src/Preset/initKit.c"))
    body = s[s.index("{", s.index("initKit[")) + 1:]
    body = body[:body.index("}")]
    return [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]


def note_name(n):
    return f"{NOTE_NAMES[n % 12]}{n // 12 - 1}"


def short_name(symbol):
    name = SHORT_NAMES.get(symbol)
    if name is None:
        sys.exit(f"SHORT_NAMES: no display name for '{symbol}'")
    if len(name) > MAX_SHORT_NAME:
        sys.exit(f"SHORT_NAMES: '{name}' ({symbol}) is longer than {MAX_SHORT_NAME} characters")
    return name


def main():
    pages = parse_menu_pages()
    dtypes = parse_dtypes()
    arrays = parse_name_arrays()
    mod_ents, mod_offs = parse_mod_targets()

    wanted = set()
    for _, _, _, page in PLUGINS:
        for sub in pages[page]:
            for _, par in sub:
                wanted.add(par)
    for extras in EXTRA.values():
        for e in extras:
            wanted.add(e[1])
    wanted |= {p for _, p in mod_ents}
    wanted.add("END_OF_SOUND_PARAMETERS")
    values = enum_values(wanted)
    kit = parse_init_kit()
    end_sound = values["END_OF_SOUND_PARAMETERS"]

    plugins_out, layout = [], {}
    for pid, pname, voice, page in PLUGINS:
        metal = voice >= 4
        params, page_syms = [], []
        by_par = {}
        for sub_i, sub in enumerate(pages[page]):
            syms, seen = [], {}
            entries = [(t, p, None, None) for t, p in sub]
            for extra in EXTRA.get(pid, []):
                if extra[0] == sub_i:
                    # insert the extra after its sibling on the page
                    entries.append(("__EXTRA__", extra[1], extra[2], extra[3]))
            for text, par, fixed, fixed_dtype in entries:
                if par == "PAR_NONE":
                    continue
                key = text.replace("TEXT_", "")
                if key in EXCLUDE_TEXT:
                    continue
                if fixed:
                    sym, name = fixed
                    dt = fixed_dtype
                else:
                    occ = seen.get(key, 0)
                    seen[key] = occ + 1
                    sym, name = (SYMBOLS_METAL if metal else {}).get((sub_i, key, occ)) or \
                        SYMBOLS.get((sub_i, key, occ)) or (None, None)
                    if not sym:
                        sys.exit(f"{pid}: no symbol for page {sub_i} {key}#{occ} ({par})")
                    dt = dtypes[values[par]]
                params.append(dict(symbol=sym, name=name, page=sub_i, par=par, dtype=dt))
                by_par[par] = params[-1]
                syms.append(sym)
            title = (PAGE_TITLES_METAL if metal else {}).get(sub_i, PAGE_TITLES[sub_i])
            if syms:
                page_syms.append(dict(title=title, controls=syms))

        # value model per parameter
        start, end = mod_offs[voice]
        for p in params:
            dt, par = p["dtype"], p["par"]
            p.update(offset=0, fwmin=0, fwmax=127, labels=None, kind="Int", modtargets=None)
            if dt.startswith("DTYPE_MENU"):
                menu = re.search(r"MENU_\w+", dt).group(0)
                if menu not in MENU_ARRAYS:
                    sys.exit(f"{pid}: unsupported menu {menu} for {par}")
                if MENU_ARRAYS[menu] is None:
                    p.update(kind="Retrigger", labels=["Off", "On"], fwmax=1)
                else:
                    arr = MENU_ARRAYS[menu]
                    n = len(arrays[arr])
                    lab = LONG_LABELS.get(arr, arrays[arr])
                    if len(lab) != n:
                        sys.exit(f"{arr}: firmware has {n} entries, LONG_LABELS {len(lab)}")
                    p.update(kind="Enum", labels=lab, fwmax=n - 1)
            elif dt == "DTYPE_PM63":
                p.update(offset=-60 if PM60.match(par) else -63)
            elif dt == "DTYPE_ON_OFF":
                p.update(kind="Toggle", fwmax=1, labels=["Off", "On"])
            elif dt in ("DTYPE_TARGET_SELECTION_LFO", "DTYPE_TARGET_SELECTION_VELO"):
                idx, lab = [0], ["Off"]
                for i in range(start, end + 1):
                    text, tpar = mod_ents[i]
                    if tpar.startswith("PAR_FX_") or tpar not in by_par:
                        continue  # FX bus and hidden params are not part of a voice plugin
                    idx.append(i)
                    lab.append(by_par[tpar]["name"])
                p.update(kind="ModTarget", labels=lab, modtargets=idx, fwmax=len(idx) - 1)
            elif dt == "DTYPE_NOTE_NAME":
                p.update(kind="Note", labels=["Any"] + [note_name(n) for n in range(1, 128)])
            elif dt == "DTYPE_0B100":
                p.update(fwmax=100)
            elif dt == "DTYPE_0B15":
                p.update(fwmax=15)
            elif dt == "DTYPE_0B127":
                pass
            else:
                sys.exit(f"{pid}: unhandled dtype {dt} for {par}")

        # defaults: the firmware Initkit, as lxr_load_init_kit() loads it
        # (param p <- initKit[p + 8] for p + 8 < END_OF_SOUND_PARAMETERS)
        for p in params:
            num = values[p["par"]]
            fw = kit[num + 8] if num + 8 < end_sound else 0
            fw = DEFAULT_OVERRIDES.get((pid, p["par"]), fw)
            if p["kind"] == "Int":
                p["default"] = fw + p["offset"]
            elif p["kind"] == "Retrigger":
                p["default"] = 1 if fw else 0
            elif p["kind"] == "ModTarget":
                p["default"] = p["modtargets"].index(fw) if fw in p["modtargets"] else 0
            else:
                p["default"] = min(fw, p["fwmax"])

        plugins_out.append(dict(id=pid, name=pname, voice=voice, params=params))
        layout[pid] = dict(name=pname, pages=page_syms)

    dd_params, dd_pages = dredrum_params()
    plugins_out.append(dict(id="dredrum", name="Dredrum", voice=DREDRUM_VOICE, params=dd_params))
    layout["dredrum"] = dict(name="Dredrum", pages=dd_pages)
    np_params, np_pages = plethora_params()
    plugins_out.append(dict(id="plethora", name="Plethora", voice=PLETHORA_VOICE, params=np_params))
    layout["plethora"] = dict(name="Plethora", pages=np_pages)
    for oid, oname, ovoice, fx in (("oneiroi", "Oneiroi", ONEIROI_VOICE, False),
                                   ("oneiroi_fx", "Oneiroi FX", ONEIROI_FX_VOICE, True)):
        o_params, o_pages = oneiroi_params(fx)
        plugins_out.append(dict(id=oid, name=oname, voice=ovoice, params=o_params))
        layout[oid] = dict(name=oname, pages=o_pages)
    for mid, mname, mvoice in (("klang", "Klang", KLANG_VOICE), ("klong", "Klong", KLONG_VOICE),
                               ("klong_fx", "Klong FX", KLONG_FX_VOICE)):
        m_params, m_pages = mutable_params(mid)
        plugins_out.append(dict(id=mid, name=mname, voice=mvoice, params=m_params))
        layout[mid] = dict(name=mname, pages=m_pages)
    for aid, aname, avoice, fx in (("acida", "Acida", ACIDA_VOICE, False),
                                   ("acida_fx", "Acida FX", ACIDA_FX_VOICE, True)):
        a_params, a_pages = acida_params(fx)
        plugins_out.append(dict(id=aid, name=aname, voice=avoice, params=a_params))
        layout[aid] = dict(name=aname, pages=a_pages)
    for bid, bname, bvoice, fx in (("bl101", "BL-101", BL101_VOICE, False),
                                   ("bl101_fx", "BL-101 FX", BL101_FX_VOICE, True)):
        b_params, b_pages = bl101_params(fx)
        plugins_out.append(dict(id=bid, name=bname, voice=bvoice, params=b_params))
        layout[bid] = dict(name=bname, pages=b_pages)
    l_params, l_pages = la50_params()
    plugins_out.append(dict(id="la50", name="LA-50", voice=LA50_VOICE, params=l_params))
    layout["la50"] = dict(name="LA-50", pages=l_pages)
    s_params, s_pages = strings_params()
    plugins_out.append(dict(id="strings", name="Strings", voice=STRINGS_VOICE, params=s_params))
    layout["strings"] = dict(name="Strings", pages=s_pages)

    write_header(plugins_out)
    os.makedirs(os.path.join(ROOT, "machinista"), exist_ok=True)
    with open(os.path.join(ROOT, "machinista", "layout.json"), "w") as f:
        json.dump(layout, f, indent=2)
        f.write("\n")
    for p in plugins_out:
        print(f"{p['id']:10s} voice {p['voice']}: {len(p['params'])} parameters")


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_header(plugins):
    o = []
    o.append("// GENERATED by tools/gen_params.py from the LXR firmware UI tables (+ Dredrum, Plethora, Oneiroi, Klang, Klong, Acida, BL-101, LA-50, Strings). Do not edit.")
    o.append("// SPDX-License-Identifier: GPL-3.0-only")
    o.append("#pragma once")
    o.append("#include \"VoiceTypes.hpp\"")
    o.append("")
    o.append("namespace maquina {")
    o.append("")
    for p in plugins:
        pid = p["id"]
        for i, prm in enumerate(p["params"]):
            if prm["labels"]:
                o.append(f"static const char* const k_{pid}_labels_{i}[] = {{ "
                         + ", ".join(cstr(l) for l in prm["labels"]) + " };")
            if prm["modtargets"]:
                o.append(f"static const uint8_t k_{pid}_targets_{i}[] = {{ "
                         + ", ".join(str(x) for x in prm["modtargets"]) + " };")
        o.append(f"static const VoiceParam k_{pid}_params[] = {{")
        for i, prm in enumerate(p["params"]):
            labels = f"k_{pid}_labels_{i}" if prm["labels"] else "nullptr"
            nlab = len(prm["labels"]) if prm["labels"] else 0
            targets = f"k_{pid}_targets_{i}" if prm["modtargets"] else "nullptr"
            short = prm.get("short") or short_name(prm["symbol"])
            o.append(f"    {{ {cstr(prm['symbol'])}, {cstr(short)}, {prm['page']}, {prm['par']}, "
                     f"ParamKind::{prm['kind']}, {prm['offset']}, {prm['fwmin']}, {prm['fwmax']}, "
                     f"{labels}, {nlab}, {targets}, {prm['default']} }},")
        o.append("};")
        o.append("")
    o.append("static const VoiceDef k_voices[] = {")
    for p in plugins:
        o.append(f"    {{ {cstr(p['id'])}, {cstr(p['name'])}, {p['voice']}, k_{p['id']}_params, "
                 f"sizeof(k_{p['id']}_params) / sizeof(k_{p['id']}_params[0]) }},")
    o.append("};")
    o.append("")
    o.append("} // namespace maquina")
    path = os.path.join(COMMON, "VoiceTables.generated.hpp")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(o) + "\n")


if __name__ == "__main__":
    main()
