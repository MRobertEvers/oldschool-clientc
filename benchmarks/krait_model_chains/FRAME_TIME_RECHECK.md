# Live XT1060 frame-time comparison

Measured on the connected Krait 300 / Adreno 320 XT1060 after renderer commit
`e2f24c3dd`, with parent checkout `152dc6469` plus the timing-only probe.
The scene is the live local Lumbridge world with player and animated NPCs.
Scene-painter code and depth-buffer behavior are unchanged.

## Frame time in milliseconds

Each run contains 12 ABBA windows of 180 frames: 1,080 before and 1,080 after.
There are 600 warmup frames and six settling frames between windows. Before
and after select the reference and optimized runtime paths in the same binary;
common allocation alignment and refactoring remain in both arms. This is not
a comparison against a separately linked untouched historical executable.

| Configuration and metric | Before | After | Change |
|---|---:|---:|---:|
| Uncapped whole-frame median | 15.107 ms | 12.865 ms | -14.8% |
| Uncapped whole-frame mean | 16.676 ms | 14.276 ms | -14.4% |
| Uncapped whole-frame p95 | 25.639 ms | 23.472 ms | -8.5% |
| Uncapped whole-frame p99 | 30.497 ms | 29.249 ms | -4.1% |
| Uncapped completed-swap median interval | 15.108 ms | 12.880 ms | -14.7% |
| Normal pacer whole-frame median | 16.176 ms | 14.329 ms | -11.4% |
| Normal pacer whole-frame p95 | 27.837 ms | 24.968 ms | -10.3% |
| Normal pacer completed-swap median interval | 19.564 ms | 19.594 ms | +0.2% |
| Normal pacer completed-swap mean interval | 20.814 ms | 20.503 ms | -1.5% |
| Normal pacer completed-swap p95 interval | 30.951 ms | 30.398 ms | -1.8% |
| Normal pacer completed-swap p99 interval | 40.979 ms | 39.356 ms | -4.0% |

The optimization reduces the work needed for a frame. Under the normal pacer,
most of that saving becomes additional waiting: median frame delivery remains
about 20 ms. The uncapped result exposes the rendering headroom. It does not
establish a corresponding increase in physical display refresh rate.

Whole-frame work starts before input polling and includes game updates, scene
construction, UI, rendering, buffer swap waits and audio, ending before the
artificial pacing wait. Cadence uses timestamps immediately after actual EGL
swaps, so skipped draws and pacing are included. These are application frame
and submission times, not photon/scanout latency or isolated GPU execution time.
No per-frame GPU drain was added; ordinary asynchronous GPU queue behavior is
preserved. EGL swap interval is requested as zero in both arms.

The dedicated diagnostic buffers timestamps, then writes them after all
measurement windows. The detailed stage profiler and PMU ioctls are disabled
for timing. Hardware counters remain separate evidence, rather than being
converted to milliseconds.

All three uncapped ABBA blocks improve median whole-frame time: 12.1%, 15.9%
and 16.3%. Mean model commands per frame differ by only 0.014% between arms,
although NPC state and movement are not deterministic. Screenshots confirm the
live world and matching camera, with the setup panel absent.

## Evidence

- [Uncapped statistics](experiments/recheck-live-uncapped-frame-times.json),
  [every frame](experiments/recheck-live-uncapped-frame-times.csv),
  [scene screenshot](experiments/recheck-live-uncapped-frame-times.png).
- [Normal-pacer statistics](experiments/recheck-live-paced-frame-times.json),
  [every frame](experiments/recheck-live-paced-frame-times.csv),
  [scene screenshot](experiments/recheck-live-paced-frame-times.png).
- [CPU-cycle recheck](experiments/recheck-live-cycles.json): renderer cycles
  decrease 30.14%, versus 33.44% in the earlier live test.
- [Instruction recheck](experiments/recheck-live-instructions.json): renderer
  instructions decrease 31.20%.
- [Fixed Grand Exchange CPU recheck](experiments/recheck-ge-cycles.json):
  cycles decrease 11.35%, versus 13.20% previously; both arms process exactly
  4,224 model commands per frame.

The `recheck-onboarding-excluded` artifacts retain the initial setup run. Its
screenshot showed character creation; it is excluded from gameplay conclusions.
The test account was then initialized and teleported to Lumbridge before the
accepted captures.

CPU-cycle and instruction reductions cover the renderer's draw and worker
threads, while the frame timer covers the larger main-loop workload. They
therefore have different denominators; the 30% renderer-cycle result is not
a claim of 30% lower frame time.

The normal APK was rebuilt and installed after measurement. Diagnostic markers
are absent from its native library. Original phone launch settings were verified
restored, and the temporary server, tunnel, manifests and test-account save were
removed; see [cleanup verification](experiments/recheck-cleanup.json).
