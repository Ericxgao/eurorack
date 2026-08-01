// Copyright 2026 Eric Gao.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_
#define PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Casio CZ-style phase distortion. One linear phase ramp feeds two readers, a
// sine and a cosine, and each reader gets its own operator bending the ramp
// before it is used as a table index. The waveform changes while the pitch does
// not, because nothing here alters how fast the ramp runs.
//
//   TIMBRE     bends the SINE path's ramp: a knee that runs the first part of
//              the cycle fast and the rest slow. The CZ "saw" bend.
//   MORPH      bends the COSINE path's ramp: a plateau held mid-cycle, which
//              holds the reader still and flattens the wave. The CZ "pulse".
//   HARMONICS  depth for both — how much of each bend is actually applied.
//              At zero the operators are bypassed and this is a sine and a
//              cosine, whatever TIMBRE and MORPH say.
//   MACRO      crossfades MAIN from the sine path to the cosine path.
//
//   AUX        the two paths ring-modulated together.
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

  // Both operators are asymmetric in time, so both leave a large DC offset —
  // measured at -0.58 on MAIN and +0.45 on AUX without this, against an SDK
  // limit of 0.2. Not a corner case either: the sine path on its own sits at
  // -0.39 with the knee fully bent.
  DCBlocker dc_blocker_out_;
  DCBlocker dc_blocker_aux_;

  DISALLOW_COPY_AND_ASSIGN(PhaseOscillatorEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_
