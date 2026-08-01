// Copyright 2026 Eric Gao.
// SPDX-License-Identifier: MIT

#include "phase-oscillator_engine.h"

// The firmware is bare-metal — it can't link libm (std::sin/exp/log/pow), so use
// the shared LUTs: plaits::Sine here, stmlib::SemitonesToRatio for 2^x.
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

namespace {

// DC blocker pole, in the same form stmlib's string model uses. 20/48000 puts
// the corner near 3 Hz — far below the lowest playable fundamental.
const float kDCBlockerPole = 1.0f - 20.0f / kSampleRate;

// Output scaling, from the measured worst case across the parameter space.
// Blocking DC on a wave this asymmetric RAISES its peak: the offset it removes
// was holding one side down.
// Measured over 150 points at full depth: MAIN peaks at 1.20 and AUX at 1.00 of
// the pre-scaling signal.
const float kOutScale = 0.67f;
const float kAuxScale = 0.80f;

// Depth taper. Corners have a harmonic tail falling only as 1/n^2, which
// reaches past Nyquist once the fundamental is high, so depth has to shrink
// faster than the room does — 1/f squared, not 1/f.
const float kFullDepthHz = 500.0f;
const float kFullDepthIncrement = kFullDepthHz / kSampleRate;

// Formant ratio for the resonant shapes: the multiple of the fundamental the
// reader runs at. Neutral at MACRO centre.
const float kStockRatio = 5.0f;
const float kMinRatio = 1.5f;
const float kMaxRatio = 16.0f;

// The formant is a real tone at ratio * f0, so it has to stay well under
// Nyquist on its own account — the window puts sidebands either side of it.
const float kMaxFormantIncrement = 0.22f;

inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Abs(float a) { return a < 0.0f ? -a : a; }

inline float WrapPhase(float phase) {
  return phase - static_cast<float>(static_cast<int>(phase));
}

// ---- DRIVE bank (HARMONICS) -------------------------------------------------
//
// Smooth curves on the ramp, applied before the shape. Each maps [0, 1] onto
// itself with f(0) = 0 and f(1) = 1, so the ramp still wraps cleanly. No
// identity entry: MORPH at zero is the bypass.

typedef float (*DriveOperator)(float);

// Skews the read toward the end of the cycle. Tilts the spectrum rather than
// adding a corner.
float DriveSquare(float phase) {
  return phase * phase;
}

// One sine of warp per cycle, deep enough to be non-monotonic — the reader
// briefly runs backwards, which is what fills the harmonics in.
float DriveWarp(float phase) {
  return phase + 0.25f * Sine(phase);
}

// Two and three per cycle: pairs and triples of bumps in the read, which land
// as formants rather than a tilt.
float DriveDoubleWarp(float phase) {
  return phase + 0.15f * Sine(phase * 2.0f);
}

float DriveTripleWarp(float phase) {
  return phase + 0.10f * Sine(phase * 3.0f);
}

// ---- SHAPE bank (TIMBRE) ----------------------------------------------------
//
// The CZ waveform set. A shape returns a read position and an amplitude window,
// both at FULL strength — depth is not applied here.
//
// It cannot be. Depth used to crossfade the read against the straight ramp, and
// that quietly requires every shape to end its cycle at exactly 1, or the
// interpolated read lands mid-cycle when the ramp wraps and the waveform steps.
// The bends end at 1 and were fine; the fold ends at 0 and the formants at
// `ratio`, so both tore. The window masks it only at full depth, which the
// pitch taper never allows. Measured at -27 dB of fold-back before this moved
// into the audio domain, where each branch is continuous on its own and the
// crossfade between two continuous signals cannot introduce a step.

typedef void (*ShapeOperator)(float phase, float ratio,
    float* read, float* window);

// Two straight segments meeting at a knee: the cycle's first part is read fast
// and the rest slow. The CZ saw.
void ShapeSaw(float phase, float ratio, float* read, float* window) {
  const float knee = 0.12f;
  *read = 0.5f * Min(phase * (1.0f / knee), 1.0f) +
      0.5f * Max(phase - knee, 0.0f) * (1.0f / (1.0f - knee));
  *window = 1.0f;
}

// Ramp, hold, ramp. The reader stops mid-cycle and the wave goes level.
void ShapePulse(float phase, float ratio, float* read, float* window) {
  const float edge = 0.25f;
  const float plateau = 0.5f;
  *read = 0.5f * Min(phase * (1.0f / edge), 1.0f) +
      0.5f * Max(phase - edge - plateau, 0.0f) * (1.0f / edge);
  *window = 1.0f;
}

// Out and back: the reader sweeps the table forward then reverses, ending where
// it began. The retrace makes the spectrum even-harmonic.
void ShapeDoubleSine(float phase, float ratio, float* read, float* window) {
  *read = 1.0f - Abs(2.0f * phase - 1.0f);
  *window = 1.0f;
}

// The three resonant shapes: the reader runs at `ratio` times the fundamental
// and a window falls across each cycle, which is the CZ's filter-sweep sound.
// All three windows reach zero at the end of the cycle, so the reader being
// mid-cycle there is multiplied away and the wave stays continuous.
//
// Reading a SINE rather than the CZ's cosine matters for the same reason at the
// START of the cycle: sin(0) is zero, so the window's reset lands on a zero
// crossing instead of a step.

// Falling ramp — the brightest of the three.
void ShapeResoSaw(float phase, float ratio, float* read, float* window) {
  *read = phase * ratio;
  *window = 1.0f - phase;
}

// Up and back down. Softer, and symmetric about mid-cycle.
void ShapeResoTriangle(float phase, float ratio, float* read, float* window) {
  *read = phase * ratio;
  *window = 1.0f - Abs(2.0f * phase - 1.0f);
}

// Flat for the first half, then falling. Holds the formant at full level
// longer, so it reads as the most vocal of the three.
void ShapeResoTrapezoid(float phase, float ratio, float* read, float* window) {
  *read = phase * ratio;
  *window = Min(2.0f * (1.0f - phase), 1.0f);
}

const DriveOperator kDriveBank[] = {
  DriveSquare, DriveWarp, DriveDoubleWarp, DriveTripleWarp,
};
const int kDriveBankSize = 4;

// Bends first, then the resonant windows, so the one crossfade that spans the
// two kinds happens once, between double sine and resonant saw.
const ShapeOperator kShapeBank[] = {
  ShapeSaw, ShapePulse, ShapeDoubleSine,
  ShapeResoSaw, ShapeResoTriangle, ShapeResoTrapezoid,
};
const int kShapeBankSize = 6;

// Turn a 0..1 knob into an index and the crossfade to its neighbour. Resolved
// once per block; the sample loop then calls two pointers and never branches.
int SelectIndex(int count, float position, float* mix) {
  float scaled = position * static_cast<float>(count - 1);
  CONSTRAIN(scaled, 0.0f, static_cast<float>(count - 1));
  int index = static_cast<int>(scaled);
  if (index > count - 2) {
    index = count - 2;
  }
  *mix = scaled - static_cast<float>(index);
  return index;
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

  // Bank selection, the depth taper and the ratio are all per-block.
  const float f0 = NoteToFrequency(parameters.note);

  const float headroom = kFullDepthIncrement / f0;
  float depth_ceiling = headroom * headroom;
  CONSTRAIN(depth_ceiling, 0.0f, 1.0f);
  const float depth = parameters.morph * depth_ceiling;

  // Neutral at MACRO centre, since the voice hands 0.5 to any engine whose
  // frequency knob is not locked to the fourth macro.
  float ratio = ApplyMacro(kStockRatio, kMinRatio, kMaxRatio, parameters.macro);
  // The formant is a real tone at ratio * f0; keep it and its sidebands down
  // where they belong instead of folding off Nyquist.
  ratio = Min(ratio, kMaxFormantIncrement / f0);
  ratio = Max(ratio, 1.0f);

  float drive_mix, shape_mix;
  const int drive_index =
      SelectIndex(kDriveBankSize, parameters.harmonics, &drive_mix);
  const int shape_index =
      SelectIndex(kShapeBankSize, parameters.timbre, &shape_mix);
  const DriveOperator drive_lower = kDriveBank[drive_index];
  const DriveOperator drive_upper = kDriveBank[drive_index + 1];
  const ShapeOperator shape_lower = kShapeBank[shape_index];
  const ShapeOperator shape_upper = kShapeBank[shape_index + 1];

  for (size_t i = 0; i < size; ++i) {
    phase_ = WrapPhase(phase_ + f0);
    const float ramp = phase_;

    // Stage one: drive, crossfaded against the untouched ramp by depth.
    const float drive_low = drive_lower(ramp);
    const float driven_shape =
        drive_low + drive_mix * (drive_upper(ramp) - drive_low);
    const float driven = ramp + depth * (driven_shape - ramp);

    // Stage two: shape, in series on the driven ramp, at full strength.
    float read_low, window_low, read_high, window_high;
    shape_lower(driven, ratio, &read_low, &window_low);
    shape_upper(driven, ratio, &read_high, &window_high);
    const float read = read_low + shape_mix * (read_high - read_low);
    const float window = window_low + shape_mix * (window_high - window_low);

    // Depth crossfades in the AUDIO domain — see the note on the shape bank.
    // Both branches are continuous across the ramp's wrap on their own, so
    // every point between them is too.
    const float dry = Sine(driven);
    const float wet = Sine(read) * window;
    const float voice = dry + depth * (wet - dry);

    // AUX is the same thing an octave up, which for the resonant shapes puts
    // the formant an octave higher over the same window.
    const float dry_octave = Sine(driven * 2.0f);
    const float wet_octave = Sine(read * 2.0f) * window;

    out[i] = kOutScale * voice;
    aux[i] = kAuxScale * (dry_octave + depth * (wet_octave - dry_octave));
  }

  dc_blocker_out_.Process(out, size, kDCBlockerPole);
  dc_blocker_aux_.Process(aux, size, kDCBlockerPole);
}

}  // namespace plaits
