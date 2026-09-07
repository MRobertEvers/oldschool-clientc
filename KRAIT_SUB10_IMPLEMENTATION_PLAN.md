# Krait implementation plan: sub-10 ms median frame work

Status: in progress, 2026-09-06. P0 comparison/capture tooling and copy inventory,
P1 compact candidate membership, and P2 untextured actor direct encoding are
implemented. The new live packed-byte verifier passes. Three combined launches
improve median whole-frame work by 4.6–6.0%, to 12.453, 12.788 and 12.284 ms; **sub-10 is
not achieved**. Candidates remain opt-in while acceptance is incomplete.

See [implementation progress and rejected experiments](benchmarks/krait_model_chains/SUB10_PROGRESS.md).
The next P3 target is stable UI subtree retention: real traces show scripted
entity-overlay geometry changes invalidating the whole emit every frame.
Host volatile-input changes are not the dominant retention blocker.

## Objective and fixed constraints

Reach **less than 10.00 ms median whole-frame work** on the XT1060, using the
same uncapped live Lumbridge workload, resolution, camera and visual settings
as the measured 12.8645 ms result. Aim for 9.5 ms during development to leave
margin, but do not count that margin as an expected saving.

Whole-frame work includes updates, scene construction, UI, rendering, normal
buffer-swap waits and audio, before artificial pacing. Completed-swap cadence
is a separate metric. Neither is physical scanout latency. Normal pacing can
continue producing approximately 20 ms intervals even after work is below 10 ms.

- Do not change the scene painter, its traversal, occlusion or ordering rules.
- Keep depth testing and depth writes disabled. Preserve exact model face order,
  priority behavior, clipping, alpha, textures, picking and UI interaction.
- Keep game tick rate, draw distance, resolution and content unchanged. Do not
  obtain the result by disabling audio, overlays, animation or rendering work.
- Hardware performance counters are the profiling mechanism. Direct whole-frame
  timestamps are the separately requested outcome measurement. Do not convert
  CPU-cycle shares into milliseconds or add parallel threads' times together.
- Preserve normal governor/pacing behavior for the corresponding test mode;
  record device conditions rather than overclocking or changing thermal policy.
- Keep changes independent of ongoing plugin/UI feature work. Record the source
  revision and local changes used for each experiment; never absorb unrelated
  modifications into a performance comparison accidentally.

## Evidence and opportunity

The optimized uncapped median is **12.8645 ms**. The remaining reduction to
10 ms is **2.8645 ms / 22.27%**. The previous pass saved 2.2425 ms in the same
comparison. The next target is therefore substantial, not a final minor tuning
step. The current evidence does not provide a defensible per-phase ms budget.

The same optimized trace has mean work 14.2763 ms, of which 1.0692 ms is inside
EGL swap and 13.2071 ms is outside it. These means add; marginal medians do not.
Swap duration is not total GPU execution time.

Fresh final-build hardware profiles, all application threads, userspace only:

| Selected functions | CPU-cycle share | L1 data-cache miss share |
|---|---:|---:|
| UI emission and two canvas/chrome width queries | 10.06% | 21.81% |
| Actor baking and packing helpers | 7.38% | 7.26% |
| General model-face ordering | 5.76% | 8.11% |
| Pose transforms | 5.02% | 1.45% |
| Scene painter and dynamic registration: excluded from changes | 5.43% | 5.56% |
| memcpy, callers not yet attributed | 4.43% | 20.20% |

The two width queries alone account for 5.34% of cycles and 15.36% of cache
misses. Main-thread cycle share is 85.28%; worker share is 10.12%. These are
flat sample shares, not exhaustive subsystem totals or elapsed-time shares.
UI work alone is not demonstrated to contain the entire required 2.86 ms.

Sources: [fresh assessment](benchmarks/krait_model_chains/SUB10_ASSESSMENT.md),
[frame-time evidence](benchmarks/krait_model_chains/FRAME_TIME_RECHECK.md),
[exact profile grouping](benchmarks/krait_model_chains/experiments/sub10-profile-breakdown.json),
and [previous architecture plan](KRAIT_UNIFIED_KERNEL_PLAN.md).

## Implementation order

```text
P0: freeze current baseline + incremental A/B + real input captures
 |
 +--> P1: compact UI width queries and correct invalidation
 |      |
 |      +--> P3: retain more valid UI emission work
 |
 +--> P2: direct actor vertex encoding
 |
 +--> P4: attribute and remove redundant copies
          (attribution starts in P0; implementation follows ownership proof)
 |
 +--> evaluate combined whole-frame result and refresh hardware profile
        |
        +-- target met --> P6: independent acceptance runs and normal APK
        |
        +-- gap remains --> P5: evidence-selected pose/model/submission work
                              then repeat the combined gate
```

Implement and measure one change at a time. The independent branches describe
dependencies, not permission to overlap on-device measurements. Never profile
two candidates concurrently on the XT1060.

## P0 — Make the next comparison trustworthy and fast

**Deliverables:** a frozen current baseline, an incremental comparator, narrow
real UI-query replay, and a copy ownership inventory.

1. Archive the current normal APK/library, hashes, source revision and runtime
   settings. Preserve the successful Lumbridge manifest/camera recipe and a
   dedicated initialized test account. Screenshot-gate entry into the world;
   character creation, loading and region transitions are not steady samples.
2. Extend `tools/perf/gles2_frame_times.py` and the diagnostic controls with a
   new comparison target for this pass. **Keep every existing optimization on
   in both arms.** Vary only the selected new candidate or the new cumulative
   bundle. The existing `complete` target compares against the pre-pass runtime
   paths and is the wrong baseline for incremental sub-10 claims.
3. Give each new mechanism an independent initialization/frame-boundary switch.
   Switch only after workers have joined; perform necessary cache rebuilds
   before measured windows. Record the full switch matrix in the result.
4. Run A/A through exactly the same switching and capture machinery to establish
   variability. Keep timestamp output buffered, PMU ioctls off in timing runs,
   detailed software profiling off, and no per-frame `glFinish`.
5. Add a narrow UI-query capture/replay using actual component state, mutation
   order, query arguments and expected answers. Preserve parent links, node
   incarnations, resolved boxes, visibility and layout publication order.
   Capture and correctness tracing are not performance measurements. Measure
   replay with hardware counters after loading and verification, as the existing
   model/pose/bake harnesses do. Do not build a general UI emulator first.
6. Inventory application-side copies of actor coordinates, alpha, vertex/index
   streams and retained UI commands. Record ownership, producer, consumers and
   lifetime, using source/disassembly and real-chain inputs. Byte counts describe
   workload, not time saved. Do not attribute all libc copy samples to the renderer.

For copy caller attribution, use hardware call-chain samples only if the
device actually produces a validated chain. DWARF sampling was unsupported on
this kernel, and frame-pointer chains cannot be assumed valid through optimized
ARM code. If unavailable, use a bounded PMU replay/A/B of a specific identified
copy path. Avoid spending another iteration rebuilding a toolchain solely for
an unproven call-graph route or substituting timer sampling.

**Exit gate:** A/A is stable enough to distinguish a candidate; original and
replayed UI query answers match; all existing renderer fast paths remain enabled
in both comparison arms. No new speedup is claimed from P0 tooling itself.
Keep this phase limited to what P1/P2 need. Once those gates pass, start the
implementation; do not postpone the first optimization to build a broader
benchmark framework.

## P1 — Stop scanning the full UI tree for width queries

**Files:** `src/app.c` (`App_MeasureRightChromeStripWidth`,
`App_MeasureLaneFrameCoreWidth`), `src/ui/uitree.h`, relevant mutation setters
in `src/ui/uitree.c`, and layout publication in `src/ui/uitree_layout.c`.

Both queries already memoize on tree pointer, dirty generation, layout sequence
and component count. A cache miss still traverses the component array. Adding
another cache with the same identity would not solve the observed work.

Implement in two separately reviewable steps:

1. Build compact candidate lists for the structural signatures each query
   accepts. Store stable node IDs plus incarnation/generation, not borrowed
   component pointers. Exclude impossible width/height/anchor-mode combinations
   when building the list, while checking current geometry and ancestor
   visibility when evaluating candidates. Rebuild membership when topology or
   relevant modes change; never assume a previously rejected node stays rejected.
2. Introduce a query dependency stamp and a tree-owned summary. Invalidate it
   on relevant canvas, geometry, topology, ancestor visibility and lifecycle
   changes. Text/color/animation changes that cannot affect the answer should
   not invalidate it. Enumerate every writer before narrowing invalidation.
   Keep conservative full recomputation for unclassified writes, pending layout,
   remounts or stale identities until coverage is established.

```text
current:
 broad tree change -> memo miss -> scan all components -> parent walks -> width

proposed:
 topology/mode change -------> rebuild compact candidate IDs
 relevant geometry/visibility -> invalidate width summary
 unrelated appearance update -> summary stays valid
 query ----------------------> validate stamp -> return summary
                                     |
                                     +-- stale -> inspect candidates -> publish
```

Do not delay updates until the next frame: a query after a same-frame script
mutation must observe the correct answer. The two width results have an explicit
dependency: lane core width depends on strip width. Publish consistent answers
without recursive invalidation. Handle tree destruction/address reuse and
generation wrap conservatively.

**Krait rationale:** replacing wide, sparse component reads and dependent parent
walks with a small contiguous candidate list reduces cache footprint and exposes
independent loads. It avoids requiring the core to hide long dependency chains.

**Correctness gate:** compare reference/optimized query answers after real
mutation chains plus focused resize, reparent, ancestor-hide, free/reuse,
root replacement, overflow and same-frame read-after-write cases. Exercise both
native UI revisions and existing gameframe scenarios. Compare rendered geometry
and interaction, not just the width integers.

**Performance gate:** fewer hardware cycles/cache misses in real query replay,
then a repeatable whole-frame gain in the app. Keep candidate-list construction
and all invalidation work inside the app measurement. No frame-time claim if
the apparent gain is inside the A/A variability band.

## P2 — Encode actor vertices directly into the final stream

**Files:** `src/platform/platform_renderer_gles2_core.c` (`gles2_bake_pose_vertices`),
`src/render/trspk_toridraw.c`, the GLES2 vertex writer/layout, and existing bake
capture/replay tools under `tools/perf/`.

World coordinates are already computed once per eligible actor vertex. The
remaining path gathers each face into a generic temporary structure, prepares
material/UV/color fields, converts colors and writes three final vertices.
Inspect the optimized assembly first: LTO may already eliminate some temporary
stores, so source-level removal alone is not evidence of a saving.

1. Add a GLES2-specific encoder for eligible ordered full-model actors. Gather
   cached XYZ and face attributes and write the final vertex layout directly.
   Retain the generic path as the reference and fallback.
2. Dispatch on model-level invariants once where possible: texture-array
   presence, alpha-array presence and supported representation. Mixed models
   keep their per-face material decisions and exact face order. Never partition
   or reorder faces to obtain a simpler loop.
3. Hoist stable palette/material/atlas lookups only when their resource lifetime
   and invalidation are explicit. Preserve texture animation, atlas exhaustion,
   alpha inversion and transparent placeholders. UVs depending on posed geometry
   must not be cached as immutable material data.
4. Keep one capacity check/reservation for the model and one dirty-range update.
   Write contiguous destination records; avoid a second staging arena or a new
   copy of the completed stream. Preserve reference float/int conversion and
   fixed-point operation ordering.

```text
current:
 sorted face -> XYZ/attribute gather -> generic face struct -> conversion -> VBO

candidate:
 model dispatch + reserve output
       |
 sorted face -> XYZ/attribute gather -> exact final GLES2 vertex record
```

**Krait rationale:** fewer intermediate stores and rereads, less per-face
dispatch, and a predictable output stream. Start with scalar direct encoding.
Only then evaluate a small unroll or NEON stores on real chains. Keep independent
loads in flight without spilling registers or expanding the hot loop excessively.
Do not assume interleaved loads, larger unrolls or NEON gathers are free.

**Correctness gate:** extend real bake replay to compare final packed bytes,
not only the generic face structure. Include textured actors, dynamic alpha,
mixed materials, skeletal/classic animation, invalid faces, capacity fallback
and camera changes. Reuse the live shadow verifier and exact pixel checks.

**Performance gate:** whole bake-chain PMU improvement, then whole-frame A/B.
Inspect generated ARM code for stack spills, duplicated checks and instruction
footprint before spending a device iteration. Reject an encoder that merely
moves equivalent work into upload or generic fallback.

## P3 — Retain valid UI emission work across localized updates

**Files:** `src/ui/uitree_emit.c`, host input stamps, relevant UI mutation/layout
code and existing UI/gameframe tests.

The emitter already has a retention gate and volatile refresh path. The gate
checks pending layout, tree identity/generation, dirty/layout sequences, hover,
drag, host inputs and buffer publication identity. Its post-refresh recheck is
required because callbacks can mutate the tree. Preserve those semantics.

1. Use real replay to identify which inputs invalidate retention and which
   actually change emitted command structure. Separate topology/geometry changes
   from refreshable content without weakening native availability or visibility.
2. First extend the existing typed volatile-refresh path for supported changing
   fields. Reuse unchanged commands in place, with current borrowed data and
   host-input stamps. Unknown callbacks or unsupported fields take the full walk.
3. If full-list invalidation still dominates after that change, add retention
   for stable subtrees with explicit dependency stamps and command ranges.
   Rebuild only affected ranges while maintaining global emission order. Account
   for range movement/copying: subtree caching that copies the entire output
   every frame is not automatically an improvement.
4. Preserve hover/drag passes, scrolling, clipping, remounts, replacements,
   cross-root anchors, script hooks and same-frame native state changes. Do not
   reuse commands whose resource or node incarnation has changed.

**Gate:** exact emitted-command semantics and interaction parity across real
host-input/mutation traces and existing UI tests, followed by PMU and app A/B.
Benchmark both quiet UI and changing chat/inventory/hover states. A quiet scene
gain cannot justify stale or disproportionately expensive interactive updates.

P3 follows P1 so width queries and emission can share established mutation
dependencies. Avoid introducing two competing notions of UI publication.

## P4 — Remove proven redundant copies at ownership boundaries

**Initial audit sites:** actor resets/clones in `3rd/toridraw/toridraw_model.c`,
pose buffer setup and index-stream copies in the GLES2 core, and retained UI
output movement. This is a candidate inventory, not an attribution of libc samples.

For each proposed deletion, draw and verify this contract:

```text
producer owns buffer -> publishes immutable range -> consumers finish
                            |                           |
                        exact lifetime             reuse/free allowed
```

Prefer generating directly into the final owned destination, reusing a buffer
whose contents are already valid, or narrowing a known dirty range. Keep copies
that separate mutable animation state, protect worker lifetimes or satisfy the
driver's input ownership. Pose reset data may be required for exact animation;
do not delete its copies merely because the source vertices are unchanged.

Use the existing aligned arenas and publication protocol rather than inventing
another handoff. Test reallocation, exhaustion, frame transitions, cancellation,
resource eviction, aliasing and main/worker ownership. Apply one proven change
per experiment. Do not replace libc memcpy globally or claim that eliminating
20% of cache-miss samples would eliminate 20% of frame time.

**Gate:** exact output/lifetime correctness, lower hardware cycles on its real
producer-to-consumer chain, and a whole-frame result above the noise band.

## P5 — Close the remaining gap from the updated profile

This phase is conditional. Do not implement every item just to fill a plan.
After P1–P4, measure the cumulative candidate against the frozen current baseline
and profile the new normal build. Calculate the actual remaining gap to 10 ms.

Candidate order, if the new evidence supports it:

1. **Remaining actor submission/material work:** retain immutable face metadata
   or eliminate repeated lookups using model/resource revision keys, with dynamic
   alpha and pose-dependent UVs handled separately.
2. **Pose transforms:** reduce redundant preparation inside a genuinely changed
   pose, or process independent vertices of one transform more efficiently.
   Same-pose reuse is already implemented. Never treat it as new savings, change
   animation tick rate, or fuse transforms across noncommuting operations.
3. **Model projection/order and dispatch:** optimize the complete real command
   chain only if it remains a material cost. Preserve stable depth ties, priority
   10/11 sequencing, strict insertion thresholds, clipping and picking. Main and
   worker execution must remain equivalent. Investigate existing work allocation
   before adding threads; a worker-cycle gain is not proof of a shorter frame.
4. **GPU work:** only if new GPU hardware-counter and frame evidence warrants it.
   Keep the current Adreno 320 white-tile optimization. No Z-buffer, face/material
   reordering, per-frame forced synchronization or speculative shader rewrites.

If the measured opportunities still do not close the gap, report the attained
median and remaining evidence. Do not silently expand into scene-painter changes,
reduce quality or advertise an unachieved sub-10 result.

## Krait rules applied across phases

The [local hardware study](../arch_fuzz/FINDINGS.md) and its critical review in
the unified plan guide hypotheses, not acceptance results. Measured memory-level
behavior favors small working sets and fewer dependent pointer loads. Keep compact
metadata physically compact and correctly aligned; do not pad every record to a
cache line and increase the footprint unnecessarily.

Hoist invariant dispatch out of face/node loops, but retain branches that
predictably skip substantial work. Use branch-miss counters when branch behavior
is the hypothesis. Small unrolls can expose independent loads; larger ones can
increase spills and instruction-cache pressure. Examine the emitted code first.

Avoid repeated ARM/NEON lane transfers and scalar-VFP/NEON mixing in a hot chain.
The study's throughput results apply to tested instruction sequences, not every
NEON instruction. The earlier blanket `-mcpu=krait` experiment regressed; do not
reintroduce it as a default. Prefer eliminating passes and traffic before tuning
the scheduling of instructions that need not execute at all.

## Measurement, decision gates and completion

**Fast iteration:** real-input replay correctness -> ARM code inspection ->
replay hardware-counter A/B -> real-app frame-time A/B. A replay win is a filter,
not acceptance. Include setup/invalidation costs at their actual frequency in
the app. Synthetic cases are for edge correctness, not performance samples.

**Per accepted mechanism:** use the current 600-frame warmup, 12 ABBA windows
of 180 frames and six settling frames, with cache state settled in both arms.
Keep every raw frame, switch configuration, command count and binary hash.
Compare whole-frame median, mean, p95, p99 and swap cadence. Use independent
ABBA blocks/runs to assess repeatability; do not treat thousands of correlated
frames as thousands of independent experiments. Do not sum incremental gains.

If a result is within the observed A/A variability, make at most one focused
follow-up justified by a specific unresolved cause, then park the candidate.
Hardware-event improvements without a frame-time gain remain explanatory
findings, not progress claimed toward the 10 ms target.

**Final acceptance:**

- At least three independent uncapped live-scene launches, each with the full
  ABBA protocol, report optimized median work below 10.00 ms. The comparison
  arm is the frozen, already optimized pre-sub10 baseline.
- Each run's result improves beyond measured control variability. The p95/p99
  and mean show no repeatable regression relative to that baseline. Report all
  launches, not only the best one, and record steady device conditions.
- Repeat normal paced gameplay and fixed GE/Varrock scene checks; verify frame
  cadence and no material regressions. Claim sub-10 only for workloads that
  actually meet it; do not generalize one Lumbridge camera to every game scene.
- Existing model/pose/bake parity, targeted new UI/ownership tests, moving-camera
  pixels, depth-disabled checks and live region/UI transitions pass.
- Repeat the result with separate fixed-configuration baseline/candidate timing
  builds, using identical compiler settings and minimal timestamp hooks but no
  runtime A/B switching. Verify that the normal APK has the same production
  paths/defaults and outputs; the timing builds still contain measurement hooks
  and must not be mislabeled as uninstrumented normal binaries.
- Install the normal optimized APK, verify diagnostic code is absent, restore
  phone settings and remove task server/tunnel/account setup. Save the final
  source/artifact hashes and complete measurements.

Suggested commit boundaries: comparator/captures; UI query candidates; UI query
invalidation; direct actor encoder; typed volatile UI refresh; optional subtree
retention; each proven copy removal; conditional P5 changes; final results.
Keep mechanisms independently reviewable and reversible.
