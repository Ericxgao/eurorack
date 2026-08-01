// Copyright 2016 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// LPC10 speech synth.

#ifndef PLAITS_DSP_SPEECH_LPC_SPEECH_SYNTH_H_
#define PLAITS_DSP_SPEECH_LPC_SPEECH_SYNTH_H_

#include "stmlib/dsp/dsp.h"

#include "plaits/dsp/dsp.h"

namespace plaits {

// Optimize for size, not speed, in code that never runs per sample. The word
// bank unpacker only executes on an actual bank change (Load() early-returns
// when the bank is already resident) and PlayFrame runs at
// kLPCSpeechSynthFPS (40 Hz), so the build's -funroll-loops and -O2 inlining
// buy nothing there while costing a lot of flash: measured with
// arm-none-eabi-g++ 10.2.1 at the project's flags, LoadNextWord alone drops
// from 1504 to 360 bytes and PlayFrame from 732 to 636, while the per-sample
// Render functions come out byte-identical.
//
// Only GCC has this attribute. Clang parses it and then errors under the SDK's
// -Werror (-Wunknown-attributes), and emcc is clang — so it must expand to
// nothing there. That costs nothing: those are host/WASM audition builds, where
// flash size is not a constraint.
#if defined(__GNUC__) && !defined(__clang__)
#define PLAITS_COLD_CODE __attribute__((optimize("Os")))
#else
#define PLAITS_COLD_CODE
#endif

const int kLPCOrder = 10;

const float kLPCSpeechSynthDefaultF0 = 100.0f;

class LPCSpeechSynth {
 public:
  LPCSpeechSynth() { }
  ~LPCSpeechSynth() { }

  struct Frame {
    // 14 bytes.
    uint8_t energy;
    uint8_t period;
    int16_t k0;
    int16_t k1;
    int8_t k2;
    int8_t k3;
    int8_t k4;
    int8_t k5;
    int8_t k6;
    int8_t k7;
    int8_t k8;
    int8_t k9;
  };

  void Init();
  
  void Render(
      float prosody_amount,
      float pitch_shift,
      float* excitation,
      float* output,
      size_t size);
  
  void PlayFrame(const Frame* frames, float frame, bool interpolate) {
    MAKE_INTEGRAL_FRACTIONAL(frame);
    
    if (!interpolate) {
      frame_fractional = 0.0f;
    }
    PlayFrame(
        frames[frame_integral],
        frames[frame_integral + 1],
        frame_fractional);
  }

 private:
  void PlayFrame(const Frame& f1, const Frame& f2, float blend);
  
  template <int scale, typename X>
  float BlendCoefficient(X a, X b, float blend) {
    float a_f = static_cast<float>(a) / float(scale);
    float b_f = static_cast<float>(b) / float(scale);
    return a_f + (b_f - a_f) * blend;
  }
  
  float phase_;
  float frequency_;
  float noise_energy_;
  float pulse_energy_;
  
  float next_sample_;
  int excitation_pulse_sample_index_;

  float k_[kLPCOrder];
  float s_[kLPCOrder + 1];

  DISALLOW_COPY_AND_ASSIGN(LPCSpeechSynth);
};

};  // namespace plaits

#endif  // PLAITS_DSP_SPEECH_LPC_SPEECH_SYNTH_H_
