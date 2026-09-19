# Krait actor packing: RGB reversal and metadata word stores

The post-P3 CPU profile still attributed 5.60% of all-thread userspace cycle
samples to the direct untextured actor encoder. ARM code inspection showed
mask/shift/or sequences for RGB conversion and multiple byte/halfword stores
for four adjacent constant tile/animation bytes.

The candidate preserves the 28-byte vertex layout. It uses byte reversal plus
an eight-bit shift to convert RGB, ORs in the separately computed alpha, and
writes `{0,0,128,128}` to the contiguous metadata bytes with a constant-size
copy which becomes a word store. A static assertion pins field adjacency.
The RGB identity holds for every input bit pattern, including an arbitrary
high byte in the palette entry; the shift drops that byte. The emitted ARM
code contains three `rev` instructions and no byte/halfword stores in the
encoder. Stack scratch drops from 16 to 4 bytes in the inspected build.

Reference and candidate instantiate one shared encoder body. The original
encoder remains the rollback. Textured/unsupported models keep the generic
path. No face ordering, vertex layout, clipping, alpha, picking, UVs, capacity
checks or dirty-range behavior changes.

## Independent filters

Every variant passed 3,616 fixture comparisons and verification of 128 captured
model bakes / 50,375 ordered faces. Each hardware run counts complete real bake
chains in twelve ABBA windows, including world-coordinate preparation.

| Variant | Cycles | Instructions | Decision |
|---|---:|---:|---|
| Non-aliasing output annotation | -1.96% | -1.48% | Not promoted; marginal replay improvement |
| Four metadata bytes as a word | -4.73% | -7.47% | Ingredient validated independently |
| RGB byte reversal | -3.70% | -11.19% | Ingredient validated independently |
| Combined scalar packing | -10.98% | -18.65% | Advanced to live app validation |

These percentages are measured independently, not added. Replay filenames are
`experiments/sub10-{noalias,store32,rev,words}-{cpu-cycles,instructions}.log`.
The live shadow build then matched 33,100 actor models / 12,865,313 complete
packed faces against the generic GLES2 writer (`sub10-words-live-verify/`).

## Whole-frame work

All preceding positive Krait changes, including P3, are enabled in both arms.
Only the packing variant changes. Protocol: 600 warmup frames, twelve ABBA
windows of 180 frames, six settling frames, uncapped; no PMU ioctls or detailed
software profiling in timestamp runs. Raw rows, source/APK hashes, switch
matrices and device conditions are preserved in the corresponding JSON/CSV.

| Launch | Median A → B | Mean A → B | p95 A → B | p99 A → B |
|---|---:|---:|---:|---:|
| A/A | 11.384 → 11.384 | See raw JSON | See raw JSON | See raw JSON |
| A/B 1 | 11.171 → 10.956 | 12.339 → 11.954 | 20.452 → 20.027 | 27.699 → 24.779 |
| A/B 2 | 10.987 → 10.744 | 12.103 → 11.865 | 20.393 → 20.634 | 24.734 → 25.596 |

All numbers are milliseconds. Both launches and all six ABBA blocks improve
the median; the run-level changes are -1.92% and -2.22%. A/A's aggregate median
difference was zero, but its individual block differences ranged from -0.382
to +0.305 ms: the small gain should be interpreted with that variability, not
as a zero-noise measurement. Tails improve in the first run and worsen in the
second; no universal tail improvement or final sub-10 acceptance is claimed.

At the user's request to enable positive results, `TORIRS_GLES2_ACTOR_WORDS`
now defaults on for ARM32 NEON. `=0` retains the prior direct encoder.
`sub10-words` isolates this change; `sub10-words-aa` changes nothing. The old
diagnostic targets pin the new flag off to preserve their historical baselines.

**Sub-10 is not achieved.** These candidate medians leave approximately
0.74–0.96 ms of whole-frame work to remove in this workload.

## Width-summary experiment retired

The post-P3 compact-query helper still accounted for 2.49% of cycle samples;
PC attribution placed most of that in candidate evaluation rather than list
construction. A tree-owned paired result memo was tested with conservative
dirty/layout/topology/dimension keys and pending-layout fallback. Fixtures
passed and repeated snapshot queries were much cheaper in replay, but app
median work regressed 11.171 → 11.323 ms (+1.36%), with all three blocks worse.
The corresponding A/A was 11.140 → 11.201 ms (+0.55%). The implementation was
removed; raw `sub10-summary-*` evidence remains. A replay hit-path improvement
is not an app gain. Narrower query dependency invalidation remains separate,
unimplemented work.
