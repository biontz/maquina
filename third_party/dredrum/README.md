# Dredrum firmware (vendored)

Unmodified copies of the Dredrum Eurorack module firmware, an "8-bit
3 oscillator percussion/drone PWM generator" for the ATmega48P at 20 MHz.

- `trig/` - v1.0, triggered (each trigger restarts the decay)
- `gate/` - v1.0 GATE (the decay is held while the gate is high)

Copyright 2014 Jan Cumpelik. Licensed under the GNU General Public License,
version 3 or (at your option) any later version, as stated in each source
file. Maquina is GPL-3.0-only, which that licence allows.

Source: see `UPSTREAM_COMMIT`.

Maquina Dredrum ports the sound engine of `trig/dredrum.c` and
`gate/dredrum-gate.c` to portable C (`engine/dredrum/`); the files here are
the reference. The noise waveform reads the firmware's own program memory,
so `tools/dredrum_flash/` compiles these sources with avr-gcc to extract
those bytes. "Dredrum" is used only to name the origin; Maquina is not
affiliated with the original authors.
