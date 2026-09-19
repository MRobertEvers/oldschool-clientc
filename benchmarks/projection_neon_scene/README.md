# AArch64 NEON scene projection benchmark

This compares the current ToriDraw production NEON entry points with four
fully hand-written AArch64/Advanced SIMD kernels:

|                     | Textured (writes orthographic XYZ) | Not textured |
|---------------------|-------------------------------------|--------------|
| Fused transform/div | `fused/tex`                         | `fused/notex`|
| Two-pass transform/div | `unfused/tex`                  | `unfused/notex` |

The default workload decodes a deterministic, evenly distributed sample of
real OSRS revision-239 `model_*.model` archives. It keeps the decoded vertex
counts, coordinate distributions, tail sizes, and textured/notextured split.
Placement is deterministic camera-space scene placement so runs do not inherit
the full client's asynchronous asset-load order. This is therefore a stable
real-geometry scene benchmark, not a capture of one particular rendered frame.

The assembly uses the same signed 16.16 sine/cosine values as ToriDraw's tables
and the same staged rounding as the renderer. It also matches the current
`vrecpe` + one Newton-Raphson refinement, clipping sentinel, and `-5001` nudge.
Every output lane is compared against the current implementation before timing;
the benchmark exits on the first mismatch.

These are the production `*_clip` families. The scene arguments hold the six
table values pre-resolved, matching the proposed "precomputed transform"
contract; output pointers and constants are prepared before the timed loop.
The production baseline remains the current header implementation unchanged.

Run on an AArch64 host from this directory:

```sh
make run
```

Options:

```sh
./bench_projection_neon_scene \
  --models ../../OSRS-Content/osrs239-content/models \
  --model-count 256 \
  --repetitions 100 \
  --samples 9
```

Input/output allocations are rounded up to four lanes and the loader duplicates
the final source vertex into padding. This permits the unfused assembly to match
the production padded-vector divide helper safely. The fused assembly uses a
hand-written scalar tail, matching the production fused path's integer divide.
Validation and checksums use only the model's declared vertex count, and tail
cost remains part of every timing.
