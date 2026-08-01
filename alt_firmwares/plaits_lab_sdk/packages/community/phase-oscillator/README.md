# Phase Oscillator

Casio CZ phase distortion, built as a chain. A linear ramp passes through a
drive curve and then a shape, and each is picked from a bank by crossfading
between neighbours, so a knob runs continuously through its bank instead of
stepping. Nothing changes how fast the ramp runs, so the waveform moves and the
pitch does not.

| Control | Panel | Bank |
| --- | --- | --- |
| HARMONICS | Drive | square → warp → double warp → triple warp |
| TIMBRE | Shape | saw → pulse → double sine → reso saw → reso triangle → reso trapezoid |
| MORPH | Depth | depth of the whole chain, and the only bypass |
| MACRO | Formant | ratio for the three resonant shapes only; neutral at centre |

Neither bank has an identity entry. Bypass is what MORPH is for, and an identity
operator would only duplicate it while eating knob travel.

**MACRO is optional.** The voice hands an engine 0.5 whenever the frequency knob
is not locked to the fourth macro (`voice.cc:219`), so it is used through
`ApplyMacro`, which is exactly neutral at centre. The engine is complete on
HARMONICS, TIMBRE and MORPH; MACRO only refines, and only for the resonant half
of the shape bank.

## The resonant shapes

The last three are not phase bends at all. They run the reader at a multiple of
the fundamental and apply a falling window over each cycle — sawtooth, triangle,
trapezoid — which is the CZ's resonant filter sweep. So a shape returns a read
position *and* a window, and the plain bends leave the window at one.

Reading a **sine** rather than the CZ's cosine matters: `sin(0)` is zero, so the
window's reset at the cycle boundary lands on a zero crossing instead of a step.
All three windows also reach zero at the end of the cycle, so the reader being
mid-cycle there is multiplied away.

The formant is a real tone at `ratio × f0`, so the ratio is capped against
Nyquist independently of the depth taper.

## Why depth is a crossfade of audio, not of phase

This is the one non-obvious thing in the file. Depth used to interpolate the
*read* against the straight ramp, which quietly requires every shape to end its
cycle at exactly 1 — otherwise the interpolated read sits mid-cycle when the
ramp wraps, and the wave steps. The bends end at 1 and were fine. The fold ends
at 0 and the formants end at `ratio`, so both tore, and the window only masks it
at full depth, which the pitch taper never allows.

Measured: **−26.9 dB** of fold-back at C7. Moving the crossfade into the audio
domain — two branches, each continuous on its own — took the worst case across
the whole bank and register to **−59.2 dB**. It costs two extra table reads per
sample, which is the cheapest thing on this core.

## Measured, not guessed

Over 150 points at full depth: worst DC **0.0028** against an SDK limit of 0.2,
MAIN peaking at 1.20 and AUX at 1.00 pre-scaling, which is where the internal
scales come from. Blocking DC *raises* peak on an asymmetric wave — the offset
it removes was holding one side down.

Aliasing, worst over the whole shape bank:

| C2 | C4 | C6 | C7 | C8 |
| --- | --- | --- | --- | --- |
| −99 dB | −81 dB | −59 dB | −64 dB | −66 dB |

Depth tapers as 1/f², steeper than the available room shrinks, because corners
have a harmonic tail falling only as 1/n².

## Building and listening

```powershell
. alt_firmwares\plaits_lab_sdk\.toolchain\activate.ps1
$P = "alt_firmwares/plaits_lab_sdk/packages/community/phase-oscillator"

plaits-lab check $P --full
plaits-lab dev $P                      # live audition, no emcc needed
plaits-sweep $P --control timbre --hold morph=1.0
```

`shape-bank.wav` walks the whole CZ set with the drive bank left alone;
`reso-sweep.wav` sweeps the formant ratio on the resonant saw, which is the
clearest demonstration of what MACRO is for.

## Not yet done

CPU cost is **unmeasured** — `qemu-system-arm` is not installed. This is the
heaviest version so far: four operator calls per sample through function
pointers that cannot inline, plus four table reads. If it does not fit,
the first thing to try is hoisting each bank into a switch outside the sample
loop, trading flash for the indirect calls.
