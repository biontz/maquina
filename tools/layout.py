"""layout.py - where things live in the repository (mirrors common/paths.mk).

  drum02/                 Erica Synths LXR-02 firmware: kick tom snare cymbal hihat
  befaco/plethora/        Befaco Noise Plethora
  befaco/oneiroi/         Befaco Oneiroi: oneiroi + oneiroi_fx
  euro-modules/dredrum/   Dredrum
  mutable-instruments/    Mutable Instruments: klang/ (Plaits), klong/ (Rings: klong + klong_fx)
  openrb/acida/           OpenRB-338 (ReBirth RB-338 reimplementation): acida + acida_fx
  s1vave/bl101/           S1Vave (SH-101-style firmware): bl101 + bl101_fx
  lasynth/la50/           LA synthesis playing D-50 patches (dsynkant + Munt): la50

each with engine/, third_party/, plugins/<plugin>/ and presets/<voice>/.
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMON = os.path.join(ROOT, "common")

DRUM02 = os.path.join(ROOT, "drum02")
PLETHORA = os.path.join(ROOT, "befaco", "plethora")
ONEIROI = os.path.join(ROOT, "befaco", "oneiroi")
DREDRUM = os.path.join(ROOT, "euro-modules", "dredrum")
KLANG = os.path.join(ROOT, "mutable-instruments", "klang")
KLONG = os.path.join(ROOT, "mutable-instruments", "klong")
ACIDA = os.path.join(ROOT, "openrb", "acida")
BL101 = os.path.join(ROOT, "s1vave", "bl101")
LA50 = os.path.join(ROOT, "lasynth", "la50")

LXR_FW = os.path.join(DRUM02, "third_party", "lxr-firmware")

# voice id -> module folder
MODULE_OF = {
    "kick": DRUM02, "tom": DRUM02, "snare": DRUM02, "cymbal": DRUM02, "hihat": DRUM02,
    "dredrum": DREDRUM,
    "plethora": PLETHORA,
    "oneiroi": ONEIROI, "oneiroi_fx": ONEIROI,
    "klang": KLANG,
    "klong": KLONG, "klong_fx": KLONG,
    "acida": ACIDA, "acida_fx": ACIDA,
    "bl101": BL101, "bl101_fx": BL101,
    "la50": LA50,
}


def preset_dir(voice):
    """factory presets of a voice: <module>/presets/<voice>/"""
    return os.path.join(MODULE_OF[voice], "presets", voice)
