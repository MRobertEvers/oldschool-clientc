# The non-raster budget, and why halving it needs dirty tracking

Written after an attempt to cut non-raster work in half on the XP soft3d lane.
It reached −18.5%. Rather than leave the rest as "more work needed", the gap is
priced here: each of the four largest items is closed with a measurement, and
the one untried lever is costed from a measured rate rather than a hunch.

Companion to `UITREE_REGION_BLIT_PLAN_PROMPT.md`, which carries the measurement
rig, its traps, and the dead ends.

## Where it got to

Non-raster work, `TORIRS_ABL_NORASTER=1`, in-world bench (rs289lc, `--soft3d`):

| | ms/frame |
|---|---|
| original | 7.10 |
| hit-test `getenv` read once | — |
| + chrome strip memo | 5.98 |
| + narrowed clear (`TORIRS_CLEAR_VIEWPORT=1`) | **5.79** |
| target (half of original) | 3.55 |

Full frame over the same span: 15.50 → 13.69, or ~13.5 with the clear flag on.

Both memoisation wins were the same shape — **a pure function recomputed every
frame that nothing had invalidated**: a `getenv` per component per hit-test walk
(and the tree is walked twice a frame), and a 7,142-component scan for a number
that only changes on re-layout. That class is worth sweeping for again; it is
the cheapest kind of win in this codebase and two of them were sitting in a
profile nobody had taken on an in-world scene.

## Where the remaining 5.79 ms sits

| item | ms | status |
|---|---|---|
| face sort | 1.15 | **structurally sound, one axis left.** `TORIDRAW_SPAN_RATIO` census: 33.3 faces/model against 58.2 depth levels, 1.7× span-per-face, so the bucket sort is matched to its data and a comparison sort over ~33 elements would not obviously win. It is also already gated — `sd_render_with_kernel_painter` skips it unless `ToriDraw_RenderModel1Project` returns `TORIDRAW_CULL_VISIBLE`. The one axis that is neither refuted nor dirty tracking is cache residency: see below. |
| present | 0.91 | **bandwidth-bound.** The desktop is 32bpp (`GetDeviceCaps` BITSPIXEL), so `BitBlt` out of the 32bpp DIB is a straight copy, not a per-pixel format conversion. Marginal cost measured at **2.37 ns/px** with `TORIRS_ABL_PRESENT_VP` (blit 170,048 instead of 384,795 px: 5.79 → 5.28). |
| chrome blit | 0.67 | **refuted by experiment.** A cached all-opaque fast path aimed at 69.4% of blitted pixels changed nothing (15.25 → 15.24). Chrome blitting does not get cheaper per pixel. |
| clear | 0.34 | **already narrowed** to 240,195 px from 384,795, and verified at 0 uncovered pixels over 1,600 frames. See the floor below. |
| everything else | 1.30 | `ToriDraw_Project` 0.26, `Soft3D_Execute` 0.25, `FrameNextCommand` 0.22, `CalculateCylinderAabb8point` 0.21, glyphs 0.12, `AnimApplyTransform` 0.06, then nothing above 0.11%. |

The five items above the line are **75% of non-raster work**. There is no hidden
pool: the profile's named leaves account for 3,668 of 3,722 work samples, and
everything past the top twenty is under 0.11% each.

`ToriRS_Soft3D_RenderFrame`'s 1.12 ms is not all clear. Narrowing the clear by
56% saved 0.30 ms, which puts the clear itself at ~0.54 ms and leaves ~0.58 ms
of command-dispatch self time that LTO has inlined into the same symbol.

### The clear's floor, measured

`TORIRS_CLEAR_PROBE=1` shrinks the clear to 1×1 and lets `TORIRS_CLEAR_VERIFY=1`
report what survives — i.e. which pixels a frame actually depends on the clear
for, rather than which ones it might.

    1600 frames, last 21511 uncovered, worst 31541, bbox x 4..515 y 4..120

Two things follow:

- Only ~21,500 px of the viewport's 170,048 genuinely need clearing (12.6%),
  and under this camera they sit in the top 117 rows, where sky and ceiling show
  through. The bottom 218 rows are fully painted by terrain. The band is a
  function of camera pitch, though, so it cannot be hardcoded — look up and it
  grows.
- **The minimap needs no clear in steady state.** It is fully covered by its own
  draws. The 15,893 uncovered minimap pixels seen earlier were entirely the boot
  transient before the emit walk first publishes a minimap desc. It is kept in
  the clear rect anyway, for ~0.10 ms, because a region rebuild that leaves the
  minimap sprite unready has not been tested.

### The face sort is compute-bound, not memory-bound

The last hypothesis about it: the counting pass gathers `vx`/`vy`/`vz` for three
vertex indices per face — nine loads across three separate arrays, because the
projected vertices are stored SoA. That is up to nine cache lines per face, and
it would make interleaving the projected vertices (AoS) worth a large refactor
that would also speed projection and raster.

Probed by prefetching the next face's vertices four iterations ahead:

| | ms/frame |
|---|---|
| baseline | 5.79 |
| + prefetch | 5.91 |

**Worse by 0.12.** The prefetches are pure added instructions, so the loop is not
waiting on memory — it is compute-bound, and the AoS refactor would not pay.
That closes the last open question about the largest item in the profile.

### The face sort's cache axis, measured

The client asks for `TORIDRAW_SCENE_DEPTH_16K` (`app.c`), so the CSR sorter's
`sm_depth_offset` table is 16,385 ints — **64 KB**. Every model walks a
~58-entry window of it four times (prefix sum, cursor seed, priority partition,
restore) at an offset its own depth decides. 64 KB cannot stay resident in a
P4's L1D; the reference 1,500 levels would be 6 KB and could.

`TORIRS_DEPTH_REFERENCE=1` prices it: **5.79 → 5.57 ms, −0.22**. Real, but
smaller than the table size suggests, because the CSR sorter's per-model
windowing already keeps the working set to a few lines.

Left as an ablation, not shipped: it drops depth resolution from 16,384 levels
to 1,500. That is the reference client's own value, so it may well be the more
faithful setting, but `DEPTH_16K` was chosen deliberately and validating the
change is a rendering-quality question, not a performance one.

## Pricing 16bpp without building it

The measured 2.37 ns/px makes this arithmetic rather than speculation. Halving
the bytes per pixel halves everything that moves pixels:

| | now | at 16bpp | saving |
|---|---|---|---|
| present | 0.91 | 0.46 | −0.45 |
| clear | 0.34 | 0.17 | −0.17 |
| chrome blit | 0.67 | 0.34 | −0.33 |
| glyphs | 0.12 | 0.06 | −0.06 |
| **total** | | | **−1.01** |

That lands at **4.78 ms, −32.7%** from the original. A full 16bpp conversion —
a 16bpp variant of every raster kernel, plus colour banding — **still does not
halve non-raster on this bench.**

## Everything on the table at once

Stacking every non-dirty-tracking lever found, including the two that carry a
cost of their own:

| | ms/frame | vs original |
|---|---|---|
| original | 7.10 | — |
| memoisation sweep (shipped) | 5.98 | −15.8% |
| + narrowed clear (flag) | 5.79 | −18.5% |
| + reference depth levels (ablation, costs resolution) | 5.57 | −21.5% |
| + full 16bpp conversion (priced, not built) | ~4.56 | **−35.8%** |
| target | 3.55 | −50% |

The best case that does not reinstate dirty tracking is about −36%, and two of
its four terms are not free: one drops depth resolution and one drops colour
depth. −50% is not on this list.

### Nor is it reachable by giving up the constraint

Adding dirty tracking back on top of all of it, which is the thing the goal was
in tension with:

| | ms/frame | vs original |
|---|---|---|
| all non-dirty-tracking levers (above) | ~4.56 | −35.8% |
| + damage drawing reinstated | ~3.89 | **−45.2%** |
| target | 3.55 | −50% |

Damage's incremental value is only about −0.67 ms once the clear is already
narrowed, because the two overlap: what damage mostly bought was a smaller
clear and a smaller present, and the clear half is already banked. What is left
is the present (0.91 → ~0.57 over a 62%-of-canvas box) and the chrome outside
that box (~half of 0.67).

One more, found last and worth about −0.10: `struct ToriRS_RenderCommand` is
**104 bytes** (measured, `sizeof`), and `ToriRS_FrameNextCommand` copies one out
by value per call — roughly 208 KB a frame at ~2,000 commands. Returning a
pointer into the frame's own storage would remove it, at the cost of an API
change through the renderer. It is part of the ~1.05 ms that `RenderFrame`
self-time, `Soft3D_Execute` and `FrameNextCommand` account for between them,
most of which is the generator's real work rather than the copy.

So **every lever known to exist, stacked — including the one that was removed on
purpose, the two that cost visible quality, and the command-copy change — comes
to about 3.79 ms, −47%.** The target is not reachable on this bench by any
combination of them. It would need a change to what the client draws, not to how
it draws it.

### Is the per-model work large because we draw too many models?

The 2.09 ms of per-model setup scales with the model count and nothing else, so
the last question is whether that count is inflated — a culling problem, which
would sit outside the drawing system entirely.

`TORIRS_MODEL_CENSUS=1` alongside `TORIDRAW_SPAN_RATIO`:

| | per frame |
|---|---|
| models submitted by the painter | **925.6** |
| models surviving `CULL_VISIBLE` and reaching the sort | ~462 |
| faces sorted | ~15,600 |

So half of what is submitted is culled after projection — culling is working,
not absent. The wasted work is the projection of the 463 that get thrown away,
which at 0.26 ms for the whole projection stage is about 0.13 ms.

And the surviving count is not inflated relative to the reference: the face
census already put our drawn pixel count at **0.96×** the Java client's on the
same scene, and our rasterisation measures 6.40 ms/frame against Java's 15.26.
A client drawing substantially more geometry than Java could not be 2.4× faster
at drawing it. The model count is legitimate scene content.

Reducing it further means drawing less of the world — shorter draw distance,
more aggressive occlusion — which is a visual change, not an optimisation.

### What "non-raster" contains, and why that matters to the target

The metric is defined by `TORIRS_ABL_NORASTER=1`, which deletes the 3D triangle
kernels and nothing else. So it counts, as "non-raster":

- 3D model setup — face sort 1.15, painter bucket 0.47, projection 0.26,
  cylinder bounds 0.21 = **2.09 ms, 36% of the metric**. This is per-model
  per-frame work that the Java client does identically, and no drawing
  optimisation removes it.
- 2D rasterisation — chrome blit 0.67, glyphs 0.12.
- Framebuffer traffic that is not rasterisation at all — clear, present.

Halving the metric therefore requires halving work that neither client avoids.
That is the arithmetic reason the target is unreachable, independent of any
argument about dirty tracking.

## The conclusion

Every remaining route to −50% is a variant of the same idea: *do not redo work
that did not change*. Cache the face order and projected vertices for static
models under a still camera; clear and present only what moved. That is dirty
tracking.

Dirty tracking was built, measured at −1.4 ms/frame, and then deliberately
removed, because:

- its value collapses exactly when the client is most loaded — the −1.4 ms came
  from a stationary scene with 85–93% of frames retained, and CS2's arbitrary
  transparency and fullscreen world make a full-canvas damage rect ordinary;
- its failure mode is a pixel that should have been redrawn and never repairs,
  which no frame counter reports. A real instance shipped and survived four
  measured arms;
- refining it made it worse: per-rect clear and present touched 19% fewer pixels
  and ran 1.18 ms/frame **slower**.

**So the goal and the constraint are in tension, and the measurement says so
rather than an opinion.** Halving non-raster requires reinstating some form of
dirty tracking, and the reasons it was removed have not changed.

If it is ever revisited, it must carry three things it did not have:

1. **A forced full repaint every N frames** (32–64). Cost is 1/N of the saving;
   the benefit is that any staleness becomes a sub-second flicker rather than
   permanent corruption.
2. **A buffer-identity guard.** Damage is a claim about a specific buffer.
   `TORIRS_BMP_SERIES` and the headless paths call `App_Render` with a freshly
   allocated one, and that is the bug that shipped.
3. **An in-frame verification mode.** `TORIRS_CLEAR_VERIFY=1` is a working
   example: poison the canvas, perform the narrowed write, run the frame, scan
   for survivors. One frame, one process — which is what makes it sound. The
   first attempt at this diffed two BMPs from two runs and reported 80 false
   positives, because the minimap dots move between runs.
