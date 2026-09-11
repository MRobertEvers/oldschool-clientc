# Hunt arms, as individually applicable patches

Every experimental arm from the XP frame-time hunt, exported with
`git format-patch`. One directory per arm; the patches inside are numbered
and apply in order.

## The base these apply to

**These do not apply to `v3`.** Every arm was developed on top of the hunt
base `f1dfe77a5` (`docs(xp): the hunt prompt`), which is itself 222 commits
ahead of `v3` and carries the raster, projection and profiling work the arms
build on. To use one:

```sh
git checkout -b try-arm f1dfe77a5
git am docs/xpbench/arms/<arm>/*.patch
```

All 60 series were verified to apply cleanly to `f1dfe77a5` with a hard reset
between each. They are stored here rather than as live branches so the hunt's
work survives without 80 worktrees.

## What these are NOT

**No arm here has a confirmed win.** Every number in the ledger was measured
on the wall-clock axis, and `rpdxp.exe` — the box's remote-desktop server —
steals up to 33.6% of the single CPU in a way that correlates with the
optimization under test. The CPU-time rerun that would settle which of these
are real had not returned when these patches were exported.

Treat the deltas below as *unconfirmed and probably contaminated*. They are
recorded so the re-measurement has something to re-measure, not as a claim
that any of this is faster.

Several arms are also `abl-*` / `*census*` probes: deliberate ablations and
instrumentation that exist to attribute cost. Those are measurement tools and
were never meant to ship.

## Arms

| Arm | Patches | Subject |
|---|---|---|
| `advver-w11` | 2 | perf(world): walk a runtime-spawn list instead of the |
| `advver-w7` | 1 | arm(prologue): gouraud triangle prologue, clip1 + walkcut |
| `hunt-abl-bucketdrain` | 1 | wip(abl-bucketdrain): checkpoint arm sources |
| `hunt-abl-gouraud` | 1 | probe(gouraud): dispatch census + two ablation rungs |
| `hunt-abl-noraster` | 1 | abl(noraster): HUNTABL_NOFACES floor ablation - the whole |
| `hunt-abl-nosort` | 1 | abl(nosort): HUNTABL_SORTMODE ablation ladder for the depth |
| `hunt-abl-notex` | 1 | wip(abl-notex): checkpoint arm sources |
| `hunt-abl-present` | 1 | abl(present): HUNTABL_PRESENT_LEVEL ladder over |
| `hunt-abl-proj` | 1 | wip(abl-proj): checkpoint arm sources |
| `hunt-abl-tri1px` | 1 | hunt(abl-tri1px): setup-vs-fill ladder, runtime-gated, all 4 |
| `hunt-r3-bucket` | 1 | wip(r3-bucket): checkpoint arm sources |
| `hunt-r3-codegen` | 1 | build(hunt): HUNT_CFLAGS/HUNT_LDFLAGS whole-program escape |
| `hunt-r3-degen` | 1 | wip(r3-degen): checkpoint arm sources |
| `hunt-r3-dispatch` | 1 | wip(r3-dispatch): checkpoint arm sources |
| `hunt-r3-eiptail` | 1 | wip(r3-eiptail): checkpoint arm sources |
| `hunt-r3-facedepth` | 1 | wip(r3-facedepth): checkpoint arm sources |
| `hunt-r3-facesort2` | 1 | wip(r3-facesort2): checkpoint arm sources |
| `hunt-r3-gspan` | 2 | perf(gouraud asm): delete the per-triangle setup, not the |
| `hunt-r3-mcount` | 1 | wip(r3-mcount): checkpoint arm sources |
| `hunt-r3-occluders` | 1 | perf(occluders): lower the wall/floor merge-area gates 8/4 -> |
| `hunt-r3-overdraw` | 1 | wip(r3-overdraw): checkpoint arm sources |
| `hunt-r3-present` | 4 | wip(r3-present): oscall census + prior title guard, for |
| `hunt-r3-proj` | 1 | wip(r3-proj): checkpoint arm sources |
| `hunt-r3-projsse2` | 1 | wip(r3-projsse2): checkpoint arm sources |
| `hunt-r3-sorter-census` | 1 | census: sorter per-model census (never timed) |
| `hunt-r3-sorter` | 1 | perf(sorter): pad the depth-bucket stride off the 1024-byte |
| `hunt-r3-sortwin` | 1 | wip(r3-sortwin): checkpoint arm sources |
| `hunt-r3-texlerp` | 1 | wip(r3-texlerp): checkpoint arm sources |
| `hunt-r3-texshort` | 1 | wip(r3-texshort): checkpoint arm sources |
| `hunt-r3-texspan` | 1 | wip(r3-texspan): checkpoint arm sources |
| `hunt-r3-tinytri` | 1 | wip(r3-tinytri): checkpoint arm sources |
| `hunt-r4-bkt` | 1 | census: sort span/occupancy census (never timed) |
| `hunt-r4-faceorder` | 1 | wip(r4-faceorder): checkpoint arm sources |
| `hunt-r4-fbtraffic` | 1 | wip(r4-fbtraffic): checkpoint arm sources |
| `hunt-r4-fixed` | 1 | wip(r4-fixed): checkpoint arm sources |
| `hunt-r4-floor` | 1 | wip(r4-floor): checkpoint |
| `hunt-r4-nonrender` | 1 | wip(r4-nonrender): checkpoint arm sources |
| `hunt-r4-soa` | 1 | wip(r4-soa): checkpoint arm sources |
| `hunt-r4-texspanset` | 3 | perf(texspan): short spans skip the dead reciprocal seed |
| `hunt-r4-tiled` | 1 | ablation(r4-tiled): l2fold -- fold the framebuffer row stride |
| `hunt-w0` | 1 | wip(hunt-w0): checkpoint arm sources |
| `hunt-w1` | 1 | wip(hunt-w1): checkpoint arm sources |
| `hunt-w10` | 1 | perf(pick): cursor gate on pick-only projection + batched |
| `hunt-w11` | 3 | perf(world): walk a runtime-spawn list instead of the |
| `hunt-w13` | 1 | wip(hunt-w13): checkpoint arm sources |
| `hunt-w14` | 1 | wip(hunt-w14): checkpoint arm sources |
| `hunt-w15` | 1 | arm(w32): 32-bit winding in the projected face order |
| `hunt-w16` | 1 | wip(hunt-w16): checkpoint arm sources |
| `hunt-w17` | 1 | wip(hunt-w17): checkpoint arm sources |
| `hunt-w18` | 1 | wip(hunt-w18): checkpoint arm sources |
| `hunt-w19` | 1 | wip(hunt-w19): checkpoint arm sources |
| `hunt-w23` | 1 | wip(hunt-w23): checkpoint arm sources |
| `hunt-w25` | 1 | wip(hunt-w25): checkpoint arm sources |
| `hunt-w3` | 1 | wip(hunt-w3): checkpoint arm sources |
| `hunt-w4` | 1 | perf(texspan): three per-span prologue variants behind knobs |
| `hunt-w5` | 2 | perf(tex): texture working-set ablation, a measurement |
| `hunt-w6` | 1 | arm(dispatch): gouraud dispatch + branchless sort |
| `hunt-w7` | 1 | arm(prologue): gouraud triangle prologue, clip1 + walkcut |
| `hunt-w8` | 4 | arm(pal_pref): prefetch the triangle's palette lines in |
| `hunt-w9` | 3 | perf(frame): world emitter writes the DRAW_MODEL command |

**Note on `hunt-r4-floor`:** its checkpoint commit swept in ~2800 unrelated
repo files (docs, tools, android) alongside the arm. It is archived here as a
source-only cumulative diff over `3rd/` and `src/` — apply it with
`git apply`, not `git am`.

## `_uncommitted/`

Seven worktrees held arm work that was never committed. Those diffs are
captured under `_uncommitted/`, restricted to `3rd/` and `src/`, each named
for its worktree. Unlike the series above they are plain `git diff` output —
apply with `git apply`. Their bases are recorded in the commit that added
them; several are not `f1dfe77a5`, so check before applying.
