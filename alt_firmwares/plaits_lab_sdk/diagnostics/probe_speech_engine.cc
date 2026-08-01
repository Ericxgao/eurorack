// Local diagnostic: render the Speech engine at a spread of HARMONICS positions
// and report level per position. Used to confirm the flash-trimmed, LPC-only
// engine still selects each utterance source and still makes sound. Host build
// only (-DTEST).
//
//   c++ -std=c++11 -O1 -DTEST -D_USE_MATH_DEFINES -I. \
//       alt_firmwares/plaits_lab_sdk/diagnostics/probe_speech_engine.cc \
//       plaits/dsp/engine/speech_engine.cc plaits/dsp/speech/*.cc \
//       plaits/resources.cc stmlib/utils/random.cc stmlib/dsp/units.cc \
//       -o probe_speech_engine

#include <cmath>
#include <cstdio>

#include "stmlib/utils/buffer_allocator.h"

#include "plaits/dsp/engine/speech_engine.h"

using namespace plaits;
using namespace stmlib;

static uint8_t shared_buffer[32768];

int main() {
  BufferAllocator allocator(shared_buffer, sizeof(shared_buffer));
  SpeechEngine engine;
  engine.Init(&allocator);
  engine.Reset();
  engine.set_prosody_amount(0.5f);
  engine.set_speed(0.5f);

  printf("%-10s %10s %10s %10s %10s\n",
         "harmonics", "peak_out", "rms_out", "peak_aux", "rms_aux");
  printf("---------- ---------- ---------- ---------- ----------\n");

  const float positions[] = {
      0.00f, 0.08f, 0.16f,   // phoneme scanning zone
      0.25f, 0.40f,          // first word bank (numbers)
      0.60f, 0.85f, 1.00f};  // second word bank (alphabet)

  for (size_t p = 0; p < sizeof(positions) / sizeof(positions[0]); ++p) {
    // A fresh engine per position: the word bank quantizer has hysteresis and
    // the LPC filter carries state, so sweeping in one pass would let an
    // earlier position colour the next one's reading.
    BufferAllocator a(shared_buffer, sizeof(shared_buffer));
    SpeechEngine e;
    e.Init(&a);
    e.Reset();
    e.set_prosody_amount(0.5f);
    e.set_speed(0.5f);

    float peak_out = 0.0f, sq_out = 0.0f, peak_aux = 0.0f, sq_aux = 0.0f;
    int n = 0;
    // ~1.5 s at 48 kHz, so a triggered utterance has time to play out.
    for (int block = 0; block < 3000; ++block) {
      EngineParameters params;
      params.note = 48.0f;
      params.harmonics = positions[p];
      params.timbre = 0.5f;
      params.morph = 0.5f;
      params.accent = 0.8f;
      params.macro = 0.5f;
      params.stereo = false;
      params.trigger = block == 0 ? TRIGGER_RISING_EDGE : TRIGGER_HIGH;

      float out[kMaxBlockSize] = {0.0f};
      float aux[kMaxBlockSize] = {0.0f};
      bool already_enveloped = false;
      e.Render(params, out, aux, kMaxBlockSize, &already_enveloped);

      for (size_t i = 0; i < kMaxBlockSize; ++i) {
        if (!std::isfinite(out[i]) || !std::isfinite(aux[i])) {
          printf("%-10.2f  NON-FINITE SAMPLE -- FAIL\n", positions[p]);
          return 1;
        }
        peak_out = std::max(peak_out, std::fabs(out[i]));
        peak_aux = std::max(peak_aux, std::fabs(aux[i]));
        sq_out += out[i] * out[i];
        sq_aux += aux[i] * aux[i];
        ++n;
      }
    }
    printf("%-10.2f %10.4f %10.4f %10.4f %10.4f\n",
           positions[p], peak_out, std::sqrt(sq_out / n),
           peak_aux, std::sqrt(sq_aux / n));
  }
  return 0;
}
