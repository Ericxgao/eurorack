# Minimal wasm32 sysroot

Six headers that let a plain clang build the live-audition module for the
`wasm32-unknown-unknown` target, so live audition needs no Emscripten.

Clang ships `stddef.h` and `stdint.h` for every target but no C++ library, and
`wasm_audition.cc` plus the plaits/stmlib headers it pulls in reference only
these six names. Nothing here implements anything: they forward to clang's own
builtin headers, add the `std::` aliases, and declare placement new. The
firmware is bare metal and links no libm, so `math.h` only has to carry the
constants the headers mention.

If a future engine needs a header that is not here, the build fails with a plain
"file not found" naming it, and the fix is usually another four-line forwarder.
Anything needing a real implementation is a sign the engine is reaching for
something the hardware does not have either.
