# Sine Lab

A sine oscillator built as a test bench. Every control moves exactly one
audible dimension, so a single-control sweep isolates one thing, and AUX
carries the unmodified fundamental to A/B against.

| Control | Panel | Range |
| --- | --- | --- |
| HARMONICS | Partials | bare sine → partials 2, 3, 4 faded in one at a time |
| TIMBRE | Drive | bare sine → self-phase-modulated, roughly five harmonics |
| MORPH | Stretch | harmonic ratios 1:2:3:4 → stretched 1:2.25:3.5:4.75 (bell-like) |
| MACRO | Sub | no sub → a sine one octave down, mixed to unity |

MAIN is the whole stack; AUX is the fundamental alone.

## Two things that are deliberate, not bugs

**MAIN gets quieter as HARMONICS comes up** (about 6 dB across the sweep). The
output is normalised by the sum of the partial gains, which is the level the
stack reaches when the partials are phase-aligned — and with harmonic ratios and
a shared phase origin they align on every cycle, so that bound is real rather
than theoretical. Normalising by power instead would hold the loudness constant
and clip on every alignment.

**TIMBRE loses depth above about C7 and reaches zero near C9.** Phase modulation
of index *I* spreads energy over roughly *I* + 3 harmonics, so the depth is
capped at whatever keeps the highest of them under Nyquist. Without that cap the
top two octaves fold their upper harmonics back down as inharmonic tones that
*descend* as pitch rises — measured at 50% of total energy before the cap, and
below −85 dB after it up to C8. A 8.4 kHz fundamental has room for two harmonics
under Nyquist; no brightness control can conjure a third.

## Building and listening

From the repository root, dot-source the local toolchain first — it selects a
host compiler and puts the sanitizer runtime on PATH for that shell only. See
`alt_firmwares/plaits_lab_sdk/.toolchain/README.md`.

```powershell
. alt_firmwares\plaits_lab_sdk\.toolchain\activate.ps1
$P = "alt_firmwares/plaits_lab_sdk/packages/community/sine-lab"

plaits-lab check $P --full
plaits-lab render $P --scenario sweep-drive --output previews/sweep-drive.wav
plaits-lab dev $P           # browser audition on http://127.0.0.1:4179/
```

## Scenarios

`reference-sine` is the control: every knob at zero, so MAIN and AUX are
identical. `sweep-partials`, `sweep-drive`, `sweep-stretch`, and `sweep-sub`
each move one control from 0 to 1 with the others pinned at zero.
`pitch-high` sits at C8 to exercise the Nyquist guards, `plucked` runs the
low-pass gate at 3 Hz, and `hero` moves everything at once.

## Parameter sweeps

`plaits-lab render` only runs declared scenarios. To probe arbitrary positions
while iterating, use the sweep tool, which reports spectral centroid, RMS, and
the share of energy that is *not* on a harmonic — the aliasing check:

```powershell
plaits-sweep $P --control timbre --steps 6
plaits-sweep $P --control harmonics --note 96 --steps 11
plaits-sweep $P --sweep-pitch --hold timbre=1.0 --hold harmonics=1.0
```

## Not yet done

`qemu/estimate.py --sweep` needs `qemu-system-arm`, which is not installed, so
this engine's CPU cost is **unmeasured**. `check --full` passes, but its own CPU
smoke test is skipped here and the SDK says to disregard that number anyway.
