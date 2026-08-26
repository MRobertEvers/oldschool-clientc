# Four fixes, re-measured on the XP box

Same lane, same box, same method as [`profiles.md`](profiles.md) — fullscreen
1024x768, live osrs239 world, Lumbridge courtyard, cpu ms/frame, palindrome
arm order, 2 reps, 60 s window each. Only the binary changed.

| round | binary | what landed |
|---|---|---|
| baseline | `e9b5777c…` | `v3` @ `3d0390d46` + the fullscreen patch |
| A | `4f4c655b…` | frame-command wipe, layout dirty list, element-id kind tag |
| B | `eef650f6…` | scenery pool indexed by element id |

| commit | change |
|---|---|
| `2b48c3296` | `ToriRS_FrameNextCommand`: one `kind` store instead of a 128-byte wipe per command |
| `a076f7665` | `UITree_LayoutResolve`: resolve from a dirty list instead of sweeping every component |
| `5fa22160b` | element ids carry their kind, so pick classification asks one entity pool instead of four |
| `5bb767296` | the scenery pool is indexed by element id, so the lookup never walks at all |

---

## 1. Frame cost

**cpu ms/frame, best of two reps** — the axis `profiles.md` §2.2 argues for,
because `rpdxp.exe` takes 27-40% of this box's one cpu and not equally across
arms.

| arm | baseline | round A | round B | total |
|---|---|---|---|---|
| `--soft3d` | 41.95 | 35.23 | **34.65** | **−17.4%** |
| `--d3d9` | 23.08 | 20.58 | **19.72** | **−14.6%** |
| `--d3d9-zbuffer` | 21.46 | 20.18 | **18.31** | **−14.7%** |

**Every run, so the spread is visible rather than summarised:**

| arm | baseline | round A | round B |
|---|---|---|---|
| `--soft3d` | 41.95, 41.96 | 36.16, 35.23 | 34.83, 34.65 |
| `--d3d9` | 23.51, 23.08 | 20.58, 21.12 | 19.72, **21.33** |
| `--d3d9-zbuffer` | 21.46, 22.07 | 20.34, 20.18 | 18.43, 18.31 |

That bolded 21.33 is a contaminated run, not a measurement: `rpdxp` took 39.0%
of the cpu in it against 27.4% in its pair, and its in-world detection came in
at 38.7 s against 4.7 s. Called out rather than quietly dropped — best-of is
what the table above uses, and this is why best-of and not mean.

**Frame rate and wall clock**, contaminated by the streamer and reported only
because hiding them would be worse:

| arm | fps baseline | fps round B | wall ms baseline | wall ms round B |
|---|---|---|---|---|
| `--soft3d` | 16.5, 16.3 | 19.7, 19.6 | 60.61, 61.35 | 50.76, 51.02 |
| `--d3d9` | 31.8, 31.9 | 36.6, 28.4 | 31.45, 31.35 | 27.32, 35.21 |
| `--d3d9-zbuffer` | 30.0, 29.4 | 32.7, 34.0 | 33.33, 34.01 | 30.58, 29.41 |

**What is and is not a result.** The box is bimodal by ~7.5%, so:

* All three totals (−17.4%, −14.6%, −14.7%) are **outside** that and are real.
* **Round B on its own is not, arm by arm** — soft3d −1.6%, d3d9 −4.2%,
  zbuffer −9.3%. Only the zbuffer arm's clears the noise floor. What makes
  round B credible is not any single delta but the composition in §2: the
  symbol it targets left the profile entirely, in all three arms.
* `--d3d9-zbuffer` is now **ahead of the painter** on cpu for the first time
  (18.31 vs 19.72, 7.2%), and both of its reps beat both of the painter's.
  Still close to the noise floor; treat as a lean, not a finding.

Working set is unchanged — 105 MB soft3d, 250/255 MB D3D9. None of these four
changes was about memory.

---

## 2. What left the frame

900 in-world frames per arm, EIP sampler, shares scaled onto each arm's own
measured cpu ms/frame. Full tables: `eip-*.txt`, with the baseline ones kept
beside them as `eip-*-before.txt`.

### Two symbols are gone outright

Both were top-5 items. Neither appears in **any** arm's top 40 now (the cutoff
is ~0.14 ms):

| symbol | soft3d | d3d9 | d3d9-zbuffer |
|---|---|---|---|
| `UITree_LayoutResolve` | 4.382 → **—** | 2.693 → **—** | 2.989 → **—** |
| `app_world_pick_finish` | 0.907 → **—** | 0.838 → **—** | 1.010 → **—** |
| `World_SceneryGetByElementId` | 0.426 → **—** | 0.187 → **—** | 0.197 → **—** |

`UITree_LayoutResolve` was the **largest single in-image symbol in both D3D9
frames**. `app_world_pick_finish` was the largest non-renderer item after it.

### Measured causes, on the dev machine

| | before | after |
|---|---|---|
| layout node visits / frame | 4,114 | **141** |
| scenery pool list steps / frame | 55,674 | **0** |
| `pick_finish` stage, mean | 78.4 µs | **1.6 µs** |
| render commands zeroed / frame | 1,621 × 128 B | one `kind` store each |

### The rest of the movement

| symbol | soft3d base → B | d3d9 base → B | zbuffer base → B |
|---|---|---|---|
| `ToriRS_FrameNextCommand` | 0.709 → — | 0.655 → — | 0.576 → — |
| `ToriDraw_ComputeProjectedFaceOrderSmall` | 2.568 → 2.500 | 2.352 → 2.365 | 0.184 → — |
| `d3d9_draw_model` | — | 0.349 → — | 1.845 → 2.029 |
| `d3d9_bake_pose_vertices` | — | 0.676 → 0.857 | 0.744 → 0.890 |
| `app_plugin_highlights_rebuild_pools` | 0.592 → — | 0.583 → 0.710 | 0.635 → 0.655 |
| *ntdll.dll* | 3.756 → 3.453 | 3.333 → 3.079 | 2.243 → 2.374 |
| *msvcrt.dll* | 1.269 → 1.150 | 1.383 → 1.432 | 1.765 → 1.824 |

Read that as *shares of a frame that got smaller*. A row flat in ms is a
larger share of the frame now, which is why several look flat or slightly up:
the frame lost 3.2-7.3 ms underneath them.

---

## 3. What is on top now

### `--d3d9-zbuffer` — 18.31 ms/frame

| share | ms | symbol |
|---|---|---|
| 12.97% | 2.374 | *ntdll.dll* |
| 11.08% | 2.029 | `d3d9_draw_model` |
| 9.96% | 1.824 | *msvcrt.dll* |
| 4.96% | 0.907 | `d3d9_ui_draw_sprite` |
| 4.86% | 0.890 | `d3d9_bake_pose_vertices` |
| 4.60% | 0.843 | `trspk_toridraw_bake_face` |
| 4.26% | 0.780 | `trspk_toridraw_face_colors` |
| 3.58% | 0.655 | `app_plugin_highlights_rebuild_pools` |
| 3.30% | 0.604 | `emit_walk_node` |

### `--d3d9` (painter) — 19.72 ms/frame

| share | ms | symbol |
|---|---|---|
| 15.61% | 3.079 | *ntdll.dll* |
| 11.99% | 2.365 | `ToriDraw_ComputeProjectedFaceOrderSmall` |
| 7.26% | 1.432 | *msvcrt.dll* |
| 5.20% | 1.026 | `painter_paint_bucket` |
| 4.35% | 0.857 | `d3d9_bake_pose_vertices` |
| 4.30% | 0.848 | `d3d9_ui_draw_sprite` |

### `--soft3d` — 34.65 ms/frame

| share | ms | symbol |
|---|---|---|
| 15.18% | 5.260 | `toridraw_gouraud_tri_opaque_s4_asm` |
| 11.01% | 3.815 | `toridraw_textri_opaque_lerp8_v3_asm` |
| 9.97% | 3.453 | *ntdll.dll* |
| 7.21% | 2.500 | `ToriDraw_ComputeProjectedFaceOrderSmall` |
| 6.67% | 2.312 | `draw_texture_scanline_transparent_blend_…` |
| 5.64% | 1.955 | `ToriDraw2D_BlitArgbTiledAlpha` |

**The three D3D9 targets left:** ntdll (2.4-3.1 ms — the kernel transition per
DP2 flush, so clustering submissions, not micro-optimisation; `d3d9.dll`'s own
code is 0.25 ms), `d3d9_draw_model` (2.03 ms in zbuffer mode, the per-face
material classification `hotspot-plan.md` §2.1 D3 proposes caching per pose),
and msvcrt (1.8 ms of vertex-staging memcpy). The vertex-bake trio
(`d3d9_bake_pose_vertices` + `trspk_toridraw_bake_face` +
`trspk_toridraw_face_colors`) is 2.51 ms together and is the same target as
D3.

---

## 4. Verification

**On the box, each round:**

* Every arm's exit BMP is 1024x768 — no arm quietly letterboxed a 765x503
  canvas and got faster for free.
* All four handrolled asm kernels `nm`-verified in each measured binary.
* All timing logins clean; no `reply=5`, so no run measured a login screen.
* Sampler overhead ~1%: the EIP runs' own cpu ms/frame (34.60 / 20.37 / 18.06)
  sit within the un-sampled reps' spread.
* Zero suspend failures in every dump.

**Locally, before each deployment:** exit-BMP diffs against the previous binary
on two offline scenes stayed inside the baseline's own run-to-run noise
(neither lane is bit-deterministic); the d3d9 retained-memory report came back
byte-identical — which is what caught the trspk pose table being sized off an
unmasked tagged id (`pose_tables_cpu 65536.28 MB`) before it ever reached the
box; `test-pick-level`, `test-minimenu-world`, `test-painters-occluders`,
`test-painters-ground-decor` and `test-uitree-builder` pass; `OPT=0` and
`OPT=1` both build clean.

**Pre-existing, before and after, not touched:**

* `test-uitree` references `inv_slot_offset_x/y`, which exist in no source file.
* `test-world-builder` calls `mkdir(path, mode)`.
* `test-world` fails at "SIM: scene reset midflight" — verified identical at
  `HEAD` and at the branch base `3d0390d46` by building `world.c` from each.
* **The compass does not render.** Present in the baseline binary and in the
  *software* exit BMP as well as the live D3D9 capture, so it is neither a
  D3D9 bug nor anything these changes caused. Separate issue.
