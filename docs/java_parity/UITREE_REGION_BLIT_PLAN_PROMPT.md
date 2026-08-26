# Prompt: plan a cheaper blit for the modern UITree under CS2

Copy everything below the line into a fresh session. It is written to be
self-contained: it carries the measurements, the constraints and the dead ends
so that session spends its time designing rather than rediscovering.

**Read the verdict section first.** A damage/dirty-rect system was built,
measured, and then deliberately removed. Do not rebuild it without reading why.

---

## Task

Find the cheapest correct way to get pixels onto the screen for a CS2-driven
UITree, on the Windows XP soft3d lane. Produce a plan, not code. End with a
recommendation and a bounded estimate in **CPU ms/frame**.

The constraint that shapes everything: **CS2 permits arbitrary transparency and
fullscreen world rasterisation, so in the general case every pixel changes every
frame.** Any design whose win depends on pixels *not* changing is a bonus on
idle frames, not an architecture.

## Where to start

Work in a new worktree off branch `merge/v3-pr49`
(`.claude/worktrees/dirtyflag-analysis`, HEAD `1dd65361f`). Read:

1. `docs/2004Scape_Memory_Usage.md` §6 — the PR #49 retain-gate verdict.
2. `git log --oneline ef44c59c6..1dd65361f` — six commits: four of measured perf
   work, one recording a negative result, one reverting the whole damage system
   with its reasons.
3. `src/platform/platform_sdl2_renderer_soft3d.c` — the clear, and
   `viewport_from_scissor`, the one choke point every draw kind funnels through.

Verify claims against the code and against measurement. Several confidently
wrong conclusions have already been produced on this problem by trusting an
instrument without checking what it counted.

## Verdict already reached: do not rebuild damage/dirty-rect drawing

It was built (`ef44c59c6`), refined (`037ced787`), measured, and reverted
(`1dd65361f`). It worked — **12.27 ms/frame against 13.69 without it** — and it
was still the wrong foundation:

- **Its value collapses when the client is most loaded.** That 1.4 ms was
  measured on a stationary scene with 85–93% of frames retained. A moving
  camera, a fullscreen interface, or any transparent overlay expands the damage
  rect to the whole canvas. CS2 makes all three ordinary.
- **Its failure mode is invisible.** A pixel that should have been redrawn and
  never repairs. No frame counter reports it. A real instance shipped and
  survived four measured arms: `TORIRS_BMP_SERIES` and the headless paths call
  `App_Render` with a freshly allocated buffer, damage applied to those, and the
  chrome would have rendered black. Found by accident while taking a screenshot.
- **Refining it made it worse.** Per-rect clear and present touched 19% fewer
  pixels and ran **1.18 ms/frame slower** — see the dead ends below.

If a plan proposes damage anyway, it must carry: a forced full repaint every N
frames (so any staleness is a sub-second flicker, not permanent corruption), a
pixel-level verify mode mirroring `TORIRS_EMIT_VERIFY` (render damaged and full,
`memcmp`, report the differing rect), and a buffer-identity guard. Absent those
three, the answer is no.

Per-region `PixMap`s are the same idea with more bookkeeping. Java's regions are
free because its layout is fixed and non-overlapping; CS2 gives neither, and the
per-rect measurement below already shows more-and-smaller blits losing on this
hardware.

## The two unconditional levers — this is where the plan should go

Both make every frame cheaper regardless of what changed, fullscreen or not.

### 1. Stop clearing what is about to be fully overwritten

The clear is **15.3% of non-raster work**. The census recorded in
`platform_sdl2_renderer_soft3d.c` says only **503 of 384,795** pixels still hold
the clear colour at end of frame — it is 99.87% redundant. It is kept because a
skybox that does not cover every pixel would show whatever the clear removed.

Java resolves this by clearing only the region that genuinely needs it:
`Client.java:5122` calls `Pix2D.cls()` once per frame with `areaGame` bound
(512×334 = 170,048 px) and never clears the chrome regions at all, because they
fully repaint. Eleven of its twelve `cls()` call sites are at `PixMap`
construction.

**The experiment is already tooled.** `TORIDRAW_FB_POISON` fills with a colour
the palette cannot produce and scans for survivors. Run it and find *where* the
503 pixels are.

- All inside the world viewport → clearing only the viewport is provably safe,
  unconditionally: **384,795 → 170,048 px, −56%, every frame.** ~0.5 ms.
- Some in chrome → you have learned exactly which chrome does not self-cover,
  and can either fix that chrome or clear a second small rect.

This is the highest-value item and it needs no dirty tracking whatsoever.

### 2. Halve the bytes per pixel

The present is memory-bandwidth-bound: 240k px × 4 B ≈ 960 KB in ~0.93 ms ≈
**1.0 GB/s**, about what a P4-era GDI blit does. A copy of N bytes cannot be
made faster; it can only be made smaller.

765×503 is 1.54 MB at 32bpp and 770 KB at 16bpp. Going 16-bit halves the clear,
every sprite blit, every raster span store *and* the present, and does not care
whether the frame is damaged or fullscreen. The reference client ran 16-bit for
exactly this reason.

Cost: banding, and a 16bpp variant of every raster kernel. The plan must scope
that honestly — it is the only change that halves the entire pixel budget at
once, and it is the direct answer to "every pixel must be blitted anyway".

### Also worth an hour

- **`SetDIBitsToDevice` vs `BitBlt`-from-memory-DC.** Some XP drivers skip an
  intermediate conversion on one path. Cheap A/B, possibly free.
- **D3D9 present with a software raster.** Rendering directly into a locked
  GPU surface is a *trap*: locked surfaces are write-combined, and the
  rasterizer reads the destination (alpha blend, z-test); read-modify-write on
  WC memory is catastrophic — the same effect that made plain stores lose to
  non-temporal ones in the clear. You would still raster into system RAM and
  copy once. The real question is whether the upload is DMA'd rather than pushed
  by the CPU through GDI, so the CPU stops paying 1.0 GB/s of its own time.
  `TORIRS_ABL_PRESENT_VP` bounds it.

## The measurement rig

XP box `rpdxp` at `http://10.10.10.2:8088` (set `$env:RPDXP`), staging
`C:\dev\mem289`, LostCity 289 server at `10.10.10.1`. Build with
`.\build_winxp.ps1`; profile lane is
`mingw32-make -C src EMBED_SERVER=1 CC=gcc PROFILE=1 winxp`.

**Rules learned the hard way. Breaking any of them has already produced a
confidently wrong answer on this exact problem:**

- Compare **CPU ms/frame**, never CPU%. The clients run at different frame
  rates, and Java renders ~31 fps, not 50.
- A/B by **env var on one binary**, not across builds.
- Use an **in-world** scene. A fresh account lands with the character-design
  modal filling the viewport; on that scene `BlitArgbAlpha` reads as 20% of
  non-raster work and the UI hit test as 0.2%, pointing at entirely the wrong
  function. Click Accept once at canvas (263, 286), then reuse that username.
- In an EIP profile, `frame_loop_step` is the pacing sleep — exclude it before
  taking percentages or everything reads ~3x too cheap. `0x7c90e514` is ntdll's
  `KiFastSystemCallRet`, the leaf of *every* syscall; attribute it to the
  nearest named caller or it collects the sleep, the BitBlt and file I/O into
  one meaningless 62% bar.
- Ablate by deletion, and check the frame rate in every arm.
- Wait ~60 s between arms using the same account, or the server still holds the
  session and the client never reaches the world.

## Dead ends — already measured, do not repeat

- **Per-rect clear and present instead of the bounding box** (`037ced787`).
  19% fewer pixels, **1.18 ms/frame slower** (12.27 → 13.45). Suspects: the
  second BitBlt's GDI transition, and the minimap rect's 146-pixel rows being
  584 bytes, too short for the non-temporal clear to amortise. Treat this as
  evidence that *more, smaller* blits is not automatically cheaper here.
- **Plain stores instead of non-temporal in the clear.** 0.75 ms/frame slower.
  Write-only rows pay read-for-ownership without NT stores.
- **A cached all-opaque fast path in `ToriDraw2D_BlitArgbAlpha`.** All-opaque
  sprites are 31.1% of blits and 69.4% of blitted pixels; skipping the
  per-pixel alpha classification changed nothing (15.25 → 15.24). Chrome
  blitting does not get cheaper per pixel, only by moving fewer pixels.
- **Replacing the face-sort bucket sort.** `TORIDRAW_SPAN_RATIO=<path>` says
  33.3 faces/model against 58.2 depth levels/model — 1.7x span-per-face, so the
  bucket sort is well matched to its data.

## Current standing, in-world bench

| | CPU ms/frame |
|---|---|
| baseline | 15.50 |
| + hit-test `getenv` read once | 14.38 |
| + chrome strip memo | **13.69 (current)** |
| (damage, since reverted) | 12.27 |

Non-raster work (`TORIRS_ABL_NORASTER=1`) profile at the 12.27 point: face sort
21.8%, present 16.7%, clear 15.3%, `painter_paint_bucket` 7.6%, everything else
below 5%. Re-profile before trusting those shares at 13.69.

Both wins so far were the same shape: **a pure function recomputed every frame
that nothing had invalidated.** `getenv` per component per hit-test walk;
a 7,142-component scan per frame for a number that changes only on re-layout.
That class is worth one more sweep before touching the blit.

## Deliverable

1. Where the 503 clear survivors are, and what clearing only the world viewport
   would cost and save.
2. A scoped assessment of 16bpp: which kernels need a variant, what it saves
   across clear + blits + raster stores + present, what it costs in quality.
3. Whichever of the hour-long experiments above is worth running, with results.
4. A staged order where each stage is independently measurable and revertible,
   with the env gate each ships behind.
5. An expected win in CPU ms/frame with its reasoning, and an explicit statement
   of what would make it not pay.
