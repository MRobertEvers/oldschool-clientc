#include "trspk_metal.h"

#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "../../tools/trspk_dash.h"

#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "osrs/game.h"
#include "tori_rs_render.h"

static TRSPK_UsageClass
trspk_metal_usage_from_torirs(uint8_t usage)
{
    switch( usage )
    {
    case TORIRS_USAGE_NPC:
        return TRSPK_USAGE_NPC;
    case TORIRS_USAGE_PLAYER:
        return TRSPK_USAGE_PLAYER;
    case TORIRS_USAGE_PROJECTILE:
        return TRSPK_USAGE_PROJECTILE;
    default:
        return TRSPK_USAGE_SCENERY;
    }
}

void
trspk_metal_event_tex_load(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
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
        trspk_metal_cache_load_texture_128(
            ctx->renderer,
            (TRSPK_TextureId)cmd->_texture_load.texture_id,
            rgba,
            anim_u,
            anim_v,
            tex ? tex->opaque : true);
        trspk_metal_cache_refresh_atlas(ctx->renderer);
    }
}

void
trspk_metal_event_res_model_load(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !cmd || !cmd->_model_load.model ||
        cmd->_model_load.model_id < 0 )
        return;
    const TRSPK_UsageClass usage = trspk_metal_usage_from_torirs(cmd->_model_load.usage_hint);
    if( usage == TRSPK_USAGE_SCENERY )
        return;
    TRSPK_BakeTransform bake = trspk_bake_transform_from_yaw_translate(
        cmd->_model_load.world_x,
        cmd->_model_load.world_y,
        cmd->_model_load.world_z,
        cmd->_model_load.world_yaw_r2pi2048);
    trspk_resource_cache_set_model_bake(ctx->cache, (TRSPK_ModelId)cmd->_model_load.model_id, &bake);
    trspk_metal_dynamic_load_model(
        ctx->renderer,
        (TRSPK_ModelId)cmd->_model_load.model_id,
        cmd->_model_load.model,
        usage,
        &bake);
}

void
trspk_metal_event_res_model_unload(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd || cmd->_model_load.model_id < 0 )
        return;
    const TRSPK_UsageClass usage = trspk_metal_usage_from_torirs(cmd->_model_load.usage_hint);
    if( usage == TRSPK_USAGE_SCENERY )
        trspk_metal_cache_unload_model(ctx->renderer, (TRSPK_ModelId)cmd->_model_load.model_id);
    else
        trspk_metal_dynamic_unload_model(ctx->renderer, (TRSPK_ModelId)cmd->_model_load.model_id);
}

void
trspk_metal_event_res_anim_load(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !cmd || !cmd->_animation_load.model ||
        cmd->_animation_load.model_gpu_id < 0 )
        return;
    const TRSPK_UsageClass usage = trspk_metal_usage_from_torirs(cmd->_animation_load.usage_hint);
    if( usage == TRSPK_USAGE_SCENERY )
        return;
    dashmodel_animate(
        cmd->_animation_load.model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t seg = cmd->_animation_load.animation_index == 1 ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                                                  : TRSPK_GPU_ANIM_PRIMARY_IDX;
    const TRSPK_BakeTransform* bake =
        trspk_resource_cache_get_model_bake(ctx->cache, (TRSPK_ModelId)cmd->_animation_load.model_gpu_id);
    trspk_metal_dynamic_load_anim(
        ctx->renderer,
        (TRSPK_ModelId)cmd->_animation_load.model_gpu_id,
        cmd->_animation_load.model,
        seg,
        (uint16_t)cmd->_animation_load.frame_index,
        usage,
        bake);
}

void
trspk_metal_event_batch3d_begin(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd )
        return;
    ctx->current_batch_id = cmd->_batch.batch_id;
    ctx->batch_active = true;
    trspk_batch32_begin(ctx->staging);
    trspk_resource_cache_batch_begin(ctx->cache, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_metal_event_batch3d_model_add(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd || !ctx->batch_active )
        return;
    TRSPK_BakeTransform bake = trspk_bake_transform_from_yaw_translate(
        cmd->_model_load.world_x,
        cmd->_model_load.world_y,
        cmd->_model_load.world_z,
        cmd->_model_load.world_yaw_r2pi2048);
    trspk_resource_cache_set_model_bake(ctx->cache, (TRSPK_ModelId)cmd->_model_load.model_id, &bake);
    trspk_dash_batch_add_model32(
        ctx->staging,
        cmd->_model_load.model,
        (uint16_t)cmd->_model_load.model_id,
        TRSPK_GPU_ANIM_NONE_IDX,
        0,
        &bake);
}

void
trspk_metal_event_batch3d_anim_add(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd || !ctx->batch_active ||
        !cmd->_animation_load.model )
        return;
    dashmodel_animate(
        cmd->_animation_load.model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t seg = cmd->_animation_load.animation_index == 1 ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                                                  : TRSPK_GPU_ANIM_PRIMARY_IDX;
    const TRSPK_BakeTransform* bake =
        trspk_resource_cache_get_model_bake(ctx->cache, (TRSPK_ModelId)cmd->_animation_load.model_gpu_id);
    trspk_dash_batch_add_model32(
        ctx->staging,
        cmd->_animation_load.model,
        (uint16_t)cmd->_animation_load.model_gpu_id,
        seg,
        (uint16_t)cmd->_animation_load.frame_index,
        bake);
}

void
trspk_metal_event_batch3d_end(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->staging || !cmd )
        return;
    trspk_batch32_end(ctx->staging);
    trspk_metal_cache_batch_submit(
        ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id, cmd->_batch.usage_hint);
    ctx->current_batch_id = 0;
    ctx->batch_active = false;
}

void
trspk_metal_event_batch3d_clear(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    trspk_metal_cache_batch_clear(ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_metal_event_draw_model(
    TRSPK_MetalEventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint32_t* sorted_indices_buffer,
    uint32_t buffer_capacity)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !game || !cmd )
        return;

    if( cmd->_model_draw.usage_hint == TORIRS_USAGE_PROJECTILE )
    {
        TRSPK_BakeTransform bake = trspk_bake_transform_from_yaw_translate(
            (int32_t)cmd->_model_draw.world_position.x,
            (int32_t)cmd->_model_draw.world_position.y,
            (int32_t)cmd->_model_draw.world_position.z,
            (int32_t)cmd->_model_draw.world_position.yaw);
        trspk_resource_cache_set_model_bake(
            ctx->cache, (TRSPK_ModelId)cmd->_model_draw.model_id, &bake);
        if( cmd->_model_draw.use_animation )
        {
            const uint8_t seg = cmd->_model_draw.animation_index == 1 ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                                                      : TRSPK_GPU_ANIM_PRIMARY_IDX;
            trspk_metal_dynamic_load_anim(
                ctx->renderer,
                (TRSPK_ModelId)cmd->_model_draw.model_id,
                cmd->_model_draw.model,
                seg,
                (uint16_t)cmd->_model_draw.frame_index,
                TRSPK_USAGE_PROJECTILE,
                &bake);
        }
        else
        {
            trspk_metal_dynamic_load_model(
                ctx->renderer,
                (TRSPK_ModelId)cmd->_model_draw.model_id,
                cmd->_model_draw.model,
                TRSPK_USAGE_PROJECTILE,
                &bake);
        }
    }

    const TRSPK_ModelPose* pose = trspk_resource_cache_get_pose_for_draw(
        ctx->cache,
        (TRSPK_ModelId)cmd->_model_draw.model_id,
        cmd->_model_draw.use_animation,
        cmd->_model_draw.animation_index,
        cmd->_model_draw.frame_index);
    if( !pose )
        return;
    const uint32_t count = trspk_dash_prepare_sorted_indices(
        game, cmd->_model_draw.model, cmd, sorted_indices_buffer, buffer_capacity);
    if( count != 0 )
        trspk_metal_draw_add_model(ctx->renderer, pose, sorted_indices_buffer, count);
}

void
trspk_metal_event_state_begin_3d(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
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
        TRSPK_MetalRenderer* r = ctx->renderer;
        int32_t w = 1;
        int32_t h = 1;
        if( r->window_width > 0u && r->window_height > 0u )
        {
            w = (int32_t)r->window_width;
            h = (int32_t)r->window_height;
        }
        else if( r->width > 0u && r->height > 0u )
        {
            w = (int32_t)r->width;
            h = (int32_t)r->height;
        }
        rect.x = 0;
        rect.y = 0;
        rect.width = w;
        rect.height = h;
        if( rect.width <= 0 )
            rect.width = 1;
        if( rect.height <= 0 )
            rect.height = 1;
    }
    trspk_metal_draw_begin_3d(ctx->renderer, &rect);
}

void
trspk_metal_event_state_clear_rect(TRSPK_MetalEventContext* ctx, const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    TRSPK_Rect rect = { cmd->_clear_rect.x, cmd->_clear_rect.y, cmd->_clear_rect.w, cmd->_clear_rect.h };
    trspk_metal_draw_clear_rect(ctx->renderer, &rect);
}

void
trspk_metal_event_state_end_3d(TRSPK_MetalEventContext* ctx, struct GGame* game, double frame_clock)
{
    if( !ctx || !ctx->renderer || !game )
        return;
    TRSPK_Mat4 view, proj;
    trspk_metal_compute_view_matrix(
        view.m,
        (float)game->camera_world_x,
        (float)game->camera_world_y,
        (float)game->camera_world_z,
        trspk_metal_dash_yaw_to_radians(game->camera_pitch),
        trspk_metal_dash_yaw_to_radians(game->camera_yaw));
    const float projection_width = ctx->renderer->pass_logical_rect.width > 0
                                       ? (float)ctx->renderer->pass_logical_rect.width
                                       : (float)ctx->renderer->width;
    const float projection_height = ctx->renderer->pass_logical_rect.height > 0
                                        ? (float)ctx->renderer->pass_logical_rect.height
                                        : (float)ctx->renderer->height;
    trspk_metal_compute_projection_matrix(
        proj.m, (90.0f * TRSPK_PI) / 180.0f, projection_width, projection_height);
    trspk_metal_remap_projection_opengl_to_metal_z(proj.m);
    ctx->renderer->frame_clock = frame_clock;
    trspk_metal_draw_submit_3d(ctx->renderer, &view, &proj);
}
