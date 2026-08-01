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
// Various flavours of speech synthesis.

#include "plaits/dsp/engine/speech_engine.h"

#include "plaits/dsp/speech/lpc_speech_synth_words.h"

namespace plaits {

using namespace std;
using namespace stmlib;

void SpeechEngine::Init(BufferAllocator* allocator) {
  lpc_speech_synth_word_bank_.Init(
      word_banks_,
      LPC_SPEECH_SYNTH_NUM_WORD_BANKS,
      allocator);
  lpc_speech_synth_controller_.Init(&lpc_speech_synth_word_bank_);
  word_bank_quantizer_.Init(LPC_SPEECH_SYNTH_NUM_WORD_BANKS + 1, 0.1f, false);
  
  prosody_amount_ = 0.0f;
  speed_ = 0.0f;
  post_filter_ = 0.0f;
}

void SpeechEngine::Reset() {
  lpc_speech_synth_word_bank_.Reset();
  post_filter_ = 0.0f;
}

void SpeechEngine::Render(
    const EngineParameters& parameters,
    float* out,
    float* aux,
    size_t size,
    bool* already_enveloped) {
  const float f0 = NoteToFrequency(parameters.note);
  
  // HARMONICS selects the utterance source across three roughly equal zones:
  // phoneme scanning up to ~0.37, the numbers bank to ~0.70, the alphabet bank
  // above that (the quantizer's hysteresis puts the real edges at 0.30/0.37 and
  // 0.63/0.70 depending on sweep direction). The naive and SAM models that the
  // original engine crossfaded into across the lower third of this knob were
  // removed to save flash, so the LPC synth -- the TI-speech voice this engine
  // is built around -- now owns the whole knob. Verified against the original:
  // both the phoneme zone and the numbers bank render identically.
  const int word_bank = word_bank_quantizer_.Process(parameters.harmonics) - 1;
  
  const bool replay_prosody = word_bank >= 0 && \
      !(parameters.trigger & TRIGGER_UNPATCHED);
  
  *already_enveloped = replay_prosody;
  
  lpc_speech_synth_controller_.Render(
      parameters.trigger & TRIGGER_UNPATCHED,
      parameters.trigger & TRIGGER_RISING_EDGE,
      word_bank,
      f0,
      prosody_amount_,
      speed_,
      parameters.morph,
      parameters.timbre,
      replay_prosody ? parameters.accent : 1.0f,
      aux,
      out,
      size);

  if ((PLAITS_STEREO_SPEECH && parameters.stereo)) {
    // OUT/AUX become L/R: replace the MACRO mix with a gentle equal-power pan
    // of the two existing paths so both are audible as a widened voice. The
    // voice path leans slightly left, the secondary formant path slightly
    // right; both appear on both channels, so a mono sum does not cancel.
    float voice_l, voice_r, secondary_l, secondary_r;
    StereoPanGains(0.4f, &voice_l, &voice_r);
    StereoPanGains(0.6f, &secondary_l, &secondary_r);
    for (size_t i = 0; i < size; ++i) {
      const float voice = out[i];
      const float secondary = aux[i];
      out[i] = voice * voice_l + secondary * secondary_l;
      aux[i] = voice * voice_r + secondary * secondary_r;
    }
  } else {
    const float voice_amount = parameters.macro * 2.0f;
    const float spectral_sharpening = (parameters.macro - 0.5f) * 5.0f;
    for (size_t i = 0; i < size; ++i) {
      const float voice = out[i];
      ONE_POLE(post_filter_, voice, 0.12f);
      if (parameters.macro < 0.5f) {
        out[i] = aux[i] + (out[i] - aux[i]) * voice_amount;
      } else {
        out[i] = voice + (voice - post_filter_) * spectral_sharpening;
      }
    }
  }
}

}  // namespace plaits
