# Editor redesign — plan and control map

Design: `Music/Plugin Design/` on the Workspace disk (`DESIGN-GUIDE.md`, `screenshots/`,
`mockups/`). This file maps every control on each board to its parameter (LV2 symbol), lists
what the boards add or leave out, and records the decisions taken before building.

Scope: UI only. DSP, parameters, LV2 symbols, presets, state and randomizer stay as they are,
except the Oneiroi padlocks (step 6). Every parameter reachable today stays reachable.

## 1. Architecture

`common/DrumUI.cpp` stays the one editor every `UI.cpp` includes. New files beside it:

| File | Holds |
|---|---|
| `common/ui/Theme.hpp` | `Theme` struct per family (`Silver303`, `Graphene`, `LxrBlack`, `Befaco`, `MutableGrey`, `EricaBW`): panel / print / accent colours, fonts, `KnobStyle`, `SliderStyle`, `SwitchStyle`, `DisplayStyle`, panel radius, window width. Picked at compile time from `MAQUINA_VOICE_INDEX` (no Makefile change needed). |
| `common/ui/Controls.cpp` | Draw functions: knob (scale, value arc, skirt, cap, pointer; S/M/L), slider + tick ladder, slide switch, LED button, rubber pad, display window, 7-segment digit, dot-matrix / OLED text, Befaco fader, badge, shift-label box, padlock, range bar. No hit-testing (stays in `DrumUI.cpp`). |
| `common/ui/Chrome.cpp` | Header (46 px) and random drawer (96 px open, 46 px folded — see decision 10), coloured by the theme accent. |
| `common/ui/Layouts.cpp` | `makeLayout()`: the existing row packing (BL-101, BL-101 FX, LXR drums, Acida FX) plus fixed layouts in panel coordinates taken from the mockups (Acida, Klang, Klong, Klong FX, Oneiroi, Oneiroi FX, Plethora, Dredrum). `makeLa50` stays. |
| `common/ui/fonts/` | The OFL fonts (static TTF instances of the weights used, `LICENSE-OFL.txt` per font), turned into `const unsigned char[]` headers by a small script; only the theme's fonts are compiled into each plugin. |

`kWidth` becomes per theme / plugin (mockup widths: 1100 most, Klang / Klong / Klong FX 880,
LA-50 1240, Oneiroi / Oneiroi FX / Plethora 1320). Heights follow the layout. Zoom keeps
scaling the whole thing; checks at 75 / 100 / 150 %.

Fonts: NanoVG uses stb_truetype, which does not read variable-font axes, so each weight is a
static TTF (Inter 700/800, IBM Plex Mono 500/600, Oswald 500/600, Mulish 800, Questrial,
Michroma, VT323, Silkscreen, Chakra Petch 600). Share Tech Mono and Archivo are in the guide's
list but no board uses them; they are left out.

## 2. Control map per board

Notation: `symbol` = parameter; *(UI)* = editor-only state, no parameter; **new** = not in
today's editor.

### Acida (`acida`, 1100 × 380)

| Board | Symbol |
|---|---|
| WAVEFORM slide switch SAW / SQUARE | `waveform` |
| ENGINE stepper (red LED digits) | `engine` (2.0 / Vintage) |
| TUNING · CUT OFF FREQ (82 px) · RESONANCE · ENV MOD · DECAY · ACCENT | `tune` `cutoff` `resonance` `env_mod` `decay` `accent` |
| ACC VEL · GAIN (44 px) | `accent_vel` `gain` |
| Logo, smiley | *(drawing)* |

All 10 parameters on the board. Random drawer: FILTER, ACCENT, WAVE, TUNE (decision 1).

### Acida FX (`acida_fx`, 1100 × 520)

| Board | Symbol |
|---|---|
| DISTORTION: On, Amount, Shape | `dist_on` `dist_amount` `dist_shape` |
| PCF FILTER: On, Mode window, Wave, Freq (big), Reso, Amount, Decay | `pcf_on` `pcf_mode` `pcf_wave` `pcf_freq` `pcf_reso` `pcf_amount` `pcf_decay` |
| COMPRESSOR: On, Amount, Thresh | `comp_on` `comp_amount` `comp_threshold` |
| TEMPO DELAY: On, Mode window, Steps, Send, Feedback, Pan | `delay_on` `delay_mode` `delay_steps` `delay_send` `delay_feedback` `delay_pan` |
| GAIN | `gain` |

All 20 on the board. Random: DIST, PCF, COMP, DELAY, SWITCHES = today's groups.

### BL-101 (`bl101`, 1100 × 860)

| Board | Symbol |
|---|---|
| MODULATOR: Rate slider, Wave switch | `modulator_rate`, `modulator_wave` — see question 3 |
| VCO: Mod, PW sliders; Range switch; PWM switch LFO / MAN / ENV | `vco_mod` `vco_pw`, `vco_range` (question 3), `vco_pwm_src` |
| SOURCE MIXER: Pulse, Saw, Sub, Noise sliders; SUB switch | `mix_pulse` `mix_saw` `mix_sub` `mix_noise`, `mix_sub_type` |
| VCF: Freq, Res, Env, Mod, Kybd | `vcf_freq` `vcf_res` `vcf_env` `vcf_mod` `vcf_kybd` |
| VCA switch ENV / GATE | `vca_mode` |
| ENV: Trig switch, A D S R | `env_trigger`, `adsr_attack` `adsr_decay` `adsr_sustain` `adsr_release` |
| LFO MODE: Sync, Fast, Key Trig | `modulator_sync` `modulator_fast` `modulator_trig` |
| VOICE: Voicing, Porta steppers, Time, Transpose | `voice_mode` `porta_mode` `porta_time` `transpose` |
| CHORD: Voice 2/3/4 buttons + shift knobs | `chord2_on` `chord2_shift` … `chord4_shift` |
| CONTROL: Fine, Bend VCO, Bend VCF, Spread, Detune | `vco_fine` `bend_vco` `bend_vcf` `stereo_spread` `unison_detune` |
| MIX: Level, Gain | `patch_level` `gain` |
| OSC DRAW: bar display, Mode, Multiply | `draw_1..16`, `draw_mode`, `draw_mult` |
| OSC CHOP: 4 × 16 pads, Overtone, Comb | `chop_bits_*_lo/hi`, `chop_overtone`, `chop_comb` |

Missing from the board: **`mix_noise_type`** (Pink / White) and the four **mask selectors**
`chop_mask_sqr/saw/sub/noise` (Custom / All / Odd / Even / 1 of 4 / 4 of 4 / Off; today a
name box right of each pad row). Plan: a small PINK / WHITE switch under NOISE, and the mask
name as a small display window right of each pad row (click = list, wheel = step), between the
pads and OVERTONE. See question 3 for the switch positions.

Random drawer shows PATCH: today's editor shows it too, because the LA-50 group `patch_*`
matches `patch_level`, which belongs to LEVEL; PATCH holds nothing for BL-101. Plan: hide a
group in the drawer when every parameter it matches belongs to an earlier group (editor-side
check; the randomizer is untouched).

As built (step 5, `makeGraphene` in `common/ui/Layouts.cpp`): sections keep the mockup's
width (or their natural one if wider), the others share the rest. Slide switches list the
options top down from the last value (as printed on the panel: SUB -1 OCT / -2 OCT / -2 PW);
WAVE and RANGE have all 6 positions. NOISE type: a small PINK / WHITE chip under the NOISE
slider (click toggles). CHOP: the mask name in a window after each pad row (click = next,
wheel = step). The pads and DRAW bars are dimmed while OVERTONE is 0 / DRAW is off (they have
no effect then). The drawer hides PATCH (a group shows only when a parameter's first matching
group is it; BL-101 is the only voice this changes).

### BL-101 FX (`bl101_fx`, 1100 × 520)

CHORUS Mode `chorus_mode` · DELAY Level / Time / Feedback `dly_level` `dly_time` `dly_feedback`,
SYNC `dly_sync` · REVERB Type `rev_type`, Level / Time / Pre-delay `rev_level` `rev_time`
`rev_predelay` · LO-FI Mode `lofi_mode`, Amount `lofi_amount`, Mix `lofi_mix` · MIX Gain `gain`.
All 13 on the board; random groups match today's.

### LA-50 (`la50`, 1240 × 800)

The three slider rows, group brackets, PCM wave selector, envelope graphs, INIT, IMPORT .SYX,
COMMON SELECT (Upper / Lower), PARTIAL SELECT (U1 U2 L1 L2), UPPER PARTIALS ON (P1 P2) and
OUTPUT GAIN are today's `makeLa50` slots, restyled; all 312 parameters stay reached the same
way. One gap: the board has **no PATCH select button**. Today PATCH is the third edit mode
(`fLaMode == 2`) and the only way to edit the patch fields of row 1 and row 3 (key shift,
fine, tone balance, bender, aftertouch bend, portamento, hold, chase, key mode, split, output
mode, reverb, total volume). See question 4.

As built (step 5, `makeLa50New`): the same `La50Panel` slots, sections from the mockup
(42 px columns), so the edit modes, drags, envelope handles, INIT and IMPORT work unchanged.
PATCH is a LED button above COMMON SELECT (decision 4). Labels of the mode being edited are
bright, the others dim; group brackets are orange. Window 1240 × 800.

### LXR drums — Kick, Tom, Snare, Cymbal, HiHat (1100 × 730)

| Board section | Symbols |
|---|---|
| OSC: Coarse, Fine, Wave, PWM | `osc_coarse` `osc_fine` `osc_wave` `osc_pwm` |
| NOISE (Snare): Freq, Mix | `noise_freq` `noise_mix` |
| AEG: graph, Attack, Decay, Slope (+ Repeat: Snare, Cymbal) | `amp_attack` `amp_decay` `amp_slope` `amp_repeat` |
| PITCH EG (not Cymbal / HiHat): graph, Amount, Decay, Slope | `pitch_amount` `pitch_decay` `pitch_slope` |
| CLICK: Wave, Volume, Freq | `trans_wave` `trans_volume` `trans_freq` |
| LFO: Wave, Rate, Amount, Phase, Target, Sync, Retrig | `lfo_wave` `lfo_rate` `lfo_amount` `lfo_offset` `lfo_target` `lfo_sync` `lfo_retrigger` |
| MOD: Velo Dest, Amount, > Vol | `velo_dest` `velo_amount` `velo_volume` |
| FM (Kick, Tom): Amount, Freq, Wave, Mode | `fm_amount` `fm_freq` `fm_wave` `fm_mode` |
| MOD OSC (Cymbal, HiHat): Freq / Gain / Wave 1 and 2 | `mod1_*` `mod2_*` |
| FILTER: Type, Cutoff, Reso, Drive (+ Key Trk on Kick) | `filter_type` `filter_freq` `filter_reso` `filter_drive` (`note_pitch`) |
| KEY (Snare, Cymbal, HiHat): Track | `note_pitch` |
| MIX: Volume, Pan, Drive, Rate, Gain | `volume` `pan` `drive` `decimation` `gain` |
| Top strip: display, page buttons | *(UI)* the page shown = last-touched section, buttons pick it |
| Top strip: VOLUME fader | see question 5 |

Mismatch: the board draws LFO **SYNC as a round toggle**, but `lfo_sync` has 12 values
(Off, 4/1 … 1/32). Plan: a small red display window with step buttons like TARGET (it fits in
the LFO box by narrowing TARGET); RETRIG stays a toggle.

Row tables: Kick / Tom use OSC · AEG · PITCH EG / CLICK · LFO · MOD / FM · FILTER (+KEY) ·
MIX; Snare and Cymbal / HiHat as in the guide. These replace `kDrumRows` / `kSnareRows` /
`kMetalRows` (only the order changes).

As built (step 4, `makeLxr` in `common/ui/Layouts.cpp`):

- Each box gets its natural width (its items side by side, like the mockups' flex rows); the
  rest of the row is shared out by the mockups' flex weights (Kick uses Tom's).
- **Display**: the page of the last parameter touched (a knob, selector, toggle, graph handle
  or list), 4 at a time like the LXR-02: pages with more than 4 (MOD: pitch EG / velocity,
  LFO, MIX: + KEY TRK and GAIN, M.OSC: 1 / 2) have a second half. Clicking the lit page
  button or the display flips halves. The touched parameter's name is in capitals (`COA`).
  Names are the firmware's three-letter short names.
- **Red windows** show short values (Sine → Sin, Lowpass → LP, Click 2 → Clk2; modulation
  targets: section letter + short name, Osc Coarse → Ocoa); the list (click on the window)
  has the full names. The small round buttons step back / on.
- LFO **SYNC** is a red window (12 values), RETRIG a round toggle, as planned above.
- The envelope graphs keep the classic editor's drag handles (small dots).
- Long wordmarks shrink to fit the 170 px column (CYMBAL).
- Drawer with 10 groups: tighter gaps (10 px) so the group titles fit.

### Klang (`klang`, 880 × 540)

| Board | Symbol |
|---|---|
| ‹ › buttons, 8 model LEDs (bank colour), model window | `model` (24 = 3 banks × 8) |
| TRACK LED + button | `key_track` |
| FREQUENCY / FINE | `coarse` / `fine` |
| HARMONICS / ENV › FM | `harmonics` / `fm_env` |
| TIMBRE / ENV › TIMBRE | `timbre` / `timbre_env` |
| MORPH / ENV › MORPH | `morph` / `morph_env` |
| DECAY / MODE (LOW-PASS GATE) | `lpg_decay` / `lpg_mode` |
| COLOUR | `lpg_colour` |
| GAIN / OUTPUT | `gain` / `output` (Out, Aux, Out + Aux, Mix) |

All 15 on the board; random groups match.

### Klong, Klong FX (`klong`, `klong_fx`, 880 × 600)

| Board | Symbol |
|---|---|
| POLY dots 1 / 2 / 4 + button | `polyphony` (1..4) — question 6 |
| MODEL six colour dots + button + window | `model` (12 values) — question 7 |
| FREQUENCY big, FINE small | `coarse`, `fine` |
| STRUCTURE big; BRIGHTNESS, DAMPING, POSITION | `structure` `brightness` `damping` `position` |
| OUTPUT box ODD / EVEN | `output` (Odd / Even, Mono) — question 6 |
| NOTES selector (Klong FX) | `note_source` |
| GAIN | `gain` |

Random groups (MODEL, RESONATOR, POLY, TUNE) match.

### Oneiroi, Oneiroi FX (`oneiroi`, `oneiroi_fx`, 1320 × 900)

| Board (knob / SHIFT box) | Symbol |
|---|---|
| VARISPEED | `loop_speed` |
| START / SOS | `loop_start` / `loop_sos` |
| LENGTH / FILTER | `loop_length` / `loop_filter` |
| RECORD / CLEAR | `loop_record` / `loop_clear` |
| PITCH / OCTAVE (Oneiroi) | `osc_pitch` / `osc_octave` |
| DETUNE / UNISON (Oneiroi) | `osc_detune` / `osc_unison` |
| SS / [WT] switch (Oneiroi) | `osc2_mode` |
| SOURCE selector (FX, INPUT area) | `loop_source` |
| MOD LEVEL, SPEED / TYPE | `mod_level`, `mod_speed` / `mod_type` |
| CUTOFF / MODE, badges LP BP HP CF | `flt_cutoff` / `flt_mode` |
| RESO / POS, badges I–IV | `flt_reso` / `flt_pos` |
| TUNE / DISSONANCE, FEEDBACK | `res_tune` / `res_dissonance`, `res_feedback` |
| REPEATS, DENSITY / FILTER | `echo_repeats`, `echo_density` / `echo_filter` |
| DECAY, SPACETIME / AUTOPAN | `amb_decay`, `amb_spacetime` / `amb_autopan` |
| Faders LOOPER SINE SS/WT (FX: LOOPER INPUT) | `vol_looper` `vol_sine` `vol_osc2` (FX: `vol_looper` `vol_input`) |
| Faders FILTER RESONATOR ECHO AMBIENCE | `mix_filter` `mix_resonator` `mix_echo` `mix_ambience` |
| ALL / OSC / LOOPER / FX switch, AMOUNT switch, ◀ ▶, RANDOM | *(UI)* `rand_levels` state: target, amount, undo / redo |
| Padlocks on OSC / LOOPER / FX | **new**: per-group lock flag, step 6 |
| SHIFT button | question 2 |
| EXT: ENVELOPE A D S R, DRONE (Oneiroi) | `env_attack` `env_decay` `env_sustain` `env_release` `env_drone` |
| EXT: SYNC, OUT GAIN | `sync`, `gain` |
| Panel SYNC LED, MOD LEVEL LED | display only: SYNC lit when `sync` ≠ Off; MOD LEVEL LED brightness follows `mod_level` (the editor has no LFO phase) |

All parameters on the board (Oneiroi 42, FX 32).

As built (step 6, `makeOneiroi`): positions from the mockups; no drawer (the window is
1320 × 896). SHIFT latches (decision 2): while lit, every knob with a SHIFT box edits its SHIFT
function (arc and the first value show it) and RECORD clears; the white boxes can be dragged,
wheeled or double-clicked at any time. Filter mode / reso position badges set the value.
Randomizer: the target lever and row names pick ALL / OSC / LOOPER / FX (FX: no OSC), the
padlocks lock OSC / LOOPER / FX (the existing per-group lock flags in `rand_levels`, honoured by
`oneiroiRandomize`), AMOUNT Full / Mid / Low, ◀ undo / ▶ redo of the last roll (their LEDs show
which is possible). Oswald (400, 500) added.

### Plethora (`plethora`, 1320 × 760)

| Board | Symbol |
|---|---|
| A: X A, Y A, CUTOFF (silver), RES, HP / BP / LP switch, algorithm readout | `a_x` `a_y` `a_cutoff` `a_reso` `a_ftype` `a_algo` |
| B: the same mirrored (white) | `b_*` |
| C: GRIT QTY, RES, HP / BP / LP, CUTOFF, SOURCE | `c_grit` `c_reso` `c_ftype` `c_cutoff` `c_source` |
| PROG A / PROG B 7-segment, BANK LED, PROGRAM button | `a_algo` / `b_algo` (digit = program 1–10, 10 shown as 0) — question 8 |
| EXT A / B / C: Attack, Decay, Level, Mute | `*_attack` `*_decay` `*_level` `*_mute` |
| EXT: ENV SYNC, MODE, GAIN | `env_sync`, `trig_mode`, `gain` |
| Drawer: RND ON / ROLL per channel | *(UI)* `oscOn[]`, roll |

Mismatch: the SOURCE switch has **three** positions (GRITTY / FILTERED / WHITE); `c_source`
has two (Gritty, White). Question 9.

As built (step 7, `makePlethora`): positions from the mockup (module 994 × 580, EXT 300 wide).
PROGRAM: a tap switches the display between A and B, a hold (0.5 s) toggles bank mode (BANK
LED); a click on a digit selects that channel, the wheel on a digit steps its program within
the bank (bank mode: its bank, keeping the program). The readouts under the CUTOFFs and MODE
open the list. SOURCE is GRITTY | WHITE (decision 9). The drawer keeps RND ON / ROLL per
channel on its right (shown with the drawer open).

### Dredrum (`dredrum`, 1100 × 640)

| Board | Symbol |
|---|---|
| OSC n: wave window + step buttons | `oscN_wave` |
| Pitch, Drop, Decay, Level | `oscN_pitch` `oscN_drop` `oscN_decay` `oscN_level` |
| Octave, Semi | `oscN_octave` `oscN_semi` |
| Track, Mute buttons; Click (OSC 3) | `oscN_track` `oscN_mute`; `click` |
| MODE bat toggle TRIG / GATE | `trig_mode` |
| GAIN | `gain` |
| OLED | *(UI)* last-touched oscillator |
| Drawer: RND ON / ROLL per OSC | *(UI)* as today |

All 30 on the board; random groups match.

## 3. Decisions

Asked before building; all proposals accepted on 2026-11-20 (Klong colour order as given in 7).

1. **Acida random groups.** The board shows CUTOFF, RESONANCE, ENV MOD, ACCENT, WAVE, TUNE;
   the randomizer has FILTER (cutoff, resonance, env mod **and decay**), ACCENT, WAVE, TUNE.
   Splitting would change the randomizer and the saved `rand_levels` keys. Decision: keep
   FILTER as one group in the drawer (this also settles the guide's open item: DECAY is in
   FILTER).
2. **Oneiroi SHIFT.** The SHIFT functions are now white boxes under the knobs. Proposal:
   SHIFT latches (click on / off, lit while on); while on, every knob with a SHIFT box edits its
   SHIFT parameter (arc and value show it). Dragging, wheeling or double-clicking the white box
   itself edits the SHIFT parameter directly at any time.
3. **BL-101 switches with fewer positions than values.** WAVE shows 4 (∿ TRI SQR S/H), the
   parameter has 6 (Saw, Inv Saw, Triangle, Square, Random, Noise); RANGE shows 4 (16' 4' 8'
   2'), the parameter has 6 (64' 32' 16' 8' 4' 2'). Proposal: switches with all 6 positions,
   labelled from the parameter (taller lever track).
4. **LA-50 PATCH select.** Proposal: a PATCH button above COMMON SELECT (same grey cap + LED),
   so the patch fields stay editable.
5. **LXR VOLUME fader** (top right). Proposal: it is `volume` (the same parameter as MIX
   VOLUME), not `gain`.
6. **Klong POLY and OUTPUT.** `polyphony` is 1..4, the board shows 1 / 2 / 4. Proposal: dots
   1 2 3 4, button steps 1→2→3→4→1. OUTPUT: the box shows ODD | EVEN for Odd / Even and one
   MONO segment lit for Mono; clicking toggles.
7. **Klong MODEL dots.** 12 models, 6 dots. Proposal: dot = model mod 6; models 7–12 (the
   "Peace" ones) light the same dot with a ring around it, and the window names the model. And the colour ↔ model order (pink, teal, yellow, blue, white, lilac → Modal,
   Sympathetic, Inharm String, FM Voice, Sympath Quant, String + Verb): confirmed.
8. **Plethora PROGRAM button.** Proposal: tap = select A or B (the selected digit glows), hold
   (≥ 0.5 s) = bank mode (BANK LED lit); wheel / drag on a digit steps that channel's program
   (bank mode: its bank). The algorithm readout under each CUTOFF stays the list.
9. **Plethora SOURCE.** The switch has 3 positions, `c_source` 2. Proposal: a two-position
   switch GRITTY / WHITE (no FILTERED), unless a third source is wanted — that would be a DSP
   change and out of scope here.
10. **Random drawer fold** (asked for while building step 3). The drawer starts folded: a
    46 px bar with [RANDOM] [BACK] on the left and [RANGES ▾] on the right. RANGES opens the
    design's 96 px drawer (range bars + padlocks) with a [▴] button that folds it again. The
    window grows / shrinks by the difference (`setSize`, as zoom does). The choice is kept in
    `ui.conf` as `drawer=open|folded`; `ui.conf` is now `key=value` lines (`zoom=`, `drawer=`,
    unknown keys kept).

## 4. Steps

As in the prompt: 2 Acida + theme / controls / chrome · 3 Klang, Klong, Klong FX · 4 LXR
drums · 5 BL-101, BL-101 FX, LA-50 · 6 Oneiroi, Oneiroi FX (+ padlocks) · 7 Plethora ·
8 Dredrum, Acida FX. One commit per step, a stop after each. Until a plugin's step is done it
keeps today's look (the old draw path stays until the last step removes it).

Checks per step: build, `tools/screenshots.sh <plugin>` vs the design screenshot, 75 / 100 /
150 % zoom, `make test` (or at least `check_presets`, `check_random`), click-through.
