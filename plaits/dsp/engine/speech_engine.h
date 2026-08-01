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
// LPC10 speech synthesis.
//
// Trimmed for flash from the original three-model engine: the naive formant
// bank and the SAM clone are gone, and HARMONICS now maps the whole knob onto
// the LPC synth (phoneme scanning, then the word banks). That removed the
// region where two synths rendered at once, so it lowers the worst-case CPU
// cost as well as the code size. See lpc_speech_synth_words.h for which word
// banks were kept and why.
//
// OUT: the LPC voice, mixed with the secondary formant path by MACRO (voice
// amount / spectral sharpening). AUX: the secondary path. In stereo mode,
// OUT/AUX become L/R: the final MACRO mix is replaced by a gentle equal-power
// pan of the two existing paths -- the voice slightly left, the secondary
// formant path slightly right -- so the same utterance widens across both
// channels.

#ifndef PLAITS_DSP_ENGINE_SPEECH_ENGINE_H_
#define PLAITS_DSP_ENGINE_SPEECH_ENGINE_H_

#include "stmlib/dsp/hysteresis_quantizer.h"

#include "plaits/dsp/engine/engine.h"
#include "plaits/dsp/speech/lpc_speech_synth_controller.h"

namespace plaits {

class SpeechEngine : public Engine {
 public:
  SpeechEngine() { }
  ~SpeechEngine() { }
  
  virtual void Init(stmlib::BufferAllocator* allocator);
  virtual void Reset();
  virtual void LoadUserData(const uint8_t* user_data) { }
  virtual void Render(const EngineParameters& parameters,
      float* out,
      float* aux,
      size_t size,
      bool* already_enveloped);
  virtual bool stereo_capable() const { return PLAITS_STEREO_SPEECH; }

  inline void set_prosody_amount(float prosody_amount) {
    prosody_amount_ = prosody_amount;
  }
  
  inline void set_speed(float speed) {
    speed_ = speed;
  }

 private:
  stmlib::HysteresisQuantizer2 word_bank_quantizer_;

  LPCSpeechSynthController lpc_speech_synth_controller_;
  LPCSpeechSynthWordBank lpc_speech_synth_word_bank_;

  float prosody_amount_;
  float speed_;
  float post_filter_;
  
  DISALLOW_COPY_AND_ASSIGN(SpeechEngine);
};

}  // namespace plaits

#endif  // PLAITS_DSP_ENGINE_SPEECH_ENGINE_H_
