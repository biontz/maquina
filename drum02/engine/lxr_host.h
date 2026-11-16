/*
 * lxr_host.h - host-side driver for the LXR-02 firmware sound engine.
 *
 * The firmware sources under third_party/lxr-firmware are compiled unmodified.
 * They keep all voice state in globals, so this layer exposes:
 *   - a "state blob" that captures every mutable global the engine uses, so each
 *     plugin instance can own a private copy (load before use, save after);
 *   - the same entry points the firmware's front panel / MIDI parser use to set
 *     parameters and trigger notes, so the knob curves are the firmware's own.
 *
 * None of these functions are thread-safe: callers serialise access (see
 * EngineHost.cpp) and load their own state blob before calling anything else.
 *
 * Copyright (C) 2026 Maquina contributors
 * Based on the LXR-02 firmware sound engine, Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#ifndef MAQUINA_LXR_HOST_H
#define MAQUINA_LXR_HOST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LXR_BLOCK_SIZE 64          /* OUTPUT_DMA_SIZE: samples per engine block */
#define LXR_ENGINE_RATE 44140.0    /* REAL_FS: the hardware's actual sample rate */

/* Engine voice slots, as numbered by the firmware. */
enum LxrVoice {
    LXR_VOICE_DRUM1 = 0,
    LXR_VOICE_DRUM2 = 1,
    LXR_VOICE_DRUM3 = 2,
    LXR_VOICE_SNARE = 3,
    LXR_VOICE_CYMBAL = 4,
    LXR_VOICE_HIHAT = 5,
    LXR_NUM_VOICES = 6
};
/* Trigger slots: as voices, plus 6 = open hihat (5 is closed hihat). */
#define LXR_TRIGGER_HIHAT_OPEN 6

/* One-time process-wide initialisation (tables, parameter pointer array) and
 * capture of the pristine post-init state. Idempotent. */
void lxr_global_init(void);

/* Per-instance state. */
size_t lxr_state_size(void);
void lxr_state_save(void* blob);       /* globals -> blob */
void lxr_state_load(const void* blob); /* blob -> globals */
void lxr_state_reset(void);            /* globals := pristine post-init state */

/* Load the firmware's built-in "Initkit" into the edit buffer and send every
 * sound parameter to the engine, as preset_init() does on the hardware. */
void lxr_load_init_kit(void);

/* Set sound parameter `par` (firmware ParamEnums index) to its 0..127 value,
 * exactly as a front-panel edit / preset load does. Mod-target parameters
 * (PAR_VEL_DEST_x, PAR_TARGET_LFOx) take an index into modTargets[].
 * Global settings past END_OF_SOUND_PARAMETERS (PAR_MIDI_NOTEx) are stored
 * in the edit buffer only, like on the hardware. */
void lxr_set_param(uint16_t par, uint8_t value);
uint8_t lxr_get_param(uint16_t par);

/* Number of entries in modTargets[] and the per-voice [start,end] window. */
uint8_t lxr_num_mod_targets(void);
void lxr_mod_target_range(uint8_t voice, uint8_t* start, uint8_t* end);
uint16_t lxr_mod_target_param(uint8_t modTargetIdx);

/* MIDI note-on for trigger slot 0..6 with the hardware's PAR_MIDI_NOTEx rule:
 * 0 = "any" (note transposes around C4=60), otherwise only that note plays,
 * at the default pitch. Returns 1 if the voice was triggered. */
int lxr_note_on(uint8_t trigger, uint8_t note, uint8_t velocity);

/* Tempo used by tempo-synced LFOs (the hardware's sequencer BPM). */
void lxr_set_bpm(uint16_t bpm);

/* Seed for the noise generator (stands in for the STM32 hardware RNG). */
void lxr_seed_rng(uint32_t seed);

/* Test hook: while frozen the RNG returns a constant, so a voice rendered on
 * its own and inside the full firmware mixer (where the other voices also draw
 * noise) can be compared sample for sample. */
void lxr_test_freeze_rng(int frozen);

/* Render one 64-sample block of a single voice, following the firmware mixer
 * path for that voice (LFO, async update, sync block, decimator, pan law).
 * out is interleaved stereo int16, LXR_BLOCK_SIZE frames. */
void lxr_render_voice(uint8_t voice, int16_t* out);

/* The firmware's own full mixer (all six voices + FX bus), for reference
 * tests. out1/out2 are the two stereo DAC buffers, LXR_BLOCK_SIZE frames each. */
void lxr_render_mixer_reference(int16_t* out1, int16_t* out2);

#ifdef __cplusplus
}
#endif
#endif
