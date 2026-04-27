#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "../../tools/trspk_dash.h"
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "osrs/game.h"
#include "tori_rs_render.h"
#include "trspk_webgl1.h"

void
trspk_webgl1_compute_default_pass_logical(
    TRSPK_Rect* out,
    int window_width,
    int window_height,
    const struct GGame* game)
{
    trspk_pass_logical_from_game(out, window_width, window_height, game);
}

static uint32_t
trspk_webgl1_prepare_sorted_indices16(
    struct GGame* game,
    struct DashModel* model,
    const struct ToriRSRenderCommand* cmd,
    uint16_t* out_indices,
    uint32_t max_indices)
{
    if( !game || !model || !cmd || !out_indices || max_indices == 0u )
        return 0;
    struct DashPosition draw_position = cmd->_model_draw.position;
    int face_order_count = dash3d_prepare_projected_face_order(
        game->sys_dash, model, &draw_position, game->view_port, game->camera);
    const int* face_order = dash3d_projected_face_order(game->sys_dash, &face_order_count);
    const int face_count = dashmodel_face_count(model);
    uint32_t count = 0;
    for( int fi = 0; fi < face_order_count; ++fi )
    {
        const int face_index = face_order ? face_order[fi] : fi;
        if( face_index < 0 || face_index >= face_count )
            continue;
        if( count + 3u > max_indices )
            break;
        const uint16_t b = (uint16_t)(face_index * 3);
        out_indices[count++] = b;
        out_indices[count++] = (uint16_t)(b + 1u);
        out_indices[count++] = (uint16_t)(b + 2u);
    }
    return count;
}

void
trspk_webgl1_event_tex_load(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    const uint8_t* rgba = 0;
    uint32_t rgba_size = 0;
    trspk_dash_fill_rgba128(
        cmd->_texture_load.texture_nullable,
        ctx->rgba_scratch,
        (uint32_t)sizeof(ctx->rgba_scratch),
        &rgba,
        &rgba_size);
    (void)rgba_size;
    if( rgba && cmd->_texture_load.texture_id >= 0 )
    {
        struct DashTexture* tex = cmd->_texture_load.texture_nullable;
        float anim_u = 0.0f, anim_v = 0.0f;
        if( tex )
        {
            const float s = trspk_texture_animation_signed(
                tex->animation_direction, tex->animation_speed);
            if( s >= 0.0f )
            {
                anim_u = s;
                anim_v = 0.0f;
            }
            else
            {
                anim_u = 0.0f;
                anim_v = -s;
            }
        }
        trspk_webgl1_cache_load_texture_128(
            ctx->renderer,
            (TRSPK_TextureId)cmd->_texture_load.texture_id,
            rgba,
            anim_u,
            anim_v,
            tex ? tex->opaque : true);
        trspk_webgl1_cache_refresh_atlas(ctx->renderer);
    }
}

void
trspk_webgl1_event_batch3d_begin(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd )
        return;
    ctx->current_batch_id = cmd->_batch.batch_id;
    ctx->batch_active = true;
    trspk_batch16_begin(ctx->staging);
    trspk_resource_cache_batch_begin(ctx->cache, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_webgl1_event_batch3d_model_add(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd || !ctx->batch_active )
        return;
    TRSPK_BakeTransform bake = trspk_bake_transform_from_yaw_translate(
        cmd->_model_load.world_x,
        cmd->_model_load.world_y,
        cmd->_model_load.world_z,
        cmd->_model_load.world_yaw_r2pi2048);
    trspk_resource_cache_set_model_bake(
        ctx->cache, (TRSPK_ModelId)cmd->_model_load.model_id, &bake);
    trspk_dash_batch_add_model16(
        ctx->staging,
        cmd->_model_load.model,
        (uint16_t)cmd->_model_load.model_id,
        TRSPK_GPU_ANIM_NONE_IDX,
        0,
        &bake);
}

void
trspk_webgl1_event_batch3d_anim_add(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd || !ctx->batch_active ||
        !cmd->_animation_load.model )
        return;
    dashmodel_animate(
        cmd->_animation_load.model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t seg = cmd->_animation_load.animation_index == 1 ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                                                  : TRSPK_GPU_ANIM_PRIMARY_IDX;
    const TRSPK_BakeTransform* bake = trspk_resource_cache_get_model_bake(
        ctx->cache, (TRSPK_ModelId)cmd->_animation_load.model_gpu_id);
    trspk_dash_batch_add_model16(
        ctx->staging,
        cmd->_animation_load.model,
        (uint16_t)cmd->_animation_load.model_gpu_id,
        seg,
        (uint16_t)cmd->_animation_load.frame_index,
        bake);
}

void
trspk_webgl1_event_batch3d_end(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->staging || !cmd )
        return;
    trspk_batch16_end(ctx->staging);
    trspk_webgl1_cache_batch_submit(
        ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id, cmd->_batch.usage_hint);
    ctx->current_batch_id = 0;
    ctx->batch_active = false;
}

void
trspk_webgl1_event_batch3d_clear(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    trspk_webgl1_cache_batch_clear(ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_webgl1_event_draw_model(
    TRSPK_WebGL1EventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint16_t* sorted_indices_buffer,
    uint32_t buffer_capacity)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !game || !cmd )
        return;
    ctx->diag_frame_model_draw_cmds++;
    if( cmd->_model_draw.usage_hint != TORIRS_USAGE_SCENERY )
        return;
    const TRSPK_ModelPose* pose = trspk_resource_cache_get_pose_for_draw(
        ctx->cache,
        (TRSPK_ModelId)cmd->_model_draw.model_id,
        cmd->_model_draw.use_animation,
        cmd->_model_draw.animation_index,
        cmd->_model_draw.frame_index);
    if( !pose )
    {
        ctx->diag_frame_pose_invalid_skips++;
        return;
    }
    const uint32_t count = trspk_webgl1_prepare_sorted_indices16(
        game, cmd->_model_draw.model, cmd, sorted_indices_buffer, buffer_capacity);
    if( count != 0 )
    {
        trspk_webgl1_draw_add_model(ctx->renderer, pose, sorted_indices_buffer, count);
        ctx->diag_frame_submitted_model_draws++;
    }
}

void
trspk_webgl1_event_state_begin_3d(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    TRSPK_Rect rect;
    if( cmd->_begin_3d.w > 0 && cmd->_begin_3d.h > 0 )
    {
        rect.x = cmd->_begin_3d.x;
        rect.y = cmd->_begin_3d.y;
        rect.width = cmd->_begin_3d.w;
        rect.height = cmd->_begin_3d.h;
    }
    else if(
        ctx->has_default_pass_logical && ctx->default_pass_logical.width > 0 &&
        ctx->default_pass_logical.height > 0 )
    {
        rect = ctx->default_pass_logical;
    }
    else
    {
        rect = trspk_webgl1_full_window_logical_rect(ctx->renderer);
    }
    trspk_webgl1_draw_begin_3d(ctx->renderer, &rect);
}

void
trspk_webgl1_event_state_clear_rect(
    TRSPK_WebGL1EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    TRSPK_Rect rect = {
        cmd->_clear_rect.x, cmd->_clear_rect.y, cmd->_clear_rect.w, cmd->_clear_rect.h
    };
    trspk_webgl1_draw_clear_rect(ctx->renderer, &rect);
}

void
trspk_webgl1_event_state_end_3d(
    TRSPK_WebGL1EventContext* ctx,
    struct GGame* game,
    double frame_clock)
{
    if( !ctx || !ctx->renderer || !game )
        return;
    TRSPK_Mat4 view, proj;
    trspk_webgl1_compute_view_matrix(
        view.m,
        (float)game->camera_world_x,
        (float)game->camera_world_y,
        (float)game->camera_world_z,
        trspk_dash_yaw_to_radians(game->camera_pitch),
        trspk_dash_yaw_to_radians(game->camera_yaw));
    const float projection_width = ctx->renderer->pass_logical_rect.width > 0
                                       ? (float)ctx->renderer->pass_logical_rect.width
                                       : (float)ctx->renderer->width;
    const float projection_height = ctx->renderer->pass_logical_rect.height > 0
                                        ? (float)ctx->renderer->pass_logical_rect.height
                                        : (float)ctx->renderer->height;
    trspk_webgl1_compute_projection_matrix(
        proj.m, (90.0f * TRSPK_PI) / 180.0f, projection_width, projection_height);
    ctx->renderer->frame_clock = frame_clock;
    trspk_webgl1_draw_submit_3d(ctx->renderer, &view, &proj);
}
