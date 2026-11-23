#ifndef SRC_PLATFORM_PLATFORM_SDL2_RENDERER_GL3ZB_H
#define SRC_PLATFORM_PLATFORM_SDL2_RENDERER_GL3ZB_H

/*
 * The depth-buffered world pass for the desktop GL 3.2 backend.
 *
 * Selected by --opengl3-zbuffer, and the peer of D3D9's
 * --d3d9-zbuffer (WINDOWS-D3D9-ZBUFFER-001). It is a different way to decide
 * what covers what, not a different renderer: the atlas, the vertex arena, the
 * pose table, the 2D batcher, picking and every resource rule are shared with
 * platform_sdl2_renderer_gl3.c through the internal header, and only the world
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
 * ToriPlatformSDL2_Renderer_GL3_Init. The app must also be in TORIRS_WORLD_DEPTH so the visible
 * set is collected without the tile wavefront and the opaque face-distance
 * sort; without that the mode is correct but pays for work it discards.
 */

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Camera;
struct ToriDraw_Scene;
struct ToriPlatformSDL2_Renderer_GL3;
struct ToriRS_RenderCommand;
struct ToriRS_RenderCommand_Model;

/** Release the pass's buffers. Safe on a renderer that never used the mode. */
void
GL3ZB_Free(struct ToriPlatformSDL2_Renderer_GL3* renderer);

/** Clear depth for one world pass, scissored to the world viewport so the UI
 *  around it is untouched. */
void
GL3ZB_BeginPass(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int gl_x,
    int gl_y,
    int gl_w,
    int gl_h);

/** Replace the projection's painter depth row with a real near/far mapping.
 *  Call after the shared matrices are built. */
void
GL3ZB_ApplyProjectionDepth(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    const struct ToriDraw_Camera* camera);

/** Depth test/write state for the opaque pass. */
void
GL3ZB_BindDrawState(struct ToriPlatformSDL2_Renderer_GL3* renderer);

/** Classify one world model's faces and submit them: opaque now, translucent
 *  queued for GL3ZB_DrawAlphaPass. `projected`: whether ctx's bench holds this
 *  model's projection (the draw projects only for a pick under the cursor);
 *  a model with translucent faces is projected here when it does not. */
void
GL3ZB_SubmitModel(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand_Model const* mcmd,
    struct ToriDraw_Scene* ctx,
    uint32_t group,
    uint32_t vertex_base,
    int face_count,
    bool projected);

/** Sort the queued translucent models back to front and write their indices,
 *  in that order, to `staging` — the tail of the frame's index staging, so
 *  they go up in the frame's one upload. `staging` must hold
 *  renderer->alpha_index_count indices. No-op when none are queued. */
void
GL3ZB_StageAlphaPass(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t* staging);

/** Draw the staged translucent models. `index_base` is where the staged tail
 *  starts in the uploaded element buffer. No-op when none are queued. */
void
GL3ZB_DrawAlphaPass(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t index_base);

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
GL3ZB_ForgetElement(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int element_id);

void
GL3ZB_ForgetTrack(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int element_id,
    int anim_index);

/** Drop the frame's queue. Must run even on a frame that bailed early, or its
 *  models would be drawn against the next frame's depth buffer and camera. */
void
GL3ZB_ResetFrame(struct ToriPlatformSDL2_Renderer_GL3* renderer);

/*
 * The rev-241 client's world pass, which Sailing OSRS
 * (platform_sdl2_renderer_gl3_os235sailing.c) and Post-TFA OSRS
 * (platform_sdl2_renderer_gl3_os241tfa.c) both run over their own vertices:
 * its depth key with each face's bias (their programs' vertex stage), a
 * buffer cleared to 0 and tested GL_GREATER, two-pass players and opcode-147
 * npcs, and the translucent pass in arrival order, writing depth. A parallel
 * camera has no depth in w; it runs the plain pass's mapping and test.
 * docs/toridraw_rszbuffer.md.
 */

/** BEGIN_3D: the pass's camera, its depth clear and its depth key. */
void
GL3ZB_Rev241Begin3D(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand* command);

/** END_3D: the opaque chain, then the runs in the client's order. */
void
GL3ZB_Rev241End3D(struct ToriPlatformSDL2_Renderer_GL3* renderer);

/** One world model's faces the client's way: a two-pass model's in priority
 *  order as an opaque and a translucent run; any other model's opaque faces
 *  into the order-free chain and its blended faces in depth-bucket order.
 *  `projected` as GL3ZB_SubmitModel's. */
void
GL3ZB_SubmitModelRev241(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    struct ToriRS_RenderCommand_Model const* mcmd,
    struct ToriDraw_Scene* ctx,
    uint32_t group,
    uint32_t vertex_base,
    int face_count,
    bool projected);

/** GL3ZB_BeginPass, clearing to 0: the client's key is larger when nearer. */
void
GL3ZB_BeginPassKeyed(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    int gl_x,
    int gl_y,
    int gl_w,
    int gl_h);

/** GL3ZB_BindDrawState, testing GL_GREATER. */
void
GL3ZB_BindDrawStateKeyed(struct ToriPlatformSDL2_Renderer_GL3* renderer);

/** The queued runs to `staging` in the client's order: two-pass opaque runs,
 *  then every other run as it arrived. */
void
GL3ZB_StageRunsInArrivalOrder(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t* staging);

/** Draw the staged runs: a two-pass run colour only then depth only, a
 *  translucent run blending and writing depth. */
void
GL3ZB_DrawRuns(
    struct ToriPlatformSDL2_Renderer_GL3* renderer,
    uint32_t index_base);

#endif
