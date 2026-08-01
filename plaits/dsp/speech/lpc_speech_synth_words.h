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
// LPC10 encoded words extracted from various TI ROMs.

#ifndef PLAITS_DSP_SPEECH_LPC_SPEECH_SYNTH_WORDS_H_
#define PLAITS_DSP_SPEECH_LPC_SPEECH_SYNTH_WORDS_H_

#include "plaits/dsp/speech/lpc_speech_synth_controller.h"

namespace plaits {

// Trimmed for flash: of the five original TI ROM banks, only the two with the
// best words-per-byte ratio are kept -- bank_1 (numbers, 82 B/word) and bank_2
// (the alphabet, 60 B/word). The three removed banks were bank_0 (colours,
// 1233 B / 7 words), bank_3 (NATO alphabet, 2524 B / 26 words) and bank_4
// (modular-synth vocabulary, 4802 B / 22 words -- 218 B per word, the most
// expensive of the five). Restoring any of them means re-adding its array here
// and to word_banks_; kLPCSpeechSynthMaxFrames (1024) is left alone so it still
// covers every original bank, the largest being bank_4 at 926 frames.
#define LPC_SPEECH_SYNTH_NUM_WORD_BANKS 2

extern const uint8_t bank_1[900];
extern const uint8_t bank_2[1552];

extern const LPCSpeechSynthWordBankData word_banks_[LPC_SPEECH_SYNTH_NUM_WORD_BANKS];

}  // namespace plaits

#endif  // PLAITS_DSP_SPEECH_LPC_SPEECH_SYNTH_WORDS_H_
