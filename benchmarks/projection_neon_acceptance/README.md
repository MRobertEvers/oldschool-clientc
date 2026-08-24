# AArch64 NEON projection acceptance and microbenchmark

This directory provides a narrow harness for the two hot, model-yaw, fused
no-clip entry points:

- `project_vertices_array_fused_noclip` (textured output)
- `project_vertices_array_fused_notex_noclip` (untextured output)

It includes the production AArch64 NEON selector and a privately renamed copy
of the scalar backend in one translation unit.  No projection arithmetic is
copied into the harness.

On an Apple arm64 host, the Makefile also links
`graphics/projection16_apple.S` when that file exists under `TORIDRAW_ROOT` and
enables a direct acceptance pass over its renderer-native entry points.  A
baseline `TORIDRAW_ROOT` without that assembly file remains buildable; the
program reports the native pass as skipped.

## Commands

Run the acceptance suite:

```sh
cd benchmarks/projection_neon_acceptance
make check
```

Run the 4-vertex microbenchmark:

```sh
cd benchmarks/projection_neon_acceptance
make benchmark
```

Run both with the defaults:

```sh
cd benchmarks/projection_neon_acceptance
make run
```

Choose the benchmark duration and odd sample count explicitly:

```sh
./bench_projection_neon_acceptance --benchmark-only \
  --iterations 5000000 --warmup 250000 --samples 9
```

Run the acceptance suite with UndefinedBehaviorSanitizer:

```sh
cd benchmarks/projection_neon_acceptance
make sanitize
```

On Apple arm64, benchmark the complete production projection dispatch against
the linked assembly path:

```sh
cd benchmarks/projection_neon_acceptance
make dispatch-benchmark
```

For a true before/after comparison, point the same source at a clean worktree
from before the optimization. This compiles the original portable dispatcher,
without the prepared-camera API or assembly:

```sh
cd benchmarks/projection_neon_acceptance
make baseline-dispatch-benchmark \
  BASELINE_TORIDRAW_ROOT=/absolute/path/to/clean-baseline/3rd/toridraw
./bench_projection_baseline_dispatch \
  --iterations 10000000 --warmup 500000 --samples 11
./bench_projection_native_dispatch \
  --iterations 10000000 --warmup 500000 --samples 11
```

`dispatch_bench.c` includes the ToriDraw unity unit so it can call the actual
static `toridraw_project_vertices_noclip` wrapper with real `ToriDraw_Scene`,
model-handle, position, and camera layouts.  One binary forces the portable
path by clearing `projection_prepared_camera_source`, then selects assembly by
preparing that same Scene for the same camera.  It validates both paths before
timing and alternates their order across an odd number of samples.

The `BENCH_PORTABLE_BASELINE` build instead includes the clean worktree's unity
unit and repeats its original portable path in both timing slots. The reported
`clean baseline` result is therefore independent of any inlining or outlining
choice introduced by the optimized wrapper.

The dispatch benchmark reports an exact-four workload and a deterministic
1,000-call shuffle containing 589 exact-four calls plus 411 calls distributed
over counts 5, 6, 7, 9, 15, 31, and 63.  Textured and untextured medians are
also combined with the same 0.3551/0.6449 workload weighting used below.  Its
large Scene-containing fixtures are heap allocated; no multi-megabyte Scene is
placed on the macOS thread stack.

The build deliberately fails outside AArch64 or when the production NEON
backend is disabled.

## Acceptance contract

For vertex counts 0 through 9, both fused functions are compared against the
scalar backend.  The suite requires:

- exact textured orthographic x/y/z;
- exact `screen_z` in both variants;
- exact screen x/y for scalar residual lanes;
- screen x/y error no greater than 4 pixels for complete four-lane NEON groups;
- no input mutation and no output write before or beyond the requested count,
  checked with canaries; and
- valid no-clip inputs with scalar projected coordinates inside +/-8192.

Coverage includes signed quotient thresholds, a near-plane reciprocal boundary
case, every possible tail length, and 256 fixed-seed randomized bounded cases.
The random seed is printed by the program for reproducibility.

When the Apple assembly is linked, its textured and untextured native kernels
are called directly for counts 0 through 9 using the private renderer ABI: six
contiguous output pointers, followed by five fully initialized four-lane camera
vectors.  They are checked against the scalar backend with the same exact and
vector-tolerance rules and the same input/output canaries.  An additional
`model_yaw == 0` case poisons sine/cosine table entry zero while it runs, proving
that zero yaw preserves the no-model-rotation semantics rather than treating
table entry zero as an ordinary rotation. A nonzero-yaw case also selects
custom sine/cosine tables, mutates and reselects the same pointers, and restores
the built-ins with `NULL`, covering the derived interleaved-table lifecycle.

The suite also calls each shared z-div tail helper directly for remainder counts
0 through 3: textured and untextured, clip and no-clip.  It checks exact scalar
division, `screen_z`, near clipping, the `-5000`/`-5001` nudge, and surrounding
canaries.  The canaries are the harness's adjacent-write check; the sanitizer
target adds arithmetic and other undefined-behavior diagnostics.

## Benchmark reporting

The benchmark times exactly four vertices per call through each production
entry point.  The production inline function is inside a non-inlined per-call
wrapper whose count and projection settings are runtime arguments.  This keeps
the trigonometric reads, NEON setup, and stores inside every timed call rather
than letting the compiler specialize the whole loop for a constant fixture.
The small wrapper-call overhead is present in both baseline and candidate
measurements.  The program reports median ns/call for textured and untextured
calls, plus this workload-weighted aggregate:

```text
0.3551 * textured + 0.6449 * untextured
```

The executable prints its compiler and host labels with every run.  Results are
host-, compiler-, build-flag-, thermal-, and power-state-specific.  The harness
contains no claimed before/after measurements; collect each revision under the
same conditions if a comparison is needed.

On macOS the CPU label comes from `machdep.cpu.brand_string`; on Linux it comes
from `/proc/cpuinfo`.  If that probe is unavailable, set an explicit label:

```sh
BENCH_CPU_LABEL="lab-aarch64-host" ./bench_projection_neon_acceptance --benchmark-only
```

`TORIDRAW_ROOT` and `TARGET` are overrideable so one harness checkout can build
the same test against two worktrees.  Set the first two paths to your existing
clean baseline and candidate worktrees, then run these commands exactly:

```sh
BASELINE_WORKTREE=/absolute/path/to/clean-baseline
CANDIDATE_WORKTREE=/absolute/path/to/candidate
cd "$CANDIDATE_WORKTREE/benchmarks/projection_neon_acceptance"
make TARGET=bench_baseline TORIDRAW_ROOT="$BASELINE_WORKTREE/3rd/toridraw"
make TARGET=bench_candidate TORIDRAW_ROOT="$CANDIDATE_WORKTREE/3rd/toridraw"
./bench_baseline --acceptance-only
./bench_candidate --acceptance-only
./bench_baseline --benchmark-only
./bench_candidate --benchmark-only
```

The two executables have distinct names, so both remain available.  Use the
same compiler, flags, host, iteration count, and sample count for the comparison;
the program labels compiler and host in its output.

This is a synthetic kernel microbenchmark, not a scene or frame benchmark.  It
includes the public inline wrapper, trigonometric-table loads, transforms,
stores, and z division, but excludes caller work such as clipping decisions,
allocation, rasterization, and real-scene memory behavior.  Canary checks catch
ordinary adjacent overwrites, and the sanitizer target is not used for timing.
