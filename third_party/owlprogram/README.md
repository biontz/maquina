# OwlProgram LibSource (vendored subset)

The parts of Rebel Technology's OWL patch library that the Oneiroi engine
uses (`third_party/oneiroi`): `FloatArray`, `AudioBuffer`, oscillators,
filters, `TapTempo`, the maths helpers and their lookup tables. Unmodified;
only these files are vendored. Source: see `UPSTREAM_COMMIT`.

Maquina replaces the rest of the OWL runtime (`engine/oneiroi/compat`):

- `Patch.h`, `heap.h`: stand-ins without the patch runtime (parameters,
  resources, MIDI, program vector).
- `owl_runtime.cpp`: `AudioBuffer::create`, the fast pow/log tables (these
  same `FastPowTable.h` / `FastLogTable.h`), the random generator per engine
  instance instead of one global seed (`basicmaths.c`'s is renamed away), and
  zero-filled `new`, as the OWL firmware's heap (`pvPortMalloc` clears every
  block; Oneiroi relies on it).

## Licence

GPL-3.0 (`LICENSE`), Rebel Technology / Martin Klang and contributors.
`fastpow.c` (Harrison Ainsworth, BSD-style) and `fastlog.c` (ICSI, GPL)
keep their own notices; both are compatible with GPL-3.0-only.
