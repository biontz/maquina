// SPDX-License-Identifier: GPL-3.0-only
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND    "Maquina"
#define DISTRHO_PLUGIN_NAME     "Maquina HiHat"
#define DISTRHO_PLUGIN_LABEL    "MaquinaHiHat"
#define DISTRHO_PLUGIN_URI      "urn:maquina:hihat"
#define DISTRHO_PLUGIN_CLAP_ID  "org.maquina.hihat"
#define DISTRHO_PLUGIN_BRAND_ID Mqna
#define DISTRHO_PLUGIN_UNIQUE_ID Mqh1

#ifdef MAQUINA_NO_UI
#define DISTRHO_PLUGIN_HAS_UI          0
#else
#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_UI_USE_NANOVG          1
#define DISTRHO_UI_USER_RESIZABLE      1
#endif
#define DISTRHO_PLUGIN_IS_RT_SAFE      1
#define DISTRHO_PLUGIN_IS_SYNTH        1
#define DISTRHO_PLUGIN_NUM_INPUTS      0
#define DISTRHO_PLUGIN_NUM_OUTPUTS     2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 1
#define DISTRHO_PLUGIN_WANT_PROGRAMS   1
#define DISTRHO_PLUGIN_WANT_TIMEPOS    1
#define DISTRHO_PLUGIN_WANT_LATENCY    1
#define DISTRHO_PLUGIN_WANT_STATE      1
#define DISTRHO_PLUGIN_WANT_FULL_STATE 1

#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Drum|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "drum-machine", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_CLAP_REMOTE_CONTROLS 1  // pages from RemotePages.generated.hpp (patches/dpf/0002)

#endif
