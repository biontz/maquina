# Maquina Strings: vendored code

## string-machine (`./`)

Source: https://github.com/jpcima/string-machine, commit 7050cb1 (2023-11-28), Jean Pierre
Cimalando, Boost Software License 1.0 (`LICENSE`, kept with the files as the licence asks).
"Digital model of electronic string ensemble instrument" (a DPF plugin).

Vendored, unmodified:

- `gen/dsp/`: the Faust-generated C++ (made by Faust + faustpp, committed upstream; Faust is
  not needed to build) of `AsymWaveshaper` (the oscillators' "enhance" soft clipper),
  `StringFiltersHighshelf`, `LFO3PhaseDual` (the ensemble's two three-phase LFOs) and
  `Delay3PhaseDigitalStereo` (the ensemble's digital delays);
- `sources/dsp/`: the Faust sources of those (`*.dsp`), `AHDSREnvelope`,
  `Delay3PhaseStereo` (the ensemble's delays, BBD or digital);
- `sources/bbd/`: the BBD delay line model (used by `Delay3PhaseStereo` in its analog mode;
  Maquina Strings runs the digital mode);
- `sources/SolinaChorusStereo.{h,cpp}`, `sources/StringSynthDefs.h`.

Not vendored: the plugin, its UI (Cairo), the voice / synth classes (`StringOsc`,
`StringFilters`, `StringSynth`), `thirdparty/` (`vco/` is GPL-2.0 and unused by the synth).

How Maquina uses it (`stringmachine/strings/engine/StringsEngine.cpp`):

- the voice is our own, after `StringOsc` + `StringFilters`: per footage an oscillator, the
  keyed one-pole high-pass and low-pass (the same formulas, in float, coefficients once per
  block), then one `AsymWaveshaper` and one `StringFiltersHighshelf` per voice;
- string-machine has two footages (8' and 4'); Maquina adds 16' and mixes the three by the
  REGISTER stations;
- the pulse with PWM is string-machine's `PwmOscillator` idea (Faust's `os.pulsetrain`: a DPW
  saw minus itself delayed by the duty, the duty swept 0.5 .. 0.26 by a triangle LFO),
  written inline: two DPW saws per footage, which costs far less than the generated code's
  4096-sample fractional delay;
- `AHDSREnvelope` is the amp envelope (hold 0);
- `SolinaChorusStereo` is the ENSEMBLE effect (digital delays; SLOW / FAST = its two LFOs,
  string-machine's depths 30.71 % / 90.55 %).

## Mutable Instruments (from `mutable-instruments/third_party/eurorack`, MIT)

Not copied here: the wave layer reads Plaits' wave tables (`plaits/resources.cc`
`wav_integrated_waves`, linked from `libklang.a`) with Plaits' wavetable engine's algorithm
(`plaits/dsp/engine/wavetable_engine.cc`) ported in `StringsEngine.cpp` (`WaveOsc`: the bank
stays whole, so 4 waves are read per sample instead of 8); the filters are stmlib's `Svf`; the
reverb is Rings' (`rings/dsp/fx/reverb.h`).
