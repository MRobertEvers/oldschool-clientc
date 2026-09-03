#ifndef SRC_PLATFORM_PLATFORM_RENDERER_GLES2_DUALCORE_H
#define SRC_PLATFORM_PLATFORM_RENDERER_GLES2_DUALCORE_H

/*
 * The dual-core GLES2 lane: the GLES2 renderer plus a second core.
 *
 * `--gles2-dualcore` / `--gles2-dualcore-zbuffer`. The Android device this
 * client targets (a 2013 Snapdragon: two Krait cores, an Adreno that sits
 * ~2% busy) spends its whole frame on one thread, and about a third of that
 * frame -- pose, cull, projection, pick test, face sort, per model -- is a
 * pure function of the command list and the scene that never touches GL.
 * This lane runs that third on the other core, one command ahead of the
 * draw, and leaves the draw everything else: the frame bus, the gather into
 * the vertex stream, the actor bakes, the interface, and GL.
 *
 * It is a SEPARATE lane, not a mode of the GLES2 renderer. It owns no GL
 * state: it wraps a ToriRS_GLES2 that main.c created and drives exactly as
 * it drives that renderer, and adds the worker thread, the scene's scratch
 * view (ToriDraw_SceneScratchViewNew), the results arena and the hand-over.
 * The renderer's only knowledge of it is GLES2ModelStageSource, a hook it
 * consults when installed and computes past when not; with the hook out,
 * the GLES2 renderer is byte for byte the --gles2 lane.
 *
 * Frame shape (ToriRS_GLES2DualCore_RenderFrame):
 *
 *   draw thread                              worker thread
 *   ------------------------------------     ---------------------------------
 *   gles2_render_frame_begin
 *   ToriRS_FrameBegin(frame)
 *   sync the scratch view
 *   install the stage source
 *   the lane's dispatch loop:
 *     scene events (loads, unloads)
 *     BEGIN_3D -> source->begin_3d           wake
 *       feed <- BEGIN_3D ------------------> begin the pass on the view
 *     pull the next LOOKAHEAD commands
 *       feed <- DRAW_MODEL j ... ----------> stage j: pose/cull/project/
 *     DRAW_MODEL -> source->take(i)          pick/sort; publish result i
 *       waits until ready > i <-----------
 *       gathers, bakes, emits
 *     ...                                    ...
 *     END_3D -> feed <- END_3D               end the pass
 *     interface, ...
 *   close the feed -----------------------> finished
 *   join the worker <---------------------
 *   remove the stage source
 *   ToriRS_FrameEnd(frame)
 *   gles2_render_frame_end
 *
 * The draw runs the frame bus ONCE, for both threads: inside a world pass
 * it translates commands a few ahead of dispatching them (a ring of
 * TORIRS_GLES2_DUALCORE_LOOKAHEAD, default 16) and publishes each into the
 * arena's feed as it is translated, so the worker stages from translated
 * commands and never walks the painter list itself. Until 2026-09-03 the
 * worker replayed the bus world-only on its own copy of the frame: 1.26 ms
 * of its 6.0 ms frame and, on terrain runs (a tile's stage is cheaper than
 * its translation), the reason it could not stay ahead of the draw -- which
 * waited 1.45 ms a frame on it.
 *
 * Why the worker starts at BEGIN_3D and not at frame begin: the frame's
 * scene events -- model loads that bake, animation loads that pose every
 * frame of a sequence into the model -- run on the draw thread before its
 * first world command, and they write the models the worker would be
 * reading. BEGIN_3D is the first instant after the last of them. It is
 * also why the lookahead runs only INSIDE a pass: every command pulled
 * ahead there is a world command, with no scene event queued behind it that
 * its staging could depend on.
 *
 * Why the worker finishes before ToriRS_FrameEnd: that is where the scene
 * frees the poses it was asked to drop this frame, and the worker may still
 * be reading one of them if the draw stopped consuming early.
 *
 * What the worker writes that the draw reads: the model pose (an element's
 * animation applied) and the element's pose record, both published before
 * the result that names them; the draw's bakes read the posed model after
 * taking the result. What the draw writes that the worker reads: nothing,
 * from BEGIN_3D on. A model that appears in two commands of one frame is
 * the one shape this does not cover (the draw could be baking it under the
 * worker's second pose); the walk emits each element once.
 *
 * Environment (read once at creation; cached like every other knob):
 *   TORIRS_GLES2_DUALCORE=0        run single-threaded through this lane --
 *                                  the A/B control arm on one binary
 *   TORIRS_GLES2_DUALCORE_WARMUP=N frames drawn single-threaded first so the
 *                                  lazily-resolved statics of the emitter
 *                                  and the kernels are settled by one
 *                                  thread (default 1)
 *   TORIRS_GLES2_DUALCORE_PIN=1    pin the worker to the second CPU
 *   TORIRS_GLES2_DUALCORE_LOOKAHEAD=N
 *                                  commands the draw translates ahead of
 *                                  dispatching them inside a world pass
 *                                  (default 16, max 31)
 *   TORIRS_GLES2_DUALCORE_DEBUG=1  a stats line every 300 frames on stderr
 */

#include <stdbool.h>

struct ToriDraw_Scene;
struct ToriRS_Frame;
struct ToriRS_GLES2;
struct ToriRS_GLES2DualCore;

/**
 * Wrap an initialised GLES2 renderer. The renderer stays the caller's: it
 * is still what main.c sizes, picks through, reads back from and frees, and
 * it must outlive this. Starts the worker thread.
 */
struct ToriRS_GLES2DualCore*
ToriRS_GLES2DualCore_New(struct ToriRS_GLES2* renderer);

/** Stops and joins the worker; does not free the wrapped renderer. */
void
ToriRS_GLES2DualCore_Free(struct ToriRS_GLES2DualCore* lane);

/** ToriRS_GLES2_RenderFrame, with the world's model stage on the other core. */
void
ToriRS_GLES2DualCore_RenderFrame(struct ToriRS_GLES2DualCore* lane, struct ToriRS_Frame* frame);

#endif
