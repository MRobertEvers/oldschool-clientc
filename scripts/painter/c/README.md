# Painter Fuzzer and Benchmarks

Two standalone C console tools for validating and measuring the painter traversal algorithms in `src/osrs/painters.c`.

---

## Tools

### `fuzz_real` — differential fuzzer + same-seed benchmark

Compiles the real engine (`src/osrs/painters.c` + `src/graphics/shared_tables.c`) and exercises both `painter_paint_world3d` (reference) and `painter_paint_bucket` against each other.

**Correctness invariant**: every terrain tile and element drawn by `world3d` must also be drawn by `bucket` (the bucket version may draw more or in a different order).

**Subcommands:**

```
./fuzz_real <start> <count>              fuzz <count> seeds starting at <start>
./fuzz_real <seed> 1 shrink             shrink a failing seed to a minimal repro
./fuzz_real <start> <count> bench [N]   same-seed benchmark, N iters each (default 200)
```

**Common invocations:**

```sh
# Run the full 7000-seed superset-invariant check
./fuzz_real 1 7000

# Regression test: seed 135 (known-good reproducer)
./fuzz_real 135 1 shrink

# Performance benchmark: 500 seeds x 500 iters, aggregate ratio
./fuzz_real 1 500 bench 500
```

### `bench_painter` — standalone synthetic benchmark

Exercises the local stub implementations (`painter_bucket.c`, `painter_world3d.c`) on hardcoded scenes. Faster to compile than `fuzz_real` since it does not pull in the full engine.

```sh
./bench_painter
```

### `bench_real` — real-engine scene benchmark

Benchmarks `painter_paint_world3d` vs `painter_paint_bucket` on scenes built with the real painter API (seeded procedural scenes) and, with the cache build, on a loaded dat2 centerzone world.

**Light build (seeded scenes only):**

```sh
make bench_real
./bench_real [start] [count] [iters]    # defaults: 1, 500, 200
```

**Heavy build (seeded + dat2 cache world):**

```sh
make bench_real_cache
./bench_real_cache [start] [count] [iters]           # seeded
./bench_real_cache cache [dir] [iters]               # dat2 world (dir auto-detected if omitted)
```

**Common invocations:**

```sh
# 500 seeds x 200 iters, aggregate ratio
./bench_real 1 500 200

# Dat2 centerzone: 72 camera sweeps (3×3 positions × 4 yaws × 2 pitches × 2 level masks) × 200 iters
./bench_real_cache cache ../../../cache 200
```

Cache-world loads skip dat2 texture decode (not needed for painter traversal); map/scenery data comes from the world-rebuild task instead. Sequences referenced by loaded locs are fetched on demand (the full sequence archive is not bulk-decoded).

An incomplete cache may print `Failed to resolve dat2 map archive m*_*.` for missing neighbor chunks; the centerzone bench still runs. A usable cache needs `xteas.json` and map data for zone center (313, 437).

The same seeded benchmark is still available via `fuzz_real ... bench` (shared harness). `fuzz_real` remains the tool for correctness fuzzing and shrinking.

---

## Host build (macOS / Linux)

```sh
cd scripts/painter/c
make           # builds bench_painter
make fuzz_real # builds fuzz_real
make bench_real # builds bench_real (real engine, seeded scenes)
```

Both binaries land in the current directory. The host build uses the system compiler (`cc`) with `-O3`.

```sh
make clean     # removes bench_painter, bench_real, bench_real_cache, fuzz_real, and dist/
```

---

## Windows cross-build (from macOS or Linux)

The Makefile adds three targets that cross-compile static `.exe` files — no DLLs required on the target machine.

| Target | Executable | Architecture | Minimum Windows |
|--------|-----------|--------------|-----------------|
| `winxp` | `dist/winxp/fuzz_real.exe`, `dist/winxp/bench_painter.exe`, `dist/winxp/bench_real.exe` | 32-bit (i686) Pentium 4 / SSE2 | Windows XP SP3 |
| `win64` | `dist/win64/fuzz_real.exe`, `dist/win64/bench_painter.exe`, `dist/win64/bench_real.exe` | 64-bit (x86_64) | Windows Vista+ |
| `windows` | both of the above | — | — |

### Prerequisites

Install a MinGW-w64 cross toolchain on your host:

**macOS (Homebrew):**
```sh
brew install mingw-w64
```

**Debian / Ubuntu:**
```sh
sudo apt install mingw-w64
```

This installs both `i686-w64-mingw32-gcc` (needed for `winxp`) and `x86_64-w64-mingw32-gcc` (needed for `win64`).

#### Vendored / custom toolchain

If you use a downloaded [winlibs](https://winlibs.com/) toolchain instead of a system package, override the compiler variables:

```sh
# 32-bit WinXP build with a vendored i686 winlibs tree
make winxp WINXP_CC=/path/to/winlibs-i686/mingw32/bin/gcc.exe

# 64-bit build with a vendored x86_64 winlibs tree
make win64 WIN64_CC=/path/to/winlibs-x86_64/mingw64/bin/gcc.exe
```

The vendored path documented elsewhere in this repo is:
```
C:\Users\mrobe\Downloads\winlibs-i686-posix-dwarf-gcc-15.2.0-mingw-w64msvcrt-13.0.0-r2\mingw32
```
(translate to `/c/Users/…` when building inside an MSYS2 shell on Windows).

### Build commands

```sh
cd scripts/painter/c

make winxp          # 32-bit XP + SSE2 only
make win64          # 64-bit modern only
make windows        # both
```

**Optional: enable AVX2 for the 64-bit build** (modern CPUs only, not XP):
```sh
make win64 WIN64_ISA=-mavx2
```

Outputs:
```
dist/
  winxp/
    fuzz_real.exe
    bench_painter.exe
    bench_real.exe
  win64/
    fuzz_real.exe
    bench_painter.exe
    bench_real.exe
```

Both directories are independent — you can copy either to the target machine without the other.

### Running on the target machine

Copy the `.exe`(s) to any directory on the Windows machine (no DLL installation needed — everything is statically linked).

**Windows XP requirements:**
- Windows XP SP3 (32-bit)
- Pentium 4 or later CPU (SSE2 required)

**Run from `cmd.exe`:**
```cmd
cd C:\wherever\you\copied\the\exe

fuzz_real.exe 1 7000
fuzz_real.exe 135 1 shrink
fuzz_real.exe 1 500 bench 500

bench_painter.exe
bench_real.exe 1 500 200
```

The CLI is identical to the host build.

---

## Compiler flags explained

### WinXP target (`winxp`)

| Flag | Effect |
|------|--------|
| `-march=pentium4 -msse2 -mfpmath=sse` | Target Pentium 4; use SSE2; use SSE registers for FP math |
| `-D_WIN32_WINNT=0x0501 -DWINVER=0x0501` | Windows XP API level |
| `-static` | Statically link libgcc, libstdc++, and winpthreads — single self-contained `.exe` |
| `-Wl,--subsystem,console:5.01` | PE subsystem version 5.01 — required for Windows XP to load the binary |

The `-msse2` flag defines `__SSE2__`, so the SSE2 code paths in the engine (e.g. `painters_bucket_simd.sse2.u.c`) activate automatically. AVX and NEON paths are not compiled.

### Win64 target (`win64`)

| Flag | Effect |
|------|--------|
| `-D_WIN32_WINNT=0x0600` | Windows Vista+ API level |
| `-static` | Single self-contained `.exe` |
| `WIN64_ISA` | Optional extra ISA flag (e.g. `-mavx2`); empty by default |

x86_64 implies SSE2, so the SSE2 code paths activate. AVX2 paths activate only when `WIN64_ISA=-mavx2` is set.

---

## Troubleshooting

### `i686-w64-mingw32-gcc: not found`

The 32-bit cross compiler is missing. Install it with `brew install mingw-w64` or `apt install mingw-w64`, or override `WINXP_CC` to point at a downloaded winlibs i686 toolchain.

### `x86_64-w64-mingw32-gcc: not found`

Same issue for the 64-bit toolchain. Install `mingw-w64` or override `WIN64_CC`.

### Link error: `undefined reference to clock_gettime`

This can happen with a stripped-down MinGW distribution that lacks the winpthreads runtime. Modern MinGW-w64 (winlibs, MSYS2 packages, or distribution packages from Homebrew/apt) includes `clock_gettime` and links correctly with `-static`. If you hit this, upgrade to a current MinGW-w64 toolchain. As a workaround, `-static` can be replaced with `-static-libgcc` (dynamic runtime) which avoids linking winpthreads entirely and lets Windows supply its own timing implementation.

### `.exe` crashes immediately on Windows XP

Verify:
1. You ran the `winxp` target (not `win64`).
2. The CPU supports SSE2 (Pentium 4 or later).
3. You are on Windows XP SP3 (earlier service packs lack the required API surface).
