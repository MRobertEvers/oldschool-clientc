# Three fixes, re-measured on the XP box

Same lane, same box, same method as [`profiles.md`](profiles.md) — fullscreen
1024x768, live osrs239 world, Lumbridge courtyard, cpu ms/frame, palindrome
arm order, 2 reps, 60 s window each. Only the binary changed.

| | before | after |
|---|---|---|
| binary sha256 | `e9b5777c8954cd8f…` | `4f4c655b597c352a…` |
| source | `v3` @ `3d0390d46` + fullscreen patch | + the three commits below |

The three changes, all measured locally before they went near the box (see
[`hotspot-plan.md`](hotspot-plan.md)):

| commit | change |
|---|---|
| `2b48c3296` | `ToriRS_FrameNextCommand`: one `kind` store instead of a 128-byte wipe per command |
| `a076f7665` | `UITree_LayoutResolve`: resolve from a dirty list instead of sweeping every component |
| `5fa22160b` | element ids carry their kind, so pick classification asks one entity pool instead of four |

---

## 1. Frame cost

**cpu ms/frame, best of two reps** — the axis `profiles.md` §2.2 argues for,
because `rpdxp.exe` takes 25-38% of this box's one cpu and not equally across
arms.

| arm | before | after | change |
|---|---|---|---|
| `--soft3d` | 41.95 | **35.23** | **−16.0%** |
| `--d3d9` | 23.08 | **20.58** | **−10.8%** |
| `--d3d9-zbuffer` | 21.46 | **20.18** | −6.0% |

**Every run, so the spread is visible rather than summarised:**

| arm | before (both reps) | after (both reps) |
|---|---|---|
| `--soft3d` | 41.95, 41.96 | 36.16, 35.23 |
| `--d3d9` | 23.51, 23.08 | 20.58, 21.12 |
| `--d3d9-zbuffer` | 21.46, 22.07 | 20.34, 20.18 |

**Wall clock and frame rate**, for completeness — contaminated by the streamer,
and reported only because hiding them would be worse:

| arm | fps before | fps after | wall ms before | wall ms after |
|---|---|---|---|---|
| `--soft3d` | 16.5, 16.3 | 18.9, 19.3 | 60.61, 61.35 | 52.91, 51.81 |
| `--d3d9` | 31.8, 31.9 | 35.0, 34.3 | 31.45, 31.35 | 28.57, 29.16 |
| `--d3d9-zbuffer` | 30.0, 29.4 | 30.5, 31.2 | 33.33, 34.01 | 32.79, 32.05 |

**What is and is not a result.** This box is bimodal by ~7.5%, so:

* soft3d −16.0% and d3d9 −10.8% are **outside** that and are real.
* **d3d9-zbuffer −6.0% is inside it and is not a result.** It is consistent
  with the other two and with the composition below, but on this evidence it is
  "no worse, probably better", not a measured 6%.
* The two D3D9 modes remain **indistinguishable from each other**: 20.58 vs
  20.18, 2% apart.

Working set is unchanged — 104 MB soft3d, 250/254 MB D3D9, as before. None of
these three changes was about memory.

---

## 2. What left the frame

900 in-world frames per arm, EIP sampler, shares scaled onto each arm's own
measured cpu ms/frame. Full tables: `eip-*.txt`, with the pre-change ones kept
beside them as `eip-*-before.txt`.

### `UITree_LayoutResolve` — gone from all three

It was the **largest single in-image symbol in both D3D9 frames** and second in
soft3d. After the dirty list it does not appear in any arm's top 20:

| arm | before | after |
|---|---|---|
| `--soft3d` | 4.382 ms (10.45%, #2) | below the top-40 cutoff (< 0.26 ms) |
| `--d3d9` | 2.693 ms (11.67%, **#1**) | below the top-40 cutoff (< 0.20 ms) |
| `--d3d9-zbuffer` | 2.989 ms (13.93%, **#1**) | below the top-40 cutoff (< 0.20 ms) |

Locally measured cause, on the live world over 1500 frames: node visits fell
**6,171,274 → 211,931**, i.e. 4,114 → 141 per frame, 29x.

### The rest of the movement

| symbol | soft3d before → after | d3d9 before → after | zbuffer before → after |
|---|---|---|---|
| `UITree_LayoutResolve` | 4.382 → **—** | 2.693 → **—** | 2.989 → **—** |
| `ToriRS_FrameNextCommand` | 0.709 → 0.624 | 0.655 → 0.540 | 0.576 → 0.465 |
| `ToriDraw_ComputeProjectedFaceOrderSmall` | 2.568 → 2.466 | 2.352 → 2.476 | 0.184 → 0.219 |
| `emit_walk_node` | 0.599 → 0.532 | 0.553 → 0.584 | 0.537 → 0.623 |
| `app_world_pick_finish` | 0.907 → 0.899 | 0.838 → 0.859 | 0.809 → 1.010 |
| `World_SceneryGetByElementId` | 0.426 → 0.336 | 0.187 → — | 0.197 → — |
| `d3d9_draw_model` | — | 0.349 → 0.395 | 1.845 → 2.194 |
| *ntdll.dll* | 3.756 → 3.391 | 3.333 → 3.184 | 2.243 → 2.466 |
| *msvcrt.dll* | 1.269 → 1.198 | 1.383 → 1.387 | 1.765 → 1.813 |
| *d3d9.dll* | — | 0.281 → 0.271 | 0.237 → 0.248 |

Read that table as *shares of a frame that got smaller*. A row that barely
moves in ms is now a **larger** share of the frame, which is why several look
flat or slightly up: the frame lost 1.3-6.7 ms underneath them.

`FrameNextCommand` is down 12-19% in every arm, which is the 128-byte wipe
leaving.

**The pick tagging did not show up as a win, and should not have.** Its whole
effect is inside `pick_classify_element`, which never had its own line in the
profile — the walks it removed were attributed to `World_SceneryGetByElementId`
(0.426 → 0.336 in soft3d; dropped out of the top 40 in both D3D9 arms) and to
`app_world_pick_finish`, which did not move. That is consistent with four pool
walks becoming one, and it is not proof of it. It is a structural change with a
correctness gate, not a measured millisecond.

---

## 3. What is now on top

The ranking the next round should work from.

### `--d3d9-zbuffer` — 20.18 ms/frame

| share | ms | symbol |
|---|---|---|
| 12.22% | 2.466 | *ntdll.dll* |
| 10.87% | 2.194 | `d3d9_draw_model` |
| 8.98% | 1.813 | *msvcrt.dll* |
| 5.00% | 1.010 | `app_world_pick_finish` |
| 4.81% | 0.971 | `d3d9_ui_draw_sprite` |
| 4.19% | 0.846 | `d3d9_bake_pose_vertices` |
| 4.06% | 0.819 | `trspk_toridraw_face_colors` |
| 4.01% | 0.810 | `trspk_toridraw_bake_face` |
| 3.09% | 0.623 | `emit_walk_node` |
| 3.01% | 0.607 | `app_plugin_highlights_rebuild_pools` |

### `--d3d9` (painter) — 20.58 ms/frame

| share | ms | symbol |
|---|---|---|
| 15.47% | 3.184 | *ntdll.dll* |
| 12.03% | 2.476 | `ToriDraw_ComputeProjectedFaceOrderSmall` |
| 6.74% | 1.387 | *msvcrt.dll* |
| 5.23% | 1.077 | `painter_paint_bucket` |
| 4.18% | 0.859 | `app_world_pick_finish` |
| 3.94% | 0.812 | `d3d9_ui_draw_sprite` |
| 3.51% | 0.723 | `d3d9_bake_pose_vertices` |

### `--soft3d` — 35.23 ms/frame

| share | ms | symbol |
|---|---|---|
| 14.84% | 5.229 | `toridraw_gouraud_tri_opaque_s4_asm` |
| 9.99% | 3.518 | `toridraw_textri_opaque_lerp8_v3_asm` |
| 9.63% | 3.391 | *ntdll.dll* |
| 7.00% | 2.466 | `ToriDraw_ComputeProjectedFaceOrderSmall` |
| 6.59% | 2.322 | `draw_texture_scanline_transparent_blend_…` |
| 5.33% | 1.879 | `ToriDraw2D_BlitArgbTiledAlpha` |

**The three D3D9 targets left, in order:** ntdll (2.5-3.2 ms, the kernel
transition per DP2 flush — clustering, not micro-optimisation), `d3d9_draw_model`
(2.19 ms in zbuffer mode, the per-face material classification that
`hotspot-plan.md` §2.1 D3 proposes caching per pose), and msvcrt (1.8 ms,
vertex-staging memcpy). Picking is now the largest non-renderer item at
~1.0 ms and still has the cursor gate (D1) unspent.

---

## 4. Verification

Each change carried its own gate before it reached the box; the box run
confirms the frame is still right:

* **Every arm's exit BMP is 1024x768** — no arm quietly letterboxed a 765x503
  canvas, which would have made it "faster" for free.
* All four handrolled asm kernels `nm`-verified in the measured binary.
* All six timing logins succeeded; no `reply=5`, so no run measured a login
  screen.
* Sampler overhead is still ~1%: the EIP runs' own cpu ms/frame (35.65 / 20.84
  / 19.57) sit within the un-sampled reps' spread.
* Zero suspend failures in all three dumps.

Local gates, before deployment: exit-BMP diffs against the pre-change binary on
two offline scenes stayed inside the baseline's own run-to-run noise; the d3d9
retained-memory report came back byte-identical; `test-pick-level`,
`test-minimenu-world`, `test-painters-occluders`,
`test-painters-ground-decor` and `test-uitree-builder` pass.

Two suites are broken on this branch **before and after** these changes and
were not touched: `test-uitree` references `inv_slot_offset_x/y`, which exist in
no source file, and `test-world-builder` calls `mkdir(path, mode)`.
