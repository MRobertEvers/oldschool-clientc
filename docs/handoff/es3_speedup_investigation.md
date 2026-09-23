# Investigate why the OpenGL ES 3.0 renderer barely beats the ES 2.0 one on Android

You are working in `/Users/matthewevers/Documents/git_repos/3draster`, branch `v3`,
at or after commit `8b2e9d37b`. A Moto X (XT1060) is on adb.

## The question

`--gles3` should be meaningfully faster than `--gles2` on this device and it is not.
Find out whether that is a *structural ceiling* or *something still left on the table*,
and say which with evidence. A negative result ("the renderer cannot win more, here is
why, and here is where the time actually is") is a completely acceptable answer — it is
probably the correct one, but it has not been proven.

## What is already established — do not re-derive these

Device: Adreno 320, armv7+NEON, Android 5.1 (API 22), 2 cores, 1728 MHz max.

- **The renderer is ~5% of the frame.** simpleperf, frame thread, in-world Lumbridge:
  face sort `toridraw_compute_projected_face_order_small_general` 9.5%,
  `bucket_paint_world` 8.0%, `ToriRS_FrameNextCommand` 5.0%,
  `find_hovered_recursive` 3.7%, projection 3.1%. `es3_dispatch` ~2.4%,
  `es3_painter_emit_model` ~1.1%.
- **The frame is CPU-bound, not GPU- or driver-bound.** `eglSwapBuffers` is
  1.0–1.2 ms of a 14–17 ms frame.
- **Both renderers draw the same geometry.** ES2 21,990 indexed + 1,556 gathered +
  3,207 actor; ES3 23,546 indexed + 2,973 actor. Same 75 static pages /
  4,884,789 vertices. ES3 issues *fewer* draw calls (15–20 vs 15–19).
- **Current standing.** Uncapped still-camera: ES3 ahead by 0.17 and 0.89 ms in two
  pairs. Capped at 20 fps (frame work): four pairs, mean −0.83 ms, bootstrap 95% CI
  [−1.68, +0.02] — crosses zero, so "not distinguishable, ES3 leaning ahead".
  ES3 *was* 27% behind (17.92 vs 14.16 ms) until five Android optimisations that were
  defaulting off were turned on.

## Already ruled out — do not spend time here

- **MSAA / EGL config.** Both lanes select config 9, rgba 8880, `samples 0`,
  `sample_buffers 0`. Reported at startup: `EGL/ES3 context up: config ...`.
- **Thermal throttling.** At 40.4 °C under load the device holds full 1728 MHz with
  `scaling_max_freq` unreduced on both cores. A low `scaling_cur_freq` reading is the
  interactive governor idling, not a cap.
- **Dropped geometry.** `TORIRS_ES3_DRAW_AUDIT=1` reports 0 dropped draw items.
- **Static re-baking.** The painter's readout shows `REBAKED 0.0`; 977 of 981 static
  models per frame resolve to the batch page.
- **Two micro-optimisations that measured flat**: a NEON triplet index packer, and
  moving the element-buffer attachment from per-draw to per-VAO. Both are in; neither
  paid. Do not assume a third micro-optimisation will.

## The hypothesis worth testing

Every advantage ES 3.0 has is GL-side — VAOs, a std140 UBO, 32-bit indices,
`glDrawRangeElements`, no 64K page splitting. Those attack the 5%. So the ceiling on a
renderer-only change is ~5% of the frame even if the GL cost went to zero.

**The interesting question is whether ES 3.0 can attack the other 95%** — the CPU-side
sort, walk and command stream — rather than only the GL calls. Things worth evaluating
(and rejecting with reasons, if they do not fit):

- The per-model CPU path: is `es3_dispatch` / `es3_painter_emit_model` doing work that
  32-bit indices or a UBO could make unnecessary, rather than merely cheaper?
- Is the face sort (9.5%) avoidable for any class of model on the depth path
  (`--gles3-zbuffer`), which has a real depth buffer and does not need painter order?
  **Compare `--gles3-zbuffer` against `--gles3`** — this has NOT been measured and is
  the most obvious untested lever.
- `glTexStorage2D`, `glMapBufferRange` and instancing are all unused; uploads still go
  through `glBufferSubData`. Quantify before implementing — uploads are not currently
  visible in the profile.

## Tooling that already exists — use it, do not rebuild it

Scratchpad: `/private/tmp/claude-501/-Users-matthewevers-Documents-git-repos-3draster/ccecd140-4d7e-405e-9d21-390de52b1958/scratchpad`

- `andbench2.sh` — uncapped still-camera run. `andstats2.py <settle_s> <log>` reads the
  swap cadence.
- `andbench3.sh` — capped run (no `--uncapped`). `andstats3.py` reads frame WORK.
- `prof_arm.sh <tag> <capped|uncapped>` — settle, then simpleperf 25 s + logcat over the
  same window. `profdiff.py` normalises two profiles to CPU-ms **per drawn frame**.
- `ab.py` — paired difference + bootstrap CI.

Env knobs: `TORIRS_SWAP_DEBUG=1` (prints `swap: mean ...` and `draw: ... work mean N ms`),
`TORIRS_ES3_DEBUG=1` / `TORIRS_GLES2_DEBUG=1` (per-frame geometry census),
`TORIRS_ES3_DRAW_AUDIT=1`, `TORIRS_ES3_READBACK`.

Servers must be up on 192.168.1.148: `torirsserver` :43596, `js5_server` :43594,
`io_server` :8390. Check before blaming the client.

Profiling: API 22 is too old for `app_profiler.py`. Stage simpleperf inside the app:
`adb shell "run-as com.torirs.client sh -c 'cat /data/local/tmp/simpleperf > simpleperf; chmod 700 simpleperf'"`
then `./simpleperf record -e cpu-clock -f 1000 -p <pid> --duration 25 -o <tag>.data`.
`cpu-cycles` is denied; `cpu-clock` works. **Split by thread** (`--comms`) — audio runs
on `AudioTrack` and will otherwise pollute any per-frame number. Build `PROFILE=1` for
symbols, into its own objdir.

## Measurement traps that have already cost this project a day

1. **Any `adb shell` command during a measurement window invalidates it.** Each one
   launches an `app_process` JVM on a 2-core phone. The original harness started its
   logcat *after* a swipe loop and every arm it ever reported was ~5× slow (13–15 ms at
   rest vs 65–79 under that drive, same build). Do every adb command *before* the window
   opens, then do not touch the device.
2. **`adb logcat` without `-d` dumps the existing buffer before streaming**, so a log
   that looks like it covers the window opens with the settle backlog.
3. **Never run two bench batches at once.** The first script's trailing `am force-stop`
   will kill the second's freshly launched client.
4. **The object directories are shared between concurrent sessions.** Build with
   `PLATFORM_OBJ_BASE=<private>`; `objverify` warns about stale objects and it means it.
5. **Never mutate the shared tree to test a hypothesis** — use `git worktree`, per
   `CLAUDE.md`.
6. Uncapped rendering heats the phone 33 → 40 °C in ~8 minutes. It does not throttle,
   but let it settle between runs and alternate arms so any drift lands on both.

## Where the code is

Four separate renderer families, one per (platform, API); each lane links only its own:

    platform_androidarmv7_renderer_opengles2_{core,painter,ui,zbuffer}.c
    platform_androidarmv7_renderer_opengles3_{core,painter,ui,zbuffer}.c
    platform_web_renderer_webgl1_*.c
    platform_web_renderer_webgl2_*.c

`src/platform/platform.mk` selects them, `platform_check.mk` enforces which lane gets
which. Shared: `platform_renderer_es{2,3}_core.h`, `_indices.h`, `_shaders.h`, and
`platform_renderer_soft3d.c`. There are deliberately **no feature levers** left in the
renderers — if you add one to A/B something, take it out again before you finish.

## What a good answer looks like

Numbers from the device, paired and alternating, with the spread stated — the pooled
window noise is sd 1.56 ms on a ~24 ms median, so anything under ~1 ms needs several
pairs before it means anything. Say plainly when a result is not distinguishable.
If the conclusion is "the renderer is 5% of the frame and this is as good as it gets,
the work is in the sort and the scene walk", that is a genuinely useful answer — back
it with the profile and stop there rather than shipping micro-optimisations that
measure flat.
