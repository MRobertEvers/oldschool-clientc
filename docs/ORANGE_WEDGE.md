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
