# Bug: `painter_paint_bucket` drops bridge-column scenery with `level_mask > 1`

**Fixed in**: `src/osrs/painters_bucket.u.c`  
**Found by**: `scripts/painter/c/fuzz_cache` running the superset invariant check against the real dat2 cache  
**Symptom**: scenery objects on bridge columns silently absent from the bucket painter's output whenever more than one terrain level is enabled (`level_mask = 0xF`), matching the normal in-game state

---

## Background: the bridge level shift

The world builder has a "LinkBelow" bridge mode for tiles that are visually above a lower-level walkway (e.g., the second floor of a building overhanging the ground). For each such column `(x, z)` the builder does a **downward shift** of the painter tile stack before the game starts drawing:

```
BEFORE shift              AFTER shift

slot 0  ground content    original level-1 content   ← new surface tile
slot 1  level-1 content   original level-2 content
slot 2  level-2 content   original level-3 content
slot 3  level-3 content   original level-0 content   ← bridge underpass (BRIDGE flag)
```

The shift is implemented with `painter_tile_copyto` (source → destination):

```c
bridge_tile_tmp = *painter_tile_at(painter, x, z, 0);     // save ground tile
for( int level = 0; level < max_levels - 1; level++ )
    painter_tile_copyto(painter, x, z, level + 1, x, z, level);  // shift down
*painter_tile_at(painter, x, z, 3) = bridge_tile_tmp;     // underpass at slot 3
painter_tile_set_bridge(painter, x, z, 0, x, z, 3);       // link surface → underpass
```

`painter_tile_copyto` sets `grid_level = dest_slot` but deliberately **leaves `terrain_level` from the source** (so `push_command_terrain` still references the correct mesh). Crucially, it also calls `clone_scenery_chain`, so the slot-0 tile's `scenery_head` now contains the scenery elements that were originally placed at level 1.

Those scenery elements were added to the painter with `painter_add_normal_scenery(..., slevel=1, ...)`, so their `element->slevel` field is **still 1**, even though they now live inside a tile whose `grid_level = 0`.

---

## The bug

When the bucket painter processes a tile it checks whether each scenery element's footprint is "ready" — that is, whether all tiles beneath the element have been painted — before emitting the element. The check was:

```c
// painters_bucket.u.c  (before fix)
int el_slevel = (int)element->slevel;          // 1, from when element was added
...
struct TilePaint* u = tile_paint_at(painter, ox, oz, el_slevel);  // looks at slot 1
if( u->step < PAINT_STEP_GROUND )
{
    all_base = 0;   // not ready — defer
    break;
}
```

For a bridge column with `level_mask = 0xF`:

| tile | grid_level | step after init |
|------|-----------|-----------------|
| slot 0 (`slevel=0`) | 0 | `READY` |
| slot 1 (`slevel=1`) | 1 | `READY` |
| slot 2 (`slevel=2`) | 2 | `READY` |
| slot 3 (`slevel=3`, BRIDGE flag) | 3 | `DONE` (bridge tiles are excluded) |

When the bucket processes the slot-0 tile and encounters the shifted element (`el_slevel=1`), it checks footprint tiles at **level 1**. Those slot-1 tiles are still `READY` (not yet processed). So `all_base = 0` and the element is deferred.

But slot-1 will not be processed until slot-0 is `DONE` (the bucket requires the tile below to be done before processing a higher level). And slot-0 cannot reach `DONE` because its deferred scenery elements — waiting on slot-1 — are never re-triggered: they live in slot-0's `scenery_head`, not slot-1's. **The result is a deadlock: the element is never drawn.**

With `level_mask = 0x1` (level-0 only) the bug does not manifest because all upper-level tiles have `step = DONE` from the start (excluded by the draw mask). `DONE >= PAINT_STEP_GROUND` so `all_base = 1` and the element is drawn normally.

---

## The fix

Use the **current tile's `grid_level`** for the footprint readiness check, exactly as `painter_paint_world3d` does:

```c
// painters_world3d.u.c  (reference)
int grid_level = painters_tile_get_grid_level(tile);
...
int oidx = painter_coord_idx(painter, lx, lz, grid_level);
if( W3(painter)->paints[oidx].draw_front )   // pending at the current slot level?
    blocked = 1;
```

```c
// painters_bucket.u.c  (after fix)
// grid_level is already available from the tile-processing preamble.
struct TilePaint* u = tile_paint_at(painter, ox, oz, grid_level);  // not el_slevel
if( u->step < PAINT_STEP_GROUND )
{
    all_base = 0;
    break;
}
```

The same change was made to the post-draw push:

```c
// before
bucket_push_tile(w, painter_coord_idx(painter, ox, oz, el_slevel));

// after
bucket_push_tile(w, painter_coord_idx(painter, ox, oz, grid_level));
```

For non-bridge columns `element->slevel == grid_level`, so the fix is a no-op. For bridge columns the correct slot-0 tiles are now checked and pushed, breaking the deadlock.

---

## How the bug was found

The `fuzz_cache` tool (`scripts/painter/c/`) loads the real dat2 world headlessly and sweeps 144 camera/yaw/pitch/level_mask combinations over the centerzone, asserting the superset invariant (`world3d ⊆ bucket`) at each point. On the first run three scenery entity IDs appeared consistently in the world3d output but were absent from the bucket output whenever `level_mask = 0xF`:

```
FAIL  sx=52 sz=26 yaw=   0 pitch=-200 mask=0xf missing terrain=0 elements=2
  missing element 4379
  missing element 4092
```

All 144 cases pass after the fix.

---

## Invariant

`painter_paint_bucket` is required to be a **superset** of `painter_paint_world3d`: every entity emitted by world3d must also appear in the bucket output (ordering may differ). The bucket may emit additional entities. This invariant is checked by `fuzz_real` (synthetic scenes) and `fuzz_cache` (real dat2 cache).

---

## Sample runs (post-fix)

### Synthetic fuzzer — `make fuzz_real && ./fuzz_real 1 500`

500 randomly generated scenes, each testing the superset invariant across all element categories:

```
OK: 500 seeds
coverage (world3d drawn / added to scene):
  scenery         3869 /    4009  ( 96.5%)
  wall            2756 /    3007  ( 91.7%)
  walldecor       2267 /    2499  ( 90.7%)
  grounddecor     1742 /    1912  ( 91.1%)
  groundobj       1384 /    1479  ( 93.6%)
```

The coverage column shows how many of the elements placed by the fuzzer were actually drawn by world3d. Elements that fall outside the camera's 50×50 draw rect are not drawn, which accounts for the non-100% coverage — this is expected and intentional.

### Benchmark — `./fuzz_real 1 200 bench 200`

200 seeds × 200 iterations per painter. Columns: scene grid size, active levels, cull-map enabled, camera offset, world3d `ns/iter`, bucket `ns/iter`, ratio. Seed marked `SLOW` when bucket > world3d.

```
seed      grid  lvl cull    cam   w3d_ns/it   bkt_ns/it   ratio
1           14    4    0  13,4      22770.0     22645.0   0.995
2           13    1    1   0,10      6585.0      5765.0   0.875
3           12    2    0   3,0      13395.0     11300.0   0.844
4           15    3    1   0,6       8785.0      7710.0   0.878
5           14    4    0   3,0      25205.0     23795.0   0.944
...
197         18    4    0  13,0      32460.0     32945.0   1.015 SLOW
198         17    1    1   0,0       8040.0      7090.0   0.882
199         20    2    0  19,16     23720.0     19935.0   0.840
200         19    3    1   8,0      13425.0     11545.0   0.860

--- aggregate over 200 seeds ---
  world3d total: 3.5 ms  (17369.0 ns/iter mean)
  bucket  total: 3.2 ms  (16031.0 ns/iter mean)
  mean ratio bucket/world3d: 0.923
  seeds where bucket slower: 13 / 200
```

The bucket painter is **~8% faster** on average. The 13 seeds where bucket is marginally slower are scenes where the bucket priority-queue overhead exceeds the savings from reduced re-traversal; these are typically small scenes (1 level, few tiles).

### Real cache — `make fuzz_cache_run`

144 sweep points across the dat2 centerzone (3 positions × 3 positions × 4 yaw × 2 pitch × 2 level masks). Each point checks the superset invariant and reports how many terrain tiles and elements world3d drew:

```
Loading dat2 cache from: ../../../cache
PASS  sx=26 sz=26 yaw=   0 pitch=-200 mask=0x1 terrain=2554 elements=1374
PASS  sx=26 sz=26 yaw=   0 pitch=-200 mask=0xf terrain=10000 elements=2154
PASS  sx=26 sz=26 yaw=   0 pitch=-350 mask=0x1 terrain=2554 elements=1374
PASS  sx=26 sz=26 yaw=   0 pitch=-350 mask=0xf terrain=10000 elements=2154
...
PASS  sx=52 sz=26 yaw=   0 pitch=-200 mask=0x1 terrain=2588 elements=1459
PASS  sx=52 sz=26 yaw=   0 pitch=-200 mask=0xf terrain=10000 elements=1825
...
PASS  sx=78 sz=78 yaw=1536 pitch=-350 mask=0x1 terrain=2582 elements=887
PASS  sx=78 sz=78 yaw=1536 pitch=-350 mask=0xf terrain=10000 elements=966

--- cache scene: 144/144 PASS ---
```

`terrain=10000` indicates the draw rect is fully covered at all levels (10,000 = 100 tiles × 4 levels × 25 radius, capped). Without the fix, the 48 sweeps with `mask=0xf` and `sx >= 52` all reported `FAIL` with 1–2 missing elements from bridge columns.
