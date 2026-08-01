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

// DC blocker pole, in the same form stmlib's string model uses. 20/48000 puts
// the corner near 3 Hz — far below the lowest playable fundamental, so it takes
// the offset out without touching the tone.
const float kDCBlockerPole = 1.0f - 20.0f / kSampleRate;

// Output scaling, set from the measured worst case across the parameter space
// rather than by eye. Blocking DC on a wave this asymmetric RAISES its peak —
// the offset it removes was holding one side down.
// Measured over 324 points at full depth: MAIN peaks at 0.90 and AUX at 1.26 of
// the pre-scaling signal. AUX is the hotter of the two because doubling squares
// the reader, so its peaks line up where MAIN's cancel.
const float kOutScale = 0.72f;
const float kAuxScale = 0.75f;

// Depth taper. Corners in a phase operator have a harmonic tail that falls only
// as 1/n^2, which reaches far past Nyquist once the fundamental is high, so the
// depth has to shrink faster than the available room does — hence 1/f SQUARED,
// not 1/f. Chaining two operators sharpens the corners further, so this is a
// touch tighter than a single bend needed.
const float kFullDepthHz = 500.0f;
const float kFullDepthIncrement = kFullDepthHz / kSampleRate;

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Abs(float a) { return a < 0.0f ? -a : a; }

inline float WrapPhase(float phase) {
  return phase - static_cast<float>(static_cast<int>(phase));
}

// Every operator maps [0, 1] onto itself with f(0) = 0 and f(1) = 0 or 1, so
// the reader lands where it started when the ramp wraps. Break that and the
// wrap becomes a step, which is a click at the fundamental and a 1/n spray of
// harmonics on top of it.
//
// They are branchless where they can be: an `if` on a float needs a compare
// plus a flag transfer to the core and stalls the pipeline, while min/max/abs
// are single instructions.

// ---- SHAPE bank (TIMBRE) -- corners, the CZ character -----------------------

float ShapeIdentity(float phase) {
  return phase;
}

// Two straight segments meeting at a knee: the first part of the cycle is read
// fast and the rest slow. The CZ saw.
float ShapeSawKnee(float phase) {
  const float knee = 0.12f;
  return 0.5f * Min(phase * (1.0f / knee), 1.0f) +
      0.5f * Max(phase - knee, 0.0f) * (1.0f / (1.0f - knee));
}

// Ramp, hold, ramp. The reader stops mid-cycle and the waveform goes level.
// The CZ pulse.
float ShapePulse(float phase) {
  const float edge = 0.25f;            // plateau is the remaining half cycle
  const float plateau = 0.5f;
  return 0.5f * Min(phase * (1.0f / edge), 1.0f) +
      0.5f * Max(phase - edge - plateau, 0.0f) * (1.0f / edge);
}

// Out and back: the reader sweeps the whole table forward then reverses. Ends
// where it began, so the wrap stays continuous, and the retrace makes the
// spectrum even-harmonic — an octave-up flavour without changing the rate.
float ShapeFold(float phase) {
  return 1.0f - Abs(2.0f * phase - 1.0f);
}

// ---- DRIVE bank (HARMONICS) -- smooth curves, applied after a shape ---------

float DriveIdentity(float phase) {
  return phase;
}

// Skews the read toward the end of the cycle. Smooth, so it tilts the spectrum
// rather than adding a corner.
float DriveSquare(float phase) {
  return phase * phase;
}

// One sine of warp per cycle. Deliberately deep enough to be non-monotonic —
// the reader briefly runs backwards, which is what fills in the harmonics.
float DriveWarp(float phase) {
  return phase + 0.25f * Sine(phase);
}

// Two per cycle, which puts a pair of bumps in the read and lands a formant
// rather than a tilt.
float DriveDoubleWarp(float phase) {
  return phase + 0.15f * Sine(phase * 2.0f);
}

typedef float (*PhaseOperator)(float);

const PhaseOperator kShapeBank[] = {
  ShapeIdentity, ShapeSawKnee, ShapePulse, ShapeFold,
};
const PhaseOperator kDriveBank[] = {
  DriveIdentity, DriveSquare, DriveWarp, DriveDoubleWarp,
};
const int kBankSize = 4;

// Turn a 0..1 knob into the two neighbouring operators and the crossfade
// between them, so the bank is continuous rather than switched. Resolved once
// per block: the sample loop then calls two pointers and never branches.
void SelectPair(const PhaseOperator* bank, float position,
    PhaseOperator* lower, PhaseOperator* upper, float* mix) {
  float scaled = position * static_cast<float>(kBankSize - 1);
  CONSTRAIN(scaled, 0.0f, static_cast<float>(kBankSize - 1));
  int index = static_cast<int>(scaled);
  if (index > kBankSize - 2) {
    index = kBankSize - 2;
  }
  *lower = bank[index];
  *upper = bank[index + 1];
  *mix = scaled - static_cast<float>(index);
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

  // Bank selection and the depth taper are per-block. At 48 kHz a block is 12
  // samples, so the division below costs a twelfth of a division per sample.
  const float f0 = NoteToFrequency(parameters.note);

  const float headroom = kFullDepthIncrement / f0;
  float depth_ceiling = headroom * headroom;
  CONSTRAIN(depth_ceiling, 0.0f, 1.0f);
  const float depth = parameters.morph * depth_ceiling;

  PhaseOperator shape_lower, shape_upper, drive_lower, drive_upper;
  float shape_mix, drive_mix;
  SelectPair(kShapeBank, parameters.timbre,
      &shape_lower, &shape_upper, &shape_mix);
  SelectPair(kDriveBank, parameters.harmonics,
      &drive_lower, &drive_upper, &drive_mix);

  const float blend = parameters.macro;

  for (size_t i = 0; i < size; ++i) {
    phase_ = WrapPhase(phase_ + f0);
    const float ramp = phase_;

    // Stage one, then stage two on its output — in series, not in parallel.
    const float shaped_low = shape_lower(ramp);
    const float shaped = shaped_low + shape_mix * (shape_upper(ramp) - shaped_low);

    const float driven_low = drive_lower(shaped);
    const float driven =
        driven_low + drive_mix * (drive_upper(shaped) - driven_low);

    // Depth crossfades the whole chain against the untouched ramp, so MORPH at
    // zero bypasses both operators rather than merely setting them to neutral.
    const float theta = ramp + depth * (driven - ramp);

    const float sine = Sine(theta);
    const float cosine = Sine(theta + kQuarterCycle);
    const float doubled = 2.0f * sine * cosine;   // = sin(2*theta)

    out[i] = kOutScale * (sine + blend * (doubled - sine));
    aux[i] = kAuxScale * doubled;
  }

  dc_blocker_out_.Process(out, size, kDCBlockerPole);
  dc_blocker_aux_.Process(aux, size, kDCBlockerPole);
}

}  // namespace plaits
