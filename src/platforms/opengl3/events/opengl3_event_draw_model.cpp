#include "platforms/common/pass_3d_builder/gpu_3d_cache2_opengl3.h"
#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_add_model_draw_scenery.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"

#include <vector>

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "osrs/game.h"
#include "tori_rs_render.h"
}

void
opengl3_event_draw_model(
    OpenGL3RenderCtx* ctx,
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
    if( mid_v2 <= 0 || !ctx || !ctx->renderer || !ctx->game )
        return;

    if( ctx->renderer )
        ctx->renderer->diag_frame_model_draw_cmds++;

    const bool use_anim = cmd->_model_draw.use_animation;
    const uint8_t anim_idx = cmd->_model_draw.animation_index;
    const uint8_t frame_idx = cmd->_model_draw.frame_index;
    if( use_anim )
    {
        printf("use_anim: %d, anim_idx: %d, frame_idx: %d\n", use_anim, anim_idx, frame_idx);
    }
    const GPUModelPosedData pose = ctx->renderer->model_cache2.GetModelPoseForDraw(
        (uint16_t)mid_v2, use_anim, (int)anim_idx, (int)frame_idx);
    if( !pose.valid || pose.gpu_batch_id == 0u )
    {
        if( ctx->renderer )
            ctx->renderer->diag_frame_pose_invalid_skips++;
        return;
    }
    if( (ToriRS_UsageHint)cmd->_model_draw.usage_hint != TORIRS_USAGE_SCENERY )
        return;

    struct DashPosition draw_position = cmd->_model_draw.position;
    int face_order_count = dash3d_prepare_projected_face_order(
        ctx->game->sys_dash, model, &draw_position, ctx->game->view_port, ctx->game->camera);
    const int* face_order = dash3d_projected_face_order(ctx->game->sys_dash, &face_order_count);
    if( face_order_count <= 0 )
        return;

    const int face_count = dashmodel_face_count(model);

    static std::vector<uint32_t> g_sorted;
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
            g_sorted.push_back(b);
            g_sorted.push_back(b + 1u);
            g_sorted.push_back(b + 2u);
        }
    }

    const uint32_t idx_count = (uint32_t)g_sorted.size();
    if( idx_count == 0u )
        return;

    opengl3_add_model_draw_scenery(
        ctx->renderer->pass3d_builder,
        ctx->renderer->model_cache2,
        (uint16_t)mid_v2,
        use_anim,
        anim_idx,
        frame_idx,
        g_sorted.data(),
        idx_count);
}
