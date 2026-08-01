# Phase Oscillator

Casio CZ-style phase distortion, built as a **chain** rather than a single bend.
A linear phase ramp passes through two operators in series before it indexes the
table, and each operator is picked from a bank of four by crossfading between
neighbours, so a knob runs continuously through its bank instead of stepping.
Composition is not commutative — that is the point. Chaining a shape into a
drive reaches waveforms neither reaches alone.

Nothing here changes how fast the ramp runs, so the waveform moves and the pitch
does not.

| Control | Panel | Bank |
| --- | --- | --- |
| TIMBRE | Shape | identity → saw knee → pulse plateau → fold |
| HARMONICS | Drive | identity → square → warp → double warp |
| MORPH | Depth | depth of the whole chain; fully down is a plain sine |
| MACRO | Octave | blends MAIN from the fundamental reader to the doubled one |

**SHAPE** is the corner family, and where the CZ character lives: a knee that
reads the first part of the cycle fast, a plateau that stops the reader
mid-cycle, and a fold that sweeps the table out and back. **DRIVE** is the
smooth family, applied afterwards: a square-law skew, and one or two sines of
warp per cycle deep enough to run the reader briefly backwards.

Every operator satisfies `f(0) = 0` and `f(1) = 0 or 1`, so the reader lands
where it started when the ramp wraps. Break that and the wrap becomes a step —
a click at the fundamental with a 1/n spray of harmonics behind it.

## Why MAIN and AUX are not sine and cosine

The two readers are a quadrature pair, but blending them directly would be
**inaudible**: `sin θ` and `cos θ` of the same warped phase have identical
magnitude spectra, so a crossfade between them is only a phase rotation. The
pair is used as a product instead — `2·sin θ·cos θ` is `sin 2θ`, an octave up on
the same waveform and a genuinely different spectrum. MACRO blends toward it;
AUX carries it alone.

## Measured, not guessed

**DC.** Several operators are asymmetric in time and leave over half of full
scale as offset, against an SDK limit of 0.2. A one-pole DC blocker at a ~3 Hz
corner holds it under 0.003 across 324 points. It is written out inline rather
than using `stmlib::DCBlocker`, which is the same filter, because
`stmlib/dsp/filter.h` includes `<algorithm>` and the MSVC standard library clang
uses on Windows wants C++14 for that header while the SDK compiles engines at
`-std=c++11`.

**Headroom.** Blocking DC *raises* peak on an asymmetric wave — the offset it
removes was holding one side down. Over 324 points at full depth, MAIN peaks at
0.90 and AUX at 1.26 of the unscaled signal; AUX is the hotter because doubling
squares the reader, so its peaks line up where MAIN's cancel. The internal
scales come from those numbers.

**Aliasing.** Corners have a harmonic tail falling only as 1/n², which reaches
past Nyquist once the fundamental is high, so depth tapers as **1/f²** — steeper
than the available room shrinks. Worst case across the register:

| ≤ C4 | C5 | C6 | C7 | C8 | C9 |
| --- | --- | --- | --- | --- | --- |
| ≤ −92 dB | −83 dB | −60 dB | −62 dB | −63 dB | −57 dB |

## Building and listening

```powershell
. alt_firmwares\plaits_lab_sdk\.toolchain\activate.ps1
$P = "alt_firmwares/plaits_lab_sdk/packages/community/phase-oscillator"

plaits-lab check $P --full
plaits-lab dev $P
plaits-sweep $P --control timbre --hold morph=1.0
plaits-sweep $P --sweep-pitch --hold morph=1.0 --hold timbre=0.4 --hold harmonics=0.6
```

`bypassed.wav` is the baseline sine. `shape-bank.wav` and `drive-bank.wav` walk
one bank each with the other left at identity — those are the ones to hear
first, because they let you learn the four characters separately before
`chained.wav` runs both at once.

## Not yet done

CPU cost is **unmeasured** — `qemu-system-arm` is not installed, and the SDK's
own host smoke test is worth nothing by its own README. This version is heavier
than the last: the bank crossfade means four operator calls per sample, through
function pointers resolved per block, so they cannot inline. If it turns out to
be too costly, collapsing each bank to a switch outside the sample loop trades
flash for those indirect calls.
