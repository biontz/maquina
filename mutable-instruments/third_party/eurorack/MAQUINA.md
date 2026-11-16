# Vendored Mutable Instruments code

Taken unmodified from Émilie Gillet's open-source Eurorack firmware, for
Maquina Klang (Plaits) and Maquina Klong / Klong FX (Rings):

- github.com/pichenettes/eurorack @ f4cfa7697095a28d292bff758beb51deb49b5a86
  - `plaits/dsp/`, `plaits/resources.{cc,h}`, `plaits/user_data.h` (Plaits firmware 1.2, 24 models)
  - `rings/dsp/`, `rings/resources.{cc,h}`
  - `README.md` (licensing section)
- github.com/pichenettes/stmlib @ d18def816c51d1da0c108236928b2bbd25c17481
  - `stmlib.h`, `dsp/*.h` + `units.cc` + `atan.cc`, `utils/*.h` + `random.cc`, `LICENSE`

Licence: the STM32 projects' code (Plaits, Rings, stmlib) is MIT (see
`stmlib/LICENSE` and the header of every file). Copyright Émilie Gillet.

The code is compiled with `-DTEST`. That selects stmlib's portable C++ paths
instead of Arm inline assembly, and a `UserData` without flash, so the 6-op
FM models use their built-in DX7 banks. See `../../eurorack.mk`.

"Mutable Instruments" is a registered trademark. As the README asks,
Maquina's plugins use neither that name nor the module names. Maquina is not
affiliated with Mutable Instruments.
