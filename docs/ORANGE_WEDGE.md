# The Inferno "orange wedge" — root cause and proposed fix

Status: **diagnosed, patch proposed, not applied.** No C source was modified by
this investigation. `git status` shows the same pre-existing modified files it
showed at session start; `src/.last_flavor` was restored after building into the
private objdir `src/build_wedge_tdo_es/`.

---

## 1. Summary

The C client's world projection scale is a **compile-time constant 512**. The
official rev-239 client **recomputes** its scale from the world viewport height
on every layout, and at this window size arrives at **191**.

The C client therefore draws the entire 3-D scene **512 / 191 = 2.68× magnified**.

The "orange wedge" is not a spurious object. It is the Zuk alcove's floor —
`0xA45409`, drawn by the same loc models in both clients — rendered 2.7× too
large. At the correct scale it is a thin sliver hugging the arena-wall line; at
2.7× it clears the wall and reads as a floating wedge.

The bug report's phrasing "the official client does not render it" is imprecise.
The official renders exactly the same geometry in exactly the same colour. It is
8 px tall and ~32 px wide there, versus 29 px tall and 71 px wide here.

### The previous session's hypothesis is refuted

The "LinkBelow-style push-down" for tile-settings bit `0x08` (VIS_BELOW) is
**wrong**, and it is wrong by measurement on both sides, not by argument:

* Over all 16384 tiles of the Inferno window the two clients agree on settings,
  underlay id, overlay id, overlay shape and height: **0 mismatches**
  (`/tmp/wedge_flagged_diff.txt`, footer `# rows=378 mismatches=0`).
* All **378** bit-8 tiles are at level 1 with `underlay=0, overlay=0`, and have
  **no geometry in either client** (official `shaped=0 flat=0`; C
  `terrain_element = -1`). There is nothing for a push-down to move.
* Bit `0x08` in the official client is culling-exemption only. The only
  mechanism that relocates a column between levels is bit `0x2`, handled
  elsewhere (`class112.method3884`).
* The C client's `RSCache_MapFloorVisBelowDrawLevel`
  (`3rd/rscache/src/datatypes/maps.h:60`) is already byte-for-byte
  `Statics.method8418`.

Additionally: in this scene the whole VIS_BELOW path is **inert**. Measured
`level_mask=0xf roof=3`, so every level is traversed regardless of the lowered
draw level.

---

## 2. Root cause

### What the official does

`Deobfuscator/instr/src/class159.java:62-129` (`method5357`, the viewport
layout), reduced to the part that matters:

```java
int var6 = viewportHeight - 334;
double zoom;
if      (var6 < 0)    zoom = client.field976;                                  // near endpoint
else if (var6 >= 100) zoom = client.field801;                                  // far endpoint
else                  zoom = (field801 - field976) * var6 / 100 + field976;
// ... aspect clamps (field1040/field810/field804/field805) may letterbox ...
client.field817 = (int)(var3 * var7 / 334.0);   // scale = viewportHeight * zoom / 334
client.field811 = var0;  client.field897 = var1;   // viewport x/y offset
client.field813 = var2;  client.field837 = var3;   // viewport w/h
```

`field817` is `getScale()` (`client.java:13962`) and is the linear divisor of the
perspective projection — `screen = centre + coord * scale / depth`.

`field976` / `field801` are set by **CS2 opcode 6200 `VIEWPORT_SETFOV`**
(`Statics.java:6268-6285`), each argument passed through

```java
method5659(v) = (int) Math.pow(2.0, v / 256.0F + 7.0F)
```

and defaulted to `256` when the result is `<= 0`. They are the **near/far
endpoints of a height interpolation**, not a value/max pair.

### What the C client does

`src/app.c:2853`

```c
app->world_camera.fov_rpi2048 = 512;
```

Set once, at world creation, and never recomputed. It is consumed by the
cull-span (`src/app.c:5809`) and by the frame camera
(`src/render/torirs_frame.c:1128`), with a duplicate literal fallback at
`src/render/torirs_frame.c:1135`.

`fov_rpi2048` is an **angle** (2048 units per turn), not a linear scale. The
projection kernel
(`3rd/toridraw/graphics/projection16_simd.scalar.u.c:27-58`) does:

```c
int fov_half            = camera_fov >> 1;
int cot_fov_half_ish16  = g_tan_table[1536 - fov_half];   /* tan(270° - h) = cot(h), ×65536 */
int cot_fov_half_ish15  = cot_fov_half_ish16 >> 1;
x *= cot_fov_half_ish15;  x >>= 6;                        /* → x * cot(h) * 512 */
```

with `g_tan_table[i] = tan(i * 2π/2048) * 65536`
(`3rd/toridraw/graphics/shared_tables.c:215`). So

> **effective linear scale = cot(fov_rpi2048 / 2) × 512**

and `fov_rpi2048 = 512` (90° FOV, half = 45°, cot = 1) gives an effective scale
of exactly **512** — matching the official's *fixed-mode* 334-high value, not
this window's.

### The dead knob

The C client **already decodes** CS2 6200 (`src/game/rs_cs2_host.c:1717-1719`):

```c
case CS2_OP_VIEWPORT_SETFOV:
    host->viewport_fov     = request.args[0];
    host->viewport_fov_max = request.args[1];
```

but it (a) stores the **raw** arguments instead of `(int)pow(2, arg/256 + 7)`,
(b) names them `fov`/`fov_max` when they are the near/far interpolation
endpoints, and (c) **is read by nothing** — `grep -rn "viewport_fov" src/`
returns only the struct declaration, the init defaults, and this setter. The
value is written and discarded.

`TORIRS_CS2_TRACE=1` confirms the opcode does fire: clientscript 42 pc=24 runs
`VIEWPORT_SETFOV(6200)` on every boot.

---

## 3. Evidence

Both clients driven against the same `mock230 --rev osrs239` server, same
`cache.osrs239`, same 765×503 canvas, same account, same `::zuk` instance.

### 3.1 Read directly from each client's own state

| | official (rev 239) | C (torirs) |
|---|---|---|
| world viewport | **765×503 @ (0,0)** | **723×503 @ (0,0)** |
| projection scale | **191** | **512** (from `fov_rpi2048=512`) |
| zoom endpoints | near 127, far 127 | *(decoded, then discarded)* |
| aspect clamps | 1 / 32767 / 1 / 32767 (inactive) | n/a |
| settled follow camera | (7104, **−743**, **5072**) | (7104, **−666**, **5299**) |
| scripted cinema camera | (7744, −1240, 5824) | (7744, −1240, 5824) |
| camera pitch | 1024/16384 = 22.5° | 128/2048 = 22.5° |
| camera yaw (cinema) | 789/16384 = 17.33° | 98/2048 = 17.23° |

Check: `(int)(503 × 127 / 334.0) = (int)191.27 = 191` — reproduces the official's
reported `getScale()` exactly.

Note the two clients agree **exactly** on the scripted cinema camera, which
rules out the camera-packet path as the cause.

Official numbers via a new read-only `proj` command added to
`Deobfuscator/instr/src/JCtl.java` (see §6). C numbers via
`TORIRS_WORLD_VIEW_DEBUG=1` (`WORLDRECT=0,0 723x503`), `TORIRS_PAINT_DEBUG=1`
and `TORIRS_CAM_DEBUG=1`.

### 3.2 Tile-column spacing — the scale, measured on screen

Official, projected by the client's own numbers at its settled camera:

```
(7104,-240,7104)  tile (55,55) → screen (383, 223)
(7104,-240,8128)  tile (55,63) → screen (383, 207)
(7104,-240,8256)  tile (55,64) → screen (383, 205.6)
```

→ tiles 55→64 span **17.4 px**.

C, from the pick sweep (`TORIRS_SIM_CLICK_AT` + `TORIRS_PICK_DEBUG`) at its
settled camera:

```
screen y 170 → tile (55,55)      screen y 143 → tile (55,59)
screen y 161 → tile (55,56)      screen y 135 → tile (55,60)
screen y 152 → tile (55,57)      screen y 120 → tile (55,64)
```

→ tiles 55→64 span **50 px**.

Ratio **50 / 17.4 = 2.87**, against the predicted 512/191 = 2.68.

Fitting all six C probes to the C camera (pitch 22.5°, eye (7104,−666,5299))
gives a linear fit with ~2 px residual and, allowing the ±half-tile
quantisation the pick introduces, bounds the C effective scale to
**[480, 614]** — comfortably containing 512 and decisively excluding 191.

### 3.3 The wedge itself

Exact `0xA45409` pixels inside a matched box over the alcove:

| capture | count | x extent | widest row | y extent |
|---|---|---|---|---|
| official `/tmp/r_06.png` | 142 | 302..455 | **30 px** | **213..220 (h=8)** |
| official `/tmp/nz_50.png` | 161 | 292..455 | **35 px** | **213..220 (h=8)** |
| C `/tmp/cw_final.bmp` (this session) | 884 | 325..449 | **71 px** | **137..165 (h=29)** |
| C `/tmp/c_zuk.png` (prior session) | 887 | 325..449 | 71 px | 137..165 (h=29) |

The C artefact reproduces bit-for-bit across sessions. Widest-row ratio
71 / 32.5 = **2.18**; height ratio 29 / 8 = **3.63**. The true ratio is bracketed
by these two (the official's patch is truncated at the top by the arena wall,
inflating the height ratio; the C patch's rows are cut by the surrounding rock,
deflating the width ratio). The predicted 2.68, plus ~9 % because the C camera
sits 227 units closer to the alcove, lands inside the bracket.

Predicting the band height directly from the alcove's tile extent
(z 61..66, ground y = −240) at the C camera:

```
scale 512 → 28.6 px      (measured C artefact: 29 px)
scale 191 → 10.7 px      (measured official:    8 px)
```

### 3.4 Confounds found and removed

Three things in the earlier record were artefacts, not data, and are corrected
here:

1. **`/tmp/off_final.png` is a broken frame** — half the scene is a black void
   and most of the UI is missing. Every "official" screen-space number derived
   from it (`y 218..230`) is unusable. Its camera reading was nonetheless valid
   and matches the fresh captures.
2. **One Inferno window per login.** After you die to Zuk the scene unloads and
   both `wedge` and `proj` report garbage
   (`eye=698304,-13293616,-743`, `sceneBase=976,103952`, `instance=false`).
   Two of my capture attempts were lost this way. Sample within ~35–60 s of
   `::zuk`.
3. **Cross-camera comparison.** The Inferno camera is animated; comparing a C
   screenshot to an official screenshot taken at a different point in the
   sequence is meaningless. All comparisons above are at each client's
   *settled* camera, with the camera state recorded in the same run as the
   screenshot.

---

## 4. Proposed change

Three edits. None of them is applied.

### 4.1 Decode the CS2 arguments — `src/game/rs_cs2_host.c:1717`

```c
    case CS2_OP_VIEWPORT_SETFOV:
        host->viewport_fov     = request.args[0];
        host->viewport_fov_max = request.args[1];
        return CS2VM_EXECNO_OK;
```

becomes

```c
    case CS2_OP_VIEWPORT_SETFOV:
        /* Reference class159.method5357 + Statics.method5659: the two args are
         * the NEAR and FAR endpoints of a zoom interpolated over viewport
         * height, each in a log2 scale (0 -> 128, 256 -> 256, 512 -> 512), and
         * each falling back to 256 when the decode is non-positive. They are
         * not a value/max pair. */
        host->viewport_zoom_near = rs_cs2_viewport_zoom_decode(request.args[0]);
        host->viewport_zoom_far  = rs_cs2_viewport_zoom_decode(request.args[1]);
        return CS2VM_EXECNO_OK;
```

with, near the top of the file:

```c
/* Statics.method5659: (int)pow(2, v/256 + 7), 256 when that is <= 0. */
static int
rs_cs2_viewport_zoom_decode(int arg)
{
    int zoom = (int)pow(2.0, (double)arg / 256.0 + 7.0);
    return zoom > 0 ? zoom : 256;
}
```

`GETFOV` (`src/game/rs_cs2_host.c:1721`) must keep answering what `SETFOV`
stored, so it should push the *raw* args back. Keep the raw pair alongside the
decoded one rather than re-encoding.

Rename the fields in `src/game/rs_cs2_host.h:296-299` accordingly, and change
the init defaults at `src/game/rs_cs2_host.c:675-678` from `128 / 896` to the
official default **`256 / 256`**.

### 4.2 Recompute the camera scale from the world box — `src/app.c:3981`

`app_update_world_viewport()` already latches the world rect into
`app->world_emit_desc`. Immediately after that loop:

```c
    /* Reference class159.method5357 (Deobfuscator/instr/src/class159.java:62).
     * The world projection scale is a function of the world viewport HEIGHT, so
     * a taller window shows MORE world rather than a bigger world. Leaving it a
     * constant is what drew the Inferno 2.68x magnified. */
    if( app->world_view_valid )
    {
        int vp_h = app->world_emit_desc.h;
        int near_zoom = app->cs2_host.viewport_zoom_near;
        int far_zoom = app->cs2_host.viewport_zoom_far;
        int d = vp_h - 334;
        int zoom;
        int scale;

        if( d < 0 )
            zoom = near_zoom;
        else if( d >= 100 )
            zoom = far_zoom;
        else
            zoom = (far_zoom - near_zoom) * d / 100 + near_zoom;

        scale = (int)((double)vp_h * (double)zoom / 334.0);
        if( scale < 1 )
            scale = 1;
        app->world_camera.fov_rpi2048 = app_fov_from_scale(scale);
    }
```

### 4.3 Convert linear scale to the angle the kernel wants

The official's knob is a linear scale; `fov_rpi2048` is a half-angle-driven
angle in 2048-per-turn units. From
`3rd/toridraw/graphics/projection16_simd.scalar.u.c:27-58` the kernel's
effective scale is `cot(fov/2) * 512`, so the inverse is:

```c
/* projection16_simd: effective_scale = cot(fov_rpi2048/2) * 512, so
 * fov_rpi2048 = atan(512/scale) * 2048/pi. scale 512 -> 512 (90 deg, the old
 * hardcoded value); scale 191 -> 791. */
static int
app_fov_from_scale(int scale)
{
    int fov = (int)lround(atan2(512.0, (double)scale) * 2048.0 / M_PI);
    if( fov < 1 )
        fov = 1;
    if( fov > 2047 )
        fov = 2047;
    return fov;
}
```

Sanity: `scale = 512 → fov = 512` (bit-identical to today's behaviour, so a
334-high viewport is unchanged); `scale = 191 → fov = 791`, whose table lookup
yields an effective scale of ~192 — a 0.5 % quantisation from the 2048-entry
tan table.

> A cleaner long-term shape is to carry the **linear scale** on
> `ToriDraw_Camera` the way the official does, and drop the angle round-trip.
> That touches `toridraw`, `painters_cullspan` and the sprite renderers, so it
> is deliberately not proposed here.

### 4.4 The duplicate literal

`src/render/torirs_frame.c:1135` repeats `fov_rpi2048 = 512` for the
no-world-camera fallback. Harmless (that path has no scene) but it should read
the same helper so the two cannot drift.

---

## 5. How to verify

```sh
cd /Users/matthewevers/Documents/git_repos/3draster
make -C src PLATFORM_OBJ_BASE=build_wedge EMBED_SERVER=1 TORIDRAW_OPT=1 torirs

SDL_VIDEODRIVER=dummy TORIRS_EXIT_BMP=/tmp/cw_fixed.bmp TORIRS_MAX_FRAMES=900 \
  TORIRS_PAINT_DEBUG=1 TORIRS_WORLD_VIEW_DEBUG=1 TORIRS_NET_CHEAT="zuk" \
  src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test --soft3d \
  > /tmp/cw_fixed.log 2>&1
```

**Gate 1 — the viewport and scale.** `grep WORLDRECT /tmp/cw_fixed.log` must
still report `0,0 723x503`, and the new scale must be `(int)(503*127/334) = 191`
→ `fov_rpi2048 = 791`. Log it or assert it in a unit test; do not infer it from
pixels.

**Gate 2 — the wedge collapses.** Count exact `0xA45409` inside the box
`x 280..470`, `y 120..245` of `/tmp/cw_fixed.bmp`:

| | before | expected after | official |
|---|---|---|---|
| height | 29 px | **~11 px** | 8 px |
| widest row | 71 px | **~26 px** | 30–35 px |
| y extent | 137..165 | **~205..216** | 213..220 |

The residual against the official is the §7(b) camera-distance difference and
should be attacked separately, not by tuning the scale.

**Gate 3 — nothing else regressed.** The whole scene shrinks by 2.68×, so this
is not a local change. Re-shoot Lumbridge (`/tmp/n_ingame.png` is the official
reference at the same canvas) and compare framing; run the existing world and
painter tests.

**Official reference capture** (the client is already patched and built):

```sh
nohup tools/perf/run_java_client.sh > /tmp/jc.log 2>&1 &
tools/perf/watchdog.sh /tmp/jc.log 90 43601
{ printf 'click 461 291\nwait 1500\ntype testc\nwait 400\nkey 10\nwait 400\n'
  printf 'type test\nwait 400\nkey 10\nwait 25000\n'
  printf 'click 250 470\nwait 600\ntype ::zuk\nwait 400\nkey 10\nwait 25000\n'
  printf 'proj 7104 -240 7104 7104 -240 8128\nshot /tmp/off_ref.png\n'; } \
  | nc -w 90 127.0.0.1 43601
```

Sample within ~35–60 s of `::zuk`; after the player dies every reading is
garbage (§3.4).

---

## 6. Telemetry added to the official client

`Deobfuscator/instr/src/JCtl.java` gains one read-only command, `proj`. It is
inert unless invoked, changes no client state, and is reachable only through the
existing `JCTL=1` control channel.

```
proj [x y z]...
→ ok proj vp=765x503 vpOff=0,0 scale=191 cam=7104,-743,5072 pitch=1024 yaw=0
         zoomNear=127 zoomFar=127 aspMin=1 aspMax=32767 hMin=1 hMax=32767
         canvas=765x503 | pt=7104,-240,7104 depth=2069 screen=383,223
```

It prints the projection's actual inputs — viewport extent, viewport origin,
scale, eye, pitch/yaw, and the CS2 zoom endpoints — and projects supplied world
points through them. Comparing screenshots cannot separate "the camera moved"
from "the lens changed"; this can.

`instr/build.sh` is green and `instr/tools/verify_api.py` prints
"API surface complete."

---

## 7. Secondary findings — measured, real, not the wedge

**(a) World viewport width.** C is **723×503**, official **765×503**. The C
world box stops 42 px short of the canvas right edge, which moves the projection
centre 21 px left (C 361.5 vs official 382.5) and clips 42 px of scene. Heights
agree, so §4.2's height-driven formula is unaffected either way. Fix separately.

**(b) Follow-camera orbit distance.** Settled eyes are official
(7104, −743, 5072) and C (7104, −666, 5299). Solving each against a ground
anchor (y = −240) at pitch 22.5° puts **both** anchors on the same tile
(z ≈ 48.6 official, 48.9 C) but at different orbit distances: **official
d ≈ 1314, C d ≈ 1113** — the C eye sits ~15 % too close. Neither matches the
`pitch*3 + 600 = 984` formula in
`app_world_camera_follow`'s comment (`src/app.c:9301`), which is cited from
Client-TS (lc254) and appears not to be the rev-239 rule. This is worth ~10 px
of the wedge's vertical position — real, but an order of magnitude smaller than
the scale error. Re-measure it after §4 lands, when the scale no longer
amplifies it.

**(c) `app->cam_script` easing is correct.** `app_cinema_ease`
(`src/app.c:9199`) is `rate + delta*rate2/1000`, which matches
`Statics.method10397` (`Statics.java:44783`) term for term, and both clients
reach the identical scripted camera (7744, −1240, 5824). The scripted-camera
path is not implicated.

---

## 8. Artefacts

| path | what |
|---|---|
| `/tmp/cw_final.bmp`, `/tmp/cw_run.log` | C client, settled Inferno camera, wedge present (this session) |
| `/tmp/cw_cam.log` | C `TORIRS_CAM_DEBUG` — scripted camera targets |
| `/tmp/r_01..r_12.png`, `/tmp/nz_*.png` | official, timed bursts through the Inferno cinematic |
| `/tmp/nzc_*.txt` | official camera trace across the same bursts |
| `/tmp/wedge_flagged_diff.txt` | 378 bit-8 tiles side by side, `mismatches=0` |
| `/tmp/off_final.txt`, `/tmp/c_wedge.log` | full per-tile dumps, both clients |
| `/tmp/c_wedge_pixowner_final.txt` | C per-pixel attribution over the wedge rect |
| `/tmp/p1.png`, `/tmp/q_w.txt` | the post-death broken state, for reference |
