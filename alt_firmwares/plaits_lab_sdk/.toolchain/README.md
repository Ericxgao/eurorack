# Local toolchain, and the model workflow

## One model per branch

`models/base` is the clean starting point: this toolchain, the sweep tool, and
nothing else. It carries no model, so branch from it rather than from whatever
model you last worked on.

```powershell
git switch models/base
git switch -c models/my-engine

. alt_firmwares\plaits_lab_sdk\.toolchain\activate.ps1
plaits-lab init alt_firmwares/plaits_lab_sdk/packages/community/my-engine --author "Eric Gao"
```

Packages under `packages/community/` are tracked on this fork (the upstream SDK
ignores them), so the branch really does hold the engine. Rendered previews are
not tracked — regenerate them with `plaits-lab render`.

One consequence worth knowing: because the package is tracked, switching from a
model branch back to `models/base` **deletes that package from disk** — it is
committed on the branch you left. That is normal git, but it will pull the rug
out from under a running `plaits-lab dev`. Stop the server before switching.

To start a model from an existing one, `plaits-lab init --from <catalog-id>`
forks it properly, with provenance and the upstream licence, which is better
than branching from another model branch and renaming.

---

# Local toolchain

Everything for this machine, in one gitignored folder. Nothing is installed
system-wide, nothing is written to your PowerShell profile, nothing touches the
registry, and no service runs in the background. Delete this folder and the
machine is exactly as it was.

```powershell
. alt_firmwares\plaits_lab_sdk\.toolchain\activate.ps1
```

The leading dot matters — the script has to be dot-sourced to change the current
shell. It only sets `$env:PATH` for that shell and defines `plaits-lab` and
`plaits-sweep`, so it dies with the window.

## What already works, with nothing new installed

Verified on this machine:

| | |
| --- | --- |
| `plaits-lab check <pkg>` | metadata, licensing, source policy, host compile |
| `plaits-lab check <pkg> --full` | the above plus ASan/UBSan and every scenario |
| `plaits-lab render <pkg> --scenario <id> --output x.wav` | offline WAV |
| `plaits-lab dev <pkg>` | browser audition on `http://127.0.0.1:4179/` |
| `plaits-sweep <pkg> --control timbre` | arbitrary parameter sweeps |

This works because the machine already has **LLVM clang 22** with the
`clang_rt.asan` runtime. That matters more than it sounds: the SDK's README says
Windows contributors need Docker for `check --full`, and that is true of
MinGW-w64, which ships no sanitizer runtime — but it is not true of clang.

Two traps the activation script exists to avoid:

- The SDK autodetects `c++` then `g++`. On this machine `g++` is
  **DaisyToolchain's ARM cross-compiler**, which compiles for the module, not
  for the host. It fails in confusing ways rather than obvious ones.
- A clang-sanitized binary needs `clang_rt.asan_dynamic-x86_64.dll` on PATH to
  start. Without it every scenario dies and the SDK prints only
  `scenario hero failed:` with an empty reason.

MSYS2's g++ also works for everything except `--full`, but putting MSYS2 on PATH
shadows the Windows `python` with one that has no numpy, which breaks
`plaits-sweep`. Clang avoids the whole problem, so prefer it.

## What still doesn't work, and what it would cost

| Missing | Needed for | Notes |
| --- | --- | --- |
| `qemu-system-arm` | `qemu/estimate.py --sweep`, the calibrated CPU estimate | the only real pre-flight CPU number |
| `emcc` | live AudioWorklet audition in `dev` | `dev` falls back to render-and-listen, which works |
| ARM GCC **4.8.3** | `build --hardware`, real firmware | the Daisy 10.2.1 on PATH is the wrong version |

`check --full` also skips its own CPU smoke test here: the SDK's `cpu_bench.cc`
is built `-std=c++11` and the MSVC STL's `<ratio>` uses C++14 digit separators.
No loss — the SDK's own README says to treat that number as meaning nothing.

## Adding the optional tools without polluting anything

`activate.ps1` looks for these paths and adds them to PATH only if they exist:

```
.toolchain/qemu/                              -> qemu-system-arm
.toolchain/emsdk/upstream/emscripten/         -> emcc
.toolchain/gcc-arm-none-eabi-4_8-2014q3/bin/  -> arm-none-eabi-gcc
```

Unpack a portable build into one of those and it is picked up on next
activation; delete the folder and it is gone. For emsdk that is a `git clone`
into `.toolchain/emsdk` followed by `emsdk install latest` and `emsdk activate
latest` — **without** `--permanent`, which is the flag that would write to your
profile. Budget about 1.5 GB.

**The alternative, and probably the better one: WSL.** WSL2 is already enabled
on this machine (`wsl --status` reports Default Version 2) with no distro
installed. One `wsl --install -d Ubuntu` gives a Linux where `apt install qemu-
system-arm g++ python3-numpy` covers every gap at once, the repo is visible at
`/mnt/c/...`, and `wsl --unregister Ubuntu` removes the entire thing — one
container to reason about instead of three portable directories, and it is the
same Linux the reviewers' builder image runs.
