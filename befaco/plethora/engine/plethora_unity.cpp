// plethora_unity.cpp - the vendored Noise Plethora port (third_party/noise-plethora) as one
// translation unit, so each plugin binary compiles it like its other sources (also cross).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "noise-plethora/plugins/Banks.cpp"
#include "noise-plethora/teensy/effect_bitcrusher.cpp"
#include "noise-plethora/teensy/effect_combine.cpp"
#include "noise-plethora/teensy/effect_flange.cpp"
#include "noise-plethora/teensy/effect_freeverb.cpp"
#include "noise-plethora/teensy/effect_granular.cpp"
#include "noise-plethora/teensy/effect_multiply.cpp"
#include "noise-plethora/teensy/effect_wavefolder.cpp"
#include "noise-plethora/teensy/filter_variable.cpp"
#include "noise-plethora/teensy/synth_pinknoise.cpp"
#include "noise-plethora/teensy/synth_pwm.cpp"
#include "noise-plethora/teensy/synth_whitenoise.cpp"
