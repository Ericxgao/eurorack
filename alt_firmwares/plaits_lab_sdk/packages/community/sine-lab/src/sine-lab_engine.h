// Copyright 2026 Eric Gao.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_SINE_LAB_ENGINE_H_
#define PLAITS_LAB_SINE_LAB_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// A sine oscillator with one clearly audible axis per control, so that a
// single-control sweep isolates exactly one thing.
//
//   HARMONICS  fades partials 2..4 in, one at a time (0 = bare sine)
//   TIMBRE     phase-modulates the fundamental with itself (0 = bare sine)
//   MORPH      stretches the partial ratios (harmonic -> bell-like)
//   MACRO      mixes in a sine one octave down
//   AUX        the unmodified fundamental, as an A/B reference
class SineLabEngine : public Engine {
 public:
  SineLabEngine() { }
  ~SineLabEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);

 private:
  static const int kNumPartials = 4;

  float phase_[kNumPartials];
  float sub_phase_;

  DISALLOW_COPY_AND_ASSIGN(SineLabEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_SINE_LAB_ENGINE_H_
