# 2004Scape memory and CPU: the rs289lc world

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



What the C client costs to run the LostCity rev-289 world, measured against the
Java client it replaces, on both a modern machine and the Windows XP target.

The short version:

* **Memory: the C client wins.** On the XP box, in the same scene, it peaks at
  **67.6 MB** against the Java client's **89.0 MB**.
* **CPU: the C client loses.** Same box, same scene, both pinned at 50 fps:
  **78.6 % of one core against 49.7 %**. Removing a per-frame `SetWindowText`
  took us to **74.7 %**; the gap is still 1.5x. Profiling the Java client shows
  why: it **redraws the sidebar and chatback only when they change**, and it
  **presents through Direct3D** rather than through a per-frame GDI blit.
* The **`<64 MB` goal is met only in a quiet scene**. 58.8 MB standing on
  tutorial-island-style terrain, 67.6 MB in a populated courtyard, 77.3 MB on
  Windows 11 (64-bit pointers), and **171.6 MB on the d3d9 lane, which is the
  default** — the renderer choice dominates everything else on this page.

---

## 1. How to reproduce this

### 1.1 The server

`manifests/manifest_rs289lc.ini` reads its cache and its login checksums off a
LostCity server; nothing in this repo builds either. Bring one up:

```sh
git clone https://github.com/LostCityRS/Engine-TS --single-branch -b 289 engine
git clone https://github.com/LostCityRS/Content   --single-branch -b 289 content
cd engine && npm install
# start.js's setup is a browser wizard; the defaults it would write are the
# ones this world needs (web 80, game 43594, engine.revision 289, sqlite), so
# write them straight from the engine's own generator instead:
#   createDefaultWorldConfig() -> saveWorldConfig()
npm run sqlite:migrate
npx tsx src/app.ts            # wait for "World ready"
```

The RSA keypair in `engine/data/config/public.pem` already matches the
`rsa_exp` / `rsa_mod` in the manifest, byte for byte. Nothing to configure.

**Use a fresh account for every run.** The previous run's character is still in
the world for a while after the client exits, and a repeat login is answered
with reply 5 ("already logged in"), which the client reports as a bare
`loginproto: login rejected, reply=5` and then sits at the login screen — a
memory measurement of a client that never loaded a world, with nothing in the
number to say so.

### 1.2 A client-side fix this needed

The client asked for `GET /title`, and this engine routes the jag archives as
`/title<crc>` — the checksum is a path component. Bare `/title` is not an older
spelling of that route; it is the same 404 with an empty parameter. So
`PlatformXIOOnDemand_New` failed at its `/versionlist` read, the on-demand
source was never built, and the client then dereferenced a NULL disk cache and
segfaulted with no output at all.

Fixed in `src/platform/platform_x_io_ondemand.c`: fetch `/crc` once, cache it,
and build every jag route as `<stem><checksum>`.

Two things that made this slower to find than it should have been, both worth
knowing:

* `OPT=1` compiles `-DNDEBUG`, so `assert()` is compiled out — including the
  `assert(px->dat1_disk || px->dat1_on_demand)` written for exactly this case.
  `src/makefile` documents that this contradicts CLAUDE.md. The assert would
  have named the problem instead of a segfault naming nothing.
* stdout is fully buffered when redirected, so a crash loses every line the
  client had printed. Get the backtrace under gdb rather than trusting an empty
  log.

### 1.3 The measurement

One harness, three places, the same kernel counters everywhere — otherwise the
C-vs-Java comparison is not a comparison:

| tool | runs on | what it does |
|---|---|---|
| `tools/mem/measure_peak.ps1` | Windows 11 | launches, polls, reports peak |
| `tools/mem/xp_measure_peak.py` | XP, via rpdxp | same, `GetProcessMemoryInfo` |
| `tools/mem/xp_launch_detached.py` | XP | launches detached, writes the pid |
| `tools/mem/xp_java_login_measure.py` | XP | launches the Java client **and types the login** |
| `tools/mem/xp_peak_read.py` | XP | reads peak counters of a running pid |
| `tools/mem/xp_cpu_measure.py` | XP | `GetProcessTimes` over a fixed window |
| `tools/mem/resolve_sites.sh` | anywhere | `addr2line` over a MEMPROF report |

`.NET`'s `Process.PeakWorkingSet64` / `PeakPagedMemorySize64` and Win32's
`PROCESS_MEMORY_COUNTERS.PeakWorkingSetSize` / `PeakPagefileUsage` are the same
two numbers, which is what lets the tables below sit side by side.

Three properties of these counters shape every script above:

* **Peak must be read from a live process.** The fields die with the handle.
  There is no after-the-fact read, so the poll loop *is* the measurement.
* **Peak is a high-water mark maintained since process start**, so a reader
  that attaches late still sees the whole run. That is what makes
  "log in by hand, then read the peak" sound, and it is why the XP tooling is
  split into a launcher and a reader.
* **`WorkingSetSize` is only ever "now".** Do not report it as a peak.

Runs are bounded by `TORIRS_MAX_FRAMES`, not by a timer: two runs of the same
frame count do the same work, two runs of the same duration do not. Peak is
reproducible to **±0.02 MB** across runs (77.68 / 77.70 MB), so sub-MB deltas
below are real.

### 1.4 Driving the XP box

`rpdxp` on `http://10.10.10.2:8088` — `/fs/put`, `/scripts/put`, `/scripts/run`
(**120 s cap**), `/input`, `/stream` (MJPEG). Two traps, both of which cost a
round here:

* **A child that shares the script's console dies when rpdxp reaps the
  script.** Launch detached work with `CREATE_NEW_CONSOLE`. The symptom is not
  an error: it is a client that was demonstrably logged in a minute ago and is
  simply gone when you come back for its counters, taking the peak with it.
* **`netsh interface portproxy` accepts and lists v4tov4 rules it cannot
  serve.** The feature rides on the IPv6 stack, so without
  `netsh interface ipv6 install` nothing ever binds and `show all` still prints
  your rules. Check `netstat -an` for an actual LISTENING line.

The port proxy is needed because the standalone Java client hardcodes
`http://127.0.0.1:<portoff+80>` as its code base (`Client.getCodeBase`) and
dials the game socket at the same host — it can only talk to the machine it
runs on. Proxying 80 and 43594 to 10.10.10.1 moves the wire instead of patching
the binary under measurement.

---

## 2. The numbers

### 2.1 Windows XP — the target

Single-core, 1022 MB RAM, 612 MB free at rest, so nothing here is a trimmed
working set. Client built `PLATFORM=win32 OPT=1`:
**`-O3 -flto -DNDEBUG -march=pentium4 -mfpmath=sse`**. (`TORIDRAW_OPT=1` is
*not* set and should not be: it pins ToriDraw to `-O2`, which is a debug aid,
not an optimisation.)

| in-world, same populated courtyard | peak WS | peak private | CPU %/core |
|---|---|---|---|
| **C client, `--soft3d`** | **67.6 MB** | 84.0 MB | **78.6 %** |
| **Java client, `-Xmx64m`** | **89.0 MB** | 95.0 MB | **49.7 %** |
| C client, d3d9 (**the default lane**) | 171.6 MB | 221.3 MB | not measured |

Quieter scene, for scale — the same C client standing on starting terrain with
`world_load: 9 chunks, 317 locs, 308 models`:

| | peak WS |
|---|---|
| C soft3d, 900 frames (~21 s) | 58.3 MB |
| C soft3d, 110 s | 58.8 MB |

The 21 s and 110 s figures differ by 0.4 MB, so the client is **not** slowly
growing — it reaches its working set and stays there. The gap between 58.8 and
67.6 MB is scene content (players, their equipment models, a denser loc set),
not time.

### 2.2 CPU, in detail

30 s window, in-world, idle, both clients at 50 fps:

| | C client (soft3d) | Java client |
|---|---|---|
| CPU user | 20.39 s | 14.83 s |
| CPU kernel | **3.19 s** | **0.08 s** |
| CPU total | 23.58 s | 14.91 s |
| % of one core | **78.6 %** | **49.7 %** |

The frame rates really are equal, which is what makes this comparable: the C
client renders **1500 frames in 30.0 s — 50.0 fps exactly**. Measured by
differencing two bounded runs (750 frames in 16.4 s, 2250 in 46.4 s) so that
boot time cancels rather than being estimated.

Two separate problems live in that table:

* **Kernel time is 40x Java's.** 3.19 s over 30 s is 2.1 ms of kernel per
  frame, on a client whose whole frame budget is 20 ms.
* **User time is 1.37x Java's** — 20.39 s against 14.83 s. This one is not a
  syscall problem; it is the software rasteriser and everything else on the
  frame path being more expensive than the Java client's.

#### Fixed: the window title was rewritten every frame

`update_window_title()` ran unconditionally in the main loop, calling
`SetWindowTextA` fifty times a second to display
`ToriRS iface=84 hover=-1 clicked=2461`. On Windows that is not a string
assignment: it enters the kernel, posts `WM_SETTEXT`, and repaints the
non-client title bar — for two numbers only a developer reads, and which no
developer was reading, because the number that changes is the *hovered
component id*.

The window is now called `ToriRS` and never renamed:

| | before | after |
|---|---|---|
| CPU kernel (30 s) | 3.19 s | **2.08 s** |
| CPU total | 23.58 s | 22.42 s |
| % of one core | 78.6 % | **74.7 %** |

About a third of the kernel time, for deleting one call. Scene population is
not identical across the two runs (peak WS 67.6 vs 61.9 MB), so read the exact
magnitude as approximate — but a 1.1 s drop in *kernel* time is the right shape
for removing 1500 `SetWindowText` calls.

Same shape as the per-spawn `fprintf` that once cost 6 ms a frame: an
unconditional, apparently-free call on a per-frame path.

### 2.4 What the Java client is doing — profiled, not guessed

The XP box has `jdk1.8.0_151`, so hprof is available:
`-agentlib:hprof=cpu=samples,interval=10,depth=12`. It writes its report at
JVM shutdown, so the client must *exit* — `GameShell.windowClosing` ->
`shutdown()` -> `System.exit(0)`, so `WM_CLOSE` works and a kill does not.
`tools/mem/xp_java_profile.py` does the whole run.

7266 samples, in-world, 40 s:

| rank | self | method |
|---|---|---|
| 1 | 63.07 % | `sun.awt.windows.WToolkit.eventLoop` |
| 2 | **21.46 %** | `jagex2.graphics.Pix2D.cls` |
| 3 | 1.25 % | `jagex2.io.ClientStream.write` |
| 6,7 | 0.95 % | `sun.java2d.d3d.D3DRenderQueue.flushBuffer` |
| 8,11 | 0.62 % | `jagex2.dash3d.World.resetVisCalc` |

hprof samples every thread, blocked ones included, so rank 1 is the AWT pump
*idle* — roughly 37 % of samples are real work. Two things fall out of the rest,
and both are about us rather than about them:

**Java presents through Direct3D; we present through the kernel.** The stack
under rank 6 is `D3DSwToSurfaceBlit.Blit` -> `D3DBlitLoops.Blit` ->
`D3DRenderQueue.flushNow`: Java2D on XP picks its D3D pipeline, so the
software-rendered pixel array is uploaded and blitted by the GPU, for ~1 % of
samples and **0.08 s of kernel time in 30 s**. Our `--soft3d` lane does a GDI
`BitBlt` of a 1.54 MB DIB section every frame, which is most of our remaining
2.08 s.

**Their clear is naive and still cheaper than ours could be.** `Pix2D.cls` is a
scalar `for` loop over `int[]`, which is why it is their single biggest Java
cost — but it clears only the *bound surface*, `areaGame`, at 512x334 =
171,008 pixels. We clear the whole 765x503 = 384,795 pixels, **2.25x more**,
albeit with non-temporal SSE stores measured at 0.50 ms against 1.19 ms for
ordinary ones. So the clear is not our problem and is not a target; it is
recorded here so nobody spends a day on it.

#### The real difference: they do not redraw the UI

`Client.gameDraw` gates the chrome on dirty flags:

```java
if (redrawSidebar)  { drawSide();  redrawSidebar  = false; }
if (redrawChatback) { drawChat();  redrawChatback = false; }
if (sceneState == 2) { minimapDraw(); ... }
```

Only `gameDrawMain` — the 3D viewport — runs every frame. An idle in-world Java
client does not touch the sidebar or the chatback at all.

The C client has a `need_redraw` flag, but it gates `UITree_EmitWalk`, the
*display-list rebuild*. Rasterisation is not gated by anything:
`App_Render` calls `ToriRS_Soft3D_RenderFrame` unconditionally, and that walks
and draws the whole emitted list — 3D and chrome together — every frame.

Roughly what that costs, on the 765x503 canvas:

| region | pixels | Java redraws it |
|---|---|---|
| 3D viewport | 512x334 = 171,008 | every frame |
| chatback | ~519x142 = ~73,700 | only when dirty |
| sidebar | ~190x261 = ~49,600 | only when dirty |
| minimap | ~146x151 = ~22,000 | on scene state |

So we repaint on the order of **120,000 extra pixels of chrome per frame**,
plus every sprite blit and glyph that composes them, fifty times a second, to
produce an image identical to the one already there. That is the leading
candidate for the 1.37x user-time gap, and it is a structural difference rather
than a constant-factor one.

### 2.3 Windows 11 — for contrast, not for the goal

x86_64, `OPT=1`, same server, same world, 900 frames:

| | peak WS | peak private |
|---|---|---|
| C client, `--soft3d` | 77.3 MB | 88.2 MB |
| C client, d3d9 (default) | 319.9 MB | 381.5 MB |

The C client costs ~10 MB more here than on XP for the same world, which is
what 64-bit pointers do to a heap that is mostly small nodes and pointer-rich
structures.

The Java client on Windows 11 is **not** a useful comparison and is recorded
only to head off the mistake:

| Java client (login screen only) | peak WS | peak private |
|---|---|---|
| default heap | 250.5 MB | 721.0 MB |
| `-Xmx64m` | 229.7 MB | 306.2 MB |

Capping the heap at 64 MB moved peak working set by 8 %. On a modern JVM the
heap is a *minority* of the footprint — metaspace, code cache, JIT, GC
structures and reserved mappings dominate — so this measures a 2026 JVM running
2005 code, not the 2005 process. The XP box's 32-bit JRE 1.8 is the honest
comparison, and it is the one used in §2.1.

For the record, `-Xmx64m` **is** the browser-equivalent configuration: the
engine's `view/java.ejs` passes `portoff`, `nodeid`, `free`(=members) and
`lowmem`, and defaults `lowmem=0`, which `Client.main` maps to `highmem`. The
full argument set is `10 0 highmem members 32`. The 2005 Java Plug-in's default
maximum heap was 64 MB. Neither `highmem` nor `lowmem` is a JVM argument — they
only flip internal buffer flags.

---

## 3. Where the C client's memory goes

`MEMPROF=1` ranks the tracked C heap by call site *at the moment of peak*, not
at exit. Windows 11, `--soft3d`, 900 frames:

```
memprof: live 69.65 MB in 403 sites (peak 69.65 MB, 33 peak dumps)
```

**69.65 MB of tracked heap inside a 77.3 MB process** — unlike the d3d9 lane,
where the gap is over 200 MB, here the heap *is* the footprint and there is
almost nothing else to blame. At exit only 0.24 MB is still live, so none of
this is a leak; it is all live-at-peak working state.

| # | MB | blocks | site |
|---|---|---|---|
| 0 | 16.00 | 1 | `3rd/rscache/src/archive.c:432` — gzip decompress buffer |
| 1 | 3.85 | 12019 | `world_decode_tile.c:600` — one `ToriDraw_Model` per terrain chunk |
| 2 | 3.74 | 11942 | `dat1_config_component.c:506` — interface components |
| 3 | 3.57 | 14 | `torirs_component_from_rscache.c:683` — component packs |
| 4 | 2.70 | 1 | `toridraw_scene.c:171` — scene emit buffer |
| 5 | 2.18 | 134 | `filelist.c:458` — decompressed jag members |
| 6 | 1.91 | 17886 | `td_scene_allocate_element_id` — scene elements |
| 7 | 1.84 | 31 | `texture_palette_bake.c:85` |
| 8 | 1.70 | 5305 | `toridraw_model.h:69` — model bodies |
| 9,10 | 1.69 ×2 | 9 each | map decode (`maps.c:445`, `torirs_map_from_rscache.c:18`) |
| 11,12 | 1.32 ×2 | 1 each | painter and minimap buffers |

Beyond the heap: `.bss` is **6.18 MB**, of which the static `app` struct is
**3.11 MB** on its own (`.text` 3.85 MB, `.rdata` 0.81 MB). So roughly 10.9 MB
of the process is the image itself before a single allocation.

### 3.1 What has been changed so far

**`archive.c` — shrink the decompress buffer to what actually decompressed.**
The buffer is sized from the gzip ISIZE footer and was then retained at full
`capacity`, never shrunk to `uncompressed_length`, for as long as the caller
held the archive.

Measured effect: **77.68 -> 77.30 MB. About 0.4 MB, not the 16 MB I expected.**
The shrink is a no-op whenever the ISIZE estimate is accurate, which it usually
is — that 16 MB block is a *genuine* 16 MB decompressed archive, not retained
slack. The change is still right (it bounds the overshoot on the doubling
fallback path, where no footer is available) but it is not a lever. Recorded
here because the wrong prediction is the useful part: the largest site in a
profile is not automatically the largest *waste*.

---

## 4. Targets, ranked

Ranked by measured size, with what is actually known about each.

### 4.1 The renderer, and it is not close

`--soft3d` 67.6 MB against d3d9's 171.6 MB on XP, and 77.3 against 319.9 MB on
Windows 11. **d3d9 is the default lane**, so the shipped configuration is
2.5-4x the software one. The d3d9 build prints its own retained-memory report,
which names the cost precisely (Windows 11 figures):

```
batch16_cpu_vertices    45.67 MB      static_vbo_default      32.00 MB
atlas_cpu               16.00 MB world + 16.00 MB ui
group_static_cpu        14.06 MB      group_static_default     8.00 MB
```

Most of that is geometry stored twice — a CPU copy plus a DEFAULT-pool copy —
plus two fixed 16 MB atlases. Anything said about a `<64 MB` budget is
meaningless until this lane's default is settled, because no amount of heap
work in §4.3 recovers 100 MB.

### 4.2 CPU, in priority order

**1. Gate the chrome rasterisation on dirty state (§2.4).** The largest known
structural difference, and the only one that plausibly closes a 1.37x user-time
gap: we repaint ~120,000 pixels of sidebar, chatback and minimap every frame
that the Java client repaints only when they change. The C client already has
the concept — `need_redraw` — but it gates the display-list *rebuild*, not the
raster. Wiring the raster to per-region dirty state is the work.

Note the trap the existing comment at `app.c:13625` already documents: emit
reads far beyond the component struct (32 `UITree_Host(...)` calls, plus
inventory, hover, drag and varp state), so a per-node dirty bit cannot gate the
*walk* faithfully. Gating the **raster by screen region** is a different and
easier problem than gating the walk by node, and it is where the pixels are.

**2. Stop presenting through the kernel.** Java gets a GPU blit for ~1 % and
0.08 s of kernel time in 30 s; we spend 2.08 s in a per-frame GDI `BitBlt` of a
1.54 MB DIB. Options, cheapest first: blit only the dirty rectangles (falls out
of item 1); or present the software framebuffer through the D3D path the d3d9
lane already links, which is what Java2D does.

**3. Rule out the plugins.** The C client runs **9 enabled plugins** on the
frame path; the Java client has no equivalent. Cheap to A/B, and it must be
done before attributing anything else to the rasteriser.

**Not targets**, on the evidence above: the framebuffer clear (§2.4 — ours is
already 2.25x the pixels at less than half the per-pixel cost), and the window
title (fixed).

`TORIRS_PERF=1` is not the tool for absolute numbers on this box — it has
measured as 69 % of the XP frame. A/B with fixed-frame wall time and perf off,
or use a sampling profiler.

### 4.3 The heap, honestly

Nothing in §3's table is a single big win; it is a long tail.

* **Small-node traffic.** 12019 + 11942 + 17886 + 5305 allocations at ~150-340
  bytes each. Beyond the bytes, each carries allocator header and rounding, and
  a Windows heap block is not cheap. An arena for terrain chunks and scene
  elements would recover both the overhead and the fragmentation.
* **Interface components: ~8.3 MB** across sites 2, 3 and the uitree. 2081
  components at ~1.7 KB each, held for the session.
* **`.bss` 6.18 MB**, half of it the static `app` struct, which is a wall of
  fixed-size arrays (`entity_overlays[2048]`, `chrome_merged[]`,
  `plugin_*[]`...). Cheap to audit, and it is paid on every platform.
* **Raw bytes alongside parsed structures.** Sites 0 and 5 (18.2 MB combined)
  are decompressed cache bytes. They are freed after parsing, so the cost is
  how many are in flight at once, which is a scheduling question rather than a
  sizing one.

### 4.4 Scene flags worth knowing about

The client builds its scene as
`SCENE_SMALL | DEPTH_16K | MODEL_ZBUFFER` at `SCRATCH_BUFFER_VERYHIGH_16K`.

`DEPTH_16K` on its own would cost **14.56 MiB** — `(16384-1500) x 513 x 2`
bytes — but `SCENE_SMALL` selects the CSR sorter instead of the dense bucket
table, so that is *not* being paid. Worth stating explicitly, because the flag
name suggests otherwise and it is the first thing anyone will reach for.

`VERYHIGH_16K` is a real cost and is load-bearing: dropping a tier makes
oversized merged NPCs vanish rather than degrade.

---

## 5. Where this leaves the goal

| | XP, quiet scene | XP, populated scene | Windows 11 |
|---|---|---|---|
| C soft3d | 58.8 MB ✅ | 67.6 MB ❌ | 77.3 MB ❌ |
| C d3d9 (default) | — | 171.6 MB ❌ | 319.9 MB ❌ |

`<64 MB` is met today only on the XP target, in software, in a quiet scene.
The ordered path to meeting it generally is: settle the **default renderer**
(§4.1, worth 100 MB+), then the **small-node arenas and the interface
components** (§4.3, worth perhaps 8-12 MB together), then `.bss`.

On CPU we are behind and the goal is unmet: **74.7 % against 49.7 %** of a
single XP core, after the title fix. The path there is §4.2, in order: gate the
chrome raster on dirty regions, stop presenting through the kernel, and rule
out the plugins.

---

## 6. PR #49 and damage-based drawing — verdict

Measured 2026-08-25 on `merge/v3-pr49` (v3 + PR #49 + a log-channel sweep).
The full profile comparison lives in `docs/java_parity/`; this section answers
the question PR #49 was evaluated for.

### 6.1 §4.2's ranking was wrong, and this is the correction

§4.2 put "gate the chrome rasterisation on dirty state" first, reasoning from
the ~120,000 pixels of sidebar and chatback we repaint that the Java client does
not. The pixel count is right. The conclusion was not.

An ablation that deletes **all** chrome rasterisation — every sidebar sprite,
every glyph, the whole chatback, not merely gates it — moves the client from
**65.9 % to 61.6 %** of one XP core. That is **4.3 points against a 30-point
gap**: about a seventh. `TORIRS_PERF` agrees independently, putting
`r_sprite + r_font + r_rect` at 7.8 % of the frame.

Where the frame actually goes: **`render` is 86.3 % of it, and `r_model` alone
is 65.6 %.** The gap to the Java client is 3D model rasterisation, which they do
too — with a 4-pixel-per-iteration span loop, out of the *Client* (C1) JIT, with
array bounds checks and a safepoint poll still in the code. See
`docs/java_parity/README.md` §3.

Two §2.4 claims also need correcting:

* **The minimap is not dirty-gated in the Java client.** `Client.gameDraw` calls
  `minimapDraw()` and `areaMap.draw(550, 4, ...)` unconditionally whenever
  `sceneState == 2`. Only `drawSide`, `drawChat` and the tab icons are gated.
* **Java's cheap presentation is not a GPU effect.** With
  `-Dsun.java2d.d3d=false` it falls back to `GDIBlitLoops.nativeBlit` and its
  kernel time is unchanged at 0.17 s per 30 s, against our 1.73 s. It wins by
  blitting 197,840 px/frame from separate per-region surfaces where we BitBlt
  all 384,795.

### 6.2 Verdict on PR #49: **adopt, with changes** — but not as the next target

The dirty/epoch model is sound and the code is careful. It is the right
foundation for damage-based drawing whenever that work happens. It is simply
not where the CPU is.

**What it gets right.** The retain gate is live rather than shadow-mode
(`app.c`, `UITree_EmitRetainGateQuiet`). The host-dependency channel is
*compare-based*, not trust-based: `app_ui_host_publish_inputs` re-hashes ambient
App state every frame and bumps an epoch only on a changed signature, so a
writer that forgets to invalidate is caught anyway. `volatile_refs` is a real
correctness fence — it notices that a byte-identical command list is not a
byte-identical picture when a desc holds a same-frame host pointer. And
`emit_visited` makes the reachability argument sound rather than hopeful.

**Question 1 — does the epoch model carry per-node damage?** Partly. The comment
at `app.c:13625` argues no per-node dirty bit can gate the emit *walk*, because
emit reads far beyond the component struct. That argument is correct and the
epoch channel answers it — but only at whole-buffer granularity:
`UITree_EmitWalk` points `observed_input_mask` at **one** buffer-level
accumulator, so the model answers "may anything have changed" and never "which
node". Deriving per-node damage from a domain bump needs a reverse index the PR
does not build.

The argument does **not** transfer to gating the raster, and that is the useful
half: raster gating needs to know *which screen rectangles differ*, not *why*.
The command list is its own oracle — and PR #49 already contains the mechanism,
as a diagnostic: the per-desc `memcmp` against the previous buffer under
`g_torirs_perf_enabled`. Union the rects of the descs that differ and that is
the damage set, with no dependency reasoning at all. Its own comment prices the
compare at ~450 KB/frame, so it wants a per-desc hash folded into
`emit_buffer_append` before it goes anywhere near the hot path.

**Question 2 — what is missing.** Easy, because the PR or the renderer already
has it:

* *Node to screen rect*: `UITreeEmitDesc` already carries `node_index`,
  `component_id`, `x/y/w/h` and `clip`.
* *Damage accumulation*: `uitree_note_mutation(tree, idx, impacts)` is a single
  choke point that already knows the node index of every mutation.
* *Clipped rasterisation*: every 2D render command already carries a scissor,
  and `viewport_from_scissor` (`platform_sdl2_renderer_soft3d.c:57`) is the one
  place they all funnel through — "every draw kind funnels through here".

Not addressed, and real work:

* *Per-node command spans* — the buffer is retained or rebuilt whole.
* *A conservative painted-extent function per emit kind.* `desc.x/y/w/h` is
  **not** the painted extent: a non-if3 sprite blits at `x+ox, y+oy` sized from
  the atlas entry, baseline text extends upward by font metrics, models project.
  `desc.clip` bounds it soundly but is the whole canvas for top-level nodes.
  Getting this wrong produces corruption that is hard to see and easy to ship.
* *Skipping at dispatch, not at the clip.* Narrowing `viewport_from_scissor`
  alone is not enough: `soft3d_draw_sprite` does malloc + memcpy + outline
  cache lookup + alpha scale + transform **before** it reaches the clipped blit.
  Damage has to reject the command before `soft3d_execute_measured`.
* *Damaged-rect present* — see 6.3.

**Question 3 — CS2.** Covered, and better than the PR's description implies,
because the signature hash is the primary mechanism rather than the
`UITree_HostInputsChanged` calls. CS2 varp writes reach `CLIENT_STATE` through
`VarPManager`'s change callback (`app_varp_change`). CS1 results — which is how
stats and inventory counts reach a widget — are cached onto the node by
`task_cs1_run` and published through `UITree_SetCS1ActiveAt` /
`UITree_SetCS1ValueAt`, i.e. as *tree* mutations that bump `dirty_gen`; the host
answers `IS_ACTIVE` from `component->cs1_active` and never runs the VM at draw
time. So the absence of skill levels from the `CLIENT_STATE` signature is not a
hole.

The failure mode if a domain is ever missed is a **frozen panel**: the gate
calls the frame quiet, the retained list is reused, and the pixels stay stale
until something unrelated bumps an epoch. The mitigations that exist and must be
kept: unclassified request kinds fall back to `UITREE_HOST_INPUT_ALL`, and
`TORIRS_EMIT_VERIFY=1` re-arms the `[emit-unsound]` detector. **Do not retire
that detector when the skip is enabled** — it is the only thing standing between
a missed domain and a silently wrong frame.

One over-conservatism worth fixing cheaply: `UITREE_HOST_IS_ACTIVE` and
`UITREE_HOST_EVAL_TEXT_PLACEHOLDER` are classified `client | inventory`, but
both are pure reads of a cached field on the node whose write already marks it
dirty. Classifying them `0` would stop every inventory change from forcing a
full rebuild of any tree containing a CS1 node.

**Question 4 — the ceiling.** Bounded by measurement, not estimated: **4.3
points of one core**, ~1.34 s per 30 s, ~0.9 ms/frame. The 3D viewport is always
dirty, so nothing in the chrome can do better. Against the ~30-point gap this is
a seventh, and against the original 1.37x user-time framing it is far short.

**Question 5 — does damaged-rect present also fix the GDI cost?** Yes, and it is
the same change rather than an independent one: the damage set is exactly the
argument list for a per-rect BitBlt. It is bounded though — the viewport plus
the minimap are always damaged, which is 197,840 of 384,795 px, so present cost
floors at ~51 % rather than at zero. Splitting one blit into several also
multiplies the per-call cost, so the rect list wants coalescing. Presenting
through D3D like Java2D does is the *independent* alternative, and note that
Java's own measurements say it is not needed: with D3D off their kernel time
does not move.

### 6.3 The revised order

1. **`r_model`** — 65.6 % of the frame, and the only item that can close a
   30-point gap. `docs/java_parity/README.md` §3.3 has the Java inner loop as an
   existence proof of what is enough on this hardware.
2. **Present only what changed** — most of the 1.73 s kernel delta.
3. **Rule out the plugins** — 8 on the frame path, `app_run` at 8.2 %.
4. **Chrome damage gating, on PR #49's foundation** — capped at 4.3 points.
5. Not targets, on evidence: the framebuffer clear (ours is ~10x better per
   pixel than theirs), and the emit walk (1.1 % of frame, already retained).

### 6.4 Two fixes that were only on the measurement branch

Both were found by this work and are in `merge/v3-pr49`:

* **The on-demand cache boot fix.** v3's `platform_x_io_ondemand.c` fetched the
  bare `/versionlist` route; this engine serves `<stem><checksum>`, so it 404'd,
  the on-demand source was never built, and the client dereferenced a NULL disk
  cache — segfaulting with an **empty log**, because `OPT=1` compiles the
  `assert` out. An `OPT=0` build named it in one run.
* **Per-frame stderr.** The in-world client was writing 178,262 bytes per 30 s,
  roughly one unbuffered syscall a frame, almost all of it a plugin restating a
  missing asset. Now behind `TORIRS_LOG`, which compiles out under `NDEBUG`:
  kernel 1.91 s to 1.73 s.
