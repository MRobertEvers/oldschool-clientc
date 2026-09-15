# Assessing a sub-10 ms median on the XT1060

The measured optimized uncapped median is 12.8645 ms. Reaching 10 ms requires
another 2.8645 ms, or a 22.27% reduction in the current whole-frame median.
The completed optimization pass saved 2.2425 ms in that comparison, so the
remaining target requires a larger absolute saving than that pass delivered.
It is a plausible stretch target, not an established outcome or a promise.
The normal application's approximately 20 ms paced cadence is a separate cap.

## What the current trace actually measures

The following partition uses **means**, calculated from the same 1,080
optimized frames, so the components add exactly. Marginal medians cannot be
added or subtracted to make a stage breakdown.

| Measured interval | Mean |
|---|---:|
| Whole main-loop frame, excluding artificial pacing | 14.2763 ms |
| Inside EGL buffer swap | 1.0692 ms |
| Everything outside the swap | 13.2071 ms |

The last row includes updates, scene construction, UI, model processing,
driver submission outside swap, audio and any waits in those paths. It is
not exclusively CPU execution time. The swap interval is not total GPU time:
GPU execution overlaps CPU work, and queue backpressure can occur elsewhere.
There were no sub-10 ms frames in either arm of this captured uncapped run.

The current trace does not contain separate stage timings. The renderer-only
PMU measurements were separate runs and count work on both threads; they
cannot be converted into an additive partition of the frame's elapsed time.

## Fresh final-build hardware profile — 2026-09-06

After Android finished booting, both hardware captures succeeded in the live
uncapped Lumbridge scene. The normal installed native library matched the
local symbols and the library used for the final normal APK, SHA-256
`f29aa1d82291afe975282a2870c836c9939f33f770075225c99d28d4382cafc7`.
Screenshots confirm the world and camera, with the setup panel absent.

- CPU cycles: 38,309 samples, zero loss; one sample per 500,000 events.
- L1 data-cache load misses: 21,031 samples, zero loss; one sample per 5,000 events.
- Each capture follows 600 rendered warmup frames and samples for 20 seconds.
  The duration bounds collection; it is not the profiling event.
- Detailed software profiling is disabled. No renderer code was changed for
  these captures, and no cycle-to-millisecond conversion is used.

| Selected flat/self symbol group | CPU-cycle samples | L1-miss samples |
|---|---:|---:|
| UI emission plus canvas/chrome width queries | 10.06% | 21.81% |
| Actor baking and packing helpers | 7.38% | 7.26% |
| General model-face ordering, main and worker | 5.76% | 8.11% |
| Pose transforms | 5.02% | 1.45% |
| Scene painter plus dynamic registration | 5.43% | 5.56% |
| memcpy | 4.43% | 20.20% |

The two width queries alone account for 5.34% of cycle samples and 15.36% of
L1-miss samples. They already memoize answers. Their broad dirty/layout
generation keys and full-array miss paths make invalidation and repeated scans
the first concrete target to inspect. These counters establish concentrated
cache traffic; they do not identify DRAM stalls or prove the caching fix yet.

The main thread accounts for 85.28% of cycle samples, the model worker 10.12%,
audio 3.73%, and other threads 0.87%. This remains a main-thread-heavy workload.
The copy samples cannot be assigned to callers using this flat profile alone.
Actor baking still gathers attributes and packs vertices after world-coordinate
reuse, so its remaining 7.38% is not a claim that the old transform work remains.

These selected groups do not sum to an exhaustive subsystem breakdown. Both
columns are hardware-event shares across application threads, **not percentages
of elapsed frame time**. GPU execution, kernel work, and blocked time are not
represented by userspace cycle samples. The finer current millisecond split
therefore remains unmeasured; the exact whole-frame/swap partition above is
still the valid wall-time evidence.

Evidence: [group definitions and counts](experiments/sub10-profile-breakdown.json),
[CPU report](profiles/final-uncapped-live-cycles-report.csv),
[cache-miss report](profiles/final-uncapped-live-l1d-report.csv),
[CPU capture metadata](profiles/final-uncapped-live-cycles-metadata.json),
[cache capture metadata](profiles/final-uncapped-live-l1d-metadata.json).
Compressed perf.data files and screenshots are stored alongside those reports.

## Historical hardware profile, retained for comparison

The earlier detailed live profile is
`profiles/post-pose.data.gz`, with metadata in
`profiles/post-pose-metadata.json`. It contains 136,296 userspace CPU-cycle
samples with zero reported loss. It was collected after pose preparation but
before the final actor-bake and shader optimizations. Its percentages must
not be presented as the fully optimized build's current cost distribution.

Selected flat/self sample shares across all sampled application threads:

| Functions | Share |
|---|---:|
| UI emission plus the two canvas/chrome width queries | 11.14% |
| Scene painter plus dynamic painter registration | 6.08% |
| General face ordering, main and worker combined | 6.07% |
| memcpy | 4.70% |
| Pose transforms | 3.89% |
| Actor face baking plus pose-vertex baking, before final cache | 9.10% |

These are selected symbols, not exhaustive subsystem totals or inclusive
call-tree costs. Copy callers cannot be assigned from flat samples alone.
The main thread accounts for 82.67% of that profile's sampled cycles, the
model worker 14.05%, and other threads the remainder. Parallel worker savings
do not necessarily shorten the main thread's critical path.

## Where to investigate next

1. The final normal build's hardware reprofile is complete. Use its main-thread
   costs and cache-miss concentrations to select the next changes; validate
   each accepted change against whole-frame time in the same scene.
2. Investigate UI emission and canvas/chrome measurement invalidation first.
   The two width functions already memoize their results, but a miss scans
   the component array. Their keys include broad tree dirty and layout
   generations. Measure whether unrelated changes keep invalidating them;
   do not propose adding a cache that already exists.
3. Reassess remaining renderer data movement, actor work, command submission,
   and worker waiting against the fresh profile. The old actor-bake costs
   have already been addressed and are not available savings a second time.
4. Keep scene-painter changes excluded as instructed. Treat additional
   projection/sort tuning as useful only when it shortens the frame's
   critical path. Preserve painter order and disabled depth buffering.

The earlier launch failure cleared after Android completed booting. Both fresh
captures above succeeded. Original launch settings were restored and verified;
the temporary server, tunnel, manifest, capture files and test save were removed.
The installed app remains the normal optimized APK.
