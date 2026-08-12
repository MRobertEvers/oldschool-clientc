/**
 * The painter's-algorithm world backend for the D3D9 renderer.
 *
 * This is the legacy RS ordering: no depth buffer at all.  Every model's faces
 * are sorted back-to-front on the CPU by ToriDraw_RenderModel2SortFaces, then
 * emitted in that order, and the hardware draws them in the order it receives
 * them.  Face priorities survive because the sort honours them.
 *
 * The backend is stateless -- correctness lives entirely in the submission
 * order -- so there is no world_state to allocate and most of the retained
 * geometry notifications are genuine no-ops.
 *
 * See platform_win32_renderer_d3d9_internal.h for the backend contract, and
 * platform_win32_renderer_d3d9_world_zbuffer.c for the depth-tested sibling.
 */

#include "platform/platform_win32_renderer_d3d9_internal.h"

#include <string.h>

static void
d3d9_mat4_mul_colmajor(const float* a, const float* b, float* out)
{
    int column;
    int row;
    int k;
    for( column = 0; column < 4; column++ )
        for( row = 0; row < 4; row++ )
        {
            float sum = 0.0f;
            for( k = 0; k < 4; k++ )
                sum += a[k * 4 + row] * b[column * 4 + k];
            out[column * 4 + row] = sum;
        }
}

static void
d3d9_remap_projection_z(float* projection)
{
    static const float clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f,
    };
    float remapped[16];
    d3d9_mat4_mul_colmajor(clip_z, projection, remapped);
    memcpy(projection, remapped, sizeof(remapped));
}

static bool
painter_create(struct ToriRS_D3D9* renderer)
{
    (void)renderer;
    return true;
}

static void
painter_destroy(struct ToriRS_D3D9* renderer)
{
    (void)renderer;
}

static void
painter_begin_pass(struct ToriRS_D3D9* renderer)
{
    /* Nothing to clear: with no depth buffer the pass carries no state from
     * the previous frame, and the shared opaque chain is reset by the core. */
    (void)renderer;
}

static void
painter_setup_projection(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Begin3D* command)
{
    (void)command;
    /* Painter mode has no depth test, so clip Z carries no information.  Fold
     * it to the constant half-range D3D expects rather than deriving it from
     * the camera's near/far planes. */
    d3d9_remap_projection_z(renderer->proj);
}

static void
painter_end_pass(struct ToriRS_D3D9* renderer)
{
    /* The single sorted chain the core already drew is the whole world. */
    (void)renderer;
}

static void
painter_reset_pass(struct ToriRS_D3D9* renderer)
{
    (void)renderer;
}

static void
painter_apply_world_states(struct ToriRS_D3D9* renderer)
{
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZWRITEENABLE, FALSE);
}

static void
painter_apply_pass_states(struct ToriRS_D3D9* renderer, bool blended_pass)
{
    /* One pass, and d3d9_set_world_states already left blending enabled for
     * it. */
    (void)renderer;
    (void)blended_pass;
}

static int
painter_model_face_count(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    int* out_sorted_face_count)
{
    int face_count = ToriDraw_RenderModel2SortFaces(command->model, renderer->scene);
    *out_sorted_face_count = face_count;
    return face_count;
}

static void
painter_model_emit(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    const struct D3D9ModelPlacement* placement)
{
    const uint32_t local_base = placement->local_base;
    int* face_order = ToriDraw_FaceOrder(renderer->scene);
    uint32_t written = 0u;
    int i;

    (void)command;
    for( i = 0; i < placement->sorted_face_count; i++ )
    {
        uint32_t face;
        uint32_t base;
        if( face_order[i] < 0 )
            continue;
        face = (uint32_t)face_order[i];
        if( face > (UINT32_MAX - local_base - 2u) / 3u )
            continue;
        base = local_base + face * 3u;
        if( base + 2u > UINT16_MAX )
            continue;
        renderer->model_indices[written++] = (uint16_t)base;
        renderer->model_indices[written++] = (uint16_t)(base + 1u);
        renderer->model_indices[written++] = (uint16_t)(base + 2u);
    }
    trspk_ibochain_push16(
        renderer->ibo_chain,
        placement->binding,
        placement->page_base,
        renderer->model_indices,
        written);
}

static void
painter_pose_baked(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    /* Nothing is cached per pose: the sort runs fresh against the live model
     * every frame. */
    (void)renderer;
    (void)element_id;
    (void)anim_index;
    (void)pose_id;
    (void)handle;
}

static void
painter_element_dropped(struct ToriRS_D3D9* renderer, int element_id)
{
    (void)renderer;
    (void)element_id;
}

static void
painter_track_dropped(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index)
{
    (void)renderer;
    (void)element_id;
    (void)anim_index;
}

static void
painter_batch_pose_baked(
    struct ToriRS_D3D9* renderer,
    int element_id,
    int anim_index,
    int pose_id,
    struct ToriDraw_ModelHandle handle)
{
    (void)renderer;
    (void)element_id;
    (void)anim_index;
    (void)pose_id;
    (void)handle;
}

static void
painter_batch_dropped(struct ToriRS_D3D9* renderer, struct TRSPK_Batch16* cpu)
{
    (void)renderer;
    (void)cpu;
}

static const struct D3D9WorldBackend g_painter_backend = {
    "painter",
    false,
    painter_create,
    painter_destroy,
    painter_begin_pass,
    painter_setup_projection,
    painter_end_pass,
    painter_reset_pass,
    painter_apply_world_states,
    painter_apply_pass_states,
    painter_model_face_count,
    painter_model_emit,
    painter_pose_baked,
    painter_element_dropped,
    painter_track_dropped,
    painter_batch_pose_baked,
    painter_batch_dropped,
};

const struct D3D9WorldBackend*
d3d9_world_backend_painter(void)
{
    return &g_painter_backend;
}
