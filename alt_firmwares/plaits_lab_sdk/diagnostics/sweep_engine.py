#!/usr/bin/env python3
"""Sweep one control of a Plaits Lab package and measure what it actually did.

`plaits-lab render` runs the scenarios a package declares. This runs the same
renderer directly, so any note and any control position can be probed without
editing scenarios.json -- which is what you want while iterating on an engine.

Three measurements per step:

  centroid    spectral centroid in Hz. Does the control move the timbre?
  rms         loudness. Does the control move the level when it shouldn't?
  inharmonic  share of spectral energy that is not on a harmonic of f0, in dB.
              This is the aliasing check. Below about -60 dB is inaudible;
              above -30 dB the engine is folding content back down and a pitch
              sweep will grow phantom tones that descend as the pitch rises.

Examples:

  py sweep_engine.py <package> --control timbre
  py sweep_engine.py <package> --control harmonics --note 96 --steps 11
  py sweep_engine.py <package> --sweep-pitch --hold timbre=1.0

Needs numpy and a host C++ compiler. The compiler's own directory is put on the
child PATH automatically, because an MSYS2 g++ invoked by absolute path cannot
load cc1plus's DLLs and exits 1 with no diagnostics at all. Doing it here rather
than in the shell leaves the parent PATH alone, so `python` keeps resolving to
the interpreter that has numpy.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

CONTROLS = ("harmonics", "timbre", "morph", "macro")
SDK_ROOT = Path(__file__).resolve().parent.parent
REPO_ROOT = SDK_ROOT.parent.parent


def default_compiler() -> str | None:
    """A HOST compiler. Deliberately not just which("g++"): an embedded ARM
    toolchain on PATH answers to that name and silently builds nothing runnable.
    """
    for variable in ("PLAITS_LAB_CXX", "CXX"):
        if os.environ.get(variable):
            return os.environ[variable]
    for candidate in (r"C:\msys64\ucrt64\bin\g++.exe",
                      r"C:\msys64\mingw64\bin\g++.exe",
                      r"C:\w64devkit\bin\g++.exe"):
        if Path(candidate).is_file():
            return candidate
    return shutil.which("c++") or shutil.which("g++")


def build_renderer(package_dir: Path, compiler: str, out: Path) -> None:
    manifest = json.loads((package_dir / "plaits-engine.json").read_text())
    source = manifest["source"]
    src_root = package_dir / source["root"]
    command = [
        compiler, "-std=c++11", "-D_USE_MATH_DEFINES", "-DTEST", "-O2", "-w",
        f'-DPLAITS_LAB_ENGINE_HEADER="{source["header"]}"',
        f'-DPLAITS_LAB_ENGINE_CLASS=plaits::{source["className"]}',
        "-DPLAITS_LAB_USER_DATA_BANK=-1",
        "-I", str(REPO_ROOT), "-I", str(src_root),
        str(SDK_ROOT / "render_model.cc"),
        *[str(src_root / f) for f in source["files"]],
        str(REPO_ROOT / "plaits" / "resources.cc"),
        str(REPO_ROOT / "stmlib" / "dsp" / "units.cc"),
        str(REPO_ROOT / "stmlib" / "utils" / "random.cc"),
        "-o", str(out),
    ]
    if os.name == "nt":
        # Otherwise the renderer needs the toolchain's libstdc++/libgcc DLLs on
        # PATH to start, and dies with a bare 0xC0000135 that says nothing.
        command[1:1] = ["-static"]
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(
        [str(Path(compiler).resolve().parent), env.get("PATH", "")])
    result = subprocess.run(command, text=True, capture_output=True, env=env)
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()
        sys.exit(f"compile failed:\n{detail}" if detail else
                 "compile failed with no diagnostics -- the compiler could not "
                 "start its own backend. Check that it is a host compiler and "
                 "not a cross-compiler for the module's ARM target.")


def render(renderer: Path, note: float, values: dict[str, float],
           gains: tuple[float, float], seconds: int = 2,
           ) -> tuple[np.ndarray, np.ndarray, int]:
    out = Path(tempfile.mktemp(suffix=".wav"))
    args = [str(renderer), str(out), str(seconds), str(note)]
    for control in CONTROLS:
        args += [str(values[control]), str(values[control])]
    # The manifest's own output gains, NOT unity. Forcing 1.0 here clipped the
    # renderer on any engine that uses its headroom, and clipping is broadband:
    # it showed up as an aliasing reading of -33 dB on an engine measuring -100.
    args += ["0", str(gains[0]), str(gains[1])]
    subprocess.run(args, check=True, capture_output=True)
    with wave.open(str(out)) as w:
        frames, channels, rate = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = np.frombuffer(w.readframes(frames), dtype="<i2")
    out.unlink()
    audio = raw.astype(np.float64).reshape(-1, channels) / 32768.0
    return audio[:, 0], audio[:, channels - 1], rate


def measure(signal: np.ndarray, rate: int, f0: float) -> dict[str, float]:
    # Skip the attack so the low-pass gate has fully opened.
    window = signal[rate // 2: rate // 2 + 32768]
    spectrum = np.abs(np.fft.rfft(window * np.hanning(len(window))))
    freqs = np.fft.rfftfreq(len(window), 1.0 / rate)
    power = spectrum ** 2

    harmonic = np.zeros_like(freqs, dtype=bool)
    for n in range(1, int(rate / 2 / f0) + 2):
        # A sub-octave is legitimate content, so start the comb at f0 / 2.
        for ratio in (n, n - 0.5):
            harmonic |= np.abs(freqs - ratio * f0) < max(0.02 * ratio * f0, 40.0)
    stray = power[~harmonic].sum() / max(power.sum(), 1e-30)

    return {
        "centroid": float((spectrum * freqs).sum() / max(spectrum.sum(), 1e-30)),
        "rms": float(np.sqrt(np.mean(window ** 2))),
        "dc": float(np.mean(window)),
        "peak": float(np.abs(window).max()),
        "inharmonic_db": -99.0 if stray <= 0 else float(10 * math.log10(stray)),
    }


def note_to_hz(note: float) -> float:
    return 440.0 * (2.0 ** ((note - 69.0) / 12.0))


def print_row(label: str, m: dict[str, float]) -> None:
    flags = []
    # Clipping first: it is broadband, so it inflates the aliasing figure and
    # would otherwise be misread as the engine folding content down.
    if m["peak"] >= 0.999:
        flags.append("CLIPPING")
    if m["inharmonic_db"] > -30.0:
        flags.append("ALIASING")
    elif m["inharmonic_db"] > -60.0:
        flags.append("some fold-back")
    if abs(m["dc"]) > 0.2:
        flags.append("DC over SDK limit")
    suffix = "  <-- " + ", ".join(flags) if flags else ""
    print(f"  {label:>14s}   centroid {m['centroid']:8.0f} Hz   rms {m['rms']:.4f}   "
          f"peak {m['peak']:.3f}   dc {m['dc']:+.3f}   "
          f"inharmonic {m['inharmonic_db']:6.1f} dB{suffix}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("package", type=Path)
    parser.add_argument("--control", choices=CONTROLS,
                        help="sweep this control from 0 to 1")
    parser.add_argument("--sweep-pitch", action="store_true",
                        help="sweep note instead of a control")
    parser.add_argument("--note", type=float, default=48.0)
    parser.add_argument("--steps", type=int, default=6)
    parser.add_argument("--hold", action="append", default=[],
                        metavar="CONTROL=VALUE",
                        help="pin a control (repeatable); unpinned default to 0")
    parser.add_argument("--seconds", type=int, default=2)
    parser.add_argument("--compiler", default=default_compiler())
    args = parser.parse_args()

    if not args.control and not args.sweep_pitch:
        parser.error("pass --control or --sweep-pitch")
    if not args.compiler:
        parser.error("no host C++ compiler found; pass --compiler")

    held = {c: 0.0 for c in CONTROLS}
    for item in args.hold:
        name, _, value = item.partition("=")
        if name not in CONTROLS:
            parser.error(f"--hold: unknown control {name!r}")
        held[name] = float(value)

    post = json.loads((args.package / "plaits-engine.json").read_text()
                      ).get("postProcessing", {})
    gains = (float(post.get("outGain", 0.8)), float(post.get("auxGain", 0.8)))

    with tempfile.TemporaryDirectory() as work:
        renderer = Path(work) / ("render.exe" if os.name == "nt" else "render")
        build_renderer(args.package, args.compiler, renderer)

        if args.sweep_pitch:
            print(f"pitch sweep, controls {held}, gains {gains}")
            for note in np.linspace(24, 120, args.steps):
                main_ch, _, rate = render(renderer, note, held, gains, args.seconds)
                m = measure(main_ch, rate, note_to_hz(note))
                print_row(f"note {note:5.1f}", m)
        else:
            print(f"{args.control} sweep at note {args.note} "
                  f"({note_to_hz(args.note):.1f} Hz), others {held}, gains {gains}")
            f0 = note_to_hz(args.note)
            for value in np.linspace(0.0, 1.0, args.steps):
                values = dict(held, **{args.control: float(value)})
                main_ch, _, rate = render(
                    renderer, args.note, values, gains, args.seconds)
                print_row(f"{args.control[:4]}={value:.2f}", measure(main_ch, rate, f0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
