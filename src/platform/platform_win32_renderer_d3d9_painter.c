/**
 * The painter's-algorithm world path for the D3D9 renderer.
 *
 * This is the legacy RS ordering: no depth buffer at all.  Every model's faces
 * are sorted back-to-front on the CPU by ToriDraw_RenderModel2SortFaces, then
 * emitted in that order, and the hardware draws them in the order it receives
 * them.  Face priorities survive because the sort honours them.
 *
 * Correctness lives entirely in the submission order, so there is nothing to
 * cache and nothing to allocate -- the whole implementation is these four
 * functions over the core's own buffers.  It is the mode the renderer runs when
 * ToriRS_D3D9->zbuffer is NULL, which is also what a renderer that never
 * reached ToriRS_D3D9_Init gets.
 *
 * platform_win32_renderer_d3d9_zbuffer.c is the depth-tested alternative.  The
 * two are peers and neither calls the other.
 */

#include "platform/platform_win32_renderer_d3d9_core.h"

#include <string.h>

static void
d3d9_painter_mat4_mul_colmajor(const float* a, const float* b, float* out)
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

void
d3d9_painter_setup_projection(struct ToriRS_D3D9* renderer)
{
    /* With no depth test clip Z carries no information, so it is folded to the
     * constant half-range D3D expects rather than derived from the camera's
     * near/far planes. */
    static const float clip_z[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f,
    };
    float remapped[16];
    d3d9_painter_mat4_mul_colmajor(clip_z, renderer->proj, remapped);
    memcpy(renderer->proj, remapped, sizeof(remapped));
}

void
d3d9_painter_apply_world_states(struct ToriRS_D3D9* renderer)
{
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZENABLE, D3DZB_FALSE);
    IDirect3DDevice9_SetRenderState(renderer->device, D3DRS_ZWRITEENABLE, FALSE);
}

int
d3d9_painter_sort_faces(
    struct ToriRS_D3D9* renderer,
    const struct ToriRS_RenderCommand_Model* command,
    int* out_sorted_face_count)
{
    int face_count = ToriDraw_RenderModel2SortFaces(command->model, renderer->scene);
    /* Every face this mode draws is a sorted face: the two counts are the same
     * number, and the core wants both. */
    *out_sorted_face_count = face_count;
    return face_count;
}

void
d3d9_painter_emit_model(
    struct ToriRS_D3D9* renderer,
    const struct D3D9ModelPlacement* placement)
{
    const uint32_t local_base = placement->local_base;
    int* face_order = ToriDraw_FaceOrder(renderer->scene);
    uint32_t written = 0u;
    int i;

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
