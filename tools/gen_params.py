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
    o.append("// GENERATED by tools/gen_params.py from the LXR firmware UI tables (+ Dredrum, Plethora, Oneiroi, Klang, Klong, Acida). Do not edit.")
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
