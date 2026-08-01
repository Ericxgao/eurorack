# Plaits Lab local toolchain — activate for THIS SHELL ONLY.
#
#   . alt_firmwares\plaits_lab_sdk\.toolchain\activate.ps1
#
# (note the leading dot — the script must be dot-sourced to affect your shell)
#
# Nothing is installed, nothing is written to your profile, nothing touches the
# registry, and no service is started. It sets $env:PATH for the current session
# and defines two functions. Close the window and every trace is gone.
#
# It prefers clang, which is the only compiler on this machine that can link the
# sanitizers `check --full` needs. MSYS2's g++ works for everything else but
# ships no sanitizer runtime — and putting MSYS2 on PATH also shadows the
# Windows `python`, which is the one with numpy.
#
# Optional tools are picked up from this directory if present but never
# downloaded; see README.md here for what to unpack where.

$ErrorActionPreference = "Stop"
$toolchain = $PSScriptRoot
$sdk = Split-Path $toolchain -Parent
$repo = Split-Path (Split-Path $sdk -Parent) -Parent

# --- host C++ compiler ------------------------------------------------------
# $env:PLAITS_LAB_CXX wins, so an unusual install needs no edit here.
if (-not $env:PLAITS_LAB_CXX) {
    $candidates = @(
        "C:\Program Files\LLVM\bin\clang++.exe",
        "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang++.exe",
        "C:\msys64\ucrt64\bin\g++.exe",
        "C:\msys64\mingw64\bin\g++.exe"
    )
    foreach ($c in $candidates) {
        $hit = Get-Item $c -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { $env:PLAITS_LAB_CXX = $hit.FullName; break }
    }
}
if (-not $env:PLAITS_LAB_CXX) {
    Write-Warning "no host C++ compiler found; set `$env:PLAITS_LAB_CXX yourself"
}

# --- sanitizer runtime ------------------------------------------------------
# A binary built with clang's -fsanitize=address needs clang_rt.asan_dynamic
# beside it or on PATH. Without this every scenario dies at startup and the SDK
# reports only "scenario <id> failed:" with an empty reason.
$asan = Get-ChildItem "C:\Program Files\LLVM\lib\clang" -Recurse `
    -Filter "clang_rt.asan_dynamic-x86_64.dll" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($asan) { $env:PATH = "$($asan.DirectoryName);$env:PATH" }

# --- optional, unpacked into this directory by hand -------------------------
$optional = [ordered]@{
    "qemu"      = Join-Path $toolchain "qemu"
    "emscripten"= Join-Path $toolchain "emsdk\upstream\emscripten"
    "arm-4.8.3" = Join-Path $toolchain "gcc-arm-none-eabi-4_8-2014q3\bin"
}
$found = @()
foreach ($name in $optional.Keys) {
    if (Test-Path $optional[$name]) {
        $env:PATH = "$($optional[$name]);$env:PATH"
        $found += $name
    }
}

# --- commands ---------------------------------------------------------------
# Injecting --compiler here rather than exporting CXX, because the SDK does not
# read CXX: it autodetects `c++` then `g++`, and on this machine `g++` is the
# DaisyToolchain ARM cross-compiler, which builds nothing runnable.
$script:PlaitsSdkPath = Join-Path $sdk "plaits_lab.py"
$script:PlaitsSweepPath = Join-Path $sdk "diagnostics\sweep_engine.py"

function global:plaits-lab {
    $takesCompiler = @("check", "render", "submit", "dev")
    $argv = @($args)
    if ($argv.Count -gt 0 -and $takesCompiler -contains $argv[0] `
        -and $argv -notcontains "--compiler" -and $env:PLAITS_LAB_CXX) {
        $argv += @("--compiler", $env:PLAITS_LAB_CXX)
    }
    & python $script:PlaitsSdkPath @argv
}

function global:plaits-sweep {
    & python $script:PlaitsSweepPath @args
}

# --- report -----------------------------------------------------------------
Write-Host ""
Write-Host "Plaits Lab toolchain active in this shell" -ForegroundColor Cyan
Write-Host "  compiler   $env:PLAITS_LAB_CXX"
Write-Host ("  sanitizers " + $(if ($asan) { "yes (check --full works)" }
                                else { "no  (check --full needs Docker)" }))
Write-Host ("  optional   " + $(if ($found) { $found -join ", " } else { "none installed" }))
Write-Host ""
Write-Host "  plaits-lab check <package> --full"
Write-Host "  plaits-lab dev <package>"
Write-Host "  plaits-sweep <package> --control timbre"
Write-Host ""
