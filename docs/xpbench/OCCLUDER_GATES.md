# Halving the occluder merge-area gates

`OCCLUDER_WALL_MIN_TILE_AREA` 8 -> 4 and `OCCLUDER_FLOOR_MIN_TILE_AREA` 4 -> 2
(`src/painters/scene_occluders.h`).

This came out of a sweep of the archived XP frame-time hunt: 47 arms rebuilt for
the win-xp lane against current head and measured on a Windows 11 box. It was
the largest effect in the whole set, and one of only two that cleared the
machine's noise floor.

## What it does

The gates decide how small a merged wall or floor may be and still be kept as an
occluder. Lowering them admits roughly twice as many occluders, which culls more
geometry before it reaches the rasteriser.

## Measurement

Scene `lumbridge-ground` (eye-level pitch 200, `orbit=10,8`, `motion=spline` --
the manifest's "closest of these to an ordinary played frame"), soft3d, 765x503,
1500 frames per run, `TORIRS_PERF=1`.

Runs are **interleaved**, baseline and candidate alternating, and compared as
medians of 8 runs each. That is not ceremony: the same binary measured 4.763 ms
early in a session and 5.489 ms an hour later on this machine, so a sweep that
measures the baseline once and the candidate afterwards charges that drift to
the candidate.

| | baseline | gates halved |
|---|---:|---:|
| frame p50 | 5.956 ms | **5.046 ms** |
| r_raster p50 | 2.725 ms | 2.229 ms |
| present p50 | 0.116 ms | 0.108 ms |
| painter commands | 2929 | 2636 |

**-15.28% frame time, faster in 8 of 8 paired runs** (a coin flip lands that
about once in 256). An earlier confirmation on the pre-merge tree gave -10.8%
over 6 of 6 pairs, so the size of the effect moves with machine conditions but
the direction does not.

The saving is where the mechanism says it should be: `r_raster` falls 0.496 ms
while `present` does not move, and 293 fewer painter commands are issued.

## Why this is safe, and the one place it is not

Each occluder is tested as its own projected shadow rectangle
(`scene_occluders_point_hidden_ref`): the occluder's bounds are spread outward
from the eye by `spread * distance >> 8` and the point is tested against that
rectangle. A smaller occluder therefore casts a proportionally smaller shadow.
It can only ever add correct culling; it cannot hide something a larger occluder
would have left visible.

The exception is tile granularity. `scene_occluders_ground_tile_hidden` samples
a tile's four **corners**, so a tile whose corners are all shadowed but whose
middle is not can be culled when it should not be. That approximation comes from
the reference client and exists at any gate value; admitting more occluders
widens the exposure to it.

## Image check

The bench scene is **not frame-deterministic** -- entities animate independently
of the camera path -- so a single before/after pixel diff cannot answer whether
the picture changed. Two runs of the *same* binary already differ.

So the comparison is two-sample: four frames captured from each binary at the
same camera, then every within-group and between-group pair compared.

| pairs | n | min | median | max |
|---|---:|---:|---:|---:|
| baseline vs baseline | 6 | 0.196% | 0.228% | 0.240% |
| halved vs halved | 6 | 0.000% | 0.177% | 0.219% |
| baseline vs halved | 16 | 0.198% | 0.282% | 0.299% |

The between-group difference sits inside the spread the same binary produces
against itself. **No image change is detectable above scene noise.** The
between-group median is slightly higher than either within-group median, so this
bounds the difference rather than proving equivalence. A decisive check needs a
static camera in a scene with no moving NPCs.

## Reverting

Restore `8` and `4` in `src/painters/scene_occluders.h`. Those are the reference
client's values; this change is a deliberate divergence from them, and the
occluder *set* differs from Client-TS even though the drawn image should not.

## Reproducing

```sh
./launch bench osrs239-bench --renderer soft3d --scene lumbridge-ground --repeat 3
```

Interleave the two binaries rather than running one sweep then the other, and
read `r_raster` alongside `frame`; `painter_commands` is the check that both
builds were asked to draw the same scene.
