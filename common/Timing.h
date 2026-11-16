// Timing.h - constants shared by the engine host and the test tools.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Engine frames between a host note-on and the engine block it is applied to:
// the resampler's maximum lookahead (64-tap half width + one 64-frame block)
// plus margin, so a note is never late. Tests that align notes to engine
// blocks (lv2_render --aligned, render_ref --plugin-ref) depend on it.
#define MAQUINA_EVENT_DELAY 128
