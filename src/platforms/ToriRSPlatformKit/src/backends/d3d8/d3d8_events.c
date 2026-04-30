#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "../../tools/trspk_dash.h"
#include "../../tools/trspk_dynamic_pass.h"
#include "../../tools/trspk_lru_model_cache.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_vertex_buffer.h"
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "osrs/game.h"
#include "tori_rs_render.h"
#include "trspk_d3d8.h"

#include <string.h>

static TRSPK_UsageClass
trspk_d3d8_usage_from_torirs(uint8_t usage)
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
trspk_d3d8_compute_default_pass_logical(
    TRSPK_Rect* out,
    int window_width,
    int window_height,
    const struct GGame* game)
{
    trspk_pass_logical_from_game(out, window_width, window_height, game);
}

static uint32_t
trspk_d3d8_prepare_sorted_indices16(
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
trspk_d3d8_event_res_model_load(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !cmd || !cmd->_model_load.model ||
        cmd->_model_load.visual_id <= 0 )
        return;
    const TRSPK_UsageClass usage = trspk_d3d8_usage_from_torirs(cmd->_model_load.usage_hint);
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
                &id_bake,
                1.0f);
        else
            trspk_lru_model_cache_get_or_emplace_untextured(
                lru,
                (TRSPK_ModelId)cmd->_model_load.visual_id,
                TRSPK_GPU_ANIM_NONE_IDX,
                0u,
                &arrays,
                &id_bake,
                1.0f);
        return;
    }

    TRSPK_BakeTransform bake = trspk_bake_transform_from_yaw_translate(
        cmd->_model_load.world_x,
        cmd->_model_load.world_y,
        cmd->_model_load.world_z,
        cmd->_model_load.world_yaw_r2pi2048);
    trspk_resource_cache_set_model_bake(
        ctx->cache, (TRSPK_ModelId)cmd->_model_load.visual_id, &bake);
    trspk_d3d8_dynamic_load_model(
        ctx->renderer,
        (TRSPK_ModelId)cmd->_model_load.visual_id,
        cmd->_model_load.model,
        usage,
        &bake);
}

void
trspk_d3d8_event_res_model_unload(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd || cmd->_model_load.visual_id <= 0 )
        return;
    const TRSPK_UsageClass usage = trspk_d3d8_usage_from_torirs(cmd->_model_load.usage_hint);
    if( usage == TRSPK_USAGE_SCENERY )
        trspk_d3d8_cache_unload_model(ctx->renderer, (TRSPK_ModelId)cmd->_model_load.visual_id);
    else
        trspk_d3d8_dynamic_unload_model(ctx->renderer, (TRSPK_ModelId)cmd->_model_load.visual_id);
}

void
trspk_d3d8_event_res_anim_load(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->cache || !cmd || !cmd->_animation_load.model ||
        cmd->_animation_load.visual_id <= 0 )
        return;
    const TRSPK_UsageClass usage = trspk_d3d8_usage_from_torirs(cmd->_animation_load.usage_hint);
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
                &id_bake,
                1.0f);
        else
            trspk_lru_model_cache_get_or_emplace_untextured(
                lru,
                (TRSPK_ModelId)cmd->_animation_load.visual_id,
                seg,
                (uint16_t)cmd->_animation_load.frame_index,
                &arrays,
                &id_bake,
                1.0f);
        return;
    }

    const TRSPK_BakeTransform* bake = trspk_resource_cache_get_model_bake(
        ctx->cache, (TRSPK_ModelId)cmd->_animation_load.visual_id);
    TRSPK_BakeTransform id_fallback = trspk_bake_transform_identity();
    const TRSPK_BakeTransform* use_bake = bake ? bake : &id_fallback;
    trspk_d3d8_dynamic_load_anim(
        ctx->renderer,
        (TRSPK_ModelId)cmd->_animation_load.visual_id,
        cmd->_animation_load.model,
        seg,
        (uint16_t)cmd->_animation_load.frame_index,
        usage,
        use_bake);
}

void
trspk_d3d8_event_tex_load(
    TRSPK_D3D8EventContext* ctx,
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
        trspk_d3d8_cache_load_texture_128(
            ctx->renderer,
            (TRSPK_TextureId)cmd->_texture_load.texture_id,
            rgba,
            anim_u,
            anim_v,
            tex ? tex->opaque : true);
        trspk_d3d8_cache_refresh_atlas(ctx->renderer);
    }
}

void
trspk_d3d8_event_batch3d_begin(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->cache || !ctx->staging || !cmd )
        return;
    ctx->current_batch_id = cmd->_batch.batch_id;
    ctx->batch_active = true;
    trspk_batch16_begin(ctx->staging);
    if( ctx->renderer )
        ctx->staging->d3d8_vertex_frame_clock = ctx->renderer->frame_clock;
    trspk_resource_cache_batch_begin(ctx->cache, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_d3d8_event_batch3d_model_add(
    TRSPK_D3D8EventContext* ctx,
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
    trspk_dash_batch_add_model16(
        ctx->staging,
        cmd->_model_load.model,
        (uint16_t)cmd->_model_load.visual_id,
        TRSPK_GPU_ANIM_NONE_IDX,
        0,
        &bake,
        ctx->cache);
}

void
trspk_d3d8_event_batch3d_anim_add(
    TRSPK_D3D8EventContext* ctx,
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
    trspk_dash_batch_add_model16(
        ctx->staging,
        cmd->_animation_load.model,
        (uint16_t)cmd->_animation_load.visual_id,
        seg,
        (uint16_t)cmd->_animation_load.frame_index,
        bake,
        ctx->cache);
}

void
trspk_d3d8_event_batch3d_end(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !ctx->staging || !cmd )
        return;
    trspk_batch16_end(ctx->staging);
    trspk_d3d8_cache_batch_submit(
        ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id, cmd->_batch.usage_hint);
    ctx->current_batch_id = 0;
    ctx->batch_active = false;
}

void
trspk_d3d8_event_batch3d_clear(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    trspk_d3d8_cache_batch_clear(ctx->renderer, (TRSPK_BatchId)cmd->_batch.batch_id);
}

void
trspk_d3d8_event_draw_model(
    TRSPK_D3D8EventContext* ctx,
    struct GGame* game,
    const struct ToriRSRenderCommand* cmd,
    uint16_t* sorted_indices_buffer,
    uint32_t buffer_capacity)
{
    /* Legacy d3d8_old MODEL_DRAW: D3DTS_WORLD from draw_position (yaw about Y, translate xyz).
     * TRSPK applies the same transform in CPU via trspk_bake_transform_apply (see
     * trspk_bake_transform_from_yaw_translate in trspk_math.h) when building VBs; submit uses
     * identity WORLD. Algebra matches legacy row-vector world * local for XZ yaw + Y translate.
     * Legacy also per-run SetTexture / SELECTARG2 / texture-matrix scroll; TRSPK uses one atlas
     * and baked UVs (trspk_d3d8_draw_submit_3d_ex). */
    if( !ctx || !ctx->renderer || !ctx->cache || !game || !cmd )
        return;
    ctx->diag_frame_model_draw_cmds++;

    const TRSPK_UsageClass draw_usage = trspk_d3d8_usage_from_torirs(cmd->_model_draw.usage_hint);
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
                            &id_bake,
                            1.0f);
                    else
                        trspk_lru_model_cache_get_or_emplace_untextured(
                            lru,
                            layout_id,
                            seg,
                            frame_i,
                            &arrays,
                            &id_bake,
                            1.0f);
                    const TRSPK_VertexBuffer* id_mesh =
                        trspk_lru_model_cache_get(lru, layout_id, seg, frame_i);
                    if( id_mesh )
                        did_defer = trspk_d3d8_dynamic_enqueue_draw_mesh_deferred(
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
    {
        ctx->diag_frame_pose_invalid_skips++;
        return;
    }
    const uint32_t count = trspk_d3d8_prepare_sorted_indices16(
        game, cmd->_model_draw.model, cmd, sorted_indices_buffer, buffer_capacity);
    if( count != 0 )
    {
        ctx->renderer->frame_clock = (double)game->cycle;
        trspk_d3d8_draw_add_model(ctx->renderer, pose, sorted_indices_buffer, count);
        ctx->diag_frame_submitted_model_draws++;
        TRSPK_Mat4 view, proj;
        trspk_d3d8_fill_view_proj_for_pass(ctx->renderer, game, &view, &proj);
        trspk_d3d8_draw_submit_3d_ex(ctx->renderer, &view, &proj, true);
    }
}

void
trspk_d3d8_event_state_begin_3d(
    TRSPK_D3D8EventContext* ctx,
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
        rect = trspk_d3d8_full_window_logical_rect(ctx->renderer);
    }
    trspk_d3d8_draw_begin_3d(ctx->renderer, &rect);
}

void
trspk_d3d8_event_state_clear_rect(
    TRSPK_D3D8EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer || !cmd )
        return;
    TRSPK_Rect rect = {
        cmd->_clear_rect.x, cmd->_clear_rect.y, cmd->_clear_rect.w, cmd->_clear_rect.h
    };
    trspk_d3d8_draw_clear_rect(ctx->renderer, &rect);
}

void
trspk_d3d8_event_state_end_3d(
    TRSPK_D3D8EventContext* ctx,
    struct GGame* game,
    double frame_clock)
{
    if( !ctx || !ctx->renderer || !game )
        return;
    TRSPK_D3D8Renderer* const r = ctx->renderer;
    r->frame_clock = frame_clock;
    if( r->pass_state.subdraw_count > 0u )
    {
        TRSPK_Mat4 view, proj;
        trspk_d3d8_fill_view_proj_for_pass(r, game, &view, &proj);
        trspk_d3d8_draw_submit_3d_ex(r, &view, &proj, true);
    }
    else
        trspk_d3d8_pass_flush_pending_dynamic_gpu_uploads(r);
}
