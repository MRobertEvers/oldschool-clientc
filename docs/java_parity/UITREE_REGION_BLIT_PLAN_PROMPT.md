# Prompt: plan a region-blit UITree that matches Java's drawing efficiency and survives CS2

Copy everything below the line into a fresh session. It is written to be
self-contained: it carries the measurements, the constraints and the dead ends
so that session spends its time designing rather than rediscovering.

---

## Task

Design (do not implement yet) a drawing architecture for the modern UITree that
reaches the Java client's per-frame blitting efficiency **and** stays correct
under CS2-driven UI, where scripts can move, resize, recolour, retext or
re-model any component at any tick.

Produce a plan, not code. End with a recommendation and a bounded estimate of
what it buys, in **CPU ms/frame**, against the numbers below.

## Where to start

Work in a new worktree off branch `merge/v3-pr49`
(`.claude/worktrees/dirtyflag-analysis`, HEAD `037ced787`). Read these first,
in this order:

1. `docs/2004Scape_Memory_Usage.md` §6 — the PR #49 retain-gate verdict.
2. `src/ui/uitree_emit.h` — `UITreeEmitBuffer`, especially `volatile_refs`,
   `volatile_unrefreshable`, `host_input_dependencies`, `publication_seq`.
3. `src/app.c`, `app_compute_damage` and `App::damage_valid` in `src/app.h` —
   the damage system that exists today.
4. `git log --oneline ef44c59c6..037ced787` — four commits of measured
   perf work whose messages carry the numbers and the reasoning.

Verify claims against the code and against measurement. Several confidently
wrong conclusions have already been produced on this problem by trusting an
instrument without checking what it counted.

## What the Java client actually does

Read it, do not take this on trust:
`LostCity_Server/javaclient/src/main/java/jagex2/`.

- `graphics/Pix2D.java:101` — `cls()` is a scalar loop over `width*height` of
  the currently bound `PixMap`.
- `client/Client.java` has **12** `cls()` call sites. Eleven are at `PixMap`
  construction (startup, resize, scene setup). Exactly **one** runs per frame,
  at `Client.java:5122`, with `areaGame` bound:
  ```java
  Pix2D.cls();
  world.renderAll(camX, camPitch, camZ, var3, camY, camYaw);
  ```
- `graphics/PixMap.java` — `draw(x, y, g)` presents that one region at (x, y).
- `client/Client.java:3555` `gameDraw()` is the dirty-flag core. The static
  chrome borders (`areaBackleft1`, `areaBackright1`, `areaBacktop1`,
  `areaBackvmid1..3`, `areaBackhmid2`) are presented **only** inside
  `if (redrawFrame)`. `redrawSidebar`, `redrawChatback`, `redrawIcons`,
  `redrawChatMode` each gate their own region's redraw and present.

So the architecture is: **one PixMap per screen region, each cleared once at
construction, each redrawn and presented only when its own dirty flag is set.**
Nothing composites across region boundaries, which is what makes "never clear"
safe.

Java's steady-state per-frame cost:

| | pixels |
|---|---|
| clear | 170,048 (`areaGame` 512×334) |
| present | 196,880 (`areaGame` + `areaMap` 172×156) |

## What we do today

One canvas, one command list, one clear, one BitBlt. PR #49's retain gate fires
on **85–93%** of in-world frames, and the damage system built on it clears and
presents only the bounding box of the live regions.

Live regions on an in-world frame, measured (`TORIRS_DAMAGE_REPORT=1`):

```
kind=10 WORLD    box=4,4   513x335   clip=0,0 765x503
kind=13 OVERLAY  box=4,4   513x335   clip=4,4 513x335
kind=11 MINIMAP  box=575,9 146x151   clip=0,0 765x503
bounding box  -> 4,4 717x335 = 240,195 px (62.4% of canvas)
```

| | pixels | vs Java |
|---|---|---|
| clear | 240,195 | +41% |
| present | 240,195 | +22% |

## The measurement rig

XP box `rpdxp` at `http://10.10.10.2:8088` (set `$env:RPDXP`), staging
`C:\dev\mem289`, LostCity 289 server at `10.10.10.1`. Build with
`.\build_winxp.ps1`; profile lane is
`mingw32-make -C src EMBED_SERVER=1 CC=gcc PROFILE=1 winxp`.

Harnesses in the job tmp dir: `carm.py` (launch + measure one arm from
`C:\dev\arm.json`), `cclick.py` (same, but clicks through the character-design
screen first). `symbolize.py` + `leaf2.py` turn an EIP stack capture into
self-time by nearest named frame.

**Rules that were learned the hard way. Violating any of them has already
produced a confidently wrong answer on this exact problem:**

- Compare **CPU ms/frame**, never CPU%. The clients run at different frame
  rates, and Java renders ~31 fps, not 50.
- A/B by **env var on one binary**, not across builds.
- Use an **in-world** scene. A fresh account lands in the world with the
  character-design modal filling the viewport; on that scene `BlitArgbAlpha`
  reads as 20% of non-raster work and the UI hit test as 0.2%, which points at
  entirely the wrong function. `cclick.py` clicks Accept; reuse that username
  afterwards.
- In an EIP profile, `frame_loop_step` is the pacing sleep — exclude it before
  taking percentages, or everything reads ~3x too cheap. `0x7c90e514` is
  ntdll's `KiFastSystemCallRet`, the leaf of every syscall; attribute it to the
  nearest named caller or it collects the sleep, the BitBlt and file I/O into
  one meaningless 62% bar.
- Ablate by deletion, and check the frame rate in every arm.

## Constraints the design must respect

1. **A `ToriDraw_ViewPort` holds one rectangle.** Clipping a draw to a union of
   regions therefore means replaying that draw once per region — and
   `TORIRSRC_POLYGON_BEGIN/POINT/END` and the model commands carry accumulation
   state that cannot be replayed. This is the single biggest structural
   obstacle to a region model, and the plan must say how it is solved.
2. `viewport_from_scissor` in `src/platform/platform_sdl2_renderer_soft3d.c` is
   the one place every draw kind funnels through. Damage clipping already rides
   it. Anything new should ride it too, or explain why not.
3. The renderer cannot decide what is stale. Whether last frame's pixels are
   still correct is a fact about the UI tree, and lives in `App`.
4. A wrong damage/dirty rect fails as **a stale pixel that never repairs**. No
   frame counter reports it. The plan must include how correctness is checked,
   not only how speed is measured.

## CS2 dynamism — the part that must not be hand-waved

The Java design gets its regions for free because the 2004 gameframe is a fixed
layout: `areaGame` is always 512×334 at (4,4). CS2-driven UI is not. Scripts
mount, move, resize, hide and restyle components at runtime, and the resizable
gameframe has no fixed region table at all.

The plan has to answer:

- Where do regions come from when the layout is script-defined? Derived from
  the tree per publication, declared by the interface, or discovered from the
  emit list?
- What happens when a region moves or resizes mid-frame — does its buffer
  reallocate, and what does that cost at 50 Hz?
- Overlapping regions. Java's never overlap; CS2 modals, tooltips, drag
  ghosts and the right-click menu all do. Which region owns a pixel, and how
  does alpha compositing across a boundary stay correct?
- How does this interact with the existing retain gate rather than duplicating
  it? `dirty_gen`, `layout_resolve_seq`, host input epochs and `volatile_refs`
  already answer "did anything change"; a second, disagreeing answer is a bug
  source.
- Memory. Per-region buffers on a 1 GB XP box with a 765×503 canvas — bound it.

## Dead ends — already measured, do not repeat

Each of these has a commit message with the numbers.

- **Per-rect clear and present instead of the bounding box** (`037ced787`).
  Touches 19% fewer pixels and measured **1.18 ms/frame slower** (12.27 → 13.45).
  Suspects: the second BitBlt's GDI transition, and the minimap rect's
  146-pixel rows being 584 bytes, too short for the non-temporal clear to
  amortise. A region design multiplies both effects — treat this as evidence
  that *more, smaller* blits is not automatically cheaper on this hardware, and
  budget for it.
- **Plain stores instead of non-temporal in the damage clear** (`56308d34e`).
  0.75 ms/frame slower. Write-only rows pay read-for-ownership without NT.
- **A cached all-opaque fast path in `ToriDraw2D_BlitArgbAlpha`.** All-opaque
  sprites are 31.1% of blits and 69.4% of blitted pixels, and skipping the
  per-pixel alpha classification changed nothing (15.25 → 15.24). Chrome
  blitting does not get cheaper per pixel; it gets cheaper by moving fewer
  pixels.
- **Replacing the face-sort bucket sort.** Census says span/model 58.2 vs
  33.3 faces/model — only 1.7× span-per-face, so the bucket sort is well matched
  to its data. `TORIDRAW_SPAN_RATIO=<path>` re-runs that census.

## Current standing, in-world bench

| | CPU ms/frame |
|---|---|
| baseline | 15.50 |
| + hit-test `getenv` read once | 14.38 |
| + damage drawing and present | 13.40 |
| + strip memo, NT damage clear | 12.43 |
| current default | 12.27 |

Non-raster work (`TORIRS_ABL_NORASTER=1`) went ~8.7 → ~5.6 ms. The remaining
non-raster profile is face sort 21.8%, present 16.7%, clear 15.3%,
`painter_paint_bucket` 7.6%, everything else below 5%.

Present + clear are **32% of non-raster work and both scale directly with
damaged area**, which is why the region question is the one worth planning.

## Deliverable

A written plan containing:

1. The region model: what a region is, where its bounds come from under CS2,
   and how the one-rect-per-ViewPort constraint is resolved.
2. How dirty state is tracked per region and how that composes with the
   existing retain gate.
3. The overlap and compositing rules.
4. A staged implementation order where each stage is independently measurable
   and independently revertible, with the env gate each stage ships behind.
5. A correctness plan for stale pixels specifically — what test catches a
   region that stops repainting.
6. An expected win in CPU ms/frame with its reasoning, and an explicit
   statement of what would make it not pay, given the per-rect result above.
7. A verdict: is a region model worth it here, or is the right answer to keep
   one canvas and spend the effort on the face sort and the present instead?
   Answering "not worth it", with evidence, is an acceptable outcome.
