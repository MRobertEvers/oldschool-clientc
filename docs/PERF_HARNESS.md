# Performance harness

Entry point for measuring and iterating on torirs client frame time,
especially under `manifest_osrs230.ini` / `manifest_osrs230_embed.ini`.

## Gate

**p95 frame work under 20 ms** (50 fps) at `-O0` for the client, with Soft3D
compiled at `-O2` via `TORIDRAW_OPT=1`. Measured by this harness in
`--uncapped` mode so the number is work time, not the 50 fps sleep.

## Build knobs

```bash
# Soft3D/ToriDraw at -O2 while the rest of the client stays -O0.
# Own OBJ_DIR suffix (_tdo) so objects never mix with a plain -O0 Soft3D build.
make -C src PLATFORM_OBJ_BASE=build_perf EMBED_SERVER=1 TORIDRAW_OPT=1 torirs
```

`TORIDRAW_OPT` follows the existing `COMPRESS_CFLAGS` / `tommath.o` precedent:
hot kernels get their own flag without forcing a full-client release build.

## Running scenarios

```bash
./tools/perf/run_perf.sh idle 900   # logged in, world visible, no input
./tools/perf/run_perf.sh ui 900     # bank open (::bank) — UITree rebuild pressure
./tools/perf/run_perf.sh world 900  # npc spawn pressure for model-instance cache

# Compare two CSVs (exit 1 on p95 regression >5% or frame p95 over 20 ms)
python3 tools/perf/compare.py before.csv after.csv
```

Env: `TORIRS_PERF=1` enables stage timers/counters; `TORIRS_PERF_CSV=<path>`
writes the machine-readable report. Embed transport requires `EMBED_SERVER=1`.

## Flamegraphs

```bash
./profile-mac.sh manifest_osrs230_embed.ini 25
# builds EMBED_SERVER=1 TORIDRAW_OPT=1 automatically for transport=embed
```

## Stages timed

```
frame → async → logic → cs2 → layout → interact → emit → paint → build → render → present
```

Residual = frame_mean − sum(stage means). Render is measured but not optimized
algorithmically in this effort (see TORIDRAW_OPT).

## Baseline (measured 2026-08-03)

Build: `-O0` client + `TORIDRAW_OPT=1` Soft3D, `EMBED_SERVER=1`,
`manifest_osrs230_embed.ini`, `--uncapped`, Soft3D, 900 frames.

### idle

| metric | value |
|--------|------:|
| frame p50 | 7.05 ms |
| frame p95 | **8.25 ms** |
| frames over 20 ms | 15 / 900 (1.7%, boot) |
| eff fps (1/mean) | 59.4 |
| render p95 | 3.64 ms |
| paint p95 | 0.92 ms |
| emit p95 | 0.45 ms |
| cs2 scripts/frame | ~13.5 |
| uitree find_id/frame | ~682 |

### ui

| metric | value |
|--------|------:|
| frame p50 | 7.36 ms |
| frame p95 | **8.21 ms** |
| frames over 20 ms | 14 / 900 (1.6%) |
| eff fps | 59.2 |

Gate: **PASS**.

### world

| metric | value |
|--------|------:|
| frame p50 | 7.07 ms |
| frame p95 | **8.56 ms** |
| frames over 20 ms | 14 / 600 (2.3%, boot-skewed mean) |

Gate: **PASS** on p95.

- Soft3D render dominates attributed steady-state work (~3.6 ms p95).
- UITree walks still visit every node twice per emit (normal + drag) plus
  hit/hover; emit_skip is high — dirty filtering works, but the walk itself
  still strides the array.
- CS2 VM pool hits ~100% after warm-up (pool size raised 4 → 16).
- Model/sprite provider caches hit well after boot; config caches remain
  session-unbounded by design (see `cache_provider.c` comment).

## Optimizations landed this pass

1. **Harness** — `src/perf/torirs_perf.{h,c}`, stage scopes in `app.c`/`main.c`,
   `tools/perf/run_perf.sh`, `tools/perf/compare.py`, embed-aware `profile-mac.sh`.
2. **`TORIDRAW_OPT=1`** — Soft3D at `-O2` in a `-O0` client (`_tdo` objdir).
3. **UITree hook indexes** — `timer_hook_ids` / `key_hook_ids` rebuilt lazily;
   logic ticks and key collection no longer scan every component every time.
4. **CS2 VM pool** — `CS2VM2_POOL_MAX` 4 → 16 (fewer Init cold paths).
5. **`TorirsLru` + `TorirsModelInstCache`** — spotanim lit bases cached (size 30,
   Client-TS SpotType.modelCache), cleared at world-load seam; hit/miss/evict
   counters wired.
6. **CacheProvider counters** — model/sprite hit/miss/evict on the derived
   caches; config caches documented as intentionally session-unbounded.

## Attempt log

| change | idle frame p95 | keep? |
|--------|---------------:|-------|
| harness + TORIDRAW_OPT + hook indexes + VM pool 16 + spotanim inst cache | **8.25 ms** | keep (gate met) |

Re-measure before claiming further gains. Prefer `compare.py` over eyeballing.

## Not done / next candidates (ranked by counter evidence)

1. Fold the four DFS walks (emit×2 + hit + hover) — walk counters dominate UITree.
2. Lazy `runtime_hooks` side-allocation (node sizeof still ~9–14 KB; `uitree_node_bytes`).
3. Loc/npc/player instance caches on the same `TorirsModelInstCache` (spot done).
4. Skip full `LayoutResolve` when only leaf dirty flags changed.
5. Enum/param host-op hash indexes if host_ops profile hot under ui scenario.

## Correctness

- Spotanim instance cache returns `ToriDraw_ModelCopy` of the cached base —
  scene owns a mutable copy; animation still applies per frame.
- Hook indexes rebuild on ApplyRuntimeHook / Push / reclaim; EnsureHookIndexes
  is the only reader contract.
