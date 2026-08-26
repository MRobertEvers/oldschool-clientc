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
| face sort | 1.15 | **structurally sound.** `TORIDRAW_SPAN_RATIO` census: 33.3 faces/model against 58.2 depth levels, 1.7× span-per-face, so the bucket sort is matched to its data and a comparison sort over ~33 elements would not obviously win. It is also already gated — `sd_render_with_kernel_painter` skips it unless `ToriDraw_RenderModel1Project` returns `TORIDRAW_CULL_VISIBLE`. |
| present | 0.91 | **bandwidth-bound.** The desktop is 32bpp (`GetDeviceCaps` BITSPIXEL), so `BitBlt` out of the 32bpp DIB is a straight copy, not a per-pixel format conversion. Marginal cost measured at **2.37 ns/px** with `TORIRS_ABL_PRESENT_VP` (blit 170,048 instead of 384,795 px: 5.79 → 5.28). |
| chrome blit | 0.67 | **refuted by experiment.** A cached all-opaque fast path aimed at 69.4% of blitted pixels changed nothing (15.25 → 15.24). Chrome blitting does not get cheaper per pixel. |
| clear | 0.34 | **already narrowed** to 240,195 px from 384,795, and verified at 0 uncovered pixels over 1,600 frames. |
| tail | 2.72 | nothing above 0.5. |

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
