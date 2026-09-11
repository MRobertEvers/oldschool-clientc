# P3: retain stable UI across projected overlay movement

The projected entity-overlay setter invalidated dirty/layout state every frame.
A 1,200-frame dependency census found no quiet frames even though native UI
and host inputs were nearly static. Forty-eight captured typed mutations were
position changes to overlay layers under the entity-overlay builtin.

The new path brackets that specific setter pass. It begins only from an exact
quiet buffer/tree publication and accounts both dirty increments and the actual
layout seed list. Topology, generic writes, broad invalidation, pending layout,
resize, hover/drag, host changes and unsupported state reject reuse. Layout
still resolves immediately through the normal resolver. The emitter rebuilds
only plain scripted entity children with the original `emit_walk_node`, then
replaces their contiguous published range. Other commands stay in place; tail
movement is necessary only when range size changes. Host arrays still refresh
through the existing typed volatile path, and callbacks are followed by the
normal full dependency recheck. Moving overlays do not set the whole-frame
"unchanged pixels" flag.

This is deliberately narrow. It requires a root-level entity-overlay builtin
following the world, plain descendant types, no active frame declaration or
drag, and no unrefreshable descriptors. App also disables it while plugins
are present. Unsupported cases retain the full walk. Nested arbitrary subtree
caching and refreshable geometry for general plugin frames are not implemented.

## Correctness

- Focused fixtures compare complete descriptor bytes/order/clipping over 80
  movements and host entity/frame overlay zero crossings. Mixed visibility,
  broad invalidation, host epochs, reparenting, empty ranges, viewport changes,
  tree clearing, callback mutations and same-frame global canvas resize all
  exercise fallback. The full UITree suite passes.
- The live shadow build matched 1,500 complete emitted command lists against
  independent full walks; screenshot-gated to the expected live scene.
- The ARM32 code uses reusable scratch for the dynamic range, not a second
  full UI list. The inspected refresh has about 2.6 KiB of stack scratch;
  invariant setup remains outside the descendant loop.

## Hardware-counter gate

Measured actual main-thread projected overlay positioning, layout, host-input
publication and command emission. This excludes driver submission, model
worker and GPU work. Each event uses its own pinned, nonmultiplexed userspace
counter, after 600 warmup frames and with 12 ABBA windows of 180 frames plus
six settling frames. No timer profiling or event-to-millisecond conversion.

| Event per frame | Reference | Candidate | Change |
|---|---:|---:|---:|
| CPU cycles | 2,872,604 | 1,149,337 | -59.99% |
| L1 data-cache load misses | 19,477 | 6,424 | -67.02% |

Every CPU-cycle window emitted 20,340 commands (113 per frame). The candidate
reused stable UI in 164–169 of 180 frames; the rest used conservative fallback.
Raw windows: `experiments/sub10-p3-ui-{cycles,l1d}.{json,log}`.

## Whole-frame gate

All earlier positive Krait mechanisms, including compact canvas queries and
direct actor encoding, are enabled in both arms. Only overlay retention varies.
The same 600/12x180/6 protocol is used with buffered whole-frame timestamps,
PMU ioctls and detailed software profiling disabled, and normal governor policy.

| Uncapped launch | Median A → B | Mean A → B | p95 A → B | p99 A → B |
|---|---:|---:|---:|---:|
| A/A control | 12.040 → 12.391 | 13.300 → 13.673 | 22.251 → 23.046 | 28.992 → 28.459 |
| P3 A/B 1 | 12.361 → 11.171 | 13.731 → 12.317 | 22.493 → 20.213 | 29.832 → 27.649 |
| P3 A/B 2 | 12.392 → 11.384 | 13.830 → 12.755 | 22.557 → 21.928 | 28.134 → 28.878 |
| P3 A/B 3 | 12.437 → 11.232 | 13.780 → 12.689 | 22.527 → 21.092 | 27.115 → 26.418 |

Numbers are milliseconds. All nine A/B blocks improve their median. Run-level
median improvements are 8.13–9.69%, exceeding the observed 2.92% A/A difference.
Mean and p95 improve in every run. p99 improves in two; the second run's p99 is
0.744 ms worse, so do not claim a universal tail-latency reduction.

**Sub-10 is still not achieved.** The new medians leave 1.17–1.38 ms to remove
in this workload. Do not add percentages to previous-pass results or infer
scanout latency from these work timestamps.

## Defaults and reproduction

`TORIRS_UI_OVERLAY_RETAIN` defaults on for ARM32 NEON and remains opt-in on
other architectures. `=0` is the full-walk rollback. `sub10-ui` isolates P3;
`sub10-ui-aa` keeps P3 off in both arms. `sub10` includes P3 in the new bundle.
Older diagnostic targets explicitly disable it to preserve their comparison.

`build_android_renderer.py --probe overlay-retain-verify` selects the live
command shadow diagnostic. `--probe ui-emit-pmu` plus `ui_emit_pmu.py` selects
real UI hardware counters. Use `--probe frame-times` with `gles2_frame_times.py`
for end-to-end A/B, and `--probe normal` to remove diagnostic code.

The fixture uses a private frozen copy of the existing compiled script pack
and server binary. Concurrent canoe script source edits made the ordinary
server freshness check fail; only this private fixture explicitly allowed the
older frozen pack. It is identical in both arms. Script/binary hashes and this
scope are preserved in `experiments/sub10-p3-fixture.json`. Client APK/source
hashes and device conditions are in each timing JSON. This pass does not
validate concurrent content changes.
