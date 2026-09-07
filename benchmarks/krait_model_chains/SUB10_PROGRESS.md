# Sub-10 ms implementation progress

The target is **not met**. Work is isolated in `/tmp/krait-sub10/worktree` at
`152dc6469e3c784ddd05dd76d15ae5dfbe02efd0` to preserve simultaneous application
feature work. The original normal APK/library and their hashes are frozen in
`/tmp/krait-sub10/baseline.{apk,so,json}`. Candidate defaults remain off pending
acceptance. Scene painter traversal/order and depth settings are unchanged.

## Candidate evidence

| Mechanism | Original live median A → B | Decision |
|---|---:|---|
| Compact canvas candidate IDs | 13.368 → 12.681 ms | Promising; retaining for combined validation |
| Direct 28-byte untextured actor encoding, canvas on in both arms | 12.071 → 11.720 ms | Promising; retaining for combined validation |
| Ancestor visibility memo | 12.208 → 12.086 ms | Retired; inside control variation, worse mean/tails |
| Compact 16-byte actor stream | 12.025 → 12.376 ms | Retired; slower |
| Compact stream with cached attribute setup | 12.559 → 12.575 ms | Retired; no gain |
| Model projection/order memo | 12.193 → 12.254 ms | Retired; no app gain despite replay improvement |
| Model memo with private main/worker ownership | 12.208 → 12.116 ms | Retired; inside control variation, worse mean |

The earlier A/A difference was +1.615% (13.185 → 13.398 ms). These isolated
results cannot be added, and the lowest recorded median is not a repeatability
claim. Raw JSON, CSV and screenshots are in `experiments/sub10-*`.

Three fresh combined launches, each 600 warmup frames, twelve 180-frame ABBA
windows and six settling frames per window, keep all earlier renderer
optimizations enabled in both arms:

| Run | Median A → B | Mean A → B | p95 A → B | p99 A → B |
|---|---:|---:|---:|---:|
| combined-1 | 13.246 → 12.453 | 16.065 → 15.053 | 27.272 → 27.062 | 55.943 → 62.068 |
| combined-2 | 13.551 → 12.788 | 15.528 → 14.998 | 24.822 → 24.938 | 42.137 → 38.479 |
| combined-3 | 12.879 → 12.284 | 14.431 → 13.634 | 23.779 → 22.283 | 30.742 → 28.574 |

All numbers are milliseconds of whole-frame work, before artificial pacing.
All nine ABBA blocks improved their median. Tails are noisy: the third run improves mean, p95 and p99, but the first
two vary. Further acceptance still needs fixed-configuration and paced/scene
checks. The third build additionally includes the late-link invalidation fix;
its source and binary provenance are recorded separately. These runs establish neither sub-10 performance nor
final acceptance. New results include build/source hashes, installed APK hash
verification, complete switch matrices, device conditions and block summaries.

## Correctness work in the continuation

- The bake diagnostic previously bypassed direct encoding. It now runs both
  complete packed writers and compares every byte before publishing the stream.
  A live scene verified 33,300 eligible actor models / 13,769,016 packed faces
  without a mismatch; screenshot and verification log are preserved in
  `experiments/sub10-direct-live-verify/`. This diagnostic is not timed.
- UI tests cover live geometry/visibility, resizing, candidate growth, clear and
  reuse, reparenting, late linking, and generation collisions. Explicit topology
  invalidation protects cached membership even if a generation value repeats.
- Retired ancestry, stream and model memo experiments stay out of the candidate.

## Copy ownership inventory (P0/P4)

| Site | Producer and owner | Consumer/lifetime | Disposition |
|---|---|---|---|
| `ToriDraw_ModelCaptureOriginalVertices` | Model-owned immutable rest coordinates and optional alpha copied from current state at bind | Subsequent reset/pose evaluations; freed on recapture or model destruction | Required independent rest state; no deletion justified |
| `ToriDraw_ModelAnimateReset` | Rest arrays → model-owned writable coordinate/alpha arrays | Animation mutates destination before projection/bake; same-pose reuse already avoids eligible repeated work | Required for changed poses; not new available savings |
| `gles2_animation_load` | Private model clone seeded with source rest pose, then posed | Retained animation bake, clone lifetime only | Repeated setup is visible in source but not attributed to steady live-frame cost |
| `gles2_sequence_push_indexed` | Caller index range → renderer-owned staging range | Later GL upload; source may be scratch or worker result storage | Copy protects lifetime; reserve/commit direct producer path already exists |
| Actor generic face → final vertex stream | Pose/world coordinates and attributes → renderer-owned stream | Upload after exact painter-order baking | Direct untextured encoder removes intermediate representation; textured/unsupported paths retain reference |
| Atlas dirty rectangle row copies | Atlas-owned strided pixels → renderer upload staging | Packed `glTexSubImage2D` source until call returns | Required for arbitrary subrectangles without unpack-subimage support; no global removal |
| UI command rotations/insertion/removal | Emitter-owned command array; descriptors borrow current host arrays | Published exact draw order and current-frame borrowed resource lifetime | Movement enforces ordering; deletion requires an equivalent emission/insertion strategy and real-chain evidence |

This inventory does not assign flat libc sample shares to callers or estimate
milliseconds. No additional copy deletion has been promoted.

## Remaining gates

P1's compact membership step is implemented; narrower dependency summaries
remain unimplemented. Captured canvas inputs are 24 real snapshots, not a full
mutation-event trace. P2 covers eligible untextured full models only and keeps
textured models on the generic path. P3 now has a real dependency census: all 1,200 post-warmup frames fail both
the dirty and layout terms; only 41 fail topology, and none fail hover. One
capture has no changed host-input epochs and the follow-up has three asset
epoch changes. All 48 sampled typed mutations in the follow-up are position
changes to `UIELEM_RS_LAYER` nodes under parent 137, the scripted entity-overlay
subtree. Node positions and identifiers are preserved in
`experiments/sub10-ui-mutation-trace/verification.log`. This selects retention
of unchanged UI subtrees as the next implementation, with full fallback for
geometry/topology/host/resource changes. A host volatile-refresh extension
alone would not address the observed blocker. The census is diagnostic,
not a performance measurement. P4 has an ownership inventory but no further
proven redundant steady-state copy. P5 remains evidence-selected. P6's three
sub-10 launches, fixed-configuration timing builds, normal APK acceptance and
final cleanup are still outstanding.

## Session cleanup

The original normal APK was reinstalled and its APK hash verified. Launch
settings match the frozen backups. The task server, tunnel, device manifest
and test save were removed; the private test account and timing candidate
artifacts are archived under `/tmp/krait-sub10/` for resumption. Candidate
defaults remain off. This restores the normal pre-sub10 app; it is not P6
acceptance of a sub-10 candidate. See `experiments/sub10-continuation-cleanup.json`.
