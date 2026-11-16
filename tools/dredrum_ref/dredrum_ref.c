// SPDX-License-Identifier: GPL-3.0-only
// dredrum_ref - bit-exact check of euro-modules/dredrum/engine/ against the real firmware.
//
// Runs the Dredrum firmware (build/dredrum/{trig,gate}.bin, the flash image
// the module is programmed with, made by tools/dredrum_flash/gen_dredrum_flash.py;
// symbols from the .elf via avr-nm) on simavr's ATmega48P at 20 MHz:
// voice settings go into the EEPROM the firmware loads at boot (oscillator
// 1's four pots also through the ADC, which the firmware reads from the
// start), CLICK is pin PC0, the trigger input PD0 (low = trigger, behind the
// input transistor).
//
// What is compared: every byte the main loop computes into its audio ring
// buffer (buffer.data[buffer.tail]), in order, and the exact computed-sample
// index at which the main loop acts on a trigger / gate change (writes to the
// `trigger` variable). The same events are replayed into dredrum_core at those
// indices and both byte streams must be identical. (The PWM output is the same
// stream ~16 samples later; its timing jitters with the buffer fill, the
// bytes do not.)
//
//   dredrum_ref <build/dredrum dir>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <simavr/avr_adc.h>
#include <simavr/avr_eeprom.h>
#include <simavr/avr_ioport.h>
#include <simavr/sim_avr.h>

#include "dredrum_core.h"

#define CYCLES_PER_SAMPLE (8 * 131) // timer 0: prescaler 8, OCR0A 130
#define MAX_SAMPLES 200000
#define MAX_EVENTS 64

typedef struct {
    const char* name;
    int gate;          // gate firmware
    uint8_t params[3][5];
    int click;
    int seconds_x10;   // run length
    // trigger input: low from on[i] for len[i] samples (ISR samples)
    int ntrig;
    int on[8], len[8];
} Scenario;

// params rows: { LEVEL, DECAY, PITCHENV, PITCH, WAVE }
static const Scenario kScenarios[] = {
    { "sine kick", 0, { { 255, 190, 110, 20, 0 }, { 0, 0, 0, 128, 3 }, { 0, 0, 0, 160, 1 } }, 0, 12,
      2, { 2000, 12000 }, { 4, 4 } },
    { "all waves", 0, { { 200, 120, 60, 40, 1 }, { 180, 60, 0, 200, 2 }, { 160, 30, 200, 90, 3 } }, 0, 12,
      3, { 1000, 6000, 15000 }, { 3, 5, 3 } },
    { "noise + click", 0, { { 120, 0, 0, 255, 3 }, { 255, 230, 255, 3, 0 }, { 255, 10, 0, 180, 1 } }, 1, 12,
      2, { 1500, 9000 }, { 4, 4 } },
    // osc 3 phase step 241 < 256: the click's end test (pointer >= 0x1ff) sees 0x1ff exactly
    { "click boundary", 0, { { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 255, 200, 0, 15, 1 } }, 1, 6,
      3, { 500, 3000, 7000 }, { 3, 3, 3 } },
    { "drone retrigger", 0, { { 100, 250, 30, 60, 2 }, { 90, 255, 0, 61, 0 }, { 80, 248, 0, 0, 3 } }, 0, 10,
      3, { 1000, 1300, 9000 }, { 3, 3, 3 } },
    { "gate hold", 1, { { 255, 200, 90, 25, 0 }, { 150, 100, 0, 128, 3 }, { 200, 20, 0, 160, 1 } }, 0, 14,
      2, { 2000, 14000 }, { 6000, 2000 } },
    { "gate noise click", 1, { { 140, 40, 40, 200, 3 }, { 255, 180, 255, 10, 2 }, { 255, 0, 0, 220, 1 } }, 1, 12,
      3, { 1000, 8000, 12000 }, { 3000, 100, 4000 } },
};

static uint32_t sym_addr(const char* dir, const char* fw, const char* sym)
{
    char cmd[512];
    snprintf(cmd, sizeof cmd, "avr-nm %s/%s.elf", dir, fw);
    FILE* p = popen(cmd, "r");
    if (!p) return 0;
    char line[256];
    uint32_t addr = 0;
    while (fgets(line, sizeof line, p)) {
        unsigned a;
        char t, name[128];
        if (sscanf(line, "%x %c %127s", &a, &t, name) == 3 && !strcmp(name, sym)) addr = a;
    }
    pclose(p);
    return addr & 0xffff; // data space: 0x800000 + address
}

static uint8_t g_hw[MAX_SAMPLES], g_port[MAX_SAMPLES];
typedef struct { int index; int kind; } Ev; // kind 1 trigger, 2 gate off

static int run(const char* dir, const Scenario* sc)
{
    const char* fw = sc->gate ? "gate" : "trig";
    char path[512];
    // flash image (.text + .data, as uploaded); erased flash reads 0xff
    static uint8_t flash[4096];
    snprintf(path, sizeof path, "%s/%s.bin", dir, fw);
    memset(flash, 0xff, sizeof flash);
    FILE* bf = fopen(path, "rb");
    if (!bf) { printf("%s: cannot read %s\n", sc->name, path); return 1; }
    const size_t flash_size = fread(flash, 1, sizeof flash, bf);
    fclose(bf);
    const uint32_t a_buffer = sym_addr(dir, fw, "buffer"), a_trigger = sym_addr(dir, fw, "trigger");
    if (!a_buffer || !a_trigger) { printf("%s: symbols buffer/trigger not found\n", sc->name); return 1; }
    // bufferstructure: data[16], head, tail, fill
    const uint32_t a_tail = a_buffer + 17;

    avr_t* avr = avr_make_mcu_by_name("atmega48p");
    avr_init(avr);
    avr->frequency = 20000000;
    avr->vcc = avr->avcc = avr->aref = 5000;
    avr_loadcode(avr, flash, (uint32_t)flash_size, 0);

    // EEPROM: voice_params[3][5] at 10
    uint8_t ee[15];
    memcpy(ee, sc->params, 15);
    avr_eeprom_desc_t d = { .ee = ee, .offset = 10, .size = 15 };
    avr_ioctl(avr, AVR_IOCTL_EEPROM_SET, &d);
    // pots: ADC2..5 -> oscillator 1 LEVEL, DECAY, PITCHENV, PITCH (ADCH = 8 MSBs, AVcc ref)
    for (int k = 0; k < 4; ++k) {
        const uint32_t mv = ((uint32_t)sc->params[0][k] * 4u + 2u) * 5000u / 1024u;
        avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_ADC_GETIRQ, ADC_IRQ_ADC0 + 2 + k), mv);
    }
    // buttons released (pull-ups), CLICK switch, trigger input idle (high)
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 2), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 4), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 5), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 1), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0), sc->click ? 0 : 1);
    avr_irq_t* trig_pin = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 0);
    avr_raise_irq(trig_pin, 1);

    const uint64_t end = (uint64_t)sc->seconds_x10 * 2000000u;
    int n = 0, nev = 0, pending = -1, pin_low = 0;
    Ev ev[MAX_EVENTS];
    uint8_t last_tail = avr->data[a_tail], last_trig = avr->data[a_trigger];
    while (avr->cycle < end && n < MAX_SAMPLES) {
        // trigger input: low during [on, on + len) ISR samples
        const int64_t s = (int64_t)(avr->cycle / CYCLES_PER_SAMPLE);
        int low = 0;
        for (int i = 0; i < sc->ntrig; ++i)
            if (s >= sc->on[i] && s < sc->on[i] + sc->len[i]) low = 1;
        if (low != pin_low) { avr_raise_irq(trig_pin, low ? 0 : 1); pin_low = low; }

        const int st = avr_run(avr);
        if (st == cpu_Done || st == cpu_Crashed) { printf("%s: cpu stopped (%d)\n", sc->name, st); return 1; }

        const uint8_t tail = avr->data[a_tail];
        // `++tail; if (tail >= 16) tail = 0;` on a volatile: 16 is stored for a
        // moment before the wrap; it is not a slot
        if (tail != last_tail && tail < 16) {
            // ++tail; data[tail] = sample: the previous slot is final now
            if (pending >= 0) g_hw[n++] = avr->data[a_buffer + pending];
            pending = tail;
            last_tail = tail;
        }
        const uint8_t tr = avr->data[a_trigger];
        if (tr != last_trig) {
            // main loop: trig fw 1 -> 0; gate fw 1 (TRIG_ON) -> 3 (GATE_ON), 2 (TRIG_OFF) -> 0 (GATE_OFF).
            // The sample being computed when it acts is the first to see it.
            const int at = n + (pending >= 0 ? 1 : 0);
            int kind = 0;
            if (!sc->gate && last_trig == 1 && tr == 0) kind = 1;
            if (sc->gate && last_trig == 1 && tr == 3) kind = 1;
            if (sc->gate && last_trig == 2 && tr == 0) kind = 2;
            if (kind && nev < MAX_EVENTS) { ev[nev].index = at; ev[nev].kind = kind; ++nev; }
            last_trig = tr;
        }
    }
    avr_terminate(avr);

    // the same through the port (noise bytes from the same build)
    const uint32_t table = sym_addr(dir, fw, "q_sine_table");
    dredrum_state ds;
    memset(&ds, 0, sizeof ds);
    memcpy(ds.params, sc->params, 15);
    ds.click_mode = (uint8_t)sc->click;
    ds.gate_mode = (uint8_t)sc->gate;
    ds.noise = &flash[table + 0xff];
    dredrum_reset(&ds);
    int e = 0;
    for (int i = 0; i < n; ++i) {
        while (e < nev && ev[e].index == i) {
            if (ev[e].kind == 1) dredrum_trigger(&ds);
            else dredrum_gate_off(&ds);
            ++e;
        }
        g_port[i] = dredrum_render_sample(&ds);
    }

    int first = -1, diffs = 0, sounding = 0;
    for (int i = 0; i < n; ++i) {
        if (g_hw[i] != g_port[i]) { if (first < 0) first = i; ++diffs; }
        if (g_hw[i] != 127) ++sounding;
    }
    if (nev < sc->ntrig || sounding < n / 20) {
        printf("  %-18s %s: %d samples, %d events, %d sounding  FAIL (stimulus did not reach the firmware)\n",
               sc->name, fw, n, nev, sounding);
        return 1;
    }
    if (diffs) {
        printf("  %-18s %s: %d samples, %d differ, first at %d (fw %u, port %u)  FAIL\n", sc->name, fw, n, diffs,
               first, g_hw[first], g_port[first]);
        return 1;
    }
    printf("  %-18s %s: %d samples identical, %d events  ok\n", sc->name, fw, n, nev);
    return 0;
}

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : "build/dredrum";
    int failures = 0;
    for (size_t i = 0; i < sizeof kScenarios / sizeof kScenarios[0]; ++i) failures += run(dir, &kScenarios[i]);
    printf("dredrum_ref: %s\n", failures ? "FAILED" : "bit-exact");
    return failures ? 1 : 0;
}
