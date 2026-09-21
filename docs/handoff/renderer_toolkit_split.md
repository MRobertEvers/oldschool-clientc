# Make every renderer core a branch-free toolkit, and the top-level renderers compose from it

## The rule

> I want the core to have no conditional branch on the features - I've already
> asked you this many times. The top level renderer will simply call into the
> functions it needs. The top level renderers should not be light wrappers -
> the 'core' is a toolkit, and the top level renderers compose from that
> toolkit.

That is the whole task. Everything below is the inventory and the traps.

Two corollaries, both previously violated and both non-negotiable:

- **No virtual dispatch.** No function pointers, no vtables, no `_lane.h`
  hook layer, no `#define`-swapped call. Selection is by which `.c` file the
  lane links, exactly as `platform.mk` already does it.
- **Uncoupled beats DRY.** Where two renderers would need a branch to share a
  body, they get two bodies. Duplication between families is the intended
  cost. Every performance regression in this area so far came from the
  coupling, not from the duplication.

## Why this matters (the evidence)

The ES3 family was 27% slower than ES2 because a shared core gated five
Android optimisations behind a lever that defaulted off for ES3 while ES2 got
them from `__arm__ && __ARM_NEON`. The flicker bug had the same root: a
`renderer->zbuffer` branch in the shared core meant the painter path never
uploaded the static arena it indexed, and the draw loop silently `continue`d.
Both were invisible because the branch made one file serve two renderers.

## Current state

Mode-branch sites are `renderer->zbuffer` (ES/WebGL, d3d9) and
`renderer->z_buffer_enabled` (gl3).

| file | lines | branches |
|---|---:|---:|
| `platform_androidarmv7_renderer_opengles2_core.c` | 4517 | 17 |
| `platform_androidarmv7_renderer_opengles2_zbuffer.c` | 1170 | 5 |
| `platform_androidarmv7_renderer_opengles2_dualcore.c` | 1312 | 1 |
| `platform_androidarmv7_renderer_opengles2_dualcore_stage.c` | 595 | 2 |
| `platform_androidarmv7_renderer_opengles2_painter.c` | 592 | 0 |
| `platform_androidarmv7_renderer_opengles2_ui.c` | 3368 | 0 |
| `platform_androidarmv7_renderer_opengles3_core.c` | 5268 | 17 |
| `platform_androidarmv7_renderer_opengles3_zbuffer.c` | 1194 | 5 |
| `platform_androidarmv7_renderer_opengles3_painter.c` | 238 | 0 |
| `platform_androidarmv7_renderer_opengles3_ui.c` | 3389 | 0 |
| `platform_web_renderer_webgl1_core.c` | 4506 | 17 |
| `platform_web_renderer_webgl1_zbuffer.c` | 1170 | 5 |
| `platform_web_renderer_webgl1_painter.c` | 592 | 0 |
| `platform_web_renderer_webgl1_ui.c` | 3368 | 0 |
| `platform_web_renderer_webgl2_core.c` | 5254 | 17 |
| `platform_web_renderer_webgl2_zbuffer.c` | 1194 | 5 |
| `platform_web_renderer_webgl2_painter.c` | 238 | 0 |
| `platform_web_renderer_webgl2_ui.c` | 3389 | 0 |
| `platform_win32_renderer_d3d9_core.c` | 7235 | 9 |
| `platform_win32_renderer_d3d9_zbuffer.c` | 1152 | 3 |
| `platform_win32_renderer_d3d9_painter.c` | 109 | 1 |
| `platform_sdl2_renderer_gl3.c` | 5795 | 5 |
| `platform_sdl2_renderer_gl3zb.c` | 690 | 2 |

Per-function attribution — the ES3 families (`opengles3`, `webgl2`) are
identical to each other, as are the ES2 families (`opengles2`, `webgl1`):

```
ES3 core (17)                         ES2 core (17)
  6  es3_draw_model                     6  es2_draw_model
  3  es3_end_3d                         3  es2_end_3d
  2  es3_bake_pose_vertices             2  es2_bake_pose_vertices
  2  es3_sequence_issue                 2  es2_frame_stream_upload
  2  es3_begin_3d                       2  es2_begin_3d
  1  es3_frame_stream_reserve           1  es2_frame_stream_reserve
  1  es3_scale_target_ensure            1  es2_scale_target_ensure

ES3/ES2 zbuffer.c (5, same shape)     opengles2 dualcore (3)
  3  es{2,3}_zbuffer_destroy             1  dualcore_arm
  1  es{2,3}_zbuffer_state               1  GLES2DualCoreStage_BeginPass
  1  es{2,3}_zbuffer_create              1  GLES2DualCoreStage_ComputeModel
```

The `_zbuffer.c` counts are self-checks (`if( !renderer->zbuffer ) return;`)
that exist only because the file is linked into a binary that may be running
the painter. Once the depth renderer is the only thing that links it, they
delete outright — they are not a branch to be factored, they are dead.

`_ui.c` is already branch-free in all four families. It stays in the toolkit
unchanged.

## Target layout, per family

Using `opengles3` as the worked example; `opengles2`, `webgl1`, `webgl2` are
the same shape.

```
toolkit (no feature branch, no mode field read):
  platform_androidarmv7_renderer_opengles3_core.c    scene walk, buffers,
                                                     bake, stream, upload,
                                                     shaders, state helpers
  platform_androidarmv7_renderer_opengles3_ui.c      unchanged

composing top-level renderers (each owns a whole frame):
  platform_androidarmv7_renderer_opengles3_painter.c
  platform_androidarmv7_renderer_opengles3_zb.c
```

Each composing renderer owns, in full and by itself:

- its constructor and `Init`, including the GL context request (depth bits or
  none) and any state the other mode never needs;
- `begin_3d`, `draw_model`, `sequence_issue` / `end_3d` — written straight
  through, calling toolkit functions in the order that mode needs;
- its own present / flush ordering.

The painter file currently being 238 lines is the thing to fix: it is a light
wrapper today and must become the actual renderer. Expect both files to end up
in the high hundreds to low thousands of lines, and the core to shrink.

Naming precedent to follow: `platform_win32_renderer_d3d9_{core,painter,
zbuffer}.c` and `platform_sdl2_renderer_gl3{,zb}.c`. Pick `_zb.c` or
`_zbuffer.c` consistently across the four families and say which in the commit
message; the existing files are `_zbuffer.c`.

## Specific things that will bite

1. **`es{2,3}_bake_pose_vertices` derives a painter-only concept.** It
   computes `ordered_painter = face_order && !renderer->zbuffer && vbo ==
   renderer->frame_stream_cpu`. The toolkit version must take
   `ordered_painter` as a parameter; the painter renderer passes the computed
   value, the depth renderer passes `false` and never pays for the test.

2. **The public API carries the mode as a flag.** `ToriRS_GLES3_Init(renderer,
   window, scene, bool z_buffer)` at
   `platform_androidarmv7_renderer_opengles3.h:56`, and the same shape for
   GLES2 at `..._opengles2.h:50`. Call sites are `src/main.c:1551` (GLES2) and
   `src/main.c:1598` (GLES3) — plus `src/main.c:1529` for GL3 if phase 2 is
   done. Two renderers means two entry points; the `bool` goes away and
   `main.c` selects the function. The handle type and the `_placement.h`
   forks change with it.

3. **`opengles2` has the dualcore stage.** `dualcore.c` and
   `dualcore_stage.c` have 3 branches between them and are Android-ES2-only.
   They belong to whichever composing renderer actually uses them; if both do,
   the mode-dependent part moves out into the callers.

4. **The `web` lane links both WebGL1 and WebGL2 families** (see
   `platform.mk:402-411`); the `android` lane links both GLES2 and GLES3
   (`platform.mk:697-708`). Splitting painter/zb doubles the entries in both
   lists, and `platform_check.mk` has `LANE_REQUIRE_web` /
   `LANE_REQUIRE_android` lists naming these files by path
   (`platform_check.mk:97-115`, `:145-151`). Update both. The lane check is
   included by `src/Makefile:97`, so it runs on every build of that lane —
   there is no separate `lane-check` target.

5. **Do not reintroduce feature levers.** The `es3_lever_opt_in` family was
   removed for exactly this reason. If a capability genuinely differs at
   runtime (not at build time), it belongs in the composing renderer's own
   code, not in a shared core's `if`.

6. **Do not re-merge the four families.** `opengles2` and `webgl1` are
   byte-similar today and it is tempting to re-unify them. Don't. Android is
   allowed NEON (`vst3q_u16`, `vst3q_u32`, the packed-word bake encoder) and
   the web families are not; that divergence is the point of the fork.

## Scope

- **Phase 1 (required):** the four ES/WebGL families — 44 core branches, 20
  zbuffer self-checks, 3 dualcore branches.
- **Phase 2 (same rule, do it after phase 1 lands and measures clean):**
  `platform_win32_renderer_d3d9_*` (9 + 3 + 1) and `platform_sdl2_renderer_
  gl3*` (5 + 2). Note that these two are the naming precedent but are *not*
  the architectural precedent — their cores still branch, which is the thing
  being fixed.

## Verification

Three lane builds, each into a private objdir so concurrent sessions do not
collide:

```sh
make -C src PLATFORM_OBJ_BASE=build_<yours> PLATFORM_TARGET=android
make -C src PLATFORM_OBJ_BASE=build_<yours> PLATFORM_TARGET=web
make -C src PLATFORM_OBJ_BASE=build_<yours>            # host lane
```

Then:

```sh
python3 tools/webgl_lane_audit.py
make -C src PLATFORM_OBJ_BASE=build_<yours> test-es3-index-pack
make -C src PLATFORM_OBJ_BASE=build_<yours> test-gles2-dualcore-stage
make -C src PLATFORM_OBJ_BASE=build_<yours> test-soft3d-interface-filter
make -C src check-pt-switch        # must print total 0
```

The structural check that the work is actually done:

```sh
grep -cE 'renderer->zbuffer|renderer->z_buffer_enabled' \
  src/platform/platform_*_renderer_*_core.c src/platform/platform_*_renderer_*_ui.c
# every one must be 0
```

And the Android objdir must contain only `androidarmv7_*` renderer objects —
no `web_renderer_*`, no `sdl2_*`.

On device, with `TORIRS_ES3_DRAW_AUDIT=1`, both the painter and the depth
renderer must report **0 dropped draw items**. That audit is what found the
real flicker cause after four wrong hypotheses; run it before claiming the
split is behaviour-preserving.

## Measurement, if you re-measure

Current baseline, capped at 20 fps (`RS_CS2_DEVICEOPTION_FPS_CAP`, seeded in
`src/app/app_boot.c`), four paired runs: ES3 faster by 0.83 ms mean, bootstrap
95% CI [-1.68, +0.02] — crosses zero, so ES2 and ES3 are **not currently
distinguishable**. Uncapped: ES2 14.16 ms, ES3 14.40/14.74 ms. Pooled window
sd is 1.56 ms on a 24.31 ms median, so a single run proves nothing.

A painter-vs-depth comparison on ES3, uncapped, two alternating pairs:

| pair | painter | depth | delta |
|---|---:|---:|---:|
| r1 | 17.71 ms (n=12, max 25.03) | 15.13 ms (n=15, max 16.01) | depth -2.58 |
| r2 | 14.20 ms (n=15, max 15.52) | 14.59 ms (n=15, max 15.16) | painter -0.39 |

The two pairs disagree in sign, and r1's painter arm is the first run of the
batch with a mean (18.67) well above its median and a 25.03 ms tail — a cold
window, not a slow renderer. r2 is the clean pair, and there the two are
0.39 ms apart against a pooled window sd of ~1.5 ms. **Painter and depth are
not currently distinguishable on ES3.** Measure both again after the split;
do not assume either mode is the one to optimise for.

Traps that have already cost runs:

- No `adb` command may run inside a measurement window; attach `logcat`
  **before** the swipe driver, not after.
- Never overlap two benchmark batches — a trailing `am force-stop` from one
  kills the client the next just launched.
- `--uncapped` in the bench args defeats the cap; `andbench3.sh` omits it.
- Per CLAUDE.md: never mutate this working tree to prove something. Use
  `git worktree add --detach` and a private `PLATFORM_OBJ_BASE`.

Scripts live in the session scratchpad: `andbench2.sh` (uncapped),
`andbench3.sh` (capped), `andstats2.py` (swap cadence), `andstats3.py` (frame
work from `App_LastFrameUs`), `ab.py` (paired + bootstrap CI), `prof_arm.sh`
and `profdiff.py` (simpleperf; `cpu-clock -f 1000`, `--comms` to split the
frame thread from `AudioTrack`).

## Frame budget, for judging whether a change can possibly matter

ES3 frame thread, uncapped, 16.22 ms/frame:

| bucket | ms | share |
|---|---:|---:|
| UI tree | 2.924 | 18.0% |
| face sort | 2.354 | 14.5% |
| scene walk | 2.345 | 14.5% |
| unclassified | 2.244 | 13.8% |
| ES3 renderer | 1.995 | 12.3% |
| projection | 1.686 | 10.4% |
| command stream | 1.257 | 7.7% |
| kernel/libc | 0.864 | 5.3% |
| game tick | 0.456 | 2.8% |

The renderer is 12% of the frame. A restructure that costs nothing and buys
clarity is the goal here; do not expect it to move the number, and do not
claim it did without a paired run.
