# Benchmark Analysis: bucket vs w3d

_(Older runs in `output.txt` used the `heap` label for the same traversal algorithm; the C implementation now uses a bucket queue instead of a binary heap.)_

## Summary

Out of **288 scenario pairs**, heap wins **238** (82.6%) and w3d wins **50** (17.4%). But the pattern is sharp -- w3d's wins cluster around specific conditions.

## When w3d wins

### Condition 1: 2 levels + center eye (dominant pattern)

This is the single largest cluster. At 2 levels with eye at center:

| Grid | Yaw values where w3d wins center | Count |
|------|----------------------------------|-------|
| 51   | 0 (all 4 scenery), 45 (all 4), 180 (all 4), 360 (3 of 4) | 15 |
| 104  | 0 (all 4), 45 (all 4), 180 (all 4), 360 (all 4) | 16 |

w3d wins **31 of 32** center+2-level cases at grids 51/104. The margins are modest but consistent: typically 2-7% faster.

### Condition 2: 2 levels + edge eye + yaw 45

| Grid | Scenery configs | Count |
|------|----------------|-------|
| 51   | all 4          | 4     |
| 104  | all 4          | 4     |

w3d wins all 8 of these. Margin ~8-10%.

### Condition 3: 1 level + large grid + edge/center + yaw 45

A smaller cluster at 1 level:

| Grid | Eye    | Scenery configs | Count |
|------|--------|----------------|-------|
| 51   | edge   | def_heap, def_w3d, rand20 | 3 |
| 104  | edge   | all 4 | 4 |
| 104  | center | def_w3d, rand20 | 2 |

Plus 2 stray wins at grid=104 lvls=1 yaw=180 center with scenery.

## When w3d never wins

- **Grid 11**: heap wins all 192 small-grid scenarios (every combination). The heap's per-tile O(log n) cost is negligible when n is small, and w3d pays a large constant overhead for seed generation + the two-pass draw_front/draw_back FSM.

- **Corner eye**: heap wins every single corner scenario across all grid sizes and level counts (0 of 96 corner cases go to w3d). From the corner the frustum covers roughly a quarter of the draw box, so the heap handles fewer tiles and its priority ordering is most efficient.

## Why the pattern exists

The core tradeoff is **O(log n) heap push/pop** vs **O(1) linked-list push/pop** with higher constant overhead:

1. **Tile count amplifies heap cost**: Center eye maximizes visible tiles within the draw radius. At grid=51 radius=25 with 2 levels, that's roughly 2 x 2,601 = 5,200 tiles. Each heap push is O(log 5200) ~ 12 comparisons. The linked list does it in O(1). With thousands of pushes per run this gap accumulates.

2. **Corner eye minimizes tile count**: From corner with a 90-degree FOV, only ~25% of the draw box is visible. Fewer tiles means the heap stays small and its log factor stays low, while w3d still pays the full seed-generation cost over the entire draw box.

3. **2 levels doubles the work in the heap's weak spot**: Each level adds another full set of tiles that need heap insertion. The heap's disadvantage scales with total tile count, while w3d's per-tile cost stays flat.

4. **Yaw 45 from edge creates asymmetric frustum load**: At yaw=45 from edge (0, grid/2), the frustum sweeps diagonally into the grid. This creates a large visible region that stresses the heap's ordering but is handled naturally by w3d's spiral seed sequence.

5. **Scenery slightly favors w3d**: When scenery triggers re-pushes (tiles revisited after a scenery clears), each re-push is another O(log n) for heap vs O(1) for the linked list. This is why at grid=104 lvls=1, w3d starts winning center cases only when scenery is present (def_w3d, rand20).

## Margin of victory

When **heap wins**, the margin is often **large** -- 1.5-4x faster at grid=11, and 20-40% at larger grids with corner/edge eye. When **w3d wins**, the margin is **narrow** -- typically 2-7%, occasionally up to 10%. This reflects that heap has lower constant overhead but worse asymptotic scaling per-operation.
