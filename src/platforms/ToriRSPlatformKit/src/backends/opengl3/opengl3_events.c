#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "../../tools/trspk_dash.h"
#include "../../tools/trspk_lru_model_cache.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_vertex_buffer.h"
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "osrs/game.h"
#include "tori_rs_render.h"
#include "opengl3_internal.h"
#include "trspk_opengl3.h"

#include <string.h>

static TRSPK_UsageClass
trspk_opengl3_usage_from_torirs(uint8_t usage)
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
trspk_opengl3_event_tex_load(
    TRSPK_OpenGL3EventContext* ctx,
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
            const float s =
                trspk_texture_animation_signed(tex->animation_direction, tex->animation_speed);
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
        trspk_opengl3_cache_load_texture_128(
            ctx->renderer,
            (TRSPK_TextureId)cmd->_texture_load.texture_id,
            rgba,
            anim_u,
            anim_v,
            tex ? tex->opaque : true);
        trspk_opengl3_cache_refresh_atlas(ctx->renderer);
    }
}

void
trspk_opengl3_event_res_model_load(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !cmd || !cmd->_model_load.model ||
        cmd->_model_load.visual_id <= 0 )
        return;
    const TRSPK_UsageClass usage = trspk_opengl3_usage_from_torirs(cmd->_model_load.usage_hint);
    if( usage == TRSPK_USAGE_SCENERY )
        return;

    trspk_resource_cache_allocate_pose_slot(
        ctx->cache, (TRSPK_ModelId)cmd->_model_load.visual_id, (int)TRSPK_GPU_ANIM_NONE_IDX, 0);

    TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(ctx->cache);
    TRSPK_BakeTransform id_bake = trspk_bake_transform_identity();
    if( lru )
    {
        TRSPK_ModelArrays arrays;
        trspk_dash_fill_model_arrays(cmd->_model_load.model, &arrays);
        if( arrays.face_count == 0u )
            return;
        if( dashmodel_has_textures(cmd->_model_load.model) )
            trspk_lru_model_cache_get_or_emplace_textured(
                lru,
                (TRSPK_ModelId)cmd->_model_load.visual_id,
                TRSPK_GPU_ANIM_NONE_IDX,
                0u,
                &arrays,
                trspk_dash_uv_calculation_mode(cmd->_model_load.model),
                ctx->cache,
                &id_bake);
        else
            trspk_lru_model_cache_get_or_emplace_untextured(
                lru,
                (TRSPK_ModelId)cmd->_model_load.visual_id,
                TRSPK_GPU_ANIM_NONE_IDX,
                0u,
                &arrays,
                &id_bake);
        return;
    }

    TRSPK_BakeTransform bake = trspk_bake_transform_from_yaw_translate(
        cmd->_model_load.world_x,
        cmd->_model_load.world_y,
        cmd->_model_load.world_z,
        cmd->_model_load.world_yaw_r2pi2048);
    trspk_resource_cache_set_model_bake(
        ctx->cache, (TRSPK_ModelId)cmd->_model_load.visual_id, &bake);
    trspk_opengl3_dynamic_load_model(
        ctx->renderer,
        (TRSPK_ModelId)cmd->_model_load.visual_id,
        cmd->_model_load.model,
        usage,
        &bake);
}

void
trspk_opengl3_event_res_model_unload(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd || cmd->_model_load.visual_id <= 0 )
        return;
    const TRSPK_UsageClass usage = trspk_opengl3_usage_from_torirs(cmd->_model_load.usage_hint);
    if( usage == TRSPK_USAGE_SCENERY )
        trspk_opengl3_cache_unload_model(ctx->renderer, (TRSPK_ModelId)cmd->_model_load.visual_id);
    else
        trspk_opengl3_dynamic_unload_model(ctx->renderer, (TRSPK_ModelId)cmd->_model_load.visual_id);
}

void
trspk_opengl3_event_res_anim_load(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !cmd || !cmd->_animation_load.model ||
        cmd->_animation_load.visual_id <= 0 )
        return;
    const TRSPK_UsageClass usage =
        trspk_opengl3_usage_from_torirs(cmd->_animation_load.usage_hint);
    if( usage == TRSPK_USAGE_SCENERY )
        return;
    dashmodel_animate(
        cmd->_animation_load.model, cmd->_animation_load.frame, cmd->_animation_load.framemap);
    const uint8_t seg = cmd->_animation_load.animation_index == 1 ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                                                  : TRSPK_GPU_ANIM_PRIMARY_IDX;

    trspk_resource_cache_allocate_pose_slot(
        ctx->cache,
        (TRSPK_ModelId)cmd->_animation_load.visual_id,
        seg,
        (int)cmd->_animation_load.frame_index);

    TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(ctx->cache);
    TRSPK_BakeTransform id_bake = trspk_bake_transform_identity();
    if( lru )
    {
        TRSPK_ModelArrays arrays;
        trspk_dash_fill_model_arrays(cmd->_animation_load.model, &arrays);
        if( arrays.face_count == 0u )
            return;
        if( dashmodel_has_textures(cmd->_animation_load.model) )
            trspk_lru_model_cache_get_or_emplace_textured(
                lru,
                (TRSPK_ModelId)cmd->_animation_load.visual_id,
                seg,
                (uint16_t)cmd->_animation_load.frame_index,
                &arrays,
                trspk_dash_uv_calculation_mode(cmd->_animation_load.model),
                ctx->cache,
                &id_bake);
        else
            trspk_lru_model_cache_get_or_emplace_untextured(
                lru,
                (TRSPK_ModelId)cmd->_animation_load.visual_id,
                seg,
                (uint16_t)cmd->_animation_load.frame_index,
                &arrays,
                &id_bake);
        return;
    }

    const TRSPK_BakeTransform* bake = trspk_resource_cache_get_model_bake(
        ctx->cache, (TRSPK_ModelId)cmd->_animation_load.visual_id);
    TRSPK_BakeTransform id_fallback = trspk_bake_transform_identity();
    const TRSPK_BakeTransform* use_bake = bake ? bake : &id_fallback;
    trspk_opengl3_dynamic_load_anim(
        ctx->renderer,
        (TRSPK_ModelId)cmd->_animation_load.visual_id,
        cmd->_animation_load.model,
        seg,
        (uint16_t)cmd->_animation_load.frame_index,
        usage,
        use_bake);
}

void
trspk_opengl3_event_batch3d_begin(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd )
        return;
    ctx->current_batch_id = cmd->_batch.batch_id;
    ctx->batch_active = true;
    trspk_batch32_begin(ctx->staging);
    trspk_resource_cache_batch_begin(ctx->cache, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_opengl3_event_batch3d_model_add(
    TRSPK_OpenGL3EventContext* ctx,
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
        ctx->cache, (TRSPK_ModelId)cmd->_model_load.visual_id, &bake);
    trspk_dash_batch_add_model32(
        ctx->staging,
        cmd->_model_load.model,
        (uint16_t)cmd->_model_load.visual_id,
        TRSPK_GPU_ANIM_NONE_IDX,
        0,
        &bake,
        ctx->cache);
}

void
trspk_opengl3_event_batch3d_anim_add(
    TRSPK_OpenGL3EventContext* ctx,
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
        ctx->cache, (TRSPK_ModelId)cmd->_animation_load.visual_id);
    trspk_dash_batch_add_model32(
        ctx->staging,
        cmd->_animation_load.model,
        (uint16_t)cmd->_animation_load.visual_id,
        seg,
        (uint16_t)cmd->_animation_load.frame_index,
        bake,
        ctx->cache);
}

void
trspk_opengl3_event_batch3d_end(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->staging || !cmd )
        return;
    trspk_batch32_end(ctx->staging);
    trspk_opengl3_cache_batch_submit(
        ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id, cmd->_batch.usage_hint);
    ctx->current_batch_id = 0;
    ctx->batch_active = false;
}

void
trspk_opengl3_event_batch3d_clear(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    trspk_opengl3_cache_batch_clear(ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_opengl3_event_draw_model(
    TRSPK_OpenGL3EventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint32_t* sorted_indices_buffer,
    uint32_t buffer_capacity)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !game || !cmd )
        return;

    const TRSPK_UsageClass draw_usage =
        trspk_opengl3_usage_from_torirs(cmd->_model_draw.usage_hint);
    const TRSPK_ModelId layout_id = (TRSPK_ModelId)cmd->_model_draw.visual_id;
    const TRSPK_ModelId pose_storage_id =
        draw_usage == TRSPK_USAGE_SCENERY ? layout_id : (TRSPK_ModelId)cmd->_model_draw.element_id;
    if( draw_usage != TRSPK_USAGE_SCENERY )
    {
        TRSPK_BakeTransform bake = trspk_bake_transform_from_yaw_translate(
            (int32_t)cmd->_model_draw.world_position.x,
            (int32_t)cmd->_model_draw.world_position.y,
            (int32_t)cmd->_model_draw.world_position.z,
            (int32_t)cmd->_model_draw.world_position.yaw);
        if( cmd->_model_draw.usage_hint == TORIRS_USAGE_PROJECTILE )
            trspk_resource_cache_set_model_bake(
                ctx->cache, (TRSPK_ModelId)cmd->_model_draw.visual_id, &bake);
        uint8_t seg = TRSPK_GPU_ANIM_NONE_IDX;
        uint16_t frame_i = 0u;
        if( cmd->_model_draw.use_animation )
        {
            seg = cmd->_model_draw.animation_index == 1 ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                                        : TRSPK_GPU_ANIM_PRIMARY_IDX;
            frame_i = (uint16_t)cmd->_model_draw.frame_index;
        }
        const uint32_t pose_ix = trspk_resource_cache_get_pose_index_for_draw(
            ctx->cache,
            layout_id,
            cmd->_model_draw.use_animation,
            cmd->_model_draw.animation_index,
            cmd->_model_draw.frame_index);
        bool did_defer = false;
        if( pose_ix < TRSPK_MAX_POSES_PER_MODEL && cmd->_model_draw.model )
        {
            TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(ctx->cache);
            if( lru )
            {
                TRSPK_ModelArrays arrays;
                trspk_dash_fill_model_arrays(cmd->_model_draw.model, &arrays);
                if( arrays.face_count != 0u )
                {
                    TRSPK_BakeTransform id_bake = trspk_bake_transform_identity();
                    if( dashmodel_has_textures(cmd->_model_draw.model) )
                        trspk_lru_model_cache_get_or_emplace_textured(
                            lru,
                            layout_id,
                            seg,
                            frame_i,
                            &arrays,
                            trspk_dash_uv_calculation_mode(cmd->_model_draw.model),
                            ctx->cache,
                            &id_bake);
                    else
                        trspk_lru_model_cache_get_or_emplace_untextured(
                            lru,
                            layout_id,
                            seg,
                            frame_i,
                            &arrays,
                            &id_bake);
                    const TRSPK_VertexBuffer* id_mesh =
                        trspk_lru_model_cache_get(lru, layout_id, seg, frame_i);
                    if( id_mesh )
                        did_defer = trspk_opengl3_dynamic_enqueue_draw_mesh_deferred(
                            ctx->renderer,
                            pose_storage_id,
                            layout_id,
                            draw_usage,
                            pose_ix,
                            seg,
                            frame_i,
                            id_mesh->vertex_count,
                            id_mesh->index_count,
                            &bake);
                }
            }
        }
        if( !did_defer )
            return;
    }

    const TRSPK_ModelPose* pose = trspk_resource_cache_get_pose_for_instance_draw(
        ctx->cache,
        layout_id,
        pose_storage_id,
        cmd->_model_draw.use_animation,
        cmd->_model_draw.animation_index,
        cmd->_model_draw.frame_index);
    if( !pose )
        return;
    const uint32_t count = trspk_dash_prepare_sorted_indices(
        game, cmd->_model_draw.model, cmd, sorted_indices_buffer, buffer_capacity);
    if( count != 0 )
        trspk_opengl3_draw_add_model(ctx->renderer, pose, sorted_indices_buffer, count);
}

void
trspk_opengl3_event_state_begin_3d(
    TRSPK_OpenGL3EventContext* ctx,
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
        TRSPK_OpenGL3Renderer* r = ctx->renderer;
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
    trspk_opengl3_draw_begin_3d(ctx->renderer, &rect);
}

void
trspk_opengl3_event_state_clear_rect(
    TRSPK_OpenGL3EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    TRSPK_Rect rect = {
        cmd->_clear_rect.x, cmd->_clear_rect.y, cmd->_clear_rect.w, cmd->_clear_rect.h
    };
    trspk_opengl3_draw_clear_rect(ctx->renderer, &rect);
}

void
trspk_opengl3_event_state_end_3d(
    TRSPK_OpenGL3EventContext* ctx,
    struct GGame* game,
    double frame_clock)
{
    if( !ctx || !ctx->renderer || !game )
        return;
    TRSPK_Mat4 view, proj;
    trspk_opengl3_compute_view_matrix(
        view.m,
        (float)game->camera_world_x,
        (float)game->camera_world_y,
        (float)game->camera_world_z,
        trspk_opengl3_dash_yaw_to_radians(game->camera_pitch),
        trspk_opengl3_dash_yaw_to_radians(game->camera_yaw));
    const float projection_width = ctx->renderer->pass_logical_rect.width > 0
                                       ? (float)ctx->renderer->pass_logical_rect.width
                                       : (float)ctx->renderer->width;
    const float projection_height = ctx->renderer->pass_logical_rect.height > 0
                                        ? (float)ctx->renderer->pass_logical_rect.height
                                        : (float)ctx->renderer->height;
    trspk_opengl3_compute_projection_matrix(
        proj.m, (90.0f * TRSPK_PI) / 180.0f, projection_width, projection_height);
    ctx->renderer->frame_clock = frame_clock;
    trspk_opengl3_draw_submit_3d(ctx->renderer, &view, &proj);
}
