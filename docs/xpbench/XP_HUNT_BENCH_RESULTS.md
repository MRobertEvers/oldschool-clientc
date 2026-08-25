# XP frame-time hunt: every arm, measured against current head
47 arms of the archived hunt, rebuilt for the win-xp lane and measured on a Windows 11 / 32-core box. Scene `lumbridge-ground` (eye-level orbit, spline motion), soft3d only, 765x503, 1500 frames per run.
## The short version
- **39 of 47 arms are indistinguishable from measurement noise.** Every one of them leaves the frame's composition unchanged: the hottest symbol is still `toridraw_textri_opaque_lerp8_v3_asm` at ~20%.
- The two effects that cleared the noise floor were **not in the rasteriser**. One was a per-frame `SetWindowTextA`; the other was occlusion culling.
- The per-frame window-title call was found here from the `win32u.dll` profile bucket and independently fixed on `v3` in e1ff3c03a, which measured it on the actual XP target: kernel time 3.19s -> 2.08s per 30s, total CPU 78.6% -> 74.7%.

## What this PR turns on
`OCCLUDER_WALL_MIN_TILE_AREA` 8 -> 4, `OCCLUDER_FLOOR_MIN_TILE_AREA` 4 -> 2.

| | baseline | gates halved |
|---|---:|---:|
| frame p50 | 5.956 ms | **5.046 ms** |
| r_raster p50 | 2.725 ms | 2.229 ms |
| present p50 | 0.116 ms | 0.108 ms |
| painter commands | 2929 | 2636 |

**-15.28%, faster in 8/8 paired runs.** Full write-up and the image check in [OCCLUDER_GATES.md](OCCLUDER_GATES.md).

## How it was measured
This machine drifts more than the arms do. The same binary measured 4.763 ms early in a session and 5.489 ms an hour later -- 15% apart on identical code. A sweep that measures the baseline once and 47 arms afterwards charges that drift to whichever arms ran last.

So every arm is run as `baseline, arm, arm, baseline` and reported against the mean of its own two brackets. Across 47 brackets those two baselines disagree by a **median 1.06%, p90 6.22%, worst 37.6%** -- which is the noise floor quoted above, measured rather than assumed. Candidates were then re-run as 6-8 alternating pairs and reported with a sign count.

Timing and profiling are separate passes against the same binary: `TORIRS_PERF=1` costs about 7% of the frame here (it shows up in its own profile as `__clock_gettime` and `__divdi3`) and the EIP sampler costs ~10% more, so each perturbs what the other measures.

## Where the frame goes
Baseline, EIP sampler, `TORIRS_PERF=0`:

| symbol | share of frame |
|---|---:|
| `toridraw_textri_opaque_lerp8_v3_asm` | 20.84% |
| `toridraw_gouraud_tri_opaque_s4_asm` | 16.65% |
| `ToriDraw_ComputeProjectedFaceOrderSmall.isra.0` | 14.16% |
| `ToriDraw_Project.isra.0` | 5.89% |
| `ToriDraw_RasterPainter.isra.0` | 5.78% |
| `painter_paint_bucket.constprop.0.isra.0` | 5.10% |
| `toridraw_stock_branching_gouraud` | 3.74% |
| `draw_texture_scanline_transparent_blend_branching_lerp8_v3_ordered.lto_priv.0` | 3.06% |
| win32u.dll *(outside the exe)* | 5.21% |
| msvcrt.dll *(outside the exe)* | 2.83% |
| ntdll.dll *(outside the exe)* | 0.68% |
| USER32.dll *(outside the exe)* | 0.23% |

## All 47 arms

`base` is the mean of the two baseline runs bracketing that arm, so each delta is local in time. `confirm` is the follow-up alternating-pair run where one was done.

| arm | base ms | arm ms | delta | cmds | verdict | confirm |
|---|---:|---:|---:|---:|---|---|
| `hunt-abl-present` | 5.131 | 4.470 | -12.88% | 2929 | **faster** | -8.72% (6/6) |
| `hunt-r3-present` | 5.082 | 4.614 | -9.21% | 2929 | **faster** | -6.88% (6/6) |
| `hunt-r3-occluders` | 4.943 | 4.493 | -9.10% | 2636 | **drew less** | -10.77% (6/6) |
| `advver-w7` | 5.298 | 4.966 | -6.28% | 2929 | unreliable (bracket drifted) | -1.09% (4/6) |
| `hunt-r3-gspan` | 5.084 | 4.871 | -4.19% | 2929 | noise | -2.98% (6/6) |
| `hunt-w8` | 5.033 | 4.844 | -3.77% | 2929 | noise | +0.22% (3/6) |
| `hunt-r3-bucket` | 5.334 | 5.141 | -3.62% | 2929 | noise |  |
| `hunt-r4-fixed` | 4.960 | 4.793 | -3.37% | 2929 | noise |  |
| `advver-w11` | 5.083 | 4.940 | -2.81% | 2929 | noise |  |
| `hunt-w17` | 4.987 | 4.876 | -2.24% | 2929 | noise |  |
| `hunt-r3-projsse2` | 5.110 | 5.004 | -2.07% | 2929 | noise |  |
| `hunt-w11` | 4.950 | 4.854 | -1.95% | 2929 | noise |  |
| `hunt-w25` | 4.955 | 4.859 | -1.93% | 2929 | unreliable (bracket drifted) |  |
| `hunt-w7` | 4.851 | 4.758 | -1.93% | 2929 | noise |  |
| `hunt-r3-codegen` | 5.156 | 5.062 | -1.83% | 2929 | noise |  |
| `hunt-w9` | 5.217 | 5.123 | -1.81% | 2929 | noise |  |
| `hunt-w1` | 4.836 | 4.768 | -1.41% | 2929 | noise |  |
| `hunt-r3-texlerp` | 5.073 | 5.015 | -1.14% | 2929 | noise |  |
| `hunt-r4-tiled` | 4.956 | 4.902 | -1.09% | 2929 | noise |  |
| `hunt-r3-facedepth` | 5.046 | 4.995 | -1.01% | 2929 | noise |  |
| `hunt-r4-faceorder` | 5.062 | 5.013 | -0.98% | 2929 | unreliable (bracket drifted) |  |
| `hunt-w14` | 4.950 | 4.901 | -0.98% | 2929 | unreliable (bracket drifted) |  |
| `uncommitted-r4-fbtraffic` | 5.226 | 5.177 | -0.93% | 2929 | noise |  |
| `hunt-w18` | 4.822 | 4.817 | -0.11% | 2929 | noise |  |
| `hunt-w13` | 4.845 | 4.846 | +0.03% | 2929 | noise |  |
| `hunt-r3-tinytri` | 5.010 | 5.013 | +0.05% | 2929 | noise |  |
| `hunt-w0` | 4.904 | 4.909 | +0.12% | 2929 | noise |  |
| `hunt-r3-sortwin` | 5.087 | 5.103 | +0.30% | 2929 | noise |  |
| `hunt-w15` | 5.911 | 5.931 | +0.33% | 2929 | unreliable (bracket drifted) |  |
| `hunt-r3-texspan` | 5.037 | 5.062 | +0.50% | 2929 | noise |  |
| `hunt-w23` | 4.823 | 4.866 | +0.89% | 2929 | noise |  |
| `hunt-r4-fbtraffic` | 4.881 | 4.927 | +0.94% | 2929 | noise |  |
| `hunt-r4-texspanset` | 4.838 | 4.886 | +0.99% | 2929 | noise |  |
| `hunt-r3-sorter-census` | 5.009 | 5.066 | +1.13% | 2929 | noise |  |
| `hunt-r3-sorter` | 5.010 | 5.070 | +1.21% | 2929 | noise |  |
| `hunt-r3-texshort` | 5.011 | 5.075 | +1.28% | 2929 | noise |  |
| `hunt-w4` | 4.870 | 4.934 | +1.31% | 2929 | noise |  |
| `hunt-r3-proj` | 5.040 | 5.111 | +1.41% | 2929 | noise |  |
| `hunt-r4-soa` | 4.925 | 4.996 | +1.45% | 2929 | noise |  |
| `hunt-r4-bkt` | 4.986 | 5.063 | +1.56% | 2929 | noise |  |
| `hunt-abl-bucketdrain` | 5.042 | 5.124 | +1.64% | 2929 | noise |  |
| `hunt-abl-nosort` | 4.980 | 5.069 | +1.78% | 2929 | noise |  |
| `hunt-abl-proj` | 4.982 | 5.076 | +1.90% | 2929 | noise |  |
| `hunt-w10` | 4.804 | 4.896 | +1.93% | 2929 | noise |  |
| `hunt-r3-degen` | 5.021 | 5.143 | +2.42% | 2929 | noise |  |
| `hunt-w3` | 4.853 | 4.973 | +2.48% | 2929 | noise |  |
| `hunt-r3-facesort2` | 5.020 | 5.202 | +3.62% | 2929 | noise |  |

## Not measured, and why
- **12 arms** whose branch diffs conflict structurally with the v3 refactor (`hunt-w6`, `hunt-w16`, `hunt-r3-dispatch`, `hunt-r4-floor` and others). Merging them means re-applying the idea to code that was rewritten underneath it, which measures a reimplementation, not the arm.
- **6 `uncommitted-*` arms** with no branch to derive a patch from.
- **`hunt-r4-nonrender`** -- defect in the arm's own patch: unterminated string literal at `world/world_cycle.c:1586`.
- **`hunt-w19`** -- hand-resolved, but its new `w19_arm.h` never applied, so the census macros are undeclared.

4 arms were hand-resolved and measured: `hunt-w8`, `hunt-w9`, `hunt-w10`, `hunt-r4-soa`. Every conflict was additive.

## Caveat for the XP target
This box runs the scene at ~5 ms against the XP target's ~35 ms. Anything bound by memory latency or the Pentium 4 pipeline is compressed here, so a raster kernel reading as noise in this table is **not** proof it reads as noise on the target. `hunt-r3-gspan` is the case in point: -2.98% is inside the noise band, but it won 6 of 6 pairs, which a coin flip does about once in sixty runs.
