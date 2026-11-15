# Noise Plethora, VCV Rack port (vendored)

The Befaco Noise Plethora Eurorack module's sound engine, as ported to VCV
Rack in the Befaco plugin for VCV. The module itself runs on a Teensy 4.0 with
the Teensy Audio library.

What's here:

- **`src/noise-plethora/`**: the port's algorithms (`plugins/P_*.hpp`, banks
  in `Banks_Def.hpp`) and its re-implementation of the Teensy Audio parts it
  needs (`teensy/`). Unmodified.
- **`reference/`**: the port's VCV module, `NoisePlethora.cpp` and its
  `plugin.hpp`. Unmodified. Not compiled; Maquina's `plugins/common/PlethoraHost.cpp`
  reproduces its signal chain (filters, saturators, DC blockers) and
  `engine/plethora/plethora_dsp.hpp` copies the pieces it needs without changing the arithmetic.
- **`src/plugin.hpp`**: not the upstream file. It is a Maquina shim:
  `teensy/` includes `../../plugin.hpp`, and the VCV Rack names it needs come
  from `engine/plethora/compat/rack.hpp`.

Source: see `UPSTREAM_COMMIT`.

## Licences

- **Port and algorithms:** © 2019-2021 VCV and contributors, GPL-3.0-or-later
  (`LICENSE.md`). The algorithms are derived from Befaco's Noise Plethora
  firmware (github.com/Befaco/Noise_plethora), whose algorithm files are
  GPL-3.0-or-later (`src/noise-plethora/LICENSE.md`). Maquina is GPL-3.0-only,
  which that licence allows.
- **`src/noise-plethora/teensy/`:** modified Teensy Audio library code
  (Paul Stoffregen and others), MIT, as stated in each file.
- **Not used:**
  - Befaco's own firmware framework (CC BY-NC-SA).
  - The VCV port's panel graphics (`res/`, Befaco's).

"Befaco" and "Noise Plethora" are used only to name the origin. Maquina is
not affiliated with Befaco or VCV.
