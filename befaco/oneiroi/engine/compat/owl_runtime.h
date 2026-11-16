// owl_runtime.h - per-instance random state for the OWL library
// (owl_runtime.cpp). Every call into the Oneiroi engine that may draw random
// numbers (construction: the looper's noise fill; processing: noise
// oscillators, the chaos generator) runs inside an OneiroiRng::Scope.
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>

struct OneiroiRng {
  uint32_t state = 33641; // never 0 (xorshift32 would stick)
  void seed(uint32_t s) { state = s ? s : 33641; }

  class Scope {
  public:
    explicit Scope(OneiroiRng& rng);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
  private:
    uint32_t* previous_;
  };
};
