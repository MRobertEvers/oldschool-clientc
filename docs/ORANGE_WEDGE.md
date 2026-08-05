# The Inferno "orange wedge" — root cause and proposed fix

Status: **fixed, and ON by default since §15.** The projection-scale change of
§4 collapses the wedge from 29 px to 8 px and moves it from y 137..165 to
y 214..221; at the official client's own eye the two clients agree on the
band's height (8), top edge (±1) and widest span (153 px). The recompute and
the eye-centred draw box are the default; `TORIRS_WEDGE_SCALE=off` and
`TORIRS_WEDGE_DRAWCENTER=orbit` restore the legacy behaviour for A/B.

§1–§9 below are the diagnosis as written before the experiment; §10 is the
experiment. Where they disagree, §10 was measured.

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

---

## 9. Draw-order evidence

**Answer up front: against the painter the C client actually ports, the wedge
tiles are *not* drawn in a materially different order. 293 of the 300 wedge-tile
pairs are concordant.** The seven inversions and the 6.1 % global residual have
exactly two measured causes, both in the *ready queue*, neither in the plane
loop, the diamond radius, the neighbour-ready check or the cull. Section 9.7
says what *is* different, and it is not a traversal bug.

Nothing in this section confirms or refutes §1–§4. A 2.68× scale error does not
reorder anything, and nothing measured here changes the scale numbers. Two
separate things; this section is only about order.

### 9.0 The camera is pinned — both logs are the same viewpoint

| | official rev-239 | C torirs |
|---|---|---|
| eye | 7104, −743, 5072 | 7104, −743, 5072 (`TORIRS_WEDGE_CAM`) |
| camera tile | 55, 39 | 55, 39 |
| pitch | 1024 / 16384 = 22.5° | 128 / 2048 = 22.5° |
| yaw | 0 | 0 |
| draw window | x[30,80) z[14,64) | x[30,80) z[14,64) (`TORIRS_WEDGE_DRAWCENTER=eye`) |
| draw distance | 25 | 25 |

Both had to be forced: the C's own settled eye is (7104, −666, 5299) = camera
tile **(55,41)**, two tiles farther in z, and its own draw box was z[23,73).
`method4241`'s traversal is centred on the camera tile and `method4193`'s
deferral tests are all written against it, so an order diff at the C's natural
camera would be noise. See §9.7(b) — the box centre is itself a real defect.

Logs (7 columns `seq plane x z drawLevel renderLevel what`, scene coords with
sceneOffset 40 already removed, so they line up without translation):

| path | what |
|---|---|
| `/tmp/off_paintorder_plain.txt` | official pass 0, `method4157`, 1741 paints |
| `/tmp/off_paintorder_sorted.txt` | official pass 1, `method4241`, 1761 paints |
| `/tmp/c_paintorder_bucket.txt` | C `painter_paint_bucket`, 3482 paints |
| `/tmp/off_settled_zbuf1.txt` | official raw MARK/PUSH/POP/CALL trace, 13700 lines |
| `/tmp/c_order_zuk.txt` | C raw MARK/SEED/PUSH/POP trace, 18151 lines |

The 25 wedge tiles are all **terrain** — every one of their paints is
`floor_shape` on the official side and `floor` on the C side, never `loc`. §1's
"drawn by the same loc models in both clients" is wrong; the geometry is the
Inferno arena-floor overlay (152). That is a correction to the prose, not to the
scale measurement.

### 9.1 Sequence positions of the wedge tiles

Ordinal within the plane-0 floor stream of each pass (`#` = ordinal, `r` = rank
among the 25). Camera tile (55,39); `d` is Manhattan distance from it.

```
  tile      d  |  C #  r  | off_sorted #  r  | off_plain #  r
  (42,57)  31  |   76  1  |          88  1   |     179   1
  (43,55)  28  |  128  2  |         128  2   |     202   2
  (47,57)  26  |  156  3  |         135  3   |     307   4
  (47,56)  25  |  170  4  |         147  5   |     306   3
  (48,57)  25  |  176  5  |         140  4   |     333   6
  (48,56)  24  |  192  6  |         155  6   |     332   5
  (72,56)  34  |  397  7  |         424  7   |     847  25
  (69,58)  33  |  399  8  |         426  8   |     818  21
  (72,55)  33  |  401  9  |         427  9   |     846  24
  (71,56)  33  |  402 10  |         442 12   |     834  23
  (71,55)  32  |  403 11  |         448 13   |     833  22
  (69,57)  32  |  405 12  |         429 10   |     817  20
  (68,57)  31  |  418 13  |         436 11   |     797  19
  (62,56)  24  |  529 14  |         511 14   |     673  18
  (58,58)  22  |  571 15  |         623 15   |     583  17
  (56,59)  21  |  578 16  |         631 16   |     534  15
  (58,57)  21  |  580 17  |         633 17   |     582  16
  (56,58)  20  |  607 18  |         640 19   |     533  14
  (54,58)  20  |  609 19  |         638 18   |     482   9
  (54,57)  19  |  615 20  |         647 20   |     481   8
  (55,58)  19  |  616 21  |         698 22   |     508  12
  (56,57)  19  |  617 22  |         648 21   |     532  13
  (55,57)  18  |  645 23  |         710 23   |     507  11
  (54,56)  18  |  646 24  |         711 24   |     480   7
  (55,56)  17  |  657 25  |         722 25   |     506  10
```

Kendall pair agreement over these 25 tiles (300 pairs), independently
recomputed:

```
  C  vs off_sorted (method4241) : 293 concordant /   7 discordant
  C  vs off_plain  (method4157) : 141 concordant / 159 discordant
  off_plain vs off_sorted       : 144 concordant / 156 discordant
```

Globally, over the 651 plane-0 floor tiles the two clients have in common
(211 575 pairs): C vs `sorted` **198 641 / 12 934 = 93.89 %** concordant; C vs
`plain` **139 886 / 71 689 = 66.12 %**. The official's own two passes agree with
each other only **61.75 %** (269 081 / 166 630 over its 934 tiles). The
"orderings are wildly different" reading is entirely an artefact of comparing
against `plain`, and `plain` disagrees with the official's *own* painter just as
much as the C does.

### 9.2 Immediate neighbours in the paint stream

Two paints either side of each wedge tile, with each neighbour's own distance.
Read the `d`s, not the coordinates: in both clients the wedge tiles sit inside
the same distance ring.

```
   tile      d  ||  OFFICIAL sorted: prev2 -> [tile] -> next2      ||  C bucket: prev2 -> [tile] -> next2
 (72,56)    34  ||  (66,60)d32 (69,59)d34 -> [ ] -> (68,59)d33 (69,58)d33   ||  (67,60)d33 (70,57)d33 -> [ ] -> (69,59)d34 (69,58)d33
 (71,56)    33  ||  (66,59)d31 (67,58)d31 -> [ ] -> (73,54)d33 (64,60)d30   ||  (68,59)d33 (72,55)d33 -> [ ] -> (71,55)d32 (70,56)d32
 (68,57)    31  ||  (78,48)d32 (67,59)d32 -> [ ] -> (74,51)d31 (76,49)d31   ||  (66,59)d31 (67,58)d31 -> [ ] -> (70,55)d31 (70,55)d31
 (58,58)    22  ||  (52,58)d22 (57,59)d22 -> [ ] -> (62,50)d18 (63,49)d18   ||  (52,59)d23 (58,59)d23 -> [ ] -> (57,59)d22 (53,59)d22
 (56,58)    20  ||  (54,58)d20 (53,57)d20 -> [ ] -> (57,57)d20 (58,56)d20   ||  (58,56)d20 (57,57)d20 -> [ ] -> (55,59)d20 (54,58)d20
 (55,58)    19  ||  (65,45)d16 (69,44)d19 -> [ ] -> (53,56)d19 (52,55)d19   ||  (53,56)d19 (54,57)d19 -> [ ] -> (56,57)d19 (57,56)d19
 (55,57)    18  ||  (68,44)d18 (69,43)d18 -> [ ] -> (54,56)d18 (53,55)d18   ||  (57,55)d18 (56,56)d18 -> [ ] -> (54,56)d18 (53,55)d18
 (55,56)    17  ||  (67,44)d17 (68,43)d17 -> [ ] -> (54,55)d17 (53,54)d17   ||  (54,55)d17 (54,55)d17 -> [ ] -> (56,55)d17 (56,55)d17
```

Full 25-row table in `/tmp/c_wedge_seq_table.txt`. Both clients are walking the
same equal-distance ring around the wedge tiles; they differ in where they enter
the ring and in a handful of same-ring tie-breaks. The official *does* leak a
few off-ring tiles in (`(65,45)d16` before `(55,58)d19`, `(62,50)d18` after
`(58,58)d22`) — those are the deferrals of §9.3.

### 9.3 Deferral: the official defers these tiles, the C mostly does not

"Deferred" = the tile was popped off the ready queue, refused by the neighbour
check without painting, dropped from the queue, and only repainted after a
neighbour re-pushed it. Measured as *number of POPs on the tile that precede its
floor paint* — 1 means popped once and drawn on the spot.

```
   tile      OFFICIAL pops-before-paint          C pops-before-paint
 (55,57)         2  DEFERRED  (gap 369 rows)         1  immediate
 (55,58)         2  DEFERRED  (gap 375)              1  immediate
 (55,56)         2  DEFERRED  (gap 372)              1  immediate
 (58,58)         2  DEFERRED  (gap 537)              2  DEFERRED (gap 554)
 (58,57)         2  DEFERRED  (gap 532)              1  immediate
 (47,57)         2  DEFERRED  (gap 183)              1  immediate
 (47,56)         2  DEFERRED  (gap  65)              1  immediate
 (48,56)         2  DEFERRED  (gap  80)              1  immediate
 (72,56)         2  DEFERRED  (gap 156)              1  immediate
 (72,55)         2  DEFERRED  (gap 155)              1  immediate
 (71,56)         2  DEFERRED  (gap 141)              2  DEFERRED (gap  75)
 other 14        1  immediate                        1  immediate
```

**Official defers 10 of the 25 wedge tiles; the C defers 2, and those 2 are a
subset.** The same holds scene-wide: on plane 0 the official refuses at least
one pop on **356 / 934 = 38.1 %** of its tiles, the C on **62 / 735 = 8.4 %**.

The queue traffic says the same thing:

```
  OFFICIAL sorted pass : MARK 1317   CALL 6   PUSH 3636   POP 2600   push/pop 1.398
  C painter_paint_bucket: MARK 2940  SEED 2   PUSH 5863   POP 5863   push/pop 1.000
```

The official pushes 1.4× more than it pops because `method3914` **relinks** an
already-queued tile to the tail; `bucket_push_if_active` **drops** the push when
`paints[ti].in_queue` is set, so every C push is matched by exactly one pop.

### 9.4 Worked trace — tile (0,55,57), 32/33 of whose pixels are exactly 0xA45409

Official (`/tmp/off_settled_zbuf1.txt`, sorted pass):

```
  5152  MARK
 12033  PUSH
 12105  POP     <- refused, no paint
 12406  PUSH
 12408  PUSH
 12473  POP
 12474  floor_shape   p=3103 (sorted-pass floor #710)
 12475  grounddecor
```

C (`/tmp/c_order_zuk.txt`):

```
   467  MARK
 14341  PUSH d=18
 14787  POP  step=0
 14788  floor         p=2620 (C floor #645)
 14789  grounddecor
```

**What overtook it.** Floors painted inside the official's refusal window
(12105 → 12473): 53 tiles, and the C's push→pop window (14341 → 14787): 29
tiles.

```
  OFFICIAL 12105..12473 :  (56,56)d18 (57,55)d18 (58,54)d18 (59,49)d14 (63,48)d17
                           (64,47)d17 (68,46)d20 ... (56,51)d13 (57,50)d13
                           (58,49)d13 (59,47)d12 (63,46)d15 (64,45)d15 (68,44)d18
  C        14341..14787 :  (55,58)d19 (56,57)d19 (57,56)d19 ... (56,56)d18
                           — d=19 and d=18 only, nothing nearer
```

Set-differenced against the 651 tiles both clients draw:

```
  A = official draws BEFORE (55,57), C draws AFTER : 41 tiles, ALL at d=12..17
      by d: {12:1, 13:5, 14:6, 15:9, 16:10, 17:10}
      e.g. (59,47) (60,47) (58,49) (59,48) (57,50) (56,51) (58,50) (57,51) (56,52)
  B = C draws BEFORE (55,57), official draws AFTER : 20 tiles, at d=4..18
      (51,39) (51,40) (50,39) (51,41) (49,39) (50,40) (49,40) (51,42) (48,40)
      (51,43) (51,44) ... (51,53)
```

Set **A** is the deferral: 41 tiles *nearer to the camera than (55,57)* that the
official painted while (55,57) was out of the queue. Set **B** is the wave
boundary of §9.6 — all 20 are in the C's wave 0 and in the official's waves 2/3.
The two sets are disjoint in cause and in distance range.

The same for **(0,71,56)**, one of the two tiles the C *also* defers, showing
the mechanism is a faithful port and only the *company it keeps* differs:

```
  OFFICIAL  (71,56): PUSH 10021  POP 10066 refused  PUSH 10163  POP 10206  paint 10207
            (72,56): paint 10023, final POP (retirement) 10162
            -> (71,56) is re-pushed at 10163, one row after (72,56) retires.
            15 floors painted in the gap, 13 of them NEARER: (68,58)d32 (69,57)d32
            (74,52)d32 ... (68,57)d31 (74,51)d31 (76,49)d31 (65,60)d31 (66,59)d31
  C         (71,56): PUSH 10304  POP 10319 refused  PUSH 10372  POP 10393  paint 10394
            (72,56): paint 10258, final POP 10370
            -> re-pushed at 10372, two rows after (72,56) retires — same rule, same
            trigger. 3 floors painted in the gap, ALL at d=33: (69,58) (68,59) (72,55).
```

Both clients refuse (71,56) because its east neighbour (72,56) has drawn its
front half but has not yet retired, and both re-admit it the instant (72,56)
retires. The official fills the 141-row gap with fifteen *nearer* tiles; the C
fills its 75-row gap with three tiles at the *same* distance. That is the whole
difference, and it is the queue.

### 9.5 The seven wedge inversions, individually

```
  (56,58)d20 vs (54,58)d20  tie  : C draws (56,58) first, official (54,58) first
  (48,57)d25 vs (47,56)d25  tie  : C draws (47,56) first, official (48,57) first
  (56,57)d19 vs (55,58)d19  tie  : C draws (55,58) first, official (56,57) first
                                   — official DEFERRED (55,58) (#698 vs #648)
  (71,56)d33 vs (69,57)d32       : C by distance, official drew d32 first (deferral)
  (71,56)d33 vs (68,57)d31       : same
  (69,57)d32 vs (71,55)d32  tie  : C draws (71,55) first, official (69,57) first
  (68,57)d31 vs (71,55)d32       : C by distance, official drew d31 first (deferral)
```

Four are exact Manhattan ties broken the other way; three are the official
running out of distance order because it deferred (71,56). There is no inversion
where the two clients disagree about *which side of the arena* is farther.

### 9.6 Where the traversals diverge — it is the queue, and only the queue

Ruled out by direct code and log comparison:

* **Plane loop — same.** `method4241`'s *marking* loop counts planes DOWN
  (`for (var4 = field1674-1; var4 >= field1733; var4--)`, class112.java:2444) but
  its *traversal* loop counts UP (`for (var21 = field1733; var21 < field1674;
  var21++)`, class112.java:2487). The C's seed generator is
  `for phase in [1,2]: for level in [0,L): ...` (`painters.c:1394`) — ascending,
  same. All 25 wedge tiles are plane 0 anyway.
* **Diamond radius / seed scan — same structure.** Official:
  `for var22 = -(drawDist+|offX|) .. 0 { for var25 = -(drawDist+|offZ|) .. 0 {
  four mirrored corners } }`, two rounds with `var20 = (var19 == 0)` selecting
  whether the neighbour checks run. C `seed_gen_next` (`painters.c:1542`) yields
  exactly `for phase in [1,2]: for level: for dx in [-R,0]: for dz in [-R,0]:
  up to 4 reflected candidates`, with `check_adjacent = (phase == 1)`. Same
  nesting, same mirroring, same first-round-unchecked rule. Both logs report
  `drawDist=25` and window `x[30,80) z[14,64)`.
* **Neighbour-ready check — same rule, same flag semantics.** Side by side:

  ```java
  // class112.method4193, class112.java:642-677   (official)
  if (var7 > 0) {                                   // plane > 0
     var13 = var4 - field1702;                      // tile directly below
     if (method3940(var13) && method3974(var13)) continue;
  }
  if (var5 <= field1755 && var5 > field1684) {      // west, and I am west of/at camX
     var14 = var4 - field1680;
     if (method3940(var14) && method3974(var14)
         && (method4216(var14) || (field1629[var4] & 1) == 0)) continue;
  }
  // ... east (&4), south (&8), north (&2) identical in shape
  //   method3940 = field1734 & 1  -> tile has content
  //   method3974 = field1734 & 4  -> not yet retired (cleared at :1170, backside pass)
  //   method4216 = field1734 & 2  -> front half not yet drawn (cleared at :681)
  ```

  ```c
  /* painters_bucket.u.c:476-528   (C) */
  if( paintgrid_level > 0 )
      if( paints[e_tile - level_stride].step != PAINT_STEP_DONE ) continue;
  if( tile_is_west_inbounds(tile_sx, camera_sx, min_draw_x) ) {
      struct TilePaint* other = &paints[e_tile - 1];
      if( other->step != PAINT_STEP_DONE &&
          (other->step == PAINT_STEP_READY || (tile->spans & SPAN_FLAG_WEST) == 0) )
          continue;
  }
  /* ... east (SPAN_FLAG_EAST), south, north identical in shape */
  ```

  `step != DONE` ≡ `method3974`, `step == READY` ≡ `method4216`, and
  `SPAN_FLAG_WEST/NORTH/EAST/SOUTH = 1/2/4/8` (`painters.h:21-24`) are the same
  bits as the official's `1/2/4/8`. This is a faithful port and §9.4 shows it
  firing identically on (71,56) in both clients.
* **Cull — different, but not an ordering effect.** At the identical camera and
  window the official paints 934 plane-0 floors and the C 735, with 651 in
  common; 283 tiles the official draws the C never does, 84 the reverse. That is
  the C's analytic cullspan. It removes tiles from the comparison; it does not
  reorder the ones that remain.

**The divergence is the ready queue.** Side by side:

```java
// class112.method4152 (pop), class112.java:595 — official
public int method4152() {
   int var1 = this.field1597;              // head sentinel
   int var3 = this.field1600[var1 << 1];   // head.next
   if (var3 >= this.field1597) return this.field1753;   // empty
   this.method4075(var3);                  // unlink
   return var3;
}
// method3914 (push), class112.java:6401 — unlink if linked, then splice
// immediately before the head sentinel, i.e. at the TAIL. FIFO. No key of any
// kind: order is exactly the order neighbours last touched a tile.
```

```c
/* painters_bucket.u.c:50 — C */
/* Pop farthest distance first; LIFO within a bucket (matches reference). */
static inline int bucket_pop(struct PainterBucketCtx* w, struct TilePaint* paints) {
    while( w->bucket_max >= 0 ) {
        int head = w->bucket_heads[w->bucket_max];
        if( head < 0 ) { w->bucket_max--; continue; }
        ...
/* push, :37 — head-insert into bucket_heads[Manhattan distance] */
paints[ti].queue_next = w->bucket_heads[dist];
w->bucket_heads[dist] = ti;
```

The C replaced an unkeyed intrusive FIFO with a **Manhattan-distance bucket
priority queue**. That changes two things:

1. **It makes the deferral test nearly vacuous.** The four neighbours
   `method4193` examines are, by construction, exactly the tiles one Manhattan
   step *farther* from the camera. A distance-descending queue has already
   retired all of them before the tile is popped, so the test cannot fire.
   Measured: 38.1 % of the official's plane-0 tiles are refused at least once,
   8.4 % of the C's. In the official, **94 % (149/158) of the steps where the
   paint stream moves *away* from the camera are caused by a tile that had been
   deferred**.
2. **It turns the stream into a distance sort.** Adjacent-step monotonicity of
   the plane-0 floor stream:

   ```
     C painter_paint_bucket   : Manhattan distance increases at    5 / 734 steps
     OFFICIAL sorted:method4241:                                 161 / 933 steps
     OFFICIAL plain:method4157 :                                 851 / 933 steps
   ```

   Excluding the wave restarts of §9.6.1, within-wave agreement with an ideal
   far→near Manhattan sort is **C 99.96 %** (4 backward steps, max +2) versus
   **official 96.19 %** (158 backward steps, max +4).

   The C's bucket *is* a distance-ordered priority queue. The official's
   diamond+list is not one and was never meant to be — the neighbour-dependency
   test, not a distance key, is what enforces back-to-front there.

The C's inline comment `LIFO within a bucket (matches reference)` is worth a
second look: the reference list is FIFO (push splices at the tail, pop takes the
head). Two of the seven wedge inversions in §9.5 are exact ties resolved the
other way, which is what a FIFO/LIFO flip inside an equal-distance group looks
like. Low stakes for the image, but the comment overstates the match.

#### 9.6.1 Both traversals fragment into waves; the wedge tiles land the same way

The queue drains before every tile is retired, the seed scan resumes, and the
stream jumps back out to the rim. Splitting each plane-0 floor stream wherever
the distance jumps by ≥5:

```
  OFFICIAL sorted : 4 waves  [343, 16, 517, 58]
      wave0 (34,63)d45 -> (50,41)d7      wave1 (32,37)d25 -> (37,40)d19
      wave2 (76,63)d45 -> (56,39)d1      wave3 (41,36)d17 -> (55,39)d0
  C bucket        : 2 waves  [292, 443]
      wave0 (33,63)d46 -> (51,39)d4      wave1 (77,63)d46 -> (55,39)d0
```

The official's 6 `CALL`s into `method4193` and the C's 2 `SEED` events are the
re-entry points. **Every wedge tile lands in the corresponding wave in both
clients**: the six western tiles (42,57) (43,55) (47,57) (47,56) (48,57) (48,56)
in wave 0 on both sides, the other nineteen in official-wave-2 / C-wave-1. The
official's extra waves 1 and 3 contain no wedge tile.

Decomposing the 12 934 discordant global pairs by the C's wave break:

```
  pairs inside one C wave : 111 027   discordant 5 413  (4.88 %)
  pairs across the break  : 100 548   discordant 7 521  (7.48 %)
```

So roughly 58 % of the residual is the wave boundary — the official splitting
the same tiles four ways where the C splits them two ways — and the rest is the
deferral effect of §9.6(1).

### 9.7 What actually *is* different, since the order mostly is not

**(a) Renderer architecture.** At default settings the stock rev-239 software
client runs `method4072`'s middle branch (class112.java:2749,
`class147.field2215.method5322() && !isGpu()`) and paints the frame **twice**:

```java
field2053 = 0; method4111(true);  method4157(proj);   // plain ascending triple loop
field2053 = 1; method4111(false); method4241(proj);   // the classic painter
field2053 = 2; callbacks.drawScene();
```

At `field2053 == 1`, `class128.method4380` (shaped tile, class128.java:104) and
`class128.method4389` (flat tile, class128.java:164) return immediately — so all
934 floor paints logged in the sorted pass are **called and discarded**. The
visible floor pixels come from `method4157` plus the depth buffer
(`class161.method5173()` true, `class510.field6014` initialised true at
class510.java:113; `::zbuf 0` toggles it). The C client is a single-pass
painter's-algorithm renderer with no depth buffer, so it is reproducing
`method4241` semantics — faithfully, as §9.1–9.6 show — while the frames it is
being diffed against were produced by `method4157` + z-buffer. That is a
renderer-architecture mismatch, not a traversal bug, and the 66 % / 62 % numbers
in §9.1 are its signature.

**(b) The C's draw box is centred on the player, not the camera.**
`app_update_painter_cull` (`src/app.c:5680-5685`) calls
`painter_set_draw_center(orbit_x >> 7, orbit_z >> 7)` while the follow camera is
active. `class112.method4111` derives the window from `field1755`/`field1765` —
the **camera** tile — unconditionally. At the pinned eye the official's window is
x[30,80) z[14,64) and the C's was x[30,80) **z[23,73)**: nine z rows the official
never considers, nine near rows dropped. Measured effect on the artefact: with
the C's own box the orange patch is **29 px** tall (y 143..171); forcing the box
eye-centred drops it to **22 px** (y 150..171). The seven removed rows are
alcove tiles at z ≥ 64 that the official's window excludes outright. The
distance metric, the wall tests and the seed generator already use the eye tile,
so only the *bounds* are wrong — which is why this shows up as extra geometry
rather than as reordering.

**(c) Speculative terrain emits.** The C emits 2940 terrain commands per frame;
only 652 become draws. Per-plane floor emits are plane0 844, plane1 626, plane2
735, plane3 735 — the official emits floors on plane 0 only, 934 of them.
`src/engine/world_builder/world_builder.c:641`
(`set[g] = terrain_vis_below[g] ? 0u : (1u << (terrain_src[g] & 3));`) runs for
every level `g` without asking whether that level has a mesh, so every visible
tile emits four floors and three die at `World_TerrainElementAt() < 0`
(`src/render/torirs_frame.c:1329`). Harmless to the image; it is why the C's
paint count (3482) is double the official's (1761) at the same camera, and why
the C log has 2940 `MARK`s against the official's 1317.

**(d) Coverage.** 283 plane-0 floors the official draws have no C counterpart
(x 32..78, z 36..61, dropped by the analytic cullspan — verified not relocated to
another plane), and 84 the C draws the official does not. About 30 % of the
official's floor paints have nothing to compare against.

### 9.8 What this does and does not say about §1–§4

It says nothing about §1–§4 either way. The ordering data neither supports nor
undermines the projection-scale measurement — a scale error cannot reorder
tiles, and reordering cannot change a tile's size. What the ordering data *does*
do is remove "the painter draws the wedge in the wrong order" from the list of
candidate causes, and add two new items to §7: the draw-box centre (9.7b) and
the renderer-architecture mismatch (9.7a). §1's claim that the wedge is "drawn
by the same loc models" is wrong — it is terrain — but that is a description
error, not a measurement error.

### 9.9 Artefacts

| path | what |
|---|---|
| `/tmp/off_paintorder_plain.txt` | official pass 0 (`method4157`), 1741 paints, 7-column |
| `/tmp/off_paintorder_sorted.txt` | official pass 1 (`method4241`), 1761 paints, 7-column |
| `/tmp/c_paintorder_bucket.txt` | C `painter_paint_bucket`, 3482 paints, 7-column |
| `/tmp/off_settled_zbuf1.txt` | official raw MARK/PUSH/POP/CALL, both passes |
| `/tmp/c_order_zuk.txt` | C raw MARK/SEED/PUSH/POP |
| `/tmp/off_wedge_tiles.txt`, `/tmp/c_wedge_tiles.txt` | per-wedge-tile traces |
| `/tmp/c_wedge_seq_table.txt` | the 25-row ordinal table |
| `/tmp/off_order_zuk_zbuf0.txt` | official with `::zbuf 0` — single pass, 1761 paints |
| `/tmp/off_zuk_D2.png`, `/tmp/c_wedge_final.bmp` | the two frames the logs describe |

Reproduce the official log: `WEDGE=1 tools/perf/run_java_client.sh`, then
`wedgelog /tmp/x_order.txt 1` over 127.0.0.1:43601 — see
`Deobfuscator/instr/RUNNING.md` and §6.

Reproduce the C log:

```
SDL_VIDEODRIVER=dummy TORIRS_WEDGE_CAM=7104,-743,5072,128,0 TORIRS_WEDGE_DRAWCENTER=eye \
  TORIRS_EXIT_BMP=/tmp/c_wedge_final.bmp TORIRS_MAX_FRAMES=900 \
  TORIRS_WEDGELOG=/tmp/c_order_zuk.txt TORIRS_WEDGELOG_AT=880 \
  TORIRS_NET_CHEAT="zuk" \
  src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test --soft3d
```

No C rendering behaviour was changed for this section; the wedge telemetry is
env-gated and inert when unarmed (terrain emit count is 2940 with the log on and
2940 with it off at the identical pinned camera).

---

## 10. Falsification tests — what is now ruled out

Two hypotheses tested and both eliminated. Recorded here because the workflow
that produced them was cancelled before it wrote its own verdict section.

### 10.1 The map data is identical across caches

Region **9043 = map square 35_83** (world x 2240–2303, z 5312–5375), derived
three independent ways — one of them from this repo's own content tree, where
`minigame_inferno/configs/inferno.constant` declares
`^inferno_template = 0_35_83_0_0`.

**The 25 wedge tiles are byte-identical in every cache.** All 25 tiles across
all 4 levels (100 rows) hash to one md5, `7a5a2eb84c937477583aeb3e3a5e7eb2`,
for `cache.osrs239`, `cache.osrs230`, `cache.osrs184` **and** `cache.kronos`:

```
L0: underlay 0, overlay 152, attr_opcode 2 -> shape 0, rotation 0,
    settings 1 (BLOCK), height byte 30 (h = -240)
L1: empty, except scene (43,55) which has settings 8
L2/L3: empty
```

`cache.rs254_zuk` carries the same geometry with ids remapped into the RS2
floor table (overlay 152 → 102) — a namespace difference, not a shape one.

**`attr_opcode 2` means shape 0, rotation 0: a plain full-tile overlay.** There
is no diagonal, fan or trapezium tile shape anywhere in the wedge set, in any
cache. The wedge *shape* is therefore not in the data; it is produced
downstream, by projection.

The `0x08` settings bit is identical everywhere too. The histogram over all
16,384 tiles is byte-for-byte the same in all five caches —
`{0: 12780, 1: 3226, 8: 378}` — the 378 bit-8 tiles are the same positions in
all five (md5 `e6979e17cab7e3df6d83860792f0d347`), all at level 1, and that set
matches the **live official client's census exactly**: 378 vs 378, empty diff.

Of the 25 wedge tiles, exactly **one** has an `0x08` tile above it. That is a
further nail in §1's superseded VIS_BELOW explanation.

Kronos/184 carry the Inferno in full: 875 plane-0 overlay tiles, 870 underlay,
and the same **2850 locs, diff 0** against osrs239. The whole square differs by
three tiles across caches, none in or near the wedge.

**Verdict: "the map data is different" is off the candidate list.**

### 10.2 Single-pass painting does not cause it

§9 established that the official at default settings paints twice into a depth
buffer, and that the C client is a single-pass painter's-algorithm renderer —
which made renderer architecture a plausible cause.

It is not. Observed directly by the operator: the official client with `::zbuf 0`
— single-pass `method4241` only, no depth buffer, i.e. the C client's
architecture — **still does not render the wedge**, even though that path is
otherwise broken and mostly black in this RuneLite build.

**Verdict: architecture is off the candidate list.**

### 10.3 Where that leaves it

Eliminated: map data (§10.1), `VIS_BELOW` geometry (§9, §10.1), draw order
(§9 — 293/300 concordant, and the official's own two passes agree with each
other *less*), renderer architecture (§10.2).

Still standing, both measured:

| | evidence | explains |
| --- | --- | --- |
| projection scale 512 vs 191 (§1–§4) | read from each client's own state | **size** (2.68×) |
| draw box centred on player, not camera (§9) | window z[23,73) vs z[14,64) | part of it — 29 px → 22 px |
| follow-camera eye ~15% too close (§7) | official d≈1314, C d≈1113 | **position**, untested |

The reframe that matters: the wedge is **not spurious geometry**. It is the
same overlay-152 tiles the official draws — 8 px at y 213–220 hugging the wall
there, 29 px at y 137–165 floating clear of it here. So the open question is not
"where does this object come from" but **"why are these tiles projected to the
wrong size *and place*"** — and a ~70 px vertical displacement is a position
error, which scale alone does not explain.

---

## 11. The experiment: both fixes applied and measured

**Answer up front: the wedge is gone, and §4's projection scale is the whole of
it.** The ~70 px vertical displacement §10.3 called "a position error scale
alone does not explain" *is* the scale — the band is a floor patch several tiles
in front of the eye, so magnifying the projection 2.68× about the viewport
centre moves it up the screen as well as making it bigger. Nothing else was
needed.

Both changes are in the tree and **off by default**. Every number below is from
the same build; the only difference between rows is environment.

### 11.1 The gates

| gate | default | what it does |
| --- | --- | --- |
| `TORIRS_WEDGE_SCALE=1` \| `auto` | **off** | §4 fix A. Recomputes `world_camera.fov_rpi2048` every layout from the world viewport **height** per `class159.method5357`, via `app_apply_wedge_scale()` (`src/app.c`, called at the end of `app_update_world_viewport`). Off ⇒ `fov_rpi2048` stays the compile-time 512. |
| `TORIRS_WEDGE_SCALE=<n>` (n ≥ 8) | — | Forces the linear scale to `n`, for bisection. |
| `TORIRS_WEDGE_ZOOM=<near>,<far>` | — | Overrides the decoded `VIEWPORT_SETFOV` endpoints (auto mode only). |
| `TORIRS_WEDGE_FOV_DEBUG=1` | off | Logs the SETFOV decode and the resulting zoom/scale/fov. |
| `TORIRS_WEDGE_DRAWCENTER=eye` | **off** | §9.7(b) fix B — pre-existing gate. Centres the painter draw box on the camera tile, as `class112.method4111` does unconditionally, instead of the orbit anchor. |
| `TORIRS_WEDGE_CAM=x,y,z,pitch,yaw` | off | Pre-existing. Pins the eye so a capture can be taken at the official's camera. |

Supporting change, always on but read by nothing in the default build:
`RS_CS2Host` gains `viewport_zoom_near` / `viewport_zoom_far`, set on every
`VIEWPORT_SETFOV` through `rs_cs2_viewport_zoom_decode()` = `Statics.method5659`
(`(int)pow(2, arg/256 + 7)`, 256 when ≤ 0). The raw args are still stored and
still what `GETFOV` answers, so CS2 behaviour is untouched.

### 11.2 Measured — exact `0xA45409` inside `x 280..470, y 120..245`

At the **C client's own settled camera** — eye (7104, −666, 5299), camera tile
(55,41), pitch 128, yaw 0:

| configuration | count | x extent | widest row | widest span | y extent (h) |
| --- | --- | --- | --- | --- | --- |
| baseline, no gates | 632 | 325..449 | 46 | 111 | **137..165 (29)** |
| rebuilt, gates off | 598 | 325..449 | 46 | 111 | 137..165 (29) |
| fix **B** only | 544 | 325..449 | 46 | 111 | 141..165 (25) |
| fix **A** only | 79 | 348..393 | 15 | 41 | **209..218 (10)** |
| fix **A+B** | 74 | 348..393 | 15 | 41 | **210..218 (9)** |

At the **official client's camera**, pinned with
`TORIRS_WEDGE_CAM=7104,-743,5072,128,0` — camera tile (55,39), the §9.0 viewpoint:

| configuration | count | x extent | widest row | widest span | y extent (h) |
| --- | --- | --- | --- | --- | --- |
| C, no fixes | 675 | 320..441 | 53 | 120 | 143..171 (29) |
| C, fix **A** only | 113 | 280..434 | 22 | **153** | 211..221 (11) |
| C, fix **A+B** | 100 | 280..434 | 22 | **153** | **213..221 (9)** |
| **official** `/tmp/r_06.png` | 142 | 302..455 | 30 | **153** | **213..220 (8)** |

Same eye, same window: **top edge 213 = 213** and **widest span 153 = 153**.
Height 9 vs 8, and the x extent is displaced 21–22 px left.

Baseline count varies run to run (632 / 598 / 675) because the arena is
animated; the **extents are bit-stable** across runs, so they are the metric.

### 11.3 What each fix contributed

* **Fix A (scale) does essentially all of it.** 29 px → 10 px and y 137..165 →
  209..218 on its own. Resulting scale: viewport height 503, decoded zoom
  endpoints 128/128, `503 × 128 / 334 = 192`, `fov_rpi2048 = 790`, effective
  scale read back through the kernel's own tan table **192.11** (official 191).
* **Fix B (eye-centred draw box) trims the top two rows** and is what lands the
  top edge exactly on the official's 213 (211..221 → 213..221 at the pinned
  eye). On its own it is a 4 px effect (29 → 25) — consistent with §9.7(b)'s
  29 → 22 at the pinned camera, and far too small to be the cause.

### 11.4 The three residuals, all already named in this document

1. **21 px horizontal displacement** — C's band is x 280..434, the official's
   302..455. That is exactly §7(a): the C world viewport is **723×503** and the
   official's **765×503**, so the projection centre sits at 361.5 instead of
   382.5, i.e. **21 px left**. The residual is not a new defect; it is the
   viewport-width defect measured through the band.
2. **Effective scale 192.11 vs 191** (0.6 %, ≈1 px on this band). Our CS2
   `VIEWPORT_SETFOV` receives raw args `0,0` → decoded zoom **128**, while the
   official's `proj` reports **127**. `503×128/334 = 192` vs `503×127/334 = 191`.
   Whether the official's 127 comes from a different SETFOV argument or from a
   saved zoom setting is unresolved and worth one grep; it cannot produce a
   visible artefact at this magnitude.
3. **Count 100 vs 142 and the narrower left edge** — the C analytic cullspan
   drops ~30 % of the official's plane-0 floors at this camera (§9.7(d): 283 of
   934 with no C counterpart), so fewer pixels of the *same* band survive.

### 11.5 What was not verifiable here

§5's gate 3 (Lumbridge re-shoot) did not produce a usable comparison: without
`TORIRS_NET_CHEAT="zuk"` this harness reaches Lumbridge with the player drawn
but **no terrain at all**, at 500 and at 900 frames, with the gate on *and* off.
That is a pre-existing property of the embedded-server harness, not a
regression from these changes — the two frames differ only in the player model
being 2.68× smaller with the gate on. A framing regression check needs a
scene that actually loads.

### 11.6 Artefacts

| path | what |
| --- | --- |
| `/tmp/wedge_base.bmp` | baseline, gates off, settled camera — the wedge |
| `/tmp/wedge_fixA.bmp`, `/tmp/wedge_fixAB.bmp` | settled camera, fix A and A+B |
| `/tmp/wedge_fixB.bmp` | settled camera, fix B alone |
| `/tmp/wedge_pin_none.bmp`, `/tmp/wedge_pin_A.bmp`, `/tmp/wedge_pin_AB.bmp` | pinned at the official eye |
| `/tmp/wedge_*_crop.png` | 4× crops of `x 280..470, y 120..245` |
| `/tmp/wedge_fixA.log` | `TORIRS_WEDGE_FOV_DEBUG` — the SETFOV decode and scale |

Reproduce (private objdir, nothing else in the tree touched):

```sh
make -C src PLATFORM_OBJ_BASE=build_wedge3 EMBED_SERVER=1 TORIDRAW_OPT=1 torirs
SDL_VIDEODRIVER=dummy TORIRS_WEDGE_SCALE=1 TORIRS_WEDGE_DRAWCENTER=eye \
  TORIRS_WEDGE_FOV_DEBUG=1 TORIRS_EXIT_BMP=/tmp/wedge_fixAB.bmp \
  TORIRS_MAX_FRAMES=900 TORIRS_NET_CHEAT="zuk" \
  src/torirs --manifest manifest_osrs230_embed.ini --user testc --pass test --soft3d
```

### 11.7 To promote this from a gate to the default

Three things, in this order: make the recompute unconditional (drop the
`app_wedge_scale_mode()` test), make the draw box eye-centred unconditionally
(delete the `follow_cam` branch's use of `orbit_x/orbit_z` for the *bounds*
only), and fix the 723 vs 765 world viewport width of §7(a) so the projection
centre stops sitting 21 px left. The §4.4 duplicate literal
(`src/render/torirs_frame.c:1135`) should read the same helper at that point.
§7(b)'s follow-camera orbit distance is then the only §7 item left, and it is
worth re-measuring after the above rather than before.

## 12. The projection scale is now a real parameter, not a constant

§11 proved the scale *is* the wedge, but it got there through a knob that could
not actually express the answer. This section is the follow-up: making toridraw
support the thing §4 asked for, and what that turned up.

### 12.1 What was wrong with the §11 mechanism

§11's `app_apply_wedge_scale()` computed the reference's integer scale and then
converted it to an **angle** with `atan`, because `ToriDraw_Camera` had only
`fov_rpi2048`. That conversion is lossy in a way §11 recorded but did not chase:
it reported an effective scale of 192.11 where the reference reads 191.

The reference has no field of view at all. `class159.method5357` writes an
integer to `client.field817`, and `Statics.java:33409` projects with it
directly — `scale * x / z + width/2`. toridraw's kernels compute
`cot(fov/2) * 512`, and the conversion opens with `fov >> 1`, so only **1024**
of the 2048 angles are distinct. The reachable scales step by ~1.7 near scale
190 and ~2.6 near 410:

| fov | 786 | 788 | **790** | **792** | 794 |
| --- | --- | --- | --- | --- | --- |
| effective scale | 195.69 | 193.89 | **192.09** | **190.31** | 188.52 |

**191 is not on that ladder.** Measured across the useful range, only **320 of
961** integer scales in [64, 1024] are reachable through an angle at all (33 %).
The angle route could never have matched the reference; it could only get near.

### 12.2 The change

`ToriDraw_Camera` now carries **both** spellings and an explicit selector:

```c
enum ToriDraw_ProjMode proj_mode;  /* SCALE (default) | FOV */
int proj_scale;                    /* class159's field817, exact */
int fov_rpi2048;                   /* an angle, for free cameras */
```

`proj_mode` selects; the unselected field is never consulted. Two fields racing
to define one quantity through "0 means use the other" is how a camera ends up
projecting with a value nobody set, so the rule is stated rather than inferred.
A zero-initialised camera selects SCALE and lands on the reference's own default
of 512.

Mechanically, the 16.16 multiplier is now resolved **once per frame** at the two
places that read the camera, instead of being recomputed by a `g_tan_table`
lookup inside every kernel. 48 kernels across 7 SIMD variants (`scalar`, `sse2`,
`sse41`, `avx`, `neon`, `sse_float`, dispatcher) take `camera_cot16` where they
used to take `camera_fov`.

### 12.3 Four more places the scale was hardcoded

Chasing the literals turned up hardcodes §4 had not named:

| site | what it did |
| --- | --- |
| `painters_cullmap.u.c:48` | passed a bare `512` as the fov to the baked cullmap's frustum test — the bake assumed scale 512 **whatever the camera projected with** |
| `painters_cullspan.u.c` | a second, independent `focal = 512.0 / tan(half_fov)`, with its own `512` fallback. Cull and raster could disagree about the scale |
| `projection.u.c` `project_perspective()` | took a `fov` argument and **ignored it**, always projecting at `UNIT_SCALE` |
| `app.c` ×2, `torirs_frame.c` | bare `= 512` camera defaults |

All now thread the same trio. The cullspan one matters beyond tidiness: a cull
frustum computed at a different scale than the rasterizer draws at removes tiles
that would have been visible, which is §11.4's residual 3.

### 12.4 A latent inversion bug in the fov knob

`cot(fov/2)` is only positive while `fov/2 < 90°`, i.e. `fov < 1024`. Past that
the tan table returns a **negative** cotangent: `fov = 1200` resolves to scale
**−142**, which mirrors the world rather than failing. Nothing had ever
constrained the domain. `TORIDRAW_PROJ_FOV_MIN/MAX` now clamp it, and
`toridraw_proj_fov_from_scale()`'s binary search is restricted to the monotonic
region — searching across the sign flip returned nonsense (round-trip drift up
to 167400 before, **0** after).

### 12.5 Measured

Same pinned eye as §11.2 (`TORIRS_WEDGE_CAM=7104,-743,5072,128,0`), exact
`0xA45409` inside `x 280..470, y 120..245`:

| configuration | count | x extent | widest span | y extent (h) |
| --- | --- | --- | --- | --- |
| default (no gates) | 676 | 320..441 | 120 | 143..171 (29) |
| `TORIRS_WEDGE_SCALE=1` + eye box | 97 | 280..434 | **153** | **214..221 (8)** |
| `TORIRS_WORLD_FOV=790` + eye box | 99 | 280..434 | **153** | 213..221 (9) |
| **official** `/tmp/r_06.png` | 142 | 302..455 | **153** | **213..220 (8)** |

Both gates report "realised scale 192", and they still differ by a pixel —
because the angle's true multiplier is 192.086 and the exact one is 192.000.
That 0.045 % is the ladder, visible in a real frame. §11 got h 9 through the
angle; the exact scale gives **h 8 = the official's h 8**.

The x displacement (280..434 vs 302..455) is unchanged and is still §7(a): the
C world viewport is 723 wide against the official's 765, so the projection
centre sits 21 px left. The remaining scale gap is 192 vs 191 — our
`VIEWPORT_SETFOV` decodes zoom 128 where the official reports 127.

### 12.6 What this cost, and how it was verified

The whole-frame BMP comparison used in §11 **is not a valid identity test**: two
runs of the *same* binary at the same pinned camera produce different files
(`7d5b2012…` vs `b3066fb2…`), because the arena is animated. Extents are stable;
byte-identity is not. The check was replaced with a deterministic probe over the
kernels themselves, built once against `HEAD` and once against the tree, sweeping
all 2046 angles through `project_vertices_array`, `project_divide` and
`project_scale_unit`:

```
HEAD  (fov arg, original kernels):     764e6626d106be73
TREE  (cot16 arg, hoisted kernels):    764e6626d106be73
negative control (deliberately fov+2): 396ce839ae52efd7
```

The hoist is exact. The negative control confirms the probe can fail.

**One deliberate behavioural change remains.** The old default resolved fov 512
through the tan table to cot16 **65535**, an effective scale of 511.992. The new
default is the integer 512, cot16 **65536**, exactly 512.000 — which makes
toridraw's default projection `p * 512 / z`, bit-identical to the reference's own
formula, which it previously was not. Cost, measured over 601835 vertex
projections across the full pitch/yaw sweep:

```
samples 601835, differing 49283 (8.19%), worst |dx| 2 px, worst |dy| 2 px
```

The 2 px cases are all near-plane geometry (the 1-unit pre-divide difference is
multiplied by 512 and divided by a small `z`); at typical scene depths it is
under 0.2 px. `test-world-builder`, `test-light-model` and `test-scanline` pass.

## 13. Painter stepping: the draw order exonerated by construction

§9 exonerated the traversal statistically (293/300 concordant pairs). This
section does it by construction: render exactly the first N painter commands,
screenshot, and watch the wedge get painted and then covered. Two new tools
made that possible, both now in the tree.

### 13.1 Boot straight into the scene

`manifest_osrs230_zuk.ini` boots the whole encounter with no environment
variables:

```sh
make -C src torirs EMBED_SERVER=1
src/torirs --manifest manifest_osrs230_zuk.ini
```

It is `manifest_osrs230_embed.ini` plus credentials (`user=testc pass=test`)
and a new manifest key, `[net:boot] cheat=zuk` — "::" commands (';'-separated,
no leading `::`) sent once right after login, the manifest spelling of the
`TORIRS_NET_CHEAT` harness hook (the env var still overrides). Plumbing:
`bootmanifest.{h,c}` → `AppConfig.net_cheat` → the existing cheat block in
`app_logic_tick`.

Verified: a run with no env vars lands in the instance and reproduces the
artefact bit-for-bit (909 exact `0xA45409` px, x 325..449, y 137..165, h 29,
widest 71 at the settled camera).

### 13.2 The painter-command cap (the v0 client's `cc`, ported)

The v0 client stepped its painter by capping how many commands the frame
consumed (`game->cc`, `v0/tori_rs_frame.u.c:1324`) with keys to nudge it.
torirs now has the same seam in `try_emit_world_draw_model`
(`src/render/torirs_frame.c`):

| knob | what |
| --- | --- |
| `TORIRS_PAINT_LIMIT=N` | consume at most N painter commands per frame; -1/absent = unlimited. The unit is `painters_index`, i.e. every consumed command, drawn or dropped, so a position names the same command each frame of the same scene. |
| `TORIRS_PAINT_LIMIT_STEP=S` | advance the cap by S every frame — pair with `TORIRS_BMP_SERIES` for a flip-book |
| `TORIRS_PAINT_LIMIT_STEP_AT=F` | start advancing only at frame F (so login/cinematic frames don't burn the sweep) |
| keys `I` / `J` / `K` / `L` / `,` | toggle unlimited↔0 / +1 / −1 / +100 / −100, logged as `paintlimit: N` (same gate as the W/S/A/D camera keys) |

`TORIRS_DRAW_ORDER=<frame>`'s dump now prints `cmd=<painters_index>` on every
line, so a cap value maps exactly to a dump line: cap `cmd+1` draws up to and
including that line.

### 13.3 What stepping showed

All at the §9.0 pinned eye (`TORIRS_WEDGE_CAM=7104,-743,5072,128,0`), default
projection (gates off), exact `0xA45409` in `x 280..470, y 120..245`:

| cap | count | y extent (h) | meaning |
| --- | --- | --- | --- |
| 1267 | 4 | — | before the first alcove tile: no band |
| 4765 | 4435 | 143..196 (54) | all 25 alcove tiles painted, nothing nearer yet: the floor is a huge orange **sheet** |
| 4900 | 1740 | 143..196 (54) | rim partially drawn |
| 5050 | 676 | 143..171 (29) | rim complete — the final image's band, already exact |
| full (5587) | 675 | 143..171 (29) | nothing after cmd ~5050 touches the band |

The 25 wedge tiles paint at cmd 1267..4764 (western six early — §9.6.1's wave
0 — the rest in the second wave, same as the official). Then commands
4765..5050 draw the **arena rim** in front of them: `inferno_floor_sand_straight_01`
(30288), `inferno_floor_lowered_02b` (30349), `inferno_floor_fixed_02a/b`
(30360/30361), `archeuus_invisible_type_1` (27787) and the
`wilderness_rocks_floor_hard_02..05` rock piles (14391–94), at slots z 44..56 —
between the camera and the alcove. They cover 85% of the sheet.

So the painter does exactly what it should: far floor first, near rim after,
rim occludes floor. **The wedge is the residue of correct occlusion** — the
strip of alcove floor whose screen rows sit above the rim's on-screen top
edge. That residue's height is a pure projection question, and it scales with
the §1 scale error: 29 px at the constant 512, and with the §11/§12 gates on
(`TORIRS_WEDGE_SCALE=1 TORIRS_WEDGE_DRAWCENTER=eye`, same build, same pinned
eye) **8 px at y 214..221** — the official's own h=8, one pixel below its
213..220.

There is no draw-order bug, no spurious geometry, and no missing occluder;
there is only the projection scale, and §11.7 remains the promotion checklist.

### 13.4 Artefacts

| path | what |
| --- | --- |
| `/tmp/zuk_manifest.bmp` | manifest-only boot, settled camera — the wedge, 909 px |
| `/tmp/zuk_order.log` | `TORIRS_DRAW_ORDER=800` dump with `cmd=` indices, pinned eye |
| `/tmp/zuk_cap{0,1267,4765,4900,5050,5200,5350,5500}.bmp` | the stepping series |
| `/tmp/zuk_full.bmp`, `/tmp/zuk_fix.bmp` | full frame, gates off (675 px) / on (97 px, h 8) |

Reproduce one step:

```sh
SDL_VIDEODRIVER=dummy TORIRS_WEDGE_CAM=7104,-743,5072,128,0 \
  TORIRS_PAINT_LIMIT=4765 TORIRS_EXIT_BMP=/tmp/step.bmp TORIRS_MAX_FRAMES=900 \
  src/torirs --manifest manifest_osrs230_zuk.ini --soft3d
```

`test-bootmanifest`, `test-world-builder`, `test-light-model` and
`test-scanline` pass with these changes.

## 14. Terrain-flags audit: every settings bit against both references

Follow-up to §9.7(c) and the user's ask to re-verify flag rendering behaviour
against the official client and Client-TS. Method: enumerate every consumer of
the tile-settings byte in rev-239 (`class100.field1398` → scene copy
`class112.field1720`) and in Client-TS (`ClientBuild.mapl`), then diff ours.
No cache data was touched — §10.1 already proved the map bytes identical
everywhere; this is purely about what the client does with them.

### 14.1 The reference semantics, bit by bit

| bit | rev-239 | Client-TS | torirs before audit |
| --- | --- | --- | --- |
| 0x1 BLOCK | collision only (`method982`, level lowered by 1 under a LINK_BELOW column) | `finishBuild` → `blockGround`, same lowering | same — collision, not rendering |
| 0x2 LINK_BELOW | `method3884`: full column shift of tile records (plane N+1→N), original plane-0 parked at plane 3, new deck flagged 0x20 + linked (`field5536`); drain loop draws the parked underpass column first | `World.pushDown` | same scheme (`painter_tile_copyto` shuffle, park at 3, `bridge_tile` link, underpass drawn on deck pop) — ✓ |
| 0x4 REMOVE_ROOF | accessor `method4160`, feeds the roof-hide logic | `roofCheck`: camera→player line walk | same line-walk port (`app_world_roof_check`) — ✓ |
| 0x8 VIS_BELOW | tile flag 0x40 → `method4161` (renderLevel) answers 0; consumed by the mark gate `renderLevel <= field1653` (class112.java:2000), the hover-pick gate (`Statics.field2292`), and the entity gate `method3986`. **Geometry never moves.** | `setLayer(level,x,z,getVisBelowLevel(...))` → `Square.drawLevel`; draw gates on `drawLevel <= maxLevel`. Geometry never moves. | **DIVERGED — fixed, §14.2** |
| 0x10 FORCE_HIGH_DETAIL | lowMem-only content gate | same (`ClientBuild.lowMem`) | n/a (no lowMem mode); minimap skip matches both |

The minimap treatment of 0x8/0x10 (skip the flagged tile's own-level bake,
bake the level+1 tile onto this level) is present and correct on our side
(`minimap.c:804-812`).

### 14.2 The divergence: VIS_BELOW relocated the mesh

An earlier session (chasing this document's wedge, on the since-refuted theory
that the alcove floor was a vis-below tile) made the world builder move a
flagged tile's terrain mesh into the *lower* level's `terrain_levels` set. Both
references instead lower only the tile's **draw level** — the cull/pick value —
and draw the mesh from its own plane's traversal slot, *after* the tile below
fully retires. The relocation reversed that order: the borrowed floor emitted
before the lower tile's walls.

Its stated motivation — surviving the roof-hide level mask — was already
covered: our mark gate `tile_excluded_by_bridge_or_draw_mask` tests
`visible_gte_level` (the reference's renderLevel), and bit 0 of the mask is
always set. The rewritten `painters_test_terrain_levels.c` proves it: the
flagged tile still draws under `level_mask=0x1` with no relocation.

This was no corner case: **312,831 tiles cache-wide** carry VIS_BELOW with
real geometry at their own level (censused from the `.jm2` map dumps —
overhangs, waterfalls, upper river tiles).

Fix (world_builder.c): the per-column loop now only calls
`painter_tile_set_draw_level(...)`; `terrain_levels` stays "own mesh only"
(carried through the bridge shuffle by `painter_tile_copyto`).

### 14.3 §9.7(c)'s speculative emits, also fixed

The builder left the default `terrain_levels` on all four levels of every
column, so mesh-less levels emitted terrain commands that died downstream at
`World_TerrainElementAt() < 0`. The reference never queues content-less tiles
(`method3940`). A new pass after `world_build_scene_terrain` clears the bits of
levels that decoded no mesh. Measured at the §9.0 pinned eye: the painter
buffer shrank **5587 → 1572 commands** with the identical 1572 draws — every
command now becomes a draw. (§13.3's `cmd=` values describe the pre-fix
buffer; the order of the draws themselves is unchanged.)

### 14.4 Verified

* Zuk scene, pinned eye: wedge extents bit-identical before/after (x 320..441,
  y 143..171, h 29) — the Inferno's 378 bit-8 tiles have no geometry, so the
  only change there is the dead-command purge.
* River Lum east (`::tele 3324 3205`), `TORIRS_TILETABLE=48,60,45,60`: the
  vis-below waterfall column now reads e.g. `54,54 L0 elem=6603 set=0x1
  order=1961` / `L1 0x09 elem=1042 set=0x2 order=1962`, with the L1 waterfall
  locs at 1963-64 — floor above emitted from its own level, immediately after
  the tile below retires, exactly the reference drain order. A flagged tile
  with no mesh (`60,57 L1 0x09 elem=-1`) emits nothing.
* `test-painters-terrain-levels` (rewritten to pin reference semantics),
  `test-world-builder`, `test-minimap`, `test-painters-occluders` all pass.
  The terrain-levels test target also gained the toridraw `shared_tables.c` it
  had been missing since §12 made the painter read `g_tan_table`.

## 15. Promoted: the reference projection is now the default

§11.7's checklist, executed. The wedge is gone from the shipped build — no
environment variables required.

### 15.1 What changed

* **Projection scale** — `app_apply_wedge_scale` now runs by default: the
  world projection scale is recomputed from the world viewport height on every
  layout (`class159.method5357`), reaching the kernels exactly through
  `proj_scale`. `TORIRS_WEDGE_SCALE=off` (or `0`) restores the legacy constant
  512 for A/B; `1|auto` and `=<n>` keep their old meanings.
* **Draw box centre** — the painter's draw box is eye-centred unconditionally
  (`class112.method4111` semantics). `TORIRS_WEDGE_DRAWCENTER=orbit` restores
  the old orbit-anchored box for A/B (the gate's polarity flipped: `eye` was
  the opt-in, `orbit` is now the opt-out).
* §4.4's fallback literal was already gone — the no-world-camera path reads
  `TORIDRAW_PROJ_SCALE_DEFAULT` since §12.

### 15.2 Measured, default build, no gates

Pinned at the official eye (`TORIRS_WEDGE_CAM=7104,-743,5072,128,0`), exact
`0xA45409` in `x 280..470, y 120..245`:

| configuration | count | x extent | widest | y extent (h) |
| --- | --- | --- | --- | --- |
| **default (promoted)** | 97 | 280..434 | 22 | **214..221 (8)** |
| `TORIRS_WEDGE_SCALE=off` + `DRAWCENTER=orbit` | 685 | 320..441 | 51 | 143..171 (29) |
| official `/tmp/r_06.png` | 142 | 302..455 | 30 | 213..220 (8) |

The default equals §12.5's gated row bit for bit; the off-switches reproduce
the legacy wedge exactly, so the A/B knob is proven both ways. At the client's
own settled camera the band is 81 px, y 210..219 (h 10) — §11.2's fix-A+B row
with the exact scale.

Lumbridge courtyard (`::tele 3222 3218`) renders at the reference framing —
the §11.5 blocker (no terrain at Lumbridge in the embed harness) is gone now
that the mock embeds real map loading, so the framing check finally ran.

`test-world-builder`, `test-scanline`, `test-light-model`,
`test-painters-terrain-levels`, `test-painters-occluders`, `test-minimap`,
`test-bootmanifest` all pass.

### 15.3 §7(a) reframed: the 21 px is the popout strip, not a defect

The last §11.7 item — "fix the 723 vs 765 world viewport width" — dissolves
on inspection. The 42 px is clientscript 5355 carving the **popout strip**
(interface 728, the launcher column that hosts the xptracker / hiscores /
loottools panels this repo deliberately implemented) out of the canvas; the
`viewport` component (clientCode 1337, which drives the reference's own
`method5357` through `class71.method2484`) then resolves at 723 inside the
shrunken `gameframe`. That is the authored layout: a Jagex-client-style boot
*with* the strip has a 723-wide world viewport, centre 361.5, and our
projection is exactly right for it. The official numbers in §3.1 were read
from RuneLite, which has no strip — its 765/382.5 is the strip-less
configuration of the same math, not a truer one. The 21 px x-offset between
our frames and RuneLite's is a chrome difference and only matters to
pixel-diff comparisons; suppress the strip (or compare against a Jagex-client
capture) if a diff needs the centres to coincide.

Residuals still open, unchanged: zoom 128 vs 127 (§11.4.2, ≈1 px) and the
follow-camera orbit distance (§7b, worth re-measuring now that the scale no
longer amplifies it).

## 16. "It's still there" — the pre-cutscene phase, measured

A user report with screenshots: the orange platform is visible **prior to the
cutscene**, in a resizable window. Measured against the official's own capture
series (`/tmp/nz_*.png` + `/tmp/nzc_*.txt` camera traces), phase by phase, all
with the §15 defaults on. The pre-cutscene scene was reproduced without racing
the mock's cutscene by teleporting to the **template** square
(`::tele 2271 5352` — `0_35_83_0_0`, byte-identical to the instance before any
loc change, §10.1).

Exact `0xA45409` over the full frame:

| phase / camera | official | ours (defaults) |
| --- | --- | --- |
| pre-cutscene, settled follow cam, 765x503 | 274 px, y 218..268 (nz_05) | **190 px, y 212..256** |
| mid-cutscene, scripted eye (7744,-1240,5824) | 12,215–12,709 px, y 242..394 (nz_15–30) | **5,457–5,844 px, y 244..326** |
| post-cutscene, settled | 475–612 px, y 213..285 (nz_35–60) | **70–185 px, y 210..256** |
| pre-cutscene, resizable 858x750 world rect | *(no capture; formula predicts ~1.5x the 765x503 counts)* | **431 px, y 313..360** |

Per-column top-edge diff at the settled camera (ours vs `r_06.png`): median
**8 px higher**, max 11, zero columns >15; ours has orange in 59 columns to the
official's 159.

Three conclusions:

1. **At every matchable camera we now show equal or LESS orange than the
   official.** The band's top edge sits ~8 px higher — exactly §7(b)'s
   follow-camera orbit distance (ours ~1113 vs official ~1314), the one
   residual still open. Nothing shows "more orange than official".
2. **The official renders the ledge too.** Its own pre-cutscene frame has
   274 exact-orange px above the rim, and its mid-cutscene camera (nz_20)
   shows the ledge as a large bounded orange band — the same composition as
   the report's second screenshot. "The official client has no such orange
   platform" is not what the captures show; the official draws the same
   geometry, bounded the same way.
3. **Ours under-draws the ledge at the cinematic camera** — 5.6k px vs the
   official's 12.5k, the band's lower half (y 326..394) missing. Top edges
   agree (244 vs 242). This is the §11.4 residual-3 cullspan overcull showing
   up as *missing* floor, and is now the largest measured divergence in the
   scene — in the opposite direction from the report.

The official's aspect clamps (`class159.method5357` field1040/810/804/805)
were re-derived for the resizable window: at 858x750 with zoom 127 they stay
inert and the scale is `(int)(750*127/334) = 285` vs our 287 — parity.

**Likely explanation for the report:** a binary built before f4eaa5d6 (the §15
default promotion) still draws the 2.68x wedge; `TORIRS_WEDGE_SCALE=off` at
900x750 reproduces the reported plateau composition. Confirm with a rebuild.
The remaining true positives to attack are §7(b) (orbit distance, ~8 px) and
the cinematic-camera cullspan under-draw above.

## 17. The instance regression: zone packets dropped during the async load

User report: still visible with `::zuk` specifically, wall intact — and the
hunch "weird locchange" was right. The loc surgery is fine; the transport of
it was lossy.

### 17.1 The mechanism

`::zuk`'s script does its scene surgery right on the heels of REBUILD_REGION
(`inferno_zuk.rs2:30-58`): teleport in, then within a couple of ticks the
plane-1 flank walls (`loc_add` of 30346/30345 inside a tele-to-plane-1
window), the crag `loc_change`s, and the prison-roof multiloc re-transmit
(`%inferno_prisonroof_hidden`, morphs 9x4 loc 30356 → nothing).

Our REBUILD is an **async task** (models load over frames), and
`exec_zone_sub_packet` opened with

```c
if( !app || !app->world || !app->world->load_complete )
    return;   /* <- silently dropped */
```

so any zone sub-packet arriving in the load window vanished. The reference
cannot lose these: its scene build runs synchronously inside the packet loop,
so every zone update processes after the rebuild it follows. Ours was a race —
headless runs (fast frames) usually won it, and one measured run lost it: the
flank walls at **0 draws over 900 frames** while an identical re-run drew
them. Interactive frame rates make the window several server ticks wide, which
is why the user saw it reliably: dropped crumble ops leave the seal wall stuck
intact (both screenshots show the intact seal face), dropped flank adds leave
holes, and the ledge/alcove orange reads as "the platform is back".

Confirmed against the official's own dump (`/tmp/off_final.txt`): official L1
carries the two flank walls (`nloc=1` at 52,53/57,58 x z 60..64); ours had no
L1 scenery there at all in the losing run.

### 17.2 The fix

Zone sub-packets that arrive while `!world->load_complete` are queued
(`App::pending_zone`, 256 entries) and replayed in arrival order once the load
completes — lazily before the next live zone packet, plus a drain in
`app_logic_tick` for a queue with no follow-up traffic. Each entry captures
the zone base AND the local player's plane **as of arrival**: the wire
addresses the player's plane at send time, and the flank adds only mean
plane 1 inside the tele window (`zone_tile` → `zone_tile_at` refactor in
`rs_gameproto_exec.c`).

Also verified on the way (all already reference-faithful): `zone_tile`'s
player-plane rule matches "LOC_ADD_CHANGE has no plane on the wire"; the
official's dynamic-add height sampling (`plane+1` under a LINK_BELOW column,
client.method1812) has no divergent counterpart here because heights are
sampled from the shifted grid; and the prison-roof multiloc morphs correctly
whenever the varbit packet is actually processed (the earlier "stuck roof" was
this same drop eating the re-transmit).

### 17.3 Verified

* Three consecutive `::zuk` runs: flank walls 1/1 drawn, all four crag pieces
  drawn, roof morphed away, identical 1698-command streams — previously
  intermittent.
* `test-net-exec` passes; settled pinned-eye wedge unchanged at 97 px h=8
  (y 214..221), the official's own extent.

## 18. Instance re-entry, and what actually paints the orange

Three things asked at once; two are answered by measurement and one was a real
server bug.

### 18.1 Re-entering an instance never rebuilt anything (FIXED)

`::zuk` ten times in one session produced **one** server scene build and
**one** REBUILD_REGION. Every later run played against the first run's arena —
walls already crumbled, prison roof already morphed away, the ledge behind
them exposed. That is the "it comes back on the second try" report.

The client was not at fault: REBUILD_REGION passes `force` to
`App_WorldRebuildBegin`, so it always rebuilds. The server never sent one.

Why `maybe_rebuild` cannot see it: it fires when the scene *window* moves, and
re-entry does not move it. The allocator hands out the first free slot and the
first free block, so freeing an instance and allocating another returns **the
same handle at the same map square** (measured: `map instance 1 released (map
square 100,1)` / `map instance 1 reserved ... (map square 100,1)`, ten times).
Handle and coordinates are both reused, so neither can distinguish "still
standing in your instance" from "a new instance built on its grave".

Fix: a pool-wide **build generation** (`mock230_mapinstance_generation`),
bumped by every `_build`. `Mock230Player::scene_instance_generation` records
what the client was last shown; `phase_client_out` compares it against the
instance under the player's feet and, on a mismatch, rebuilds the server's own
scene (collision + locs + occupancy), resets zone tracking and sets
`rebuild_pending`. The server's copy is as stale as the client's, so both are
rebuilt together.

Measured, 9 entries in one session:

| | before | after |
| --- | --- | --- |
| instances reserved | 9 | 9 |
| server scene builds | **1** | **9** |
| REBUILD_REGION sent | **1** | **9** |
| client rebuild_shift | **1** | **9** |

### 18.2 What paints the orange — named

A new probe answers this directly instead of by inference:
**`TORIRS_PIXOWNER=x0,x1,y0,y1[,RRGGBB]`** snapshots the rect after every
render command and attributes each changed pixel to the command that wrote it,
aggregating into `colour cmd kind elem loc/TERRAIN tile|wpos pixels`
(`TORIRS_PIXOWNER_OUT`, `TORIRS_PIXOWNER_AT`). Inert unless armed.

At the cutscene camera, pinned with `TORIRS_WEDGE_CAM=7744,-1240,5824,128,98`
(the official's own `nz_20` eye), the exact-`0xA45409` pixels are **not
terrain**. They are ground-decor locs:

```
a45409 cmd=388 elem=18118 loc wpos=6336,-240,7488 pixels=59   -> slot 49,58
a45409 cmd=350 elem=18121 loc wpos=6208,-240,7488 pixels=56   -> slot 48,58
a45409 cmd=312 elem=18423 loc wpos=6080,-240,7488 pixels=55   -> slot 47,58
a45409 cmd=429 elem=18115 loc wpos=6464,-240,7488 pixels=42   -> slot 50,58
```

Cross-referenced through `TORIRS_TILETABLE`, every one is **loc 30291
`inferno_floor_small_plane_02`** — `shape1=22`, i.e. **ground decor**, 97 of
them in the template (rows lz 49/50/51, 29 each), all **level 0**. Its
neighbours in the same band are **30288 `inferno_floor_sand_straight_01`**
(shape 22, 110 instances, level 0) and **30290 `inferno_floor_small_plane_01`**
(shape 10). The arena floor terrain (overlay 152) is a *different*, darker
family — `8c6332 / 896131 / 815b2e` — measured in the same probe run.

So the orange band is the Inferno's lava-lit floor-plane decor strip at the
wall base. It is authored content, at the level the template puts it, drawn by
both clients. Nothing spurious, no multiloc, no terrain flag involved.

### 18.3 And the official draws more of it, not less

At the identical pinned eye, exact `0xA45409` over the whole frame:

| | official | ours |
| --- | --- | --- |
| cutscene camera (nz_20 eye) | **12,215–12,709 px** | **5,724 px** |
| settled camera | 274 px | 190 px |

We under-draw it roughly 2:1 — §11.4 residual 3 (the analytic cullspan drops
~30 % of the official's plane-0 floors at this camera). There is no camera at
which we paint *more* orange than the reference.

Two confounds worth recording, both of which cost a measurement here:

1. **`TORIRS_PIXOWNER` perturbs the timeline.** It is O(commands × rect) per
   frame, and the game clock is wall-clock driven, so frame N with the probe on
   is a *later* game state than frame N without it. Pin the camera
   (`TORIRS_WEDGE_CAM`) instead of trusting a frame number.
2. **`TORIRS_EXIT_BMP` renders one extra frame**, so a probe keyed to "the last
   frame" and a screenshot taken in the same run can describe different states.

## 19. Ground decor and the tile flags — audited, and a toggle

Hypothesis under test: ground decor is not inheriting the terrain's flag
handling (a dropped push-down), which would explain the orange band.

### 19.1 TORIRS_NO_GROUND_DECOR — the band is decor, confirmed

New gate, read once, checked by all three painter variants
(`painter_ground_decor_enabled`). At the pinned cutscene camera:

| | exact `0xA45409` |
| --- | --- |
| default | **5,724 px** |
| `TORIRS_NO_GROUND_DECOR=1` | **914 px** |

The wide orange plateau disappears and only the thin lava line along the wall
remains. That independently confirms §18.2's pixel attribution: the band is
**shape-22 ground decor** (`inferno_floor_small_plane_02` 30291,
`inferno_floor_sand_straight_01` 30288), not terrain.

### 19.2 The flag inheritance is correct — now pinned by tests

Three new cases in `painters_test_terrain_levels.c`:

* **`test_link_below_carries_ground_decor`** — a bridge push-down moves the
  deck's ground decor with the tile, and it is emitted once from the
  pushed-down tile under a level-0-only mask. Both references move the whole
  tile record (`World.pushDown` reassigns `Square` objects; `method3884`
  relinks the per-tile arrays), and `painter_tile_copyto` copies the struct.
* **`test_bridge_underpass_draws_no_decor`** — the parked underpass tile draws
  ground + wall + scenery and **not** decor. That is the entirety of
  `class112.java:792-812`; asserting it stops a well-meaning "draw everything
  on the tile" from becoming a divergence.
* **`test_vis_below_reveals_the_tiles_ground_decor`** — VIS_BELOW lowers the
  whole TILE's draw level, so decor standing on it is revealed with the mesh.
  The reference has no per-feature draw level (`method4161` is per tile, and
  the mark gate tests it before any contents are considered); ours is the same
  shape and this pins it.

Scale check for why this matters: **45,536 ground-decor locs sit on
LINK_BELOW columns** cache-wide (censused from the `.jm2`/`.jl2` dumps), so
the bridge case is routine, not a corner.

**One caution recorded because it nearly became a false report.** The
push-down test failed on the first run, which looked like exactly the
suspected bug. It was the *test helper* that was wrong: it copied the tiles
but skipped the draw-level pass that the world builder runs as part of the
same LINK_BELOW handling. `painter_tile_copyto` deliberately does not move
`visible_gte_level`, so without that pass the pushed-down deck keeps the
source level's value and a level-0 mask hides the tile it just became. With
the helper completed the client passes unchanged. A simulation of a build step
has to simulate all of it.

### 19.3 And on these tiles there is no flag to inherit

The band's own tiles carry `flags=0x01` (BLOCK) at level 0 and nothing at all
above — no LINK_BELOW, no VIS_BELOW:

```
 47,58   L0  0x01 elem=15003 set=0x1 order=310
 48,58   L0  0x01 elem=15002 set=0x1 order=348
 49,58   L0  0x01 elem=15001 set=0x1 order=386
 50,58   L0  0x01 elem=15000 set=0x1 order=427
```

Terrain and decor are both plain level 0 there, so no push-down is being
dropped on the geometry that draws the band. Combined with §18.3 (the official
draws 12.2–12.7k of the same orange at this eye against our 5.7k), the band
remains authored content that we under-draw rather than over-draw.

## 20. Is the orange decor ours or Jagex's? — Jagex's, verified

§19 called the band "authored", meaning **authored by Jagex in the cache**.
That deserved checking rather than asserting, because
`manifest_osrs230_zuk.ini` boots `cache.osrs239.baked` — the content tree
baked back into a cache — and `main_file_cache.idx5` (maps) **does** differ
between it and pristine `cache.osrs239`. So the bake touches maps, and the
question "did we add this decor?" was live.

Answer: **no**. Booting the identical scene from the pristine cache (a
scratch manifest with `dir=<abs>/cache.osrs239`, everything else unchanged)
and diffing the rendered loc set at the same pinned camera:

| loc | pristine `cache.osrs239` | baked |
| --- | --- | --- |
| 30291 `inferno_floor_small_plane_02` (the band) | **88** | **88** |
| 30288 `inferno_floor_sand_straight_01` | 39 | 39 |
| 30290 `inferno_floor_small_plane_01` | 34 | 34 |
| 30293 `inferno_roof_small_plane_01` | 85 | 85 |
| exact `0xA45409` pixels | 5,717 | 5,741 |

683 vs 684 locs overall, and **every one of the 13 differing lines is
runtime cutscene state**, not map data: `inferno_*_state1` in one run against
its `_state2` counterpart in the other (30332/30333/30334 → 30339/30340/30341,
plus 30324 `inferno_wall_edge_large_06`), which is `~inferno_loc_swap` firing
at slightly different points in the collapse. The idx5 difference is
re-encoding, not re-authoring.

`OSRS-Content/osrs239-content/maps/m35_83.jl2` is also unmodified since its
unpack commit (`git status` clean, single commit `b925318331 content`).

**The Inferno grid's content files already match the cache source exactly, so
there is nothing to remove.** Deleting the decor would have put us *further*
from the reference, which draws more of it than we do (§18.3).

### 20.1 `tools/dump_map_locs` is stale

The intended tool for this comparison could not do it. It refuses to run
without `<cache_dir>/xteas.json`, which no rev-237+ cache ships (maps are
unencrypted from then on) — fixed here to warn and continue. But it is
*also* written against the pre-move `osrs/rscache/dat2a/` API and the root
`CMakeLists.txt` no longer configures (it references `src/osrs/…` sources
that do not exist), so only a stale prebuilt binary survives in `build/`, and
that binary segfaults against the current cache layout. Porting it to
`3rd/rscache` is real work and was not done; the client itself is the
tested path for reading a cache and is what these numbers come from.

## 21. Two tools for looking at this scene by hand

### 21.1 `::zukquiet` — the arena with nothing happening in it

`[debugproc,zukquiet]` (`minigame_inferno/scripts/inferno.rs2`, next to
`[debugproc,zuk]`) allocates the instance and teleports to the same standing
spot, but does **not** queue `inferno_zuk_start`: no cutscene camera, no seal
collapse, no `loc_change`/`loc_add` churn, no npcs.

That matters for every measurement in this document. `::zuk` is a *sequence*
that rewrites walls while you look at it, so two screenshots of "the same"
scene are routinely different states — §16 and §18 both lost a reading that
way. `::zukquiet` is static, so a screenshot means one thing. Leave with
`::tele <x> <z>`.

```sh
make -C src EMBED_SERVER=1 torirs
src/torirs --manifest manifest_osrs230_zuk.ini      # then type ::zukquiet
# or headless:
TORIRS_NET_CHEAT=zukquiet src/torirs --manifest manifest_osrs230_zuk.ini
```

### 21.2 Painter-cap hotkeys (already present, now verified)

The §13.2 cap is drivable from the keyboard, v0's bindings:

| key | effect |
| --- | --- |
| `I` | stop / start — toggles the cap between 0 (draw nothing) and unlimited |
| `J` / `K` | +1 / −1 command |
| `L` / `,` | +100 / −100 commands |

Each change logs `paintlimit: N` to stderr. Same gate as the W/A/S/D debug
camera keys: an active world, and not typing in chat.

**Two traps, both of which cost a false conclusion here.**

`TORIRS_SIM_WORLD_KEY` runs **before the frame loop** (so does
`TORIRS_SIM_KEYS`), which is before any world exists — driving these keys with
it produces silence and looks exactly like a broken hotkey. The in-loop driver
is `TORIRS_SIM_HOTKEY=<frame>,<key>[;…]`:

```sh
TORIRS_NET_CHEAT=zukquiet TORIRS_SIM_HOTKEY="600,i;620,l;640,j" \
  src/torirs --manifest manifest_osrs230_zuk.ini --soft3d
#   -> paintlimit: 0 / paintlimit: 100 / paintlimit: 101
```

And `TORIRS_KEY_DEBUG=1` now reports the gate state when a cap key is seen
(`world_active`, `world_view_valid`, the three chat-focus flags), because
every one of those silently swallows the whole debug key set. It is keyed off
raw key state, not `key_event_count` — that counter only fills when a
component carries an onKey hook, so it is 0 for exactly these keys.

### 21.3 Multi-tile loc registration is faithful (walls/glyphs)

Checked while chasing "the walls and glyphs render behind the terrain": a loc
larger than 1x1 is registered on **every tile it covers**, not just its anchor
(`compute_normal_scenery_spans`), each with the reference's neighbour-blocking
span flags (`field1629`: WEST/EAST/SOUTH/NORTH), and the drain draws it at the
first covering tile popped with an `ElementPaint::drawn` latch — the same
shape as the official's per-tile `field1691` lists plus its drawn flag. The
Inferno's flank walls are 2x5 and the glyph
(`inferno_collapsing_wall_safespot_state1`, 30338, "Ancestral Glyph") is 6x3,
so an anchor-only registration would have drawn them far too early; it does
not happen. **The occlusion attribution for the wall/glyph pixels themselves
is not finished** — that is where to pick this up.

## 22. The footprint outline, and what it acquitted

### 22.1 TORIRS_HOVER_FOOTPRINT — red outline of a loc's painter footprint

New debug feature + a new overlay primitive (`UITREE_ENTITY_OVERLAY_LINE` →
`TORIRSRC_LINE`, box + diagonal, both renderers' 2D path):

* `TORIRS_HOVER_FOOTPRINT=1` — outline the hovered loc's footprint tiles.
* `TORIRS_HOVER_FOOTPRINT=<loc_id>` — outline every instance of that loc.
  This form exists for headless runs: `TORIRS_SIM_HOVER` parks the mouse
  before the frame loop, so an exit screenshot has no hover to read.

Each footprint tile is outlined at terrain height through `app_world_project`
(the health-bar projector), 2 px red. The point: the painter orders scenery by
its FOOTPRINT while the model draws wherever its vertices land, and nothing on
screen said which tiles the painter believed a loc covered.

### 22.2 The glyph is correctly placed — measured, not argued

`TORIRS_HOVER_FOOTPRINT=30338` + `TORIRS_EMIT_LOC=30338` at a pinned camera:

```
world=(7104,-240,7936)  tile=(54,59)  slot=(54,59)
extent -> tiles x[54..57] z[59..65]   (footprint x 54..56, z 59..64)
```

Model centred on its footprint; extent matches to within one tile on the far
(+x/+z) sides only — overhang AWAY from the camera is drawn-earlier territory
and harmless. The "glyph face on the near wall" that suggested a displaced
model is a different loc (the seal face decor on the wall itself). Hypothesis
"glyph base size / placement is wrong" is measured OUT.

### 22.3 Painter order: verified lattice-sound at four cameras

The reference's transitive invariant — a tile pops only after its entire
outward quadrant (per camera-tile axes) has retired — checked over full
TORIRS_DRAW_ORDER streams with `lattice_check.py` at four camera tiles
including inside the wall band: **zero terrain-vs-terrain and zero
loc-vs-terrain violations**. The stepping GIF's "near area filled while far
area still black" happens ACROSS quadrants, which the lattice does not order
— and does not need to for a correct final image.

### 22.4 What is real: the 148-unit decor plane on a 128-unit tile

`TORIRS_EMIT_LOC=30291` (`inferno_floor_small_plane_02`, and 30288 measures
the same): model extent **x[-74..74] z[-74..74]** on a 1x1 (128-unit)
footprint — a ~10-unit spill onto ALL FOUR neighbouring tiles. Painted at its
own tile's slot, the far-side spill lands on top of the farther neighbour's
already-drawn pixels (rock bases, wall feet), and the near-side spill is
later overdrawn by the nearer neighbour's terrain. That is the mechanism of
the thin orange fringes "behind" the rocks — measured earlier as 2,982 exact
`0xA45409` pixels overwriting drawn content at the glyph camera.

Open question, deliberately not guessed: the official ships the same model
with the same 1x1 footprint and a one-pass painter reproduces the same
mechanism, so this may be reference-identical. Settling it needs an official
capture at a matched camera (its settled/cinematic captures showed MORE
orange than ours at every matched eye, §18.3). Until then this is the best
candidate for any residual "orange over rocks" — and the two probes that
answer it in minutes are `TORIRS_HOVER_FOOTPRINT=<id>` +
`TORIRS_NO_GROUND_DECOR=1`.

## 23. "Why does the big loc draw before farther 1x1 locs?" — measured bounds

`U` now unlocks/relocks the camera in a live session (follow cam stands down;
W/A/S/D fly, R/F raise/lower, arrows rotate; relock snaps back through the
follow's teleport path). Park the eye and hold `K`/`,` to watch the order.

What is verified, extending §22.3's checker to **loc-vs-loc** (a later loc
whose footprint is fully outward of an earlier loc's footprint = violation):
zero violations at six camera tiles, including cameras placed so the glyph's
3x6 footprint straddles a quadrant boundary. The multi-tile emission rule
itself is the reference's: emit at the first covering-tile pop where every
footprint tile's ground is down, which in practice is the nearest footprint
tile's pop — and that pop already required its outward quadrant retired.

What remains genuinely different from the official, and is the honest answer
to the question: **same-ring ordering**. Within one Manhattan ring the bucket
pops LIFO with near-vacuous deferrals, where the official's FIFO + 38%
deferral rate (§9.5-9.6) interleaves differently — four of the seven §9.5
wedge inversions were exactly such ties. A big loc can therefore emit before
same-ring or diagonal-ring 1x1s that the official happens to draw first.
That is visible while stepping and harmless in the final image unless the
models overlap on screen; no on-screen overlap case has been isolated yet.

New lead, unexplained: from camera tile (60,61) — right beside it — the glyph
(30338) does not draw at all (0 emits in the full stream). Cull, not order;
worth its own look.

The "one tile row south" placement question stays open: re-examine with the
§22 outline now that the overlay projector uses the real scale and a flat
SW-corner plane — the two projector defects it had would each read as a
southward shift of the model relative to the outline.

## 24. FOUND AND FIXED: the STACK_BASE containment inverted loc order

The user's stepping session showed 1x1 locs rendering AFTER the glyph wall
they sit behind. Isolated, root-caused, fixed, and pinned by a test.

### 24.1 The cast, from the cache sources (`m35_83.jm2` / `.jl2`)

Scene coords = template + (24, 8). Camera for every number below: tile
(54,53), i.e. `TORIRS_WEDGE_CAM=6976,-1100,6848,180,0`, `::zukquiet`.

**The wall.** Loc **30338 `inferno_collapsing_wall_safespot_state1`**
("Ancestral Glyph"): template `0 30 51: 30338 10 3` — level 0, anchor
(30,51) → scene (54,59), shape 10 (centrepiece), rotation 3. Config:
`models=33037, width=6, length=3, blockwalk=1, active=1, contourground=0,
sharelight=1`. Odd rotation swaps the extents → **3x6 footprint, scene
x 54..56, z 59..64**. Registered `PNTR_SCENERY_STACK_BASE` purely because
`size_x*size_z > 1` (world_scenery.u.c).

**The hostages.** Loc **30290 `inferno_floor_small_plane_01`**
(`models=33050`, no size keys = 1x1), shape 10 — fifteen of them on the
wall's own footprint tiles: template (30..32, 52..56) → scene (54..56,
60..64). NOT ground decor — shape 10 puts them in the scenery chain, which
is what made them containable.

**The tiles.** Every footprint tile is identical in the map data:
`h30 o152;0;0 f1` — height byte 30 (y = -240), overlay 152 (the orange
floor), shape 0 rot 0, settings 0x1 (BLOCK). No LINK_BELOW, no VIS_BELOW —
no flag machinery involved anywhere in this defect.

**Bystanders.** 30291 planes (shape 22, ground decor) line the row in front
(template z 51); the 9x4 prison roof 30356 anchors at scene (51,64).

### 24.2 The defect, in draw-order numbers

Before the fix (`/tmp/glyph_order.txt`):

```
cmd=1073  LOC 30338 3x6 @(54,59)      <- the wall emits
cmd=1074  LOC 30290 @(56,64) manh=13  <- then its OWN farther tiles' planes
cmd=1075  LOC 30290 @(55,64) manh=12
   ... 13 more, descending distance ...
cmd=1092  LOC 30290 @(54,60) manh=7
```

Fifteen 1x1 locs, all strictly farther than the wall's near edge, all
emitted after it — a tall near loc painted first, then farther geometry
painted over it from behind. That is the on-screen "wall renders behind the
terrain", finally in one causal chain.

Mechanism: every multi-tile loc is a STACK_BASE, and
`scenery_blocked_by_stack_base` deferred any co-tile scenery whose footprint
the undrawn base CONTAINS. So each farther footprint tile's pop skipped its
own 30290 (contained by the undrawn wall), the wall emitted at its NEAREST
footprint tile's pop, and only then were the planes released — in the exact
inverted order the stepping GIF showed. The reference has no loc-vs-loc
containment: a loc draws when its own footprint grounds are down
(Client-TS World.draw; the official's per-tile class140 slots).

### 24.3 The fix, and what it preserves

`scenery_blocked_by_stack_base` now exempts static elements
(`el - painter->elements < painter->static_element_count`): cache locs order
purely by the footprint rule. The containment stays for DYNAMIC elements —
the rule's actual purpose — so an obj stack dropped on a table still draws
after the table under it.

After the fix, same camera: **39 of 39** 30290 planes draw before the wall
(cmd 1088); zero after. Settled pinned-eye wedge unchanged (97 px, h=8).

### 24.4 The test

`test_stack_base_does_not_defer_static_locs` (painters_test_terrain_levels.c)
reconstructs it minimally: a 3x5 STACK_BASE wall, two static 1x1s on its
farther footprint tiles, one DYNAMIC 1x1 added after
`painter_mark_static_count`. Asserts the static planes emit BEFORE the wall
and the dynamic obj AFTER it. Mutation-checked: reverting the exemption fails
exactly the two static-order assertions.

## 25. §24.3 corrected: the reference rule is a distance sort, not an exemption

The user pushed back on §24.3's static/dynamic framing, and the reference
agrees with them. Read in full this time:

* `class112.java:1014-1016` — every entity that passes the readiness scan
  gets a key from **`method3971`: the Manhattan distance from the camera tile
  to the FARTHEST corner of its footprint** (max extent per axis, summed).
* `class112.java:1030-1058` — the ready batch is drawn by a selection loop
  taking the **maximum key first**, ties broken toward the larger squared
  fine distance of the entity centre.
* The readiness scan (`label614`) skips only the unready entity — a pending
  big loc never holds back the 1x1s sharing its tiles. There is **no
  containment rule and no static/dynamic distinction anywhere.**

So the correct port, now in place:

1. `scenery_blocked_by_stack_base` is **deleted** — all three paint variants.
   §24.3's static-only exemption produced the right order in the Inferno by
   accident and the wrong rule in general.
2. The bucket variant gains the reference batch sort
   (`scenery_sort_ready_batch`, painters_i.h): farthest-corner key
   descending, centre tie-break. The world3d and distancemetric variants
   **already had exactly this sort** — the production bucket was the one
   variant missing it, which is the disparity behind every "locs draw in the
   wrong order" observation this document has chased.
3. Cost discipline per review: the sort runs at most once per tile pop, each
   element is sorted in exactly the one batch it is emitted from (drawn
   elements leave the set), 0/1-element batches return immediately, and both
   keys are computed once per element, not per comparison.

`PNTR_SCENERY_STACK_BASE` now has no consumer; the flag is still written by
the builder and kept for the day obj-stack layering needs it — remove it if
that day turns out never to come.

Tests: `test_stack_base_does_not_defer_static_locs` re-asserted to reference
semantics (all three 1x1s — static AND dynamic — before the wall), and
`test_ready_batch_sorts_by_far_corner` pins the sort itself on a hand-built
batch (wall key 7, far 1x1 key 7 tie-broken ahead of it by centre, near 1x1
key 3 last).

Verified: glyph scene — 39/39 planes before the wall, zero after; lattice
checks still zero violations; settled pinned-eye wedge byte-stable at 96 px
h=8; world-builder / occluders / net-exec / uitree suites green.
