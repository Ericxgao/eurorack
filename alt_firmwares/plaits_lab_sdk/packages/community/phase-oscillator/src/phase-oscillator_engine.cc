// Copyright 2026 Eric Gao.
// SPDX-License-Identifier: MIT

#include "phase-oscillator_engine.h"

// The firmware is bare-metal — it can't link libm (std::sin/exp/log/pow), so use
// the shared LUTs: plaits::Sine here, stmlib::SemitonesToRatio for 2^x.
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

namespace {

// A quarter cycle of lead turns the sine LUT into a cosine reader.
const float kQuarterCycle = 0.25f;

// How far TIMBRE may pull the knee in from the neutral half-cycle. Stopping
// short of the ramp's start keeps the reciprocal below finite bounds; at 0.02
// the first segment already runs 25x faster than the second.
const float kMinKnee = 0.02f;
const float kNeutralKnee = 0.5f;

// How much of the cycle MORPH may hold flat. A full cycle of plateau would be
// a constant, so the two ramps either side keep a tenth of the cycle each.
const float kMaxPlateau = 0.9f;

// Both operators are piecewise-linear, so the output has corners, and a corner
// is a derivative discontinuity whose harmonics fall off only as 1/n^2. That is
// the CZ sound, and it is also aliasing waiting to happen: the depth has to be
// walked back as the fundamental rises. Below this the full depth is safe.
const float kFullDepthHz = 550.0f;
const float kFullDepthIncrement = kFullDepthHz / kSampleRate;

// Output scaling, set from the measured worst case rather than by eye. Removing
// DC from a wave this asymmetric RAISES its peak excursion — the offset it
// subtracts was holding one side down — so MAIN reached 1.52 before the
// manifest's 0.8 gain and clipped. These keep the worst point in the parameter
// space at about 0.95 after that gain.
const float kOutScale = 0.62f;
const float kAuxScale = 0.85f;

// DC blocker pole, in the same form stmlib's string model uses. 20/48000 puts
// the corner near 3 Hz — far below the lowest playable fundamental, so it takes
// the offset out without touching the tone.
const float kDCBlockerPole = 1.0f - 20.0f / kSampleRate;

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }

inline float WrapPhase(float phase) {
  return phase - static_cast<float>(static_cast<int>(phase));
}

// Two straight segments meeting at `knee`, mapping [0, 1] onto [0, 1] with the
// halfway point pulled to the knee. Branchless via min/max, which are single
// instructions here — an `if` on a float costs a compare plus a flag transfer
// and stalls the pipeline.
inline float BendKnee(float phase, float knee, float inverse_knee,
    float inverse_rest) {
  return 0.5f * Min(phase * inverse_knee, 1.0f) +
      0.5f * Max(phase - knee, 0.0f) * inverse_rest;
}

// Ramp, hold, ramp. `edge` is the width of each ramp, `plateau` the flat middle
// where the reader stops and the waveform goes level.
inline float BendPlateau(float phase, float edge, float inverse_edge,
    float plateau) {
  return 0.5f * Min(phase * inverse_edge, 1.0f) +
      0.5f * Max(phase - edge - plateau, 0.0f) * inverse_edge;
}

}  // namespace

void PhaseOscillatorEngine::Init(stmlib::BufferAllocator* allocator) {
  Reset();
}

void PhaseOscillatorEngine::Reset() {
  phase_ = 0.0f;
  dc_blocker_out_.Reset();
  dc_blocker_aux_.Reset();
}

void PhaseOscillatorEngine::Render(const EngineParameters& parameters,
    float* out, float* aux, size_t size, bool* already_enveloped) {
  *already_enveloped = false;

  // All of the shape maths is per-block. At 48 kHz a block is 12 samples, so
  // the four divisions below cost a third of a division per sample.
  const float f0 = NoteToFrequency(parameters.note);

  // Taper the depth as the pitch rises, because a corner of fixed sharpness
  // puts out a fixed NUMBER of harmonics: hold the depth while the fundamental
  // doubles and the highest harmonic doubles with it, straight past Nyquist.
  //
  // The taper is 1/f SQUARED, which is steeper than the room shrinks. A plain
  // 1/f was tried and left 11th-to-14th harmonics folding back at -35 dB by C7;
  // the corners here are piecewise-linear, so their tail only falls as 1/n^2
  // and reaches far enough that the taper has to outrun it. Squared keeps full
  // depth to about C5 and fades the bends out above that, which is the honest
  // trade: phase distortion has no bandlimited form at this cost.
  const float headroom = kFullDepthIncrement / f0;
  float depth_ceiling = headroom * headroom;
  CONSTRAIN(depth_ceiling, 0.0f, 1.0f);
  const float depth = parameters.harmonics * depth_ceiling;

  // TIMBRE: knee position for the sine path.
  const float knee = kNeutralKnee - parameters.timbre * (kNeutralKnee - kMinKnee);
  const float inverse_knee = 1.0f / knee;
  const float inverse_rest = 1.0f / (1.0f - knee);

  // MORPH: plateau width for the cosine path.
  const float plateau = parameters.morph * kMaxPlateau;
  const float edge = (1.0f - plateau) * 0.5f;
  const float inverse_edge = 1.0f / edge;

  const float blend = parameters.macro;

  for (size_t i = 0; i < size; ++i) {
    phase_ = WrapPhase(phase_ + f0);

    // Each operator is applied as a crossfade from the untouched ramp, so
    // HARMONICS is a genuine depth: at zero the bend is bypassed entirely
    // rather than merely parameterised to something neutral.
    const float bent_sine = BendKnee(phase_, knee, inverse_knee, inverse_rest);
    const float bent_cosine = BendPlateau(phase_, edge, inverse_edge, plateau);

    const float sine = Sine(phase_ + depth * (bent_sine - phase_));
    const float cosine = Sine(
        phase_ + depth * (bent_cosine - phase_) + kQuarterCycle);

    out[i] = kOutScale * (sine + blend * (cosine - sine));
    aux[i] = kAuxScale * sine * cosine;
  }

  dc_blocker_out_.Process(out, size, kDCBlockerPole);
  dc_blocker_aux_.Process(aux, size, kDCBlockerPole);
}

}  // namespace plaits
