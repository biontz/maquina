/* dredrum_core.h - the Dredrum sound engine, ported from the ATmega48P
 * firmware (third_party/dredrum/trig/dredrum.c, gate/dredrum-gate.c).
 *
 * Bit-exact with the firmware: one call of dredrum_render_sample() is one
 * pass of the firmware's buffer-fill loop and returns the byte it writes to
 * the PWM compare register (OCR1A), 0..254, 127 = silence. The firmware
 * plays these at DREDRUM_RATE: timer 0 in CTC mode, 20 MHz / 8 / (130 + 1)
 * = 19083.97 Hz (the firmware's comment says 15625 Hz, which would need
 * OCR0A = 159; LFUSE 0xff, so no clock divider).
 *
 * The firmware's arithmetic runs with 16-bit int; every intermediate value
 * here fits in 16 bits, so plain C on a 32/64-bit host gives the same
 * results (the casts below keep the firmware's variable widths).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Engine: Copyright 2014 Jan Cumpelik. Port: Maquina.
 */
#ifndef DREDRUM_CORE_H
#define DREDRUM_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DREDRUM_RATE (20000000.0 / (8.0 * 131.0)) /* Hz */
#define DREDRUM_PWM_RATE (20000000.0 / 256.0)      /* timer 1, 8-bit fast PWM */
#define DREDRUM_NUM_OSC 3
#define DREDRUM_SILENCE 127

/* voice_params[osc][...] in the firmware */
enum { DREDRUM_LEVEL = 0, DREDRUM_DECAY = 1, DREDRUM_PITCHENV = 2, DREDRUM_PITCH = 3, DREDRUM_WAVE = 4,
       DREDRUM_NUM_PARAMS = 5 };
enum { DREDRUM_SINE = 0, DREDRUM_SQUARE = 1, DREDRUM_SAW = 2, DREDRUM_NOISE = 3 };

/* Program memory as seen by the noise wave: flash bytes starting at
 * q_sine_table + 0xff, 0x200 of them (wave pointer 0..0x1ff). Differs between
 * the trig and the gate firmware builds (tools/dredrum_flash). */
#define DREDRUM_NOISE_BYTES 0x200

typedef struct {
    uint8_t playing;
    uint32_t phase_acc;
    uint16_t level_acc;
    /* Maquina extension, not in the firmware: pitch ratio (key tracking,
     * octave/semitone shift) in 16.16 fixed point; 0 or 0x10000 = 1.0, which
     * runs the firmware arithmetic unchanged (bit-exact). Otherwise the whole
     * phase step (pitch + pitch drop) is scaled, with frac carrying the
     * fractional part. */
    uint32_t ratio_q16;
    uint16_t frac;
} dredrum_osc;

typedef struct {
    uint8_t params[DREDRUM_NUM_OSC][DREDRUM_NUM_PARAMS];
    uint8_t click_mode;  /* CLICK switch: oscillator 3 stops after one cycle */
    uint8_t gate_mode;   /* 0: trig firmware, 1: gate firmware */
    uint8_t gate_high;   /* gate firmware: GATE_ON, the decay is held */
    dredrum_osc osc[DREDRUM_NUM_OSC];
    const uint8_t* noise; /* DREDRUM_NOISE_BYTES */
} dredrum_state;

/* Power-up state: all oscillators silent. Parameters are left as they are. */
void dredrum_reset(dredrum_state* s);

/* Rising trigger/gate edge: every oscillator restarts. In gate mode the
 * decay then holds until dredrum_gate_off(). */
void dredrum_trigger(dredrum_state* s);
void dredrum_gate_off(dredrum_state* s);

uint8_t dredrum_render_sample(dredrum_state* s);

/* Maquina extension: 16.16 pitch ratio for oscillator n (see dredrum_osc) */
void dredrum_set_ratio(dredrum_state* s, int n, uint32_t ratio_q16);

/* the firmware's waverender() */
uint8_t dredrum_waverender(const dredrum_state* s, uint16_t wave_pointer, uint8_t wave_type);

int dredrum_any_playing(const dredrum_state* s);

#ifdef __cplusplus
}
#endif

#endif
