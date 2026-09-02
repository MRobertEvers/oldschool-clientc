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
 *   sync the scratch view; copy the frame
 *   install the stage source
 *   gles2_render_frame_commands:
 *     scene events (loads, unloads)
 *     BEGIN_3D -> source->begin_3d ------->  wake: replay the world pass
 *     DRAW_MODEL -> source->take(i)          (world-only frame, on the view)
 *       waits until ready > i <-----------   pose/cull/project/pick/sort
 *       gathers, bakes, emits                publish result i
 *     ...                                    ...
 *     END_3D, interface, ...                 END_3D: finished
 *   join the worker <---------------------
 *   remove the stage source
 *   ToriRS_FrameEnd(frame)
 *   gles2_render_frame_end
 *
 * Why the worker starts at BEGIN_3D and not at frame begin: the frame's
 * scene events -- model loads that bake, animation loads that pose every
 * frame of a sequence into the model -- run on the draw thread before its
 * first world command, and they write the models the worker would be
 * reading. BEGIN_3D is the first instant after the last of them.
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
