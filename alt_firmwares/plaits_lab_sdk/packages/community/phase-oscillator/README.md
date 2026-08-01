# Phase Oscillator

Casio CZ-style phase distortion. A single linear phase ramp feeds two readers, a
sine and a cosine, and each reader has its own operator bending the ramp before
it indexes the table. Nothing here changes how fast the ramp runs, so the
waveform moves and the pitch does not.

| Control | Panel | Does |
| --- | --- | --- |
| HARMONICS | Depth | how much of both bends is applied; fully down bypasses them |
| TIMBRE | Bend | knee on the SINE path — first part of the cycle fast, rest slow (CZ saw) |
| MORPH | Plateau | flat hold mid-cycle on the COSINE path, levelling the wave (CZ pulse) |
| MACRO | Blend | crossfades MAIN from the sine path to the cosine path |

MAIN is that crossfade; AUX is the two paths ring-modulated.

Both operators are branchless — piecewise-linear via `min`/`max` rather than
`if`, because a float compare needs a flag transfer and stalls the pipeline.
All the shape maths, including four divisions, is per-block.

## Three things measured, not guessed

**DC.** Both operators are asymmetric in time, so both leave a large offset:
−0.58 on MAIN and +0.45 on AUX at the worst point, against an SDK limit of 0.2.
It is not a corner case — the sine path alone sits at −0.39 with the knee fully
bent. A one-pole DC blocker at a ~3 Hz corner takes it under 0.004 across 162
points. The blocker is written out inline rather than using `stmlib::DCBlocker`,
which is the same filter, because `stmlib/dsp/filter.h` includes `<algorithm>`
and the MSVC standard library clang uses on Windows wants C++14 for that header
while the SDK compiles engines at `-std=c++11`.

**Headroom.** Removing DC from a wave this asymmetric *raises* its peak — the
offset it subtracts was holding one side down. MAIN reached 1.52 before the
manifest's 0.8 gain, and clipped. The internal scales (0.62 and 0.85) come from
measuring the worst point in the parameter space, not from taste.

**Aliasing.** Piecewise-linear corners have a 1/n² harmonic tail that reaches a
long way, so depth has to fall as pitch rises. A 1/f taper was not enough — it
left harmonics 11 to 14 folding back at −35 dB by C7, identified by their peaks
landing at 0.410, 1.413, 2.415, 3.418 × f0, one harmonic apart, which is the
signature of folding around Nyquist. The taper is 1/f² instead:

| | ≤ C4 | C5 | C6 | C7 | C9 |
| --- | --- | --- | --- | --- | --- |
| inharmonic energy | ≤ −79 dB | −66 dB | −45 dB | −43 dB | −52 dB |

Full depth holds to about C5 and the bends fade above it. Phase distortion has
no bandlimited form at this cost — the CZ hardware aliased too — so the residual
around C6–C7 is the trade, not an oversight.

## Building and listening

```powershell
. alt_firmwares\plaits_lab_sdk\.toolchain\activate.ps1
$P = "alt_firmwares/plaits_lab_sdk/packages/community/phase-oscillator"

plaits-lab check $P --full
plaits-lab dev $P
plaits-sweep $P --control timbre --hold harmonics=1.0
plaits-sweep $P --sweep-pitch --hold harmonics=1.0 --hold timbre=1.0
```

Start from `previews/bypassed.wav` — depth at zero, so a plain sine — then
`sweep-bend.wav` and `sweep-plateau.wav`, which move one operator each with the
other bypassed.

## Not yet done

CPU cost is **unmeasured**: `qemu-system-arm` is not installed, and the SDK's own
host smoke test is worth nothing by its own README. Two sine lookups and about a
dozen flops per sample, with the shape maths hoisted per-block, but that is
reasoning rather than a measurement.
