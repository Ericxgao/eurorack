// Copyright 2026 Eric Gao.
// SPDX-License-Identifier: MIT

#include "sine-lab_engine.h"

// The firmware is bare-metal — it can't link libm (std::sin/exp/log/pow), so use
// the shared LUTs: plaits::Sine here, stmlib::SemitonesToRatio for 2^x.
#include "plaits/dsp/oscillator/sine_oscillator.h"

namespace plaits {

namespace {

// Sine() wraps, but only for a non-negative argument. Phase modulation can push
// the argument below zero, so the whole cycle count is biased up by an integer
// number of periods — which the LUT wrap then removes for free. Kept small,
// because every bit spent on the integer part is a bit lost from the phase.
const float kPhaseModulationBias = 2.0f;

// Maximum phase-modulation depth at TIMBRE = 1, in CYCLES — not radians, since
// that is what phase is measured in here. Half a cycle is an index of pi, which
// reaches a saw-like spectrum of roughly five harmonics. Must stay below the
// bias above so the biased argument can never go negative.
const float kMaxPhaseModulation = 0.5f;

// Carson's rule: phase modulation of index I radians spreads energy over about
// I + 2 harmonics. Converting the depth above from cycles gives 2*pi*d + 2.
// The margin is 3 rather than the textbook 2, because that figure is where the
// sidebands stop being *significant*, not where they stop; measured, 2 still
// leaves audible fold-back in the top octaves. The cost is that TIMBRE loses
// depth as pitch rises and reaches zero around C9 — which is honest rather than
// a defect, since a 8.4 kHz fundamental has room for two harmonics under
// Nyquist and no brightness control can conjure a third.
const float kHarmonicsPerCycleOfModulation = 6.283185307f;
const float kCarsonHarmonics = 3.0f;

// Partial n is placed at (n + (n - 1) * stretch) x the fundamental. At MORPH = 1
// the fourth partial sits at 4.75x instead of 4x — far enough off the harmonic
// series to ring rather than blend.
const float kMaxStretch = 0.25f;

inline float WrapPhase(float phase) {
  return phase - static_cast<float>(static_cast<int>(phase));
}

}  // namespace

void SineLabEngine::Init(stmlib::BufferAllocator* allocator) {
  Reset();
}

void SineLabEngine::Reset() {
  for (int i = 0; i < kNumPartials; ++i) {
    phase_[i] = 0.0f;
  }
  sub_phase_ = 0.0f;
}

void SineLabEngine::Render(const EngineParameters& parameters, float* out,
    float* aux, size_t size, bool* already_enveloped) {
  *already_enveloped = false;

  // Everything down to the sample loop runs once per block. At 48 kHz a block
  // is 12 samples, so this arithmetic costs a twelfth of what it would inside
  // the loop — the single biggest lever on this core.
  const float f0 = NoteToFrequency(parameters.note);
  const float stretch = parameters.morph * kMaxStretch;
  const float sub_increment = f0 * 0.5f;

  // Cap the modulation depth so the highest harmonic it produces stays under
  // Nyquist. Without this the top two octaves fold their upper harmonics back
  // down as inharmonic tones that fall as the pitch rises — the one artifact
  // that would make a pitch sweep here untrustworthy.
  float modulation_ceiling = (0.5f / f0 - kCarsonHarmonics) /
      kHarmonicsPerCycleOfModulation;
  CONSTRAIN(modulation_ceiling, 0.0f, kMaxPhaseModulation);
  const float phase_modulation = parameters.timbre * modulation_ceiling;
  const float sub_gain = parameters.macro;

  // The fundamental is always at unity, so HARMONICS = 0 leaves a bare sine and
  // the running normalization can never reach zero.
  float increment[kNumPartials];
  float gain[kNumPartials];
  float normalization = 1.0f + sub_gain;
  increment[0] = f0;
  gain[0] = 1.0f;

  // Partial gains only ever decrease with n, and so do the Nyquist cutoffs, so
  // the active partials are a prefix — count them here and let the sample loop
  // stop early instead of paying for a float compare per partial per sample.
  int num_partials = 1;
  for (int i = 1; i < kNumPartials; ++i) {
    const float ratio = static_cast<float>(i + 1) +
        static_cast<float>(i) * stretch;
    increment[i] = f0 * ratio;

    // HARMONICS spans 0..1 and has three partials to bring in, so each one
    // occupies a third of the travel and they arrive in turn.
    float g = parameters.harmonics * 3.0f - static_cast<float>(i - 1);
    CONSTRAIN(g, 0.0f, 1.0f);

    // At or above Nyquist a partial would fold back down as a phantom tone
    // that tracks pitch the wrong way, which would wreck a pitch sweep.
    if (increment[i] >= 0.5f) {
      g = 0.0f;
    }
    if (g == 0.0f) {
      break;
    }
    gain[i] = g;
    normalization += g;
    ++num_partials;
  }
  const float scale = 0.7f / normalization;

  for (size_t i = 0; i < size; ++i) {
    phase_[0] = WrapPhase(phase_[0] + increment[0]);
    const float fundamental = Sine(phase_[0]);

    // Self-phase-modulation: cheap, and it walks the spectrum from a pure sine
    // up to something saw-like without ever leaving the sine LUT.
    float sum = Sine(
        phase_[0] + phase_modulation * fundamental + kPhaseModulationBias);

    for (int p = 1; p < num_partials; ++p) {
      phase_[p] = WrapPhase(phase_[p] + increment[p]);
      sum += gain[p] * Sine(phase_[p]);
    }

    sub_phase_ = WrapPhase(sub_phase_ + sub_increment);
    sum += sub_gain * Sine(sub_phase_);

    out[i] = scale * sum;
    aux[i] = 0.7f * fundamental;
  }
}

}  // namespace plaits
