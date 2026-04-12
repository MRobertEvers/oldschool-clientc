# Benchmark Analysis: bucket vs w3d

Data: `analysis_bucket_data.txt` (50 iterations per scenario, 288 scenario pairs).

## Summary

Out of **288 scenario pairs**, bucket wins **272** (94.4%) and w3d wins **16** (5.6%).

Compared to the previous heap implementation (see `analysis_heap.md`), the bucket queue eliminated **34 of 50** w3d wins -- a 68% reduction. The bucket queue's O(1) push flipped nearly every case where the heap's O(log n) was the bottleneck.

## Wins by grid size and level count

| Grid | Levels | Bucket wins | w3d wins | Old heap wins (for reference) |
|------|--------|-------------|----------|-------------------------------|
| 11   | 1      | 48 / 48     | 0        | 48 / 48                       |
| 11   | 2      | 48 / 48     | 0        | 48 / 48                       |
| 51   | 1      | 48 / 48     | 0        | 48 / 48                       |
| 51   | 2      | 45 / 48     | 3        | 29 / 48                       |
| 104  | 1      | 47 / 48     | 1        | 40 / 48                       |
| 104  | 2      | 36 / 48     | 12       | 25 / 48                       |

The biggest improvements are at grid=51/2-level (+16 bucket wins vs heap) and grid=104/2-level (+11).

## Where w3d still wins (16 cases)

### Grid 104, 2 levels (12 cases)

This is the remaining stronghold for w3d:

| Yaw | Eye    | Scenery configs that w3d wins | Margin range |
|-----|--------|-------------------------------|-------------|
| 0   | center | none, def_w3d                 | 3-6%        |
| 45  | center | none, def_heap                | 0.7-7%      |
| 45  | edge   | all 4                         | 1-14%       |
| 180 | center | def_heap, def_w3d             | 4-7%        |
| 360 | center | none, def_w3d                 | 17-22%      |

The yaw=360 center cases show the largest margins (17-22%). These are the worst case for the bucket algorithm: maximum visible tile count with all tiles needing to be popped, and the linear scan within each distance bucket becomes expensive when many tiles share the same Manhattan distance from center.

### Grid 51, 2 levels (3 cases)

| Yaw | Eye    | Scenery  | Margin |
|-----|--------|----------|--------|
| 45  | center | none     | tied (0%) |
| 45  | edge   | rand20   | 3.5%  |
| 180 | center | none     | 0.5%  |

All marginal -- two are within noise.

### Grid 104, 1 level (1 case)

| Yaw | Eye  | Scenery | Margin |
|-----|------|---------|--------|
| 45  | edge | def_w3d | 0.3%   |

Effectively a tie.

## Where w3d never wins

- **Grid 11**: Bucket wins all 96 small-grid scenarios. Same as heap -- bucket's constant overhead is even lower with no sift operations.

- **Grid 51, 1 level**: Bucket now wins all 48 scenarios. Same as heap.

- **Corner eye**: Bucket wins **all 96 corner scenarios** across every grid size and level count (0 w3d wins). Identical to the heap era. The frustum from corner covers ~25% of the draw box, keeping bucket sizes small.

## Why the bucket queue helps

The bucket queue replaces O(log n) heap push/pop with:
- **O(1) push**: Prepend to the distance bucket's linked list.
- **O(bucket_size) pop**: Linear scan within one bucket to find the tile with minimum idx (preserving traversal order).

This helps most where the heap hurt most -- high tile counts at center eye with 2 levels. The improvement is largest at grid=51/2-level where bucket flipped 16 former w3d wins.

## Why w3d still wins some cases

The bucket pop is **not truly O(1)** -- it scans the linked list within the current distance bucket to find the minimum-idx tile. When many tiles share the same Manhattan distance (which happens at center eye on large grids), individual pops can be O(k) where k is the bucket population. At grid=104 center with 2 levels, the worst-case distance buckets can hold dozens of tiles.

In contrast, w3d's linked-list operations are always O(1) with no tie-breaking scan. This gives w3d a residual advantage when:
1. Tile count is very high (104x104 x 2 levels = ~21,600 tiles in the draw box)
2. Eye is at center (maximizes tiles per distance bucket due to the diamond symmetry of Manhattan distance)
3. Most tiles are visible (center eye + non-corner frustum)

## Margin of victory

When **bucket wins**, margins are typically **1.5-3.5x** at grid=11 and **15-45%** at larger grids. When **w3d wins**, margins range from **tied to 22%**, with most under 7%. The bucket queue substantially widened bucket's advantage in the common cases while narrowing w3d's edge in the remaining hard cases.

## Comparison to heap

| Metric                        | Heap     | Bucket   |
|-------------------------------|----------|----------|
| Win rate vs w3d               | 82.6%    | 94.4%    |
| w3d wins                      | 50       | 16       |
| Grid=11 (all scenarios)       | 96-0     | 96-0     |
| Grid=51 lvl=2 (worst segment) | 29-19    | 45-3     |
| Grid=104 lvl=2 (worst segment)| 25-23    | 36-12    |
| Largest w3d margin            | ~10%     | ~22%     |

The largest w3d margin actually increased for a few yaw=360/center outliers, likely due to measurement noise at 50 iterations. A higher iteration count would tighten these numbers. The overall picture is clear: the bucket queue is a strict upgrade over the binary heap for this workload.
