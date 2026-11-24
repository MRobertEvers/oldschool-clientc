#ifndef SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_REV241_H
#define SRC_PLATFORM_PLATFORM_WIN32_RENDERER_D3D9_REV241_H

/*
 * The rev-241 client's world pass on D3D9, as runs: what the four OSRS
 * renderers queue, and in what order, whichever lane draws them.
 *
 *   platform_win32_renderer_d3d9ff_os235sailing.c / _os241tfa.c   XP, fixed function
 *   platform_win64_renderer_d3d9sm3_os235sailing.c / _os241tfa.c  win64, shaders
 *
 * A model's faces go out as the client draws them [class174]:
 *
 *   two-pass model     (players, opcode-147 npcs) every face in render-priority
 *                      order: an opaque run, then a translucent one, each
 *                      drawn colour only, then depth only.
 *   any other model    its plain opaque faces into the depth state's per-page
 *                      buckets (order-free); its blended faces one translucent
 *                      run, far to near by depth bucket, no priorities.
 *   a biased face      (face_z_offsets) a run per bias value, so a lane with
 *                      no vertex stage can bias it per draw (D3DRS_DEPTHBIAS).
 *   a projected face   Post-TFA only: a textured face whose P/M/N projector
 *                      plane is not its own plane (the Ocellus void) -- a run
 *                      per projector, drawn with the projector's texture
 *                      matrix: the texel where the eye ray meets the plane
 *                      [class171.method5476]. Every other textured face's
 *                      baked texcoords already are that texel.
 *
 * Opaque-phase runs draw after the bucketed opaque chain, the translucent
 * phase after them, each phase in arrival (the painter's) order.
 */

#include "platform/platform_win32_renderer_d3d9_core.h"

#include <stdbool.h>
#include <stdint.h>

enum D3D9Rev241RunKind
{
    D3D9_REV241_RUN_TWO_PASS = 0,
    D3D9_REV241_RUN_TRANSLUCENT,
    D3D9_REV241_RUN_BIASED,
    D3D9_REV241_RUN_PROJECTED,
};

struct D3D9Rev241Run
{
    uint8_t kind;
    /* Drawn in the translucent phase, blending (and still writing depth). */
    bool translucent;
    uint32_t binding;
    uint32_t page_base;
    uint32_t index_offset;
    uint32_t index_count;
    /* BIASED: twice the faces' bias, world units, and the model's camera
     * depth, from which a lane with no vertex stage makes a depth offset. */
    float depth_bias;
    float view_depth;
    /* PROJECTED: the texture, and the texcoords as affine functions of world
     * position -- (s, t, q) = (x, y, z, 1) . rows, texel (s/q, t/q), u
     * clamped and v wrapped -- with the texture's scroll folded in. */
    int tex_id;
    float projector[4][3];
};

#define D3D9_REV241_LANE_OBJECTS 8u

struct D3D9Rev241World
{
    uint16_t* indices;
    uint32_t index_count;
    uint32_t index_capacity;
    struct D3D9Rev241Run* runs;
    uint32_t run_count;
    uint32_t run_capacity;
    /* One run's indices at a time, for d3d9_retained_prepare. */
    struct TRSPK_IBOChain* run_chain;
    /* A model's blended faces and their depth buckets, for the sort. */
    void* face_keys;
    uint32_t face_key_capacity;
    /* A model's faces a run owns, while it is emitted. */
    uint8_t* marks;
    uint32_t mark_capacity;
    /* Post-TFA: each projected texture as a texture of its own. */
    IDirect3DTexture9** projector_textures;
    /* The lane's device objects (win64: shaders, declaration), released with
     * the world. */
    IUnknown* lane_objects[D3D9_REV241_LANE_OBJECTS];
};

/** Make ::rev241 (and the depth state, whose material tables it reads). */
bool
d3d9_rev241_create(struct ToriPlatformWin32_Renderer_D3D9* renderer);

/** Drop the pass's runs: every BEGIN_3D. */
void
d3d9_rev241_reset(struct ToriPlatformWin32_Renderer_D3D9* renderer);

/** DRAW_MODEL up to the emitter: begin, the depth cull and pick, the place.
 *  False: nothing to emit. */
bool
d3d9_rev241_place(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    struct D3D9ModelPlacement* placement);

/** A placed model's faces into the runs and the opaque buckets (Sailing). */
void
d3d9_rev241_emit_model(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement);

/** The same, with its projected faces as PROJECTED runs (Post-TFA). */
void
d3d9_rev241_emit_model_projected(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement);

/** One run's indices on its own chain and prepared to draw. False: nothing
 *  to draw. Then: d3d9_set_world_states, the run's states, d3d9_draw_ranges_*. */
bool
d3d9_rev241_prepare_run(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run);

/** The bucketed opaque chain prepared to draw. False: nothing to draw. */
bool
d3d9_rev241_prepare_opaque(struct ToriPlatformWin32_Renderer_D3D9* renderer);

/** A PROJECTED run's texture, bound to stage 0 (u clamped, v wrapped). False:
 *  it could not be made, and the run is skipped. */
bool
d3d9_rev241_bind_projector_texture(
    struct ToriPlatformWin32_Renderer_D3D9* renderer,
    const struct D3D9Rev241Run* run);

/** The camera depth the pass's projection maps to window depth 0..1 as
 *  A + B / depth: A and B from renderer->proj. */
void
d3d9_rev241_depth_mapping(
    const struct ToriPlatformWin32_Renderer_D3D9* renderer,
    float* out_a,
    float* out_b);

/** out = a . b (row-vector D3D matrices). */
void
d3d9_rev241_multiply(const D3DMATRIX* a, const D3DMATRIX* b, D3DMATRIX* out);
/** The inverse of `m`; false when it has none. */
bool
d3d9_rev241_invert_affine(const D3DMATRIX* m, D3DMATRIX* out);

#endif
