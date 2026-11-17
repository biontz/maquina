# OpenRB-338 (vendored subset)

Source: https://github.com/pingw33n/openrb-338, commit
c100bbe7e2a74a64424b0437bd292bddf435f085 (2026-11-17), GPL-3.0-only (`LICENSE`).
OpenRB-338 is a reimplementation of the ReBirth RB-338 2.01 audio engine, bit-exact
with the original.

## What is here

- `crates/orb-engine/` and `crates/rbs/` (the engine and the song-format crate it
  depends on), unmodified, except:
  - `crates/orb-engine/Cargo.toml`: `[dev-dependencies]` removed (the corpus tests and
    their crates are not vendored);
  - `crates/orb-engine/tests/` and `crates/rbs/tests/` (the corpus integration tests)
    removed.
- `Cargo.toml`: the workspace manifest, trimmed to these two crates.

Not vendored: the UI, the player, the plugin, the CLI, the mod tools, and
`assets/Standard.rbm` (ReBirth's drum samples). Maquina uses only the TB-303 and the
four effects, which need no file of the original: their tables are computed from
constants.

## How Maquina uses it

- `openrb/acida/engine/AcidaDsp.{hpp,cpp}` is a C++ port of `tb303.rs`, `dist.rs`,
  `pcf.rs`, `comp.rs`, `delay.rs` and the table builders of `tables/mod.rs`
  (`PcfPatterns.generated.hpp` holds the PCF patterns of `tables/data.rs`).
- `tools/acida_ref` builds this Rust engine (cargo) and runs scenario scripts on it;
  `tools/acida_test` runs the same scripts on the C++ port and requires the output
  to match bit for bit (`make acida-check`, part of `make test`).

The plugins are unofficial and not affiliated with Reason Studios (ReBirth) or
Roland (TB-303).
