#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_WEBGL1ZB_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_WEBGL1ZB_H

/*
 * The depth-buffered world pass for the WebGL1 backend.
 *
 * Selected by --webgl1-zbuffer, and the peer of D3D9's
 * --d3d9-zbuffer (WINDOWS-D3D9-ZBUFFER-001). It is a different way to decide
 * what covers what, not a different renderer: the atlas, the vertex arena, the
 * pose table, the 2D batcher, picking and every resource rule are shared with
 * platform_sdl2_renderer_webgl1.c through the internal header, and only the world
 * pass differs.
 *
 * ## What the mode changes
 *
 * The painter path submits every face in the order ToriDraw's priority/depth
 * sort produced and lets later pixels win. This path lets the depth buffer
 * decide, which frees it from that sort — but only for geometry whose result
 * does not depend on draw order:
 *
 *   opaque, cutout    Depth-order independent. Natural face order, front-facing
 *                     faces only, depth test and write on.
 *   blended           Still order-dependent, because translucency composites.
 *                     Kept in the model's own priority order, queued, then
 *                     drawn back to front after the opaque pass with the depth
 *                     test on and depth writes OFF.
 *
 * A model with no translucent faces never runs the sort at all. That is the
 * point of the mode, and it is why the classification exists.
 *
 * ## What it needs from the caller
 *
 * The context must have a depth buffer, which is decided at
 * ToriRS_GL3_Init. The app must also be in TORIRS_WORLD_DEPTH so the visible
 * set is collected without the tile wavefront and the opaque face-distance
 * sort; without that the mode is correct but pays for work it discards.
 */

#include <stdint.h>

struct ToriDraw_Camera;
struct ToriDraw_Scene;
struct ToriRS_GL3;
struct ToriRS_RenderCommand_Model;

/** Release the pass's buffers. Safe on a renderer that never used the mode. */
void
WEBGL1ZB_Free(struct ToriRS_GL3* renderer);

/** Clear depth for one world pass, scissored to the world viewport so the UI
 *  around it is untouched. */
void
WEBGL1ZB_BeginPass(
    struct ToriRS_GL3* renderer,
    int gl_x,
    int gl_y,
    int gl_w,
    int gl_h);

/** Replace the projection's painter depth row with a real near/far mapping.
 *  Call after the shared matrices are built. */
void
WEBGL1ZB_ApplyProjectionDepth(
    struct ToriRS_GL3* renderer,
    const struct ToriDraw_Camera* camera);

/** Depth test/write state for the opaque pass. */
void
WEBGL1ZB_BindDrawState(struct ToriRS_GL3* renderer);

/** Classify one world model's faces and submit them: opaque now, translucent
 *  queued for WEBGL1ZB_DrawAlphaPass. */
void
WEBGL1ZB_SubmitModel(
    struct ToriRS_GL3* renderer,
    struct ToriRS_RenderCommand_Model const* mcmd,
    struct ToriDraw_Scene* ctx,
    uint32_t group,
    uint32_t vertex_base,
    int face_count);

/** Draw the queued translucent models, back to front. No-op when none. */
void
WEBGL1ZB_DrawAlphaPass(struct ToriRS_GL3* renderer);

/*
 * Drop a cached face classification.
 *
 * The classification is keyed by (element, track, pose) and is only valid for
 * the baked pose it was computed from. Unloading an element, or replacing an
 * animation track, must drop it — otherwise a pose id that now means a
 * different pose would be drawn with the previous one's opaque/blended split,
 * which shows up as geometry in the wrong pass rather than as a missing model.
 */
void
WEBGL1ZB_ForgetElement(
    struct ToriRS_GL3* renderer,
    int element_id);

void
WEBGL1ZB_ForgetTrack(
    struct ToriRS_GL3* renderer,
    int element_id,
    int anim_index);

/** Drop the frame's queue. Must run even on a frame that bailed early, or its
 *  models would be drawn against the next frame's depth buffer and camera. */
void
WEBGL1ZB_ResetFrame(struct ToriRS_GL3* renderer);

#endif
