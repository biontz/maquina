# D-50 reference notes (for Maquina LA-50)

What is known about the instrument LA-50 imitates, from published sources, and how LA-50
handles each point. Facts are summarised in our own words; see the sources for the originals.

## Sources
- [MT-1] Simon Trask, "Roland D50 (Part 1): Linear Arithmetic Synthesiser", *Music
  Technology*, May 1987. https://www.muzines.co.uk/articles/roland-d50/2026
- [MT-2] Simon Trask, "Roland D50 Synthesiser (Part 2)", *Music Technology*, June 1987.
  https://www.muzines.co.uk/articles/roland-d50-synthesiser/2036
- [SOS] Martin Russ, "L.A. Synthesis: What Is It? (Part 2)", *Sound On Sound*, June 1987
  (teardown and measurements). https://www.muzines.co.uk/articles/l-a-synthesis-what-is-it/1975
- [MAME] `src/mame/roland/roland_d50.cpp` (memory map, ROM list; skeleton driver, no sound).
- [FW] Our reading of the D-50 v2.22 program ROM (behaviour only; see `../third_party/MAQUINA.md`).
- [MIDI] D-50 MIDI Implementation 2.00 (patch layout: `tools/la50_layout.py`).

## Hardware
| Fact | Source | LA-50 |
|---|---|---|
| CPU NEC µPD78312 (78K/3), 12 MHz; 64 KB program EPROM (27C512) | SOS, MAME | — (the firmware analysis reads this code) |
| Sound chip: Fujitsu custom "LA chip" (MB87136), 32 sounds (partials) at once, 28-bit internal | SOS, MAME | Munt's LA32 wave generator; 32 partials (16 voices × 2) |
| PCM ROM 4 Mbit (512 KB, two chips on early boards, one later) | SOS, MAME | not included; built-in stand-ins |
| Sample rate measured at 31.4 kHz (half-rate component at 15.7 kHz); 16-bit DAC (Burr-Brown PCM54) | SOS | engine runs at 32 kHz (the LA32's nominal rate with a 32.768 MHz clock); difference unresolved |
| Measured S/N ~77 dB | SOS | — |
| 2×40 character backlit LCD | MT-2, SOS | the editor's LCD |

## Architecture
| Fact | Source | LA-50 |
|---|---|---|
| Patch = 2 tones (Upper, Lower) + patch block; tone = common + 2 partials | MT-1, MIDI | same (448-byte patch, 311 parameters) |
| Partial: synth (WG → TVF → TVA) or PCM (no TVF; 23 of 56 partial parameters apply) | SOS, MT-1 | same |
| Synth waves: saw, square with pulse width | MT-1, MT-2 | same (Munt) |
| 7 structures: synth/PCM pairs, mixed or ring-modulated (digital) | MT-1 | same |
| TVF filter: the chip filters a square wave; a saw is derived arithmetically | SOS | Munt's LA32 model does this |
| Envelopes: TVF and TVA 5-stage (times + levels); pitch envelope | MT-1, MT-2 | Munt's logic + D-50 release / key follow [FW] |
| 3 LFOs per tone: triangle, saw, square, random; rate, delay, sync | MT-2 | same |
| Pitch can be modulated by LFO 1 only; PW, cutoff and level by any of the 3 | MT-2 | same |
| Key follow on pitch and cutoff, incl. negative and fractional (3/8, 5/8, 7/8, quarter / eighth tones) | MT-2 | same values (Munt tables) |
| Bias point and level per partial: fades above or below a key, range from a few notes to octaves | MT-2 | same; direction semantics as Munt (D-50 code unclear, [FW]) |
| Velocity ±50 and aftertouch ±7 per partial, on pitch, cutoff, LFO depth, attack time | MT-2 | same parameters |
| No release velocity, no polyphonic aftertouch | MT-2 | same (channel aftertouch only) |

## Voices and key modes
| Fact | Source | LA-50 |
|---|---|---|
| Whole: 16 voices; Dual: 8; Split: 8 + 8 (solo variants 8+1, 1+8) | MT-1 | 16 voices shared; Dual takes 2. **Split shares the 16 voices instead of a fixed 8 + 8** |
| Whole-solo, dual-solo, separate, separate-solo | MT-1 | same |
| Separate: Lower tone on its own MIDI channel; the keyboard plays the Upper tone | MT-2 | Separate: channel 2 plays Lower |
| MIDI mode 4 (mono, 8 channels, per-voice bend) | MT-2 | not supported |

## Effects
| Fact | Source | LA-50 |
|---|---|---|
| EQ and chorus per tone (the review says "per partial"; the patch format has them per tone) | MT-1, MIDI | per tone |
| Chorus: 8 types, digital | MT-1, MT-2 | 8 types, our own algorithm |
| Reverb: 32 types per patch (rooms, halls, plates, delays, gates, reverse, "twisted space"…), balance only, 4 output modes | MT-1, MT-2 | 32 types, our own approximations; same controls |
| Chase Play: delay-like repeat of the other tone, level and time; on/off via footswitch | MT-1, MT-2 | not implemented |
| Clicks when balances or effects change while playing (early units) | MT-2 | not reproduced |

## Memory and control
| Fact | Source | LA-50 |
|---|---|---|
| 64 internal patches + 64 on a card; factory set on a ROM card | MT-1, MT-2 | imports any .syx bank as presets; Roland factory patches not shipped |
| PG-1000 programmer: 56 sliders, two functions each, partial on/off any time | MT-2 | the editor follows it |
| Foot controllers: volume, hold, an assignable switch (patch up, portamento, Chase) and pedal (aftertouch, modulation, balance) | MT-2 | host automation / MIDI CCs |

## Firmware findings applied [FW]
- Release (T5): rate 128 − 1.25 × (T5 − time key follow), no level term; descending unless End level is 1.
- TVA time key follow: (key − 60) >> (4 − TKF); TVF: (key − 60) >> (5 − TKF).
- First TVA segment: time minus velocity follow plus key follow.
- Chorus Rate / Depth: linear around a centre per type and modulator ((value − 50), rate
  clamped to 1–200, depth to 0–127 chip units); tremolo only in Tremolo and Chorus + Tremolo.
  Low Depth leaves most types nearly still. Units assumed: 0.05 Hz, 0.06 ms per unit.
- Not applied (unconfirmed): a TVF cutoff range of 2 × (cutoff + 27) and the D-50's TVF bias curve.
