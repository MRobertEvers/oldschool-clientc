# Plan: catching and passing the Java client

Companion to `README.md`, which has the measurements this rests on.

## 0. The question that was asked, answered first

> Can you write kernels matching the Java disassembly?

**We already have them, and ours are better.** There is no work here.

Java's `Pix3D.gouraudRaster`, as compiled by C1:

```asm
  mov  esi, [ebx+esi*4+0ch]        ; one shade lookup
  mov  [edi+ecx*4+0ch], esi        ; four stores
  mov  [edi+ebx*4+0ch], esi
  inc  ebx
  mov  [edi+ebx*4+0ch], esi
  inc  ebx
  mov  [edi+ebx*4+0ch], esi
  test dword ptr [0a20100h], eax   ; safepoint poll, per 4 px
  ; plus 2 array bounds checks per group
```

Ours, `3rd/toridraw/graphics/raster/gouraudhsllightness/gouraud_tri_i686.S`:

```asm
6000:
	LOOKUP	%eax, %ecx        /* one shade lookup */
	movl	%ecx, 0(%edi)     /* four stores */
	movl	%ecx, 4(%edi)
	movl	%ecx, 8(%edi)
	movl	%ecx, 12(%edi)
	addl	$16, %edi
	addl	V_SXH4(%esp), %eax
	decl	%edx
	jne	6000b
```

Same structure — one palette lookup per four pixels — because both are
transcriptions of the same reference client. Its header says so outright: *"The
span is walked in blocks of FOUR with ONE palette lookup per block ... Not
eight, not per-pixel"*, and `toridraw_gouraud_tri_asm_test` pins it
bit-identical to the C reference. We differ from Java only by **not** having a
bounds check and a safepoint poll per group.

So the inner loop is not the gap, and "match the Java disassembly" would be a
downgrade. The gap is everything around it.

## 1. Where the frame actually goes

`r_model` is the **parent** scope of `r_project` / `r_sort` / `r_raster`
(`platform_sdl2_renderer_soft3d.c:945-1024`), so those do not add to it. Of the
attributed 3D work:

| | share of attributed model work |
|---|---|
| `r_raster` — actual pixels | **53 %** |
| `r_project` | 25 % |
| `r_sort` | 22 % |

Nearly half of 3D time is spent deciding what to draw, before a pixel is
written. And per frame, from the same report:

| counter | per frame | |
|---|---|---|
| `r_model_drawn` | 579 | models rasterised |
| `r_model_culled` | 299 | rejected on the bounds cylinder — **cheap**, the test returns before the vertex transform |
| `r_model_sort_empty` | **141** | passed cull, paid **full per-vertex projection**, sorted, emitted **zero faces** |
| `r_model_faces` | **6,682** | faces rasterised |

6,682 faces into a 512x334 viewport is roughly **22 px per face** — and the
kernel's own comment records that **59.5 % of spans never enter the 4-px block
loop** at all, i.e. are under four pixels. We are paying triangle and span setup
over and over for a handful of pixels each.

**This is the shape of the problem: too many tiny faces, not a slow pixel loop.**

## 2. The one thing I could not measure, and it gates everything

Both clients run the same algorithm on the same scene. Ours costs 13.2 ms of CPU
per frame, theirs 7.1 ms. Since our inner loop is provably at least as good,
only two explanations remain:

* **(a) We draw more geometry than they do** — weaker occlusion or a longer
  draw distance. Their profile shows `World.resetVisCalc` (2.18 % self), which
  is the reference client's tile-visibility pass.
* **(b) Our per-face and per-model overhead is higher** at equal geometry.

These call for opposite work, so **settle this before writing any kernel.**

### Experiment 1 — decompose `render` without profiler overhead

`TORIRS_PERF` is ~69 % of the XP frame, so its absolute split cannot be trusted.
Do it by deletion instead, the way the chrome number was established (65.9 % ->
61.6 %). Add two env-gated ablations beside the existing `TORIRS_ABL_NOCHROME`:

* `TORIRS_ABL_NORASTER=1` — run project + sort, skip `RenderModel3Raster`.
* `TORIRS_ABL_NOMODELS=1` — skip model dispatch entirely.

Three runs give the true split of the 65.9 % into raster / setup / everything
else, with no instrumentation in the frame. Roughly a 20-minute job; the harness
(`carm.py`, working-set readiness, liveness assertions) already exists.

### Experiment 2 — are we drawing more faces than Java?

Add a face counter to the Java client (`World.renderAll` is ~30 lines from
`Model.draw`; the source is in `LostCity_Server/javaclient`) and print it once a
second. Compare against our 6,682. **If Java draws materially fewer faces, stop
here and go to §4** — no amount of kernel work closes a geometry gap.

## 3. If it is per-face overhead (hypothesis b)

Ranked by measured size, cheapest first.

1. **The 141 sort-empty models.** 14 % of drawn models pay a full per-vertex
   projection and a face sort to emit nothing. The code already flags this:
   *"work spent for no pixels and wants its own name"*. Options: remember the
   verdict per (model, coarse camera angle), or find the cheaper reject that
   the bounds cylinder is missing. Worth up to ~14 % of `r_project + r_sort`.
2. **Span setup, not span fill.** With 59.5 % of spans under 4 px, the
   per-span prologue — edge division, clip test, colour prestep — is the cost.
   The wins here are structural: incremental edge stepping instead of a divide
   per scanline, and a specialised path for the 1-3 px case that skips the
   block-loop setup entirely (the kernel already branches early for it; the
   question is how much prologue still runs before that branch).
3. **`r_sort` at 22 %.** [[scene-small-csr-sorter-slower]] already found and
   fixed a 2.7x regression here; worth re-reading whether the windowed CSR
   sorter is still the right structure at 6,682 faces.

## 4. If it is geometry (hypothesis a) — the likelier case

1. **Occlusion.** The reference client culls by tile visibility
   (`World.resetVisCalc`). We have `scene_occluders.c` and a `hunt/r3-occluders`
   branch; find out whether it is on in this configuration and what it rejects.
2. **Draw distance / LOD.** Compare our visible-tile radius against the
   reference's.
3. **Face priority and merged models.** 6,682 faces from 579 models is ~11.5
   faces per model, which is small — suggesting many tiny models rather than a
   few large ones. Per-model dispatch (1,019/frame) may then be the real cost,
   not per-face.

## 5. Independent of all of the above

* **Present only what changed.** Java blits 197,840 px/frame from per-region
  surfaces; we `BitBlt` all 384,795 every frame. Worth most of the 1.73 s vs
  0.17 s kernel delta. Floors at ~51 %, since the viewport and minimap are
  always damaged. Falls out of damage tracking (§6 of the memory doc).
* **Rule out the plugins.** 8 on the frame path, `app_run` 8.2 % of frame,
  and the Java client has no equivalent. Cheap A/B, and it must be done before
  attributing anything else to the renderer.
* **`TORIRS_MODEL_ZBUFFER=0`.** Opt-in per model (rs2012 imports), so probably
  not live in a Lumbridge scene — but it is a one-env-var A/B and would show
  immediately if depth-tested kernels are running where they need not.

## 6. What NOT to do

* **Do not rewrite the gouraud/texture inner loops to match Java** (§0).
* **Do not touch the framebuffer clear.** Ours is non-temporal SSE; theirs is a
  C1 scalar loop with a bounds check and a poll per pixel. We are ~10x better
  per pixel there.
* **Do not build damage-based drawing first.** Measured ceiling 4.3 of 30
  points. It is item 4, not item 1.
* **Do not quote hprof percentages.** They are safepoint-poll-density weighted
  (README §3.4).

## 7. Order

| # | Work | Why |
|---|---|---|
| 1 | Experiments 1 and 2 | Decide between §3 and §4; ~1 hour total |
| 2 | Plugins A/B, `TORIRS_MODEL_ZBUFFER=0` A/B | Two env vars, rules out 8.2 % of frame |
| 3 | Whichever of §3 / §4 the experiments name | The 30-point gap lives here |
| 4 | Damaged-rect present | Most of the kernel delta |
| 5 | Chrome damage gating on PR #49 | Capped at 4.3 points |

The honest summary: we are losing to a client running on the **non-optimising
C1 JIT**, using an inner loop we have already beaten, which means the loss is in
how much work we hand that loop — and the next hour of measurement decides
whether that is geometry or per-face overhead.

---

## 8. Experiment 1 result — measured, and it moves the target twice

Run 2026-08-25, one binary (`torirs_e1.exe`), three arms back to back, only the
environment differing. Ablation by deletion, no profiler in the frame.

| arm | % of one core | user / 30 s |
|---|---|---|
| baseline | **73.1 %** | 20.05 s |
| `TORIRS_ABL_NORASTER=1` (project + sort, no pixels) | **38.6 %** | 9.77 s |
| `TORIRS_ABL_NOMODELS=1` (no 3D at all) | **32.1 %** | 7.94 s |

Decomposition of the 73.1:

| component | points | share of frame |
|---|---|---|
| **model rasterisation** | **34.5** | **47 %** |
| projection + face sort + model dispatch | 6.5 | 9 % |
| everything that is not the 3D pass | 32.1 | 44 % |

Baseline drifted 65.9 -> 73.1 between sessions on the same build family (scene
population; the spread is about +-3 points and this pair is wider). The three
arms above ran consecutively, so their *differences* are sound even though the
absolute baseline is not comparable across sessions.

### 8.1 This overturns the TORIRS_PERF split

`TORIRS_PERF` put rasterisation at 53 % of attributed model work and
projection + sort at 47 % — near parity. By deletion it is **34.5 against 6.5,
a factor of 5.3**. `r_project` and `r_sort` wrap small per-model work, so the
fixed per-scope clock cost inflates them far more than it inflates `r_raster`.
This is the same trap [[xp-perf-instrumentation-dominates-frame]] records; it
applies *within* the stage table, not only to its total.

**Projection and sorting are not a target.** 6.5 points, and the 141
sort-empty models per frame are a slice of that 6.5, not of the 30-point gap.
Item 1 of section 3 in the plan is withdrawn.

### 8.2 There are two gaps, not one

Java's **entire** frame is 35.5 % of a core. Ours splits into two pieces that
are each about that size:

* **Rasterisation alone: 34.5 points.** Our raster costs what Java's whole
  client costs — while running an inner loop that is provably better than
  theirs per pixel (section 0 of the plan). At 6,682 faces and ~6.9 ms of raster
  per frame that is roughly **2,000 cycles per face** on a ~2 GHz P4, which no
  22-pixel triangle should cost. Either we write far more pixels than the
  viewport implies (a painter's algorithm has unbounded overdraw, and we run one
  for all but the opted-in z-buffered models), or the per-triangle and
  per-scanline prologue dwarfs the fill. **Measuring pixels-written per frame is
  the next decisive number.**
* **Everything that is not 3D: 32.1 points.** With the entire model pass
  deleted we still burn nearly Java's whole-frame budget. The chrome raster
  ablation already accounted for only 4.3 of it, and the clear and the present
  are perhaps 5 more between them. That leaves roughly **20 points in app
  logic, CS2, plugins, networking and input** — a target nobody has looked at,
  and the Java client has no plugin layer at all.

### 8.3 Revised order

| # | Work | Why |
|---|---|---|
| 1 | **Pixel census**: pixels written per frame, and the span-length distribution | Decides whether the 34.5 points is overdraw or per-triangle setup. `TORIDRAW_SPAN_CENSUS` exists; note the makefile withdraws the asm kernels from a census build, which is fine for counting. |
| 2 | **Plugins A/B** — run with all 8 disabled | Bounds the ~20 non-3D points; two minutes, one env var |
| 3 | Whichever of overdraw / per-triangle setup the census names | The 34.5 points |
| 4 | Damaged-rect present | Most of the kernel delta |
| 5 | Chrome damage gating on PR #49 | 4.3 points |

Withdrawn from the earlier ordering: projection/sort work (8.1), and matching
the Java inner loop (section 0 — ours is already better).

---

## 9. How much each client draws — measured on both sides

The Java client was instrumented directly: a counter in `Pix3D`'s three triangle
entry points and three span entry points, reporting on a wall clock. Counts
only, one add per span and never per pixel, so the client stays at its 50 fps
cap and the scene being counted does not change. Ours is the existing
`TORIDRAW_SPAN_CENSUS` probe over 1,500 frames.

| per frame | torirs | Java | ratio |
|---|---|---|---|
| triangles | 6,682 | ~5,140 | **1.30x** |
| spans | 27,045 | ~52,900 | 0.51x |
| **pixels written** | **136,419** | **~402,000** | **0.34x** |
| pixels per triangle | 20.4 | 78.2 | 0.26x |
| pixels per span | **5.04** | ~7.6 | 0.66x |
| overdraw vs 512x334 viewport | 0.80x | 2.35x | |

**We draw a third of the pixels the Java client draws, and take longer doing
it.** Our rasterisation is 6.9 ms/frame (34.5 points of one core) for 136k
pixels -- about **50 ns/px**. Java's *entire frame*, everything included, is
7.1 ms for 402k pixels, so its raster is **under 17.7 ns/px** and really nearer
12. We are at least 2.8x slower per pixel and realistically closer to 4x.

### 9.1 Why: the good kernel almost never runs

The span-length distribution is the whole story:

| span length | share of spans | share of pixels |
|---|---|---|
| 1 px | **34.2 %** | 6.8 % |
| <= 2 px | 53.9 % | 14.6 % |
| <= 4 px | 73.5 % | 27.7 % |
| <= 8 px | 86.1 % | 43.1 % |

A third of our spans are a **single pixel**. Three quarters are four or fewer,
which is the point at which the hand-written kernel's 4-pixel block loop takes
zero trips -- its own comment already recorded 59.5 % taking none, and at 5.04
px/span average it is worse than that. The inner loop we beat Java on is
**effectively dead code**; everything is per-span prologue: edge stepping,
clip test, colour prestep, the call itself.

Java avoids this not by having a better loop but by having **bigger triangles**:
78 pixels each against our 20, and 7.6-pixel spans against our 5.04, so their
4- and 8-pixel unrolls actually execute.

### 9.2 The geometry difference is the opposite of the hypothesis

Section 2 of the plan asked whether we draw *more* than Java. We draw 30 % more
**triangles** and 66 % fewer **pixels**. So it is not a draw-distance or
occlusion deficit -- if anything we cull more. It is that our geometry arrives
as many small triangles where theirs arrives as fewer large ones.

Note also the coverage figure: at 136k pixels into a 171k-pixel viewport we do
not cover the screen once, while Java covers it 2.35 times. Either a large
surface (terrain) reaches the framebuffer through a path the span census does
not instrument, or our scene genuinely contains far less large-area geometry
than the reference. **That is the next thing to check, and it is cheap** -- the
census is per-kernel, so a kernel it does not cover would explain the whole
discrepancy and would also mean the 50 ns/px above is overstated.

### 9.3 What this changes

* **Do not chase the inner loop.** Confirmed twice now: the loop is better than
  Java's and it barely executes.
* **Attack span setup, or attack triangle size.** Either make the prologue cheap
  enough for a 1-5 pixel span, or stop generating so many tiny triangles.
* **The 9.08 % in `ToriDraw_ComputeProjectedFaceOrderSmall`** (EIP profile) is
  the same disease: 6,682 faces to sort, more than Java's 5,140, for less
  painted area.
