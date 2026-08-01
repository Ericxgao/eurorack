// Copyright 2026 Eric Gao.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_
#define PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Casio CZ-style phase distortion, built as a chain rather than a single bend.
// A linear phase ramp passes through two operators in SERIES before it is used
// as a table index, and each operator is chosen from a bank by crossfading
// between neighbours, so the knob runs continuously through the bank instead of
// stepping. Composition is not commutative, which is the point: chaining a
// shape into a drive reaches waveforms neither reaches alone.
//
//   TIMBRE     scans the SHAPE bank   — identity, saw knee, pulse plateau, fold
//   HARMONICS  scans the DRIVE bank   — identity, square, warp, double warp
//   MORPH      depth of the whole chain. At zero the ramp passes through
//              untouched and this is a sine, whatever the other two say.
//   MACRO      blends MAIN from the fundamental reader to the doubled one.
//
//   AUX        the doubled reader alone, an octave up on the same waveform.
//
// The two readers are a quadrature pair, sin and cos of the warped phase. Note
// that blending those directly would be inaudible — same magnitude spectrum,
// only a phase rotation — so the pair is used as a product instead:
// 2*sin*cos is sin of twice the warped phase, which is a different spectrum.
class PhaseOscillatorEngine : public Engine {
 public:
  PhaseOscillatorEngine() { }
  ~PhaseOscillatorEngine() { }
  void Init(stmlib::BufferAllocator* allocator);
  void Reset();
  void LoadUserData(const uint8_t* user_data) { }
  void Render(const EngineParameters& parameters, float* out, float* aux,
      size_t size, bool* already_enveloped);

 private:
  // A one-pole DC blocker, spelled out rather than using stmlib::DCBlocker,
  // which is the same filter: stmlib/dsp/filter.h includes <algorithm>, and the
  // MSVC standard library clang uses on Windows requires C++14 for that header
  // while the SDK compiles engines at -std=c++11. Six lines beats the
  // dependency, and a bare-metal engine has no business pulling in <algorithm>.
  struct DCBlocker {
    void Reset() { x_ = 0.0f; y_ = 0.0f; }

    inline void Process(float* in_out, size_t size, float pole) {
      float x = x_;
      float y = y_;
      while (size--) {
        const float input = *in_out;
        y = y * pole + input - x;
        x = input;
        *in_out++ = y;
      }
      x_ = x;
      y_ = y;
    }

    float x_;
    float y_;
  };

  float phase_;

  // Several operators are asymmetric in time and leave a large offset — over
  // half of full scale at the worst point, against an SDK limit of 0.2.
  DCBlocker dc_blocker_out_;
  DCBlocker dc_blocker_aux_;

  DISALLOW_COPY_AND_ASSIGN(PhaseOscillatorEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_
