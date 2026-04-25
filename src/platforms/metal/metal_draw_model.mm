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

    const int face_count = dashmodel_face_count(model);

    /* GPU3DCache2 bakes **three expanded vertices per face** in original face order (textured and
     * untextured). Local indices are `f*3 + {0,1,2}`; `pose.vbo_offset` is the first vertex of this
     * model slice (`Pass3DBuilder2SubmitMetal` `baseVertex`). */
    static std::vector<uint16_t> g_sorted;
    g_sorted.clear();
    if( face_order_count > 0 && face_count > 0 )
    {
        g_sorted.reserve((size_t)face_order_count * 3u);
        for( int fi = 0; fi < face_order_count; ++fi )
        {
            const int face_index = face_order ? face_order[fi] : fi;
            if( face_index < 0 || face_index >= face_count )
                continue;
            const uint32_t b = (uint32_t)face_index * 3u;
            g_sorted.push_back((uint16_t)b);
            g_sorted.push_back((uint16_t)(b + 1u));
            g_sorted.push_back((uint16_t)(b + 2u));
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
