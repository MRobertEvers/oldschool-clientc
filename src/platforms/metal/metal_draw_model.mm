// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

#include <vector>

void
metal_frame_event_model_draw(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashModel* model = cmd->_model_draw.model;
    if( !model || !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
        !dashmodel_face_colors_c_const(model) || !dashmodel_vertices_x_const(model) ||
        !dashmodel_vertices_y_const(model) || !dashmodel_vertices_z_const(model) ||
        !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
        !dashmodel_face_indices_c_const(model) || dashmodel_face_count(model) <= 0 )
        return;

    const int mid_v2 = cmd->_model_draw.model_id;
    if( mid_v2 <= 0 )
        return;

    const bool use_anim = cmd->_model_draw.use_animation;
    const uint8_t anim_idx = cmd->_model_draw.animation_index;
    const uint8_t frame_idx = cmd->_model_draw.frame_index;
    const GPUModelPosedData pose = ctx->renderer->model_cache2.GetModelPoseForDraw(
        (uint16_t)mid_v2, use_anim, (int)anim_idx, (int)frame_idx);
    if( !pose.valid )
        return;

    struct DashPosition draw_position = cmd->_model_draw.position;
    int face_order_count = dash3d_prepare_projected_face_order(
        ctx->game->sys_dash, model, &draw_position, ctx->game->view_port, ctx->game->camera);
    const int* face_order = dash3d_projected_face_order(ctx->game->sys_dash, &face_order_count);

    const int fc = dashmodel_face_count(model);
    const bool textured = dashmodel_has_textures(model);

    /* Dynamic index buffer is uint16. Merged batch vertex indices often exceed 65535, so we store
     * **local** indices (within this model's VBO slice) and add `pose.vbo_offset` at draw time
     * via Metal `baseVertex` in Pass3DBuilder2SubmitMetal. */
    static std::vector<uint16_t> g_sorted;
    g_sorted.clear();
    if( face_order_count > 0 && fc > 0 )
    {
        g_sorted.reserve((size_t)face_order_count * 3u);
        for( int fi = 0; fi < face_order_count; ++fi )
        {
            const int f = face_order ? face_order[fi] : fi;
            if( f < 0 || f >= fc )
                continue;
            if( textured )
            {
                const uint32_t b = (uint32_t)f * 3u;
                g_sorted.push_back((uint16_t)b);
                g_sorted.push_back((uint16_t)(b + 1u));
                g_sorted.push_back((uint16_t)(b + 2u));
            }
            else
            {
                const faceint_t* fa = dashmodel_face_indices_a_const(model);
                const faceint_t* fb = dashmodel_face_indices_b_const(model);
                const faceint_t* fc_c = dashmodel_face_indices_c_const(model);
                g_sorted.push_back((uint16_t)fa[f]);
                g_sorted.push_back((uint16_t)fb[f]);
                g_sorted.push_back((uint16_t)fc_c[f]);
            }
        }
    }

    const uint32_t idx_count = (uint32_t)g_sorted.size();
    uint16_t* idx_ptr = idx_count ? g_sorted.data() : nullptr;

    ctx->renderer->mtl_pass3d_builder.AddModelDrawYawOnly(
        (uint16_t)mid_v2,
        draw_position.x,
        draw_position.y,
        draw_position.z,
        draw_position.yaw,
        use_anim,
        anim_idx,
        frame_idx,
        idx_ptr,
        idx_count);
}
