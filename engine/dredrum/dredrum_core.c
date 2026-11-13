/* dredrum_core.c - see dredrum_core.h.
 *
 * Statement for statement the firmware's buffer-fill loop and waverender(),
 * with the same variable widths. Oscillator 3 differs from 1 and 2 only in
 * the click mode check, exactly as in the firmware. The pitch ratio
 * (dredrum_set_ratio) is Maquina's; at 1.0 the firmware code path runs.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Engine: Copyright 2014 Jan Cumpelik. Port: Maquina.
 */
#include "dredrum_core.h"

/* third_party/dredrum/trig/8bit_q_sine.h (identical in gate/) */
static const uint8_t q_sine_table[128] = {
    0, 1, 3, 4, 6, 7, 9, 10, 12, 14, 15, 17, 18, 20, 21, 23,
    24, 26, 28, 29, 31, 32, 34, 35, 37, 38, 40, 41, 43, 44, 46, 47,
    48, 50, 51, 53, 54, 56, 57, 58, 60, 61, 63, 64, 65, 67, 68, 69,
    71, 72, 73, 74, 76, 77, 78, 79, 81, 82, 83, 84, 85, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105,
    106, 107, 108, 108, 109, 110, 111, 112, 112, 113, 114, 115, 115, 116, 117, 117,
    118, 118, 119, 119, 120, 121, 121, 122, 122, 122, 123, 123, 124, 124, 124, 125,
    125, 125, 126, 126, 126, 126, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
};

void dredrum_reset(dredrum_state* s)
{
    int i;
    for (i = 0; i < DREDRUM_NUM_OSC; ++i) {
        s->osc[i].playing = 0;
        s->osc[i].phase_acc = 0;
        s->osc[i].level_acc = 0;
    }
    s->gate_high = 0;
}

void dredrum_trigger(dredrum_state* s)
{
    int i;
    for (i = 0; i < DREDRUM_NUM_OSC; ++i) {
        s->osc[i].playing = 1;
        s->osc[i].phase_acc = 0;
        s->osc[i].level_acc = 0;
        s->osc[i].frac = 0;
    }
    s->gate_high = s->gate_mode ? 1 : 0;
}

void dredrum_gate_off(dredrum_state* s) { s->gate_high = 0; }

void dredrum_set_ratio(dredrum_state* s, int n, uint32_t ratio_q16) { s->osc[n].ratio_q16 = ratio_q16; }

int dredrum_any_playing(const dredrum_state* s)
{
    return s->osc[0].playing || s->osc[1].playing || s->osc[2].playing;
}

uint8_t dredrum_waverender(const dredrum_state* s, uint16_t wave_pointer, uint8_t wave_type)
{
    uint8_t wave = 127;
    switch (wave_type) {
    case DREDRUM_SINE:
        if (wave_pointer > 0xff) {
            if (wave_pointer > 0x017f) {
                /* 4. quarter */
                wave_pointer &= 0xff;
                wave_pointer = 0xff - wave_pointer;
                wave = q_sine_table[wave_pointer];
            } else {
                /* 3. quarter */
                wave_pointer &= 0xff;
                wave = q_sine_table[wave_pointer];
            }
            wave = (uint8_t)(0x7f - wave);
        } else {
            if (wave_pointer > 0x7f) {
                /* 2. quarter */
                wave_pointer = 0xff - wave_pointer;
                wave = q_sine_table[wave_pointer];
            } else {
                /* 1. quarter */
                wave = q_sine_table[wave_pointer];
            }
            wave = (uint8_t)(wave + 0x7f);
        }
        break;
    case DREDRUM_SQUARE:
        wave = wave_pointer > 0xff ? 0xff : 0x00;
        break;
    case DREDRUM_SAW:
        wave_pointer >>= 1;
        wave = (uint8_t)(0xff - wave_pointer);
        break;
    case DREDRUM_NOISE:
        /* pgm_read_byte(&q_sine_table[0xff + wave_pointer]): program memory
         * past the table */
        wave = s->noise ? s->noise[wave_pointer & (DREDRUM_NOISE_BYTES - 1)] : 127;
        break;
    }
    return wave;
}

static int16_t render_osc(dredrum_state* s, int n)
{
    dredrum_osc* o = &s->osc[n];
    const uint8_t* p = s->params[n];
    int16_t out = 0;
    uint8_t elapsed;
    uint16_t wave_pointer;

    /* level accumulator - decay (gate firmware: only while the gate is low) */
    if (!s->gate_high)
        o->level_acc = (uint16_t)(o->level_acc + ((0xff - p[DREDRUM_DECAY]) >> 3));
    /* elapsed time runs from 0 to 127 */
    elapsed = (uint8_t)(o->level_acc >> 9);
    if (elapsed >= 127) o->playing = 0; /* end of sound */
    /* pitch accumulator - pitch, pitch drop */
    if (o->ratio_q16 == 0 || o->ratio_q16 == 0x10000) {
        o->phase_acc += (uint32_t)((p[DREDRUM_PITCH] << 4) + 1);
        o->phase_acc += (uint32_t)(((127 - elapsed) * p[DREDRUM_PITCHENV]) >> 4);
    } else {
        /* Maquina: the same step, scaled by the pitch ratio */
        const uint32_t step = (uint32_t)((p[DREDRUM_PITCH] << 4) + 1) +
                              (uint32_t)(((127 - elapsed) * p[DREDRUM_PITCHENV]) >> 4);
        const uint64_t scaled = (uint64_t)step * o->ratio_q16 + o->frac;
        o->phase_acc += (uint32_t)(scaled >> 16);
        o->frac = (uint16_t)scaled;
    }
    wave_pointer = (uint16_t)(o->phase_acc >> 8);
    if (n == 2) {
        /* end of sound after one cycle if click mode is on */
        if (s->click_mode && wave_pointer >= 0x01ff) o->playing = 0;
    } else {
        wave_pointer &= 0x01ff;
    }

    if (o->playing) {
        uint8_t wave;
        uint16_t volume;
        wave_pointer &= 0x01ff;
        wave = dredrum_waverender(s, wave_pointer, p[DREDRUM_WAVE]);
        volume = p[DREDRUM_LEVEL];                      /* 0..255 */
        volume = (uint16_t)(volume * (127 - elapsed));  /* 0..32385 */
        volume >>= 8;                                   /* 0..126 */
        out = (int16_t)wave;                            /* 0..255 */
        out = (int16_t)(out - 128);                     /* -128..127 */
        out = (int16_t)(out * (int16_t)volume);         /* -16128..16002 */
        out = (int16_t)(out >> 7);                      /* -126..125, arithmetic shift as avr-gcc */
    }
    return out;
}

uint8_t dredrum_render_sample(dredrum_state* s)
{
    int16_t mix = 0;
    int n;
    for (n = 0; n < DREDRUM_NUM_OSC; ++n)
        mix = (int16_t)(mix + render_osc(s, n));
    if (mix < -127) mix = -127;
    if (mix > 127) mix = 127;
    mix += 127;
    return (uint8_t)mix;
}
