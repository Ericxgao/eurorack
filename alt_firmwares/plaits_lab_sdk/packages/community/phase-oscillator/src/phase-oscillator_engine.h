// Copyright 2026 Eric Gao.
// SPDX-License-Identifier: MIT

#ifndef PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_
#define PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_

#include "plaits/dsp/engine/engine.h"

namespace plaits {

// Casio CZ-style phase distortion, built as a chain. A linear ramp passes
// through a drive operator and then a shape operator before it is read, and
// each operator is picked from a bank by crossfading between neighbours, so a
// knob runs continuously through its bank instead of stepping.
//
//   HARMONICS  DRIVE bank  — square, warp, double warp, triple warp.
//              Smooth curves, applied to the ramp first.
//   TIMBRE     SHAPE bank  — saw, pulse, double sine, and the three resonant
//              windows. The CZ waveform set, applied second.
//   MORPH      depth, for the whole chain. At zero both operators are bypassed
//              and this is a sine, whatever the other two say.
//
// Neither bank has an identity entry: bypass is what MORPH is for, and an
// identity operator would only duplicate it while stealing knob travel.
//
// The three resonant shapes are not phase bends at all. They run the reader at
// a multiple of the fundamental and apply a falling amplitude window over each
// cycle — a sawtooth, triangle, or trapezoid — which is the CZ's resonant
// filter sweep. So a shape operator returns a read position AND a window, and
// the plain bends simply leave the window at one.
//
//   MACRO      formant ratio for those three, and nothing else. Neutral at
//              centre through ApplyMacro, because the voice hands an engine
//              0.5 whenever the frequency knob is not locked to this. The
//              engine is complete on the three real knobs; this only refines.
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

  // Several operators are asymmetric in time and leave a large offset, and the
  // windowed ones are one-sided by construction.
  DCBlocker dc_blocker_out_;
  DCBlocker dc_blocker_aux_;

  DISALLOW_COPY_AND_ASSIGN(PhaseOscillatorEngine);
};

}  // namespace plaits

#endif  // PLAITS_LAB_PHASE_OSCILLATOR_ENGINE_H_
