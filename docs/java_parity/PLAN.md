# Plan: catching and passing the Java client

> ## ⚠ READ FIRST: the Java client renders 31 fps, not 50
>
> Measured 2026-08-25 by counting `Pix2D.cls()` (one call per frame) — **31.0 fps,
> steady over 26 samples**. Our client renders **50.0 fps**. Every "% of one core"
> comparison in this document, including the headline **74.7 % against 49.7 %**,
> compares two clients doing different amounts of work per second and is
> **invalid as written**.
>
> | | fps | CPU % of one core | **CPU ms per FRAME** |
> |---|---|---|---|
> | torirs | 50.0 | 74.8 | **14.96** |
> | Java | 31.0 | 50.3 | **16.23** |
>
> **Per frame we already use less CPU than the Java client.** We burn more total
> because we render 61 % more frames. Java's pacer targets ~50 fps and cannot
> reach it: with all three `Pix3D` rasterisers ablated it jumps to **49.5 fps at
> 4.8 % CPU**, so it is raster-bound down to 31.
>
> Consequences for everything below:
> * Compare **CPU ms per frame**, never CPU %.
> * **Report fps in every arm, for both clients.** An ablation on a client that
>   is missing its frame cap absorbs the saving as frame time rather than CPU:
>   removing Java's gouraud raster drops its pixels 41 % and its CPU by *zero*.
> * `docs/java_parity/PLAN.md` §9–12 were written before this was known.



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

---

## 10. CORRECTION to section 9: the pixel comparison was wrong, and it inverts

Section 9 concluded we draw a third of Java's pixels. **That is wrong.** It was
built on `TORIDRAW_SPAN_CENSUS`, and that probe instruments exactly ONE call
site pair -- `gouraudhsllightness.screen.opaque.bary.branching.s4` -- out of ten
kernel classes. Our hottest kernel by profile, the textured one at 15.02 % self,
contributed **zero** to it. The coverage anomaly flagged in 9.2 was the symptom.

`TORIDRAW_FACE_CENSUS` is the right instrument: it records at the dispatch site,
above either kernel, so it survives the asm and covers every class. 1,500 frames:

| per frame | torirs | Java | ratio |
|---|---|---|---|
| triangles | 6,910 | ~5,140 | 1.34x |
| **pixels** | **1,446,366** | **~402,000** | **3.60x** |
| **overdraw** vs 512x334 | **8.46x** | 2.35x | 3.6x |
| **ns per pixel** | **~4.8** | ~10.6 | **0.45x** |

So the true picture is the inverse of section 9:

* **Our per-pixel rasterisation is about twice as fast as Java's.** The
  hand-written kernels are doing their job. Section 0's reading of the two
  disassemblies was right; section 9's arithmetic on top of a broken census was
  not.
* **We rasterise 3.6x more pixels than the Java client.** Overdraw is 8.46x
  against their 2.35x.
* Our triangles are not small either: gouraud averages **168 px**, textured
  **2,022 px**. The "20 px per triangle" in section 9 was the same artifact.

By class, per frame:

| class | faces | pixels | px/face |
|---|---|---|---|
| gouraud | 5,433 | 915,131 | 168 |
| tex blend opaque (asm) | 225 | 454,908 | 2,022 |
| flat | 1,121 | 46,248 | 41 |
| tex blend trans | 113 | 28,735 | 254 |
| tex flat opaque | 18 | 1,343 | 75 |

Caveat, stated rather than buried: face-census area is the **geometric** area of
the triangle (capped per face), not pixels actually stored, so faces clipped by
the viewport inflate it. That cannot account for a 3.6x gap, but it does mean
8.46x is an upper bound on real overdraw.

### 10.1 Java's terrain does NOT use a separate kernel

Checked, because it would have invalidated the Java side the same way. `Pix3D`
exposes exactly six public raster entry points -- three triangle, three span --
and all six are instrumented. `World.java`, which draws the terrain, calls
`Pix3D.gouraudTriangle` (6 sites) and `Pix3D.textureTriangle` (5 sites)
directly. The Java figure includes terrain and is sound.

### 10.2 What the target now is

**Overdraw, and nothing else in the raster.** We paint every pixel of the
viewport 8.46 times where the reference paints it 2.35 times, with a kernel
already twice as fast per pixel. Closing to their overdraw at our per-pixel rate
would cut rasterisation from 34.5 points to under 10.

That makes the ordering:

1. **Find where the 8.46x comes from.** Prime suspects, in order: the terrain
   painting under everything that later covers it, the painter's algorithm
   drawing back-to-front with no occlusion, and `SCENE_SMALL`'s draw distance.
   The reference culls with a tile-visibility pass (`World.resetVisCalc`, which
   shows in its profile); we have `scene_occluders.c` and it is not established
   that it is doing anything here.
2. Present (13.4 % of samples in BitBlt).
3. The 32.1 non-3D points.

Withdrawn from section 9: "attack span setup", "make triangles bigger", and the
claim that the 4-px block loop is dead code. At 168 px per gouraud face the
block loop runs plenty; the span-length distribution that suggested otherwise
was measured on the one unrepresentative kernel the span census covers.

---

## 11. The discrepancy, found: two censuses were measuring different things

Sections 9 and 10 disagreed by a factor of ten in opposite directions. Both were
wrong, and for the same reason: **the two clients' censuses did not measure the
same quantity.**

* Ours recorded the **unclipped** geometric area of the projected triangle,
  capped at the whole 765x503 framebuffer. A terrain face seen at a grazing
  angle projects far outside the viewport and counted its entire area.
* Java's recorded span width **before** the function's own `hclip` clamp, so a
  span running off the left or right edge counted its full width.

Both are now corrected and measure the same thing -- pixels the rasteriser
actually stores. Ours clips the triangle to the viewport exactly
(Sutherland-Hodgman, then shoelace); Java's applies the same clamp `hclip`
applies, computed at the top so no control flow moves. Both report the raw
figure alongside, so the two can never silently drift apart again.

| per frame | torirs | Java | ratio |
|---|---|---|---|
| triangles | 6,924 | 5,100 | 1.36x |
| **pixels stored** | **341,692** | **225,500** | **1.52x** |
| overdraw vs 512x334 | 2.00x | 1.32x | 1.52x |
| **raw / clipped** | **4.21x** | **1.78x** | **2.4x** |
| ns per pixel | ~20.2 | ~22 | ~0.9x |

### 11.1 What is actually true

* **Our per-pixel rasterisation is roughly at parity with Java's**, ~20 ns/px
  against ~22. Not 2x better (section 10), not 4x worse (section 9).
* **We draw 1.52x more pixels and 1.36x more triangles.** Real, and worth about
  1.9 ms of the frame -- but modest.
* **We waste far more work on off-screen area.** Our projected triangles carry
  4.21x more area than lands in the viewport; Java's carry 1.78x. So roughly
  **76 % of our projected triangle area is off-screen** and gets clipped away
  span by span, against 44 % of theirs. Every one of those spans still pays its
  setup before being clipped to nothing.

### 11.2 Where the 2x total actually comes from

Frame CPU, ours 13.3 ms against Java's 7.1 ms:

| | torirs | Java (est.) | gap |
|---|---|---|---|
| rasterisation | 6.9 ms | ~5.0 ms | +1.9 ms |
| everything not 3D | 6.4 ms | ~2.1 ms | **+4.3 ms** |

**The non-3D half is the bigger half.** The 32.1-point measurement from
experiment 1 was right and is where the gap mostly lives -- and the EIP profile
already names its largest single item: 13.4 % of work samples in `BitBlt`.

### 11.3 Revised targets

1. **The non-3D 32.1 points** -- 4.3 ms of the 6.2 ms gap. Present/`BitBlt`
   first (13.4 % of samples, and a damaged-rect present is a known change),
   then the cs2 / input / networking ablations that were never run.
2. **Off-screen overhang.** 4.21x raw-to-clipped against Java's 1.78x. Reject or
   clip triangles at the triangle level rather than per span.
3. **1.36x the triangles, 1.52x the pixels.** Worth ~1.9 ms; genuine but the
   smallest of the three.

Withdrawn: "overdraw is the whole raster gap" (section 10.2) -- our overdraw is
2.00x against Java's 1.32x, a 1.5x difference, not 3.6x. And per-pixel kernel
work is at parity, so neither "our kernel is twice as fast" nor "we are 4x
slower per pixel" survives.

### 11.4 The lesson worth keeping

Three successive conclusions -- 0.34x, 3.60x, 1.52x -- came from one unexamined
assumption: that two counters called "pixels" on either side of a comparison
were counting the same thing. Neither was wrong in isolation; they were
incomparable. **A cross-client number needs the definition checked on both
sides before the ratio is taken, not after it looks surprising.**

---

## 12. Rasterisation: measured on both sides by ablation. Ours already wins.

Java's rasterisation cost had only ever been estimated. It is now measured the
same way ours is -- by deletion. `Pix3D`'s three span functions return at entry
under `JAVA_ABL_NORASTER=1`, so the scene walk, the triangle setup and the edge
stepping all still run and only the pixel writing disappears. Four arms, same
session, minutes apart:

| | baseline | raster ablated | **raster cost** |
|---|---|---|---|
| **Java** | 50.3 % | 4.2 % | **46.1 pts = 9.22 ms/frame** |
| **torirs** | 74.8 % | 43.2 % | **31.6 pts = 6.32 ms/frame** |

Against the corrected pixel counts from section 11:

| | pixels/frame | raster ms | **ns per pixel** |
|---|---|---|---|
| **torirs** | 341,692 | **6.32** | **18.5** |
| Java | 225,500 | 9.22 | 40.9 |

**Our rasterisation is 1.46x faster in absolute time and 2.2x faster per pixel,
while drawing 1.52x more pixels.** No change was needed to achieve this; it was
already true, and every earlier statement to the contrary came from an
instrument, not from the client.

### 12.1 Two independent checks that this is real

* **Java's ablated remainder is exactly the clear.** 4.2 points is 0.84 ms/frame.
  `Pix2D.cls` writes 171,008 pixels at the eight instructions per pixel its
  compiled form shows (section 3.2); on this box that is ~0.7-0.9 ms. The
  residue after removing the spans is the clear, and it lands where the
  disassembly says it should.
* **It also settles the hprof question.** hprof attributed 65 % of Java's
  in-world work to `Pix2D.cls`. By ablation the clear is about **8 %** of its
  frame. The 65 % was safepoint-poll density, exactly as section 3.4 argued --
  now confirmed by a measurement that has no sampling bias at all.
* **The disassembly predicted the ratio.** Java's textured inner loop is ~24
  instructions per pixel with two array bounds checks and constant stack
  spilling out of C1; ours is SSE2, eight pixels per perspective divide, no
  bounds checks. A 2.2x per-pixel gap is the expected size of that difference.

### 12.2 What this means for the remaining gap

Our frame is 74.8 % against Java's 50.3 % in the same session. Rasterisation is
**not** the reason -- we are ahead there by 2.9 ms/frame. The gap is:

| | torirs | Java | gap |
|---|---|---|---|
| rasterisation | 6.32 ms | 9.22 ms | **-2.90 ms (we win)** |
| everything else | 8.64 ms | 0.84 ms | **+7.80 ms** |
| total | 14.96 ms | 10.06 ms | +4.90 ms |

**Everything that is not rasterisation costs us 8.64 ms against Java's 0.84 ms.**
That is the whole gap and then some. The EIP profile already names its largest
single item -- 13.4 % of work samples in `BitBlt` -- and the rest is the
projection, the face sort, the emit walk, CS2, input, networking and the plugin
layer that the Java client does not have at all.

The raster work in sections 3 and 9 of this plan is therefore **closed**. Do not
spend more time on the kernels, the span loops, or overdraw: at 18.5 ns/px
against 40.9 we are already the faster rasteriser, and even eliminating our
rasterisation entirely would leave us slower than the Java client overall.

---

## 13. Rasterisation, measured with the frame rate verified in every arm

Sections 9-12 were all measured without checking frame rate, and the frame rates
are not equal: **we render 50 fps, the Java client renders 31.** Redone with
both clients reporting their own fps, and every figure normalised **per frame**.

| arm | fps | CPU ms/frame |
|---|---|---|
| torirs baseline | 50.0 | 15.15 |
| torirs, raster ablated | 50.0 | 8.75 |
| Java baseline | 31.0 | 16.23 |
| Java, all three rasterisers ablated | 49.5 | 0.97 |

| | pixels/frame | **raster ms/frame** | **ns per pixel** |
|---|---|---|---|
| **torirs** | 341,692 | **6.40** | **18.7** |
| Java | 355,744 | 15.26 | 42.9 |

**Our rasterisation is 2.4x cheaper per frame and 2.3x faster per pixel, on
0.96x the pixel count.** Geometry is at parity: 6,924 faces against 7,269, and
341,692 pixels against 355,744.

### 13.1 The geometry ratios in sections 9-12 were all frame-rate errors

Java's census prints per SECOND and Java runs at 31 fps; dividing by 50 gave
1.52x. Corrected, the two clients draw the same amount:

| per frame | torirs | Java | ratio |
|---|---|---|---|
| triangles | 6,924 | 7,269 | 0.95x |
| pixels | 341,692 | 355,744 | 0.96x |

So "we draw a third of Java's pixels" (S9), "3.6x more" (S10) and "1.52x more"
(S11) were all wrong, and all from the same root cause: **a per-second number
divided by an assumed frame rate.**

### 13.2 Why the per-kernel Java ablations were unreadable

Ablating Java's gouraud raster alone changed its CPU by **nothing** (50.3 % ->
50.3 %) even though `gspans/s` went 1,608,944 -> 0 and pixels fell 41 %. Java's
baseline misses its frame cap, so a client that gets cheaper spends the saving
on frame rate rather than on CPU. Only the all-kernels arm crossed the cap
(49.5 fps) and showed the cost.

**An ablation is only readable when the client hits its frame cap in every arm.**
Ours does -- 50.0 fps in both -- which is why the torirs figures above are sound.

### 13.3 Consequence: rasterisation is not our problem

Per frame, against the Java client:

| | torirs | Java | gap |
|---|---|---|---|
| rasterisation | 6.40 ms | 15.26 ms | **-8.86 ms (we win)** |
| everything else | 8.75 ms | 0.97 ms | **+7.78 ms** |
| total | 15.15 ms | 16.23 ms | -1.08 ms |

We are already the cheaper client per frame. We burn more CPU per SECOND only
because we draw 61 % more frames.

Optimising the rasteriser further would be tuning the one part that is 2.3x
ahead. The remaining work is **everything that is not rasterisation**: 8.75 ms
against 0.97 ms, whose largest named item is the per-frame full-DIB `BitBlt`
(13.4 % of EIP work samples).

---

## 14. "The rest of it", profiled: it is not the UITree

Profiled with `TORIRS_ABL_NORASTER=1`, so model rasterisation is deleted and the
sampler sees only the remaining **8.75 ms/frame**. EIP sampler at ~510 Hz,
18,389 samples over 36.1 s, **50.0 fps confirmed in the arm**, pacing sleep
excluded. 8,288 work samples.

| group | share of non-raster | ms/frame |
|---|---|---|
| **chrome sprite + glyph blit** | **19.0 %** | **1.66** |
| **face sort** (`ToriDraw_ComputeProjectedFaceOrderSmall`) | **14.8 %** | **1.30** |
| **present** (`BitBlt` under `gdi_paint_latest`) | **13.4 %** | **1.17** |
| projection (`Project`, `CalculateCylinderAabb8point`, `AnimApplyTransform`) | 6.6 % | 0.58 |
| `painter_paint_bucket` | 5.2 % | 0.46 |
| UI hit test (`hit_test_interactive_recursive` + its msvcrt) | 4.5 % | 0.39 |
| **UITree emit** (`EmitWalk`, `emit_walk_node`, `app_ui_host_publish_inputs`) | **3.7 %** | **0.32** |
| `ToriRS_FrameNextCommand` | 3.1 % | 0.27 |

**The UITree is not where the time goes.** Emit is 3.7 % of the non-raster work,
about **0.32 ms/frame**. PR #49's retain gate already did its job; there is
nothing left to win there.

### 14.1 Reading the unresolved addresses

Three DLL addresses carry real weight and had to be attributed by their callers
rather than guessed:

| address | samples | called from |
|---|---|---|
| `[0x7c90e514]` (ntdll) | 11,163 | **10,039 from `frame_loop_step`** — the pacing sleep, i.e. idle — and only 1,054 from `gdi_paint_latest`, which is the real `BitBlt` |
| `[0x77c46fa3]` (msvcrt) | 571 | **511 from `ToriDraw2D_BlitArgbAlpha`** — the per-sprite `memcpy` |
| `[0x77c36cc1..cca]` (msvcrt) | 371 | **~315 from `hit_test_interactive_recursive`** |

The first of those is why the idle filter matters: 90 % of the ntdll samples are
the frame cap doing its job, and counting them as present cost would have
inflated presentation about tenfold.

### 14.2 The three worth acting on

1. **Chrome sprite blitting, 1.66 ms/frame.** `soft3d_draw_sprite` does a
   `malloc` + `memcpy` of the sprite's pixels **per draw** before it reaches the
   clipped blit — that `memcpy` alone is 6.2 % of non-raster work, more than the
   entire UITree emit. An opaque/unmodified fast path that blits straight from
   the atlas would remove most of it, and the chrome ablation already bounds the
   whole class at 4.3 points.
2. **The face sort, 1.30 ms/frame**, in one function, over 6,924 faces.
3. **Present, 1.17 ms/frame**, blitting all 384,795 px of the DIB when Java
   blits 197,840 from per-region surfaces. `App::ui_retained_frame` is already
   wired for the damage signal.

Together 4.13 ms of the 8.75, against Java's whole non-raster budget of 0.97 ms.
