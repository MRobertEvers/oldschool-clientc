# Beating the Java client: where the time actually goes

Both clients, same XP box, same rev-289 LostCity world, both pinned at 50 fps.
Everything here was measured on 2026-08-25 against `merge/v3-pr49`
(v3 + PR #49 + the log-channel sweep), except where it says otherwise.

**The headline finding reverses the previous plan.** `docs/2004Scape_Memory_Usage.md`
§4.2 ranked "gate the chrome rasterisation on dirty state" as target 1, on the
reasoning that we repaint ~120,000 pixels of sidebar and chatback that the Java
client does not. That is true, and it is worth **4.3 points of one core** —
about a seventh of the gap. The other six sevenths are **3D model
rasterisation**, which the Java client does too, and does far more cheaply.

---

## 1. The numbers

`% of one core` over a fixed 30 s in-world window, from `GetProcessTimes`, both
clients idle and logged in. This is the number that decides everything below;
it is the only one immune to profiler overhead.

| arm | % of one core | user / 30 s | kernel / 30 s |
|---|---|---|---|
| Java, D3D on (default) | **35.5 %** | 10.48 s | 0.17 s |
| Java, D3D off (`-Dsun.java2d.d3d=false`) | 41.4 % | 12.23 s | 0.17 s |
| C client, v3 + PR #49, logging on | 66.0 % | 17.91 s | 1.91 s |
| C client, + log channels | 65.9 % | 18.03 s | 1.73 s |
| C client, + **all chrome raster deleted** | **61.6 %** | 16.69 s | 1.80 s |

Run-to-run spread on the C arms is about **±3 points** (66.0 / 71.3 on identical
builds), driven by scene population. Differences smaller than that are not
readable from a single pair, which is why the chrome ablation was run as a
deletion rather than as an optimisation.

Three things fall straight out:

* **The gap is 30 points**, 65.9 % against 35.5 %.
* **Chrome rasterisation is 4.3 of those points.** Deleting *all* of it — every
  sidebar sprite, every glyph, the whole chatback — leaves us at 61.6 %. A
  perfect damage system cannot beat that number, because it cannot draw less
  than nothing.
* **Kernel time is ours to fix and is not a GPU story.** With D3D disabled Java
  falls back to a GDI blit (`GDIBlitLoops.nativeBlit` appears in its profile)
  and its kernel time *does not move*: 0.17 s either way, against our 1.73 s.
  The 10x is not "they have a graphics card".

---

## 2. What the Java client does per frame, from its source

Read out of `LostCity_Server/javaclient/src/main/java/jagex2/`, not inferred
from a profile.

`Client.gameDraw()` is the whole frame:

```java
if (sceneState == 2) gameDrawMain();          // 3D viewport, EVERY frame
...
if (redrawSidebar)  { drawSide(); redrawSidebar  = false; }   // gated
if (redrawChatback) { drawChat(); redrawChatback = false; }   // gated
if (sceneState == 2) { minimapDraw(); areaMap.draw(550, 4, graphics); }  // EVERY frame
if (redrawIcons)    { ... }                                    // gated
```

and `gameDrawMain()` ends:

```java
Pix2D.cls();                          // clears the BOUND surface only
world.renderAll(...);                 // 3D raster
entityOverlays(); coordArrow(...); otherOverlays();
areaGame.draw(4, 4, super.graphics);  // blits ONLY the 512x334 viewport
```

**Correction to §2.4 of the memory doc:** the minimap is *not* dirty-gated. It
is redrawn and re-blitted every frame like the viewport. Only the sidebar, the
chatback and the tab icons are gated.

### 2.1 The architecture that matters: many surfaces, not one framebuffer

Java allocates a separate offscreen `PixMap` per screen region and blits each
one independently, only when it has redrawn it. `PixMap.draw` is
`consumerSetPixels(); graphics.drawImage(image, x, y, this)` — a per-region
present.

| surface | size | pixels | blitted |
|---|---|---|---|
| `areaGame` | 512x334 | 171,008 | every frame |
| `areaMap` | 172x156 | 26,832 | every frame |
| `field1616` (sidebar) | 190x261 | 49,590 | only when dirty |
| `field1619` (chatback) | 479x96 | 45,984 | only when dirty |
| `areaBackhmid1` (tab icons) | 249x45 | 11,205 | only when dirty |
| the nine `areaBack*` frame borders | — | — | once, on `redrawFrame` |

Steady state Java blits **197,840 px/frame**. We `BitBlt` the whole
**384,795 px** DIB every frame — **1.95x** — and we clear all 384,795 where
Java clears only the bound 171,008 (**2.25x**).

So Java is not doing something clever with damage rectangles. It has simply
**never had one big framebuffer**: the regions are physically separate images,
and "only present what changed" is the natural consequence rather than an
optimisation someone added.

---

## 3. What the Java client does per frame, from its machine code

hsdis-i386 is installed on the box (FCML 1.3.0 build, the clean-room
disassembler, so it is redistributable), which makes
`-XX:CompileCommand=print,<method>` dump real compiled code. Two things had to
be settled that no sampler could settle.

### 3.1 The JVM is the Client VM — C1 only

```
java version "1.8.0_151"
Java HotSpot(TM) Client VM (build 25.151-b12, mixed mode, sharing)
```

A single core and 1 GB of RAM is not a "server-class machine", so HotSpot picks
C1. **Java is beating us by 30 points without its optimising compiler.** Every
piece of generated code below is C1 output: no vectorisation, no loop unrolling
of its own, bounds checks left in.

### 3.2 Their clear is as bad as it looks

`Pix2D.cls` compiled (`data/java_jit_assembly.txt`):

```asm
loop:
  mov  ebx, <Pix2D class oop>        ; reload the class …
  mov  ebx, [ebx+58h]                ; … and the static `pixels` array, EVERY iteration
  cmp  edi, [ebx+8h]                 ; array bounds check, every iteration
  jnb  throw
  mov  dword ptr [ebx+edi*4+0ch], 0  ; store ONE int
  inc  edi
  test dword ptr [0a20100h], eax     ; safepoint poll, every iteration
  cmp  edi, esi
  jl   loop
```

Eight instructions and a polling load per **4 bytes**, over 171,008 pixels.
Ours is `TORIDRAW_FB_CLEAR32`, non-temporal SSE, measured at 0.50 ms for
384,795 px against their scalar loop's 1.19 ms-equivalent for 171,008. **Our
clear is roughly an order of magnitude better per pixel and is not a target.**

### 3.3 Their 3D raster is a 4-pixel-per-iteration span loop

`Pix3D.gouraudRaster` inner loop, compiled:

```asm
  mov  esi, [ebx+esi*4+0ch]          ; one palette/shade lookup …
  ...
  mov  [edi+ecx*4+0ch], esi          ; … then FOUR stores
  mov  [edi+ebx*4+0ch], esi
  inc  ebx
  mov  [edi+ebx*4+0ch], esi
  inc  ebx
  mov  [edi+ebx*4+0ch], esi
  test dword ptr [0a20100h], eax     ; one poll per 4 pixels
```

That is the classic RS raster shape: **one shade lookup amortised over four
pixels**, hand-unrolled in the Java source. Roughly 3.75 instructions/pixel out
of C1.

### 3.4 Why hprof's numbers must not be used as a cost model

hprof reported 65.14 % of in-world work in `Pix2D.cls` and 1.09 % in all of
`Pix3D`. `-XX:+PrintCompilation` shows the raster methods being compiled
(`gouraudRaster`, `textureRaster`, `flatRaster`, `gouraudTriangle`,
`textureTriangle`), so they are hot; and the assembly shows why the sampler
disagrees:

| method | safepoint polls |
|---|---|
| `Pix2D.cls` | **1 per pixel** |
| `Pix3D.gouraudRaster` | **1 per 4 pixels** |

hprof samples at safepoints, so a loop that polls every pixel is
disproportionately catchable. **The 65 %/1 % split is poll-density-weighted,
not time-weighted.** The tables in `table_java_d3d_*.md` are kept because the
*shape* is informative — what appears at all, and what never appears — but no
plan should be built on their percentages.

What the tables legitimately show is an absence: **no `drawSide`, no `drawChat`,
no `PixFont` anywhere in the in-world profile.** The chrome really is not being
drawn.

---

## 4. What our client does per frame

`TORIRS_PERF=1`, 1800 frames, in-world (`table_c_client_stages.md`, raw in
`data/c_client_torirs_perf.txt`). The profiler is ~69 % of the frame on this
box, so these are **ratios**, not milliseconds.

| stage | share of frame |
|---|---|
| **render** | **86.3 %** |
| app_run | 8.2 % |
| present | 3.3 % |
| logic | 2.8 % |
| cs2 | 1.9 % |
| interact | 1.6 % |
| build / paint | 1.4 % each |
| **emit** | **1.1 %** |

and inside `render`:

| sub-stage | share of render | share of frame |
|---|---|---|
| **r_model** | **76.0 %** | **65.6 %** |
| r_raster | 24.4 % | 21.0 % |
| r_project | 11.6 % | 10.0 % |
| r_sort | 10.3 % | 8.9 % |
| **chrome (r_sprite+r_font+r_rect)** | **9.0 %** | **7.8 %** |
| r_clear | 1.9 % | 1.6 % |

The 7.8 % predicted for chrome matches the ablation's 4.3 points of 65.9 %
(6.5 %) closely enough to trust both.

---

## 5. Every place Java saves time that we do not

Ordered by measured or bounded value, not by how interesting it is.

| # | What Java does | What we do | Worth |
|---|---|---|---|
| 1 | Rasterises the 3D scene with a 4-px unrolled span loop, one shade lookup per 4 px | `r_model` is 65.6 % of our frame; our spans average **7.24 px** and we are span-setup bound, not store bound | **the bulk of the 30-point gap** |
| 2 | Blits 197,840 px/frame from per-region surfaces | `BitBlt` of the whole 384,795 px DIB every frame | present is 3.3 % of frame; kernel 1.73 s vs their 0.17 s |
| 3 | `drawSide`/`drawChat` gated on dirty flags | rasterise the whole command list every frame | **4.3 points**, measured by deletion |
| 4 | Clears only the bound 171,008 px surface | clear all 384,795 px | 1.6 % of frame — **but our clear is already ~10x better per pixel; not a target** |
| 5 | No per-frame logging | was writing 178 KB of stderr per 30 s, ~1 syscall/frame | 0.18 s kernel — **fixed**, in this branch |
| 6 | No plugin layer on the frame path | 8 plugins enabled, `app_run` 8.2 % of frame | unmeasured; §4.2 item 3 still open |

And two places **we** are already ahead, recorded so nobody spends a day on them:

* **The clear.** Non-temporal SSE against a C1 scalar loop with a bounds check
  and a safepoint poll per pixel.
* **The emit walk.** 1.1 % of frame, and PR #49's retain gate takes the steady
  state to ~23 ns/op. There is nothing left to win here — which is the main
  practical finding about PR #49 itself.

---

## 6. What this means for the plan

1. **Do not build damage-based drawing first.** It is capped at 4.3 points, and
   the machinery is the most complex item on the list. It belongs after the
   raster work, and PR #49 is the right foundation for it when that time comes
   (see `docs/2004Scape_Memory_Usage.md` §6 for the adopt-with-changes verdict
   and the specific follow-ups).
2. **Go at `r_model`.** It is 65.6 % of the frame and the gap is ~30 points;
   nothing else can close it. The census already says our spans are 7.24 px and
   that we are span-setup bound — the Java code is a direct existence proof
   that a 4-px-per-iteration inner loop with an amortised shade lookup is
   enough to hold 50 fps on this hardware, out of a *non-optimising* JIT.
3. **Then the present.** Blit the regions that changed instead of the whole DIB.
   This falls out of item 1's damage tracking, and is worth most of the kernel
   delta.
4. **Rule out the plugins** before attributing anything further to the renderer.

The uncomfortable summary: the Java client is not beating us with better
architecture in the parts we have been optimising. It is beating us in the
inner loop of the software rasteriser, while running on the *client* JIT.

---

## 7. Files here

| file | what it is |
|---|---|
| `table_java_d3d_on.md` / `table_java_d3d_off.md` | hprof in-world tables, idle pump and boot stacks removed. **Read the shape, not the percentages** (§3.4). |
| `table_c_client_stages.md` | our `TORIRS_PERF` stage split, as ratios |
| `flame_java_d3d_on.svg` / `flame_java_d3d_off.svg` | flamegraphs of the in-world Java frame |
| `*.svg.folded` | folded stacks, regenerable into any flamegraph tool |
| `data/java_d3d*.hprof.txt` | raw hprof reports |
| `data/java_jit_assembly.txt` | JIT-compiled machine code for `Pix2D.cls` and the three `Pix3D` rasters |
| `data/c_client_torirs_perf.txt` | raw `TORIRS_PERF=1` report |

### Reproducing

Server: LostCity Engine-TS branch 289, `engine.revision 289`, web 80, game
43594. The client needs `--rev lc289`; `--rev 289` is not a rev *name* and,
with `OPT=1`'s `-DNDEBUG`, segfaults with an empty log instead of saying so.

Java arms: `tools/mem/xp_java_login_measure.py` with `detach: true`, then
sample CPU. Add `-Dsun.java2d.d3d=false` to `args` for the GDI arm, and
`-agentlib:hprof=cpu=samples,interval=10,depth=12` for a profile.

C arms: launch and measure in **one** rpdxp script — it reaps the process tree
when a script ends, and `CREATE_NEW_CONSOLE` does not survive that. Readiness
must be checked on working-set size, not on a log line: the boot milestones are
`TORIRS_LOG` narration and are compiled out of an optimized build.
