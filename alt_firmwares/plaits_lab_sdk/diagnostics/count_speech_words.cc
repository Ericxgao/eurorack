// Local diagnostic: report how many words and LPC frames each Speech word bank
// holds, plus its flash cost. Used to decide which banks to keep when trimming
// the Speech engine's flash footprint. Host build only (-DTEST).
//
//   c++ -std=c++11 -O1 -DTEST -D_USE_MATH_DEFINES -I. \
//       alt_firmwares/plaits_lab_sdk/diagnostics/count_speech_words.cc \
//       plaits/dsp/speech/lpc_speech_synth.cc \
//       plaits/dsp/speech/lpc_speech_synth_controller.cc \
//       plaits/dsp/speech/lpc_speech_synth_phonemes.cc \
//       plaits/dsp/speech/lpc_speech_synth_words.cc \
//       stmlib/utils/random.cc -o count_speech_words

#include <cstdio>
#include <set>
#include <utility>

#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/speech/lpc_speech_synth_words.h"

using namespace plaits;
using namespace stmlib;

static uint8_t shared_buffer[65536];

int main() {
  printf("%-8s %8s %8s %8s %10s\n",
         "bank", "bytes", "words", "frames", "B/frame");
  printf("-------- -------- -------- -------- ----------\n");

  int total_bytes = 0, total_words = 0, total_frames = 0, max_frames = 0;
  for (int bank = 0; bank < LPC_SPEECH_SYNTH_NUM_WORD_BANKS; ++bank) {
    BufferAllocator allocator(shared_buffer, sizeof(shared_buffer));
    LPCSpeechSynthWordBank word_bank;
    word_bank.Init(word_banks_, LPC_SPEECH_SYNTH_NUM_WORD_BANKS, &allocator);
    word_bank.Load(bank);

    // num_words_ is private; recover the count by sweeping the address input
    // and collecting the distinct word boundary pairs it reports.
    std::set<std::pair<int, int> > words;
    for (int i = 0; i <= 20000; ++i) {
      int start = -1, end = -1;
      word_bank.GetWordBoundaries(float(i) / 20000.0f, &start, &end);
      if (start >= 0) {
        words.insert(std::make_pair(start, end));
      }
    }

    const int bytes = int(word_banks_[bank].size);
    const int frames = word_bank.num_frames();
    printf("bank_%-2d  %8d %8d %8d %10.1f\n",
           bank, bytes, int(words.size()), frames,
           frames ? double(bytes) / double(frames) : 0.0);
    total_bytes += bytes;
    total_words += int(words.size());
    total_frames += frames;
    if (frames > max_frames) {
      max_frames = frames;
    }
  }

  printf("-------- -------- -------- -------- ----------\n");
  printf("%-8s %8d %8d %8d\n", "TOTAL", total_bytes, total_words, total_frames);
  printf("\nRAM: frames_ is allocated for %d frames x %d B = %d B\n",
         kLPCSpeechSynthMaxFrames, int(sizeof(LPCSpeechSynth::Frame)),
         int(kLPCSpeechSynthMaxFrames * sizeof(LPCSpeechSynth::Frame)));
  printf("Largest bank needs %d frames, so the cap has %d frames (%d B) of slack.\n",
         max_frames, kLPCSpeechSynthMaxFrames - max_frames,
         int((kLPCSpeechSynthMaxFrames - max_frames) *
             sizeof(LPCSpeechSynth::Frame)));
  return 0;
}
