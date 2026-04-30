/**
 * GFX event handlers — TRSPK-based refactor. Included by d3d8_fixed_state.cpp.
 * Atlas is the primary path; per-id binding is reachable via tex_mode == PER_ID.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d8.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
#include "tori_rs_render.h"
}

#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "../../tools/trspk_dash.h"
#include "../../tools/trspk_resource_cache.h"
#include "../../tools/trspk_lru_model_cache.h"
#include "../../tools/trspk_batch16.h"

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_state.h"
#include "d3d8_fixed_pass.h"
#include "d3d8_fixed_cache.h"
#include "trspk_d3d8_fixed.h"

#ifdef __cplusplus
#define TRSPK_D3D8_FIXED_EVT extern "C"
#else
#define TRSPK_D3D8_FIXED_EVT
#endif

#define D3D8_FIXED_EVENT_HEAD                                                   \
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(ctx->renderer);               \
    if( !p || !p->device || !ctx->platform_renderer || !ctx->game )             \
        return;                                                                  \
    IDirect3DDevice8* dev = p->device;                                          \
    struct Platform2_Win32_Renderer_D3D8* renderer = ctx->platform_renderer;    \
    struct GGame* game = ctx->game;                                             \
    const float u_clock = ctx->u_clock;                                         \
    const int vp_w = ctx->frame_vp_w;                                           \
    const int vp_h = ctx->frame_vp_h;                                           \
    (void)renderer; (void)dev; (void)u_clock; (void)vp_w; (void)vp_h; (void)p

/* === Scratch buffer for 128x128 RGBA conversion ============================ */
static uint8_t s_rgba128[TRSPK_TEXTURE_DIMENSION * TRSPK_TEXTURE_DIMENSION *
                          TRSPK_ATLAS_BYTES_PER_PIXEL];

/* === Per-instance model pose helpers ======================================== */

/** Register a per-instance model pose in the ResourceCache using a GPU VBO pointer. */
static void
d3d8_fixed_set_instance_pose(
    D3D8FixedInternal* p,
    uint16_t visual_id,
    uint8_t  seg,
    uint16_t frame_idx,
    IDirect3DVertexBuffer8* vbo,
    uint32_t vertex_count)
{
    if( !p || !p->cache || !vbo )
        return;

    const uint32_t pose_idx = trspk_resource_cache_allocate_pose_slot(
        p->cache, visual_id, (int)seg, (int)frame_idx);

    TRSPK_ModelPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.vbo           = (TRSPK_GPUHandle)(uintptr_t)vbo;
    pose.vbo_offset    = 0u;          /* per-instance: whole VBO is this model */
    pose.element_count = vertex_count; /* expanded: face_count * 3 */
    pose.batch_id      = TRSPK_SCENE_BATCH_NONE;
    pose.valid         = true;
    trspk_resource_cache_set_model_pose(p->cache, visual_id, pose_idx, &pose);
}

/** Emplace model into LRU + create GPU VBO + set ResourceCache pose. */
static void
d3d8_fixed_emplace_and_register(
    D3D8FixedInternal* p,
    IDirect3DDevice8* dev,
    struct DashModel* model,
    uint16_t visual_id,
    uint8_t  seg,
    uint16_t frame_idx)
{
    if( !p || !dev || !model || !p->cache )
        return;

    TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(p->cache);
    if( !lru )
        return;

    TRSPK_ModelArrays arrays;
    trspk_dash_fill_model_arrays(model, &arrays);
    if( arrays.face_count == 0u )
        return;

    TRSPK_BakeTransform id_bake = trspk_bake_transform_identity();
    const TRSPK_VertexBuffer* vb = nullptr;
    if( dashmodel_has_textures(model) )
        vb = trspk_lru_model_cache_get_or_emplace_textured(
            lru, visual_id, seg, frame_idx, &arrays,
            trspk_dash_uv_calculation_mode(model),
            (p->tex_mode == D3D8_FIXED_TEX_ATLAS) ? p->cache : nullptr,
            &id_bake);
    else
        vb = trspk_lru_model_cache_get_or_emplace_untextured(
            lru, visual_id, seg, frame_idx, &arrays, &id_bake);

    if( !vb )
        return;

    IDirect3DVertexBuffer8* gpu_vb =
        d3d8_fixed_lazy_ensure_lru_vbo(p, dev, visual_id, seg, frame_idx);
    if( !gpu_vb )
        return;

    d3d8_fixed_set_instance_pose(p, visual_id, seg, frame_idx, gpu_vb, vb->vertex_count);
}

/* === event_tex_load ======================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_tex_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    const int tex_id = cmd->_texture_load.texture_id;
    struct DashTexture* texture = cmd->_texture_load.texture_nullable;

    {
        const int kcmd = (int)TORIRS_GFX_RES_TEX_LOAD;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log(
                "cmd TEXTURE_LOAD tex_id=%d texture=%p",
                tex_id,
                (void*)texture);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }

    /* Forward to CPU rasterizer (dash3d). */
    if( game->sys_dash && texture && texture->texels )
        dash3d_add_texture(game->sys_dash, tex_id, texture);

    if( !texture || !texture->texels || tex_id < 0 )
        return;

    const float anim_signed =
        d3d8_texture_animation_signed(texture->animation_direction, texture->animation_speed);
    float anim_u = 0.0f, anim_v = 0.0f;
    if( anim_signed >= 0.0f )
        anim_u = anim_signed;
    else
        anim_v = -anim_signed;

    if( p->tex_mode == D3D8_FIXED_TEX_ATLAS )
    {
        /* Atlas mode: upload into ResourceCache atlas + refresh D3D texture. */
        if( p->cache )
        {
            const uint8_t* rgba = nullptr;
            uint32_t rgba_size  = 0u;
            trspk_dash_fill_rgba128(
                texture,
                s_rgba128,
                (uint32_t)sizeof(s_rgba128),
                &rgba,
                &rgba_size);
            (void)rgba_size;
            if( rgba )
                trspk_resource_cache_load_texture_128(
                    p->cache,
                    (TRSPK_TextureId)tex_id,
                    rgba,
                    anim_u,
                    anim_v,
                    texture->opaque ? true : false);
            d3d8_fixed_cache_refresh_atlas(p, dev);
        }
    }
    else
    {
        /* Per-id mode: create or replace individual D3D8 texture. */
        auto old_it = p->texture_by_id.find(tex_id);
        if( old_it != p->texture_by_id.end() && old_it->second )
        {
            old_it->second->Release();
            p->texture_by_id.erase(old_it);
        }
        p->texture_anim_speed_by_id[tex_id] = anim_signed;
        p->texture_opaque_by_id[tex_id]     = texture->opaque ? true : false;

        IDirect3DTexture8* dt = nullptr;
        HRESULT htex = dev->CreateTexture(
            (UINT)texture->width, (UINT)texture->height,
            1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &dt);
        if( FAILED(htex) || !dt )
        {
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD: CreateTexture failed hr=0x%08lX tex_id=%d",
                (unsigned long)htex, tex_id);
            return;
        }
        D3DLOCKED_RECT lr;
        if( FAILED(dt->LockRect(0, &lr, nullptr, 0)) )
        {
            dt->Release();
            return;
        }
        for( int row = 0; row < texture->height; ++row )
            memcpy(
                (unsigned char*)lr.pBits + row * lr.Pitch,
                texture->texels + row * texture->width,
                (size_t)texture->width * 4u);
        dt->UnlockRect(0);
        p->texture_by_id[tex_id] = dt;
    }
}

/* === event_res_model_load ================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_res_model_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashModel* model  = cmd->_model_load.model;
    const int         vid    = cmd->_model_load.visual_id;

    {
        const int kcmd = (int)TORIRS_GFX_RES_MODEL_LOAD;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log(
                "cmd MODEL_LOAD visual_id=%d model=%p face_count=%d",
                vid, (void*)model,
                model ? dashmodel_face_count(model) : -1);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }

    if( !model || vid <= 0 || !p->cache )
        return;
    if( dashmodel_face_count(model) <= 0 )
        return;

    d3d8_fixed_emplace_and_register(
        p, dev, model, (uint16_t)vid, TRSPK_GPU_ANIM_NONE_IDX, 0u);
}

/* === event_res_model_unload ================================================ */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_res_model_unload(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    const int vid = cmd->_model_load.visual_id;

    {
        const int kcmd = (int)TORIRS_GFX_RES_MODEL_UNLOAD;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log("cmd MODEL_UNLOAD visual_id=%d", vid);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }

    if( vid <= 0 )
        return;

    /* Release all GPU VBOs for this model (all segments and frame indices). */
    for( auto it = p->lru_vbo_by_key.begin(); it != p->lru_vbo_by_key.end(); )
    {
        /* The visual_id occupies bits 39..24 of the key. */
        const uint16_t key_vid = (uint16_t)((it->first >> 24) & 0xFFFFu);
        if( key_vid == (uint16_t)vid )
        {
            if( it->second )
                it->second->Release();
            it = p->lru_vbo_by_key.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if( p->cache )
    {
        TRSPK_LruModelCache* lru = trspk_resource_cache_lru_model_cache(p->cache);
        if( lru )
            trspk_lru_model_cache_evict_model_id(lru, (TRSPK_ModelId)vid);
        trspk_resource_cache_clear_model(p->cache, (TRSPK_ModelId)vid);
    }
}

/* === event_batch3d_begin =================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_begin(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    const uint32_t bid = cmd->_batch.batch_id;

    if( !p->cache || !p->batch_staging )
        return;

    trspk_batch16_begin(p->batch_staging);
    p->batch_staging->d3d8_vertex_frame_clock = p->frame_clock;
    trspk_resource_cache_batch_begin(p->cache, (TRSPK_BatchId)bid);

    p->current_batch_id = bid;
    p->batch_active     = true;
}

/* === event_batch3d_model_add =============================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_model_add(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashModel* model = cmd->_model_load.model;
    const int         vid   = cmd->_model_load.visual_id;

    if( !p->batch_active || !p->cache || !p->batch_staging || !model || vid <= 0 )
        return;
    if( dashmodel_face_count(model) <= 0 )
        return;

    /* Use identity bake: vertices stay in model-local space.
     * The world matrix at draw time provides camera-relative position + yaw. */
    TRSPK_BakeTransform id_bake = trspk_bake_transform_identity();
    trspk_resource_cache_set_model_bake(p->cache, (TRSPK_ModelId)vid, &id_bake);
    trspk_dash_batch_add_model16(
        p->batch_staging, model, (uint16_t)vid,
        TRSPK_GPU_ANIM_NONE_IDX, 0u,
        &id_bake, p->cache);
}

/* === event_batch3d_end ===================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_end(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    const uint32_t bid = cmd->_batch.batch_id;

    if( !p->batch_staging || bid != p->current_batch_id )
        return;

    trspk_batch16_end(p->batch_staging);
    d3d8_fixed_cache_batch_submit(p, dev, bid);

    p->batch_active = false;
}

/* === event_batch3d_clear =================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_clear(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    const uint32_t bid = cmd->_batch.batch_id;
    d3d8_fixed_cache_batch_clear(p, bid);
}

/* === event_res_anim_load =================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_res_anim_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashModel* model = cmd->_animation_load.model;
    const int         vid   = cmd->_animation_load.visual_id;

    if( !model || vid <= 0 || !p->cache )
        return;

    /* Skip scenery — scenery animations go through event_batch3d_anim_add. */
    if( cmd->_animation_load.usage_hint == TORIRS_USAGE_SCENERY )
        return;

    /* Animate the model into its posed state. */
    dashmodel_animate(
        model,
        cmd->_animation_load.frame,
        cmd->_animation_load.framemap);

    const uint8_t seg = cmd->_animation_load.animation_index == 1
                            ? TRSPK_GPU_ANIM_SECONDARY_IDX
                            : TRSPK_GPU_ANIM_PRIMARY_IDX;
    const uint16_t frame_idx = (uint16_t)cmd->_animation_load.frame_index;

    d3d8_fixed_emplace_and_register(p, dev, model, (uint16_t)vid, seg, frame_idx);
}

/* === event_batch3d_anim_add ================================================ */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_anim_add(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashModel* model = cmd->_animation_load.model;
    const int         vid   = cmd->_animation_load.visual_id;

    if( !p->batch_active || !p->cache || !p->batch_staging || !model || vid <= 0 )
        return;

    dashmodel_animate(
        model,
        cmd->_animation_load.frame,
        cmd->_animation_load.framemap);

    const uint8_t  seg       = cmd->_animation_load.animation_index == 1
                                   ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                   : TRSPK_GPU_ANIM_PRIMARY_IDX;
    const uint16_t frame_idx = (uint16_t)cmd->_animation_load.frame_index;

    /* Reuse the identity bake that was set during BATCH3D_MODEL_ADD. */
    const TRSPK_BakeTransform* bake =
        trspk_resource_cache_get_model_bake(p->cache, (TRSPK_ModelId)vid);
    TRSPK_BakeTransform id_bake = trspk_bake_transform_identity();
    const TRSPK_BakeTransform* use_bake = bake ? bake : &id_bake;

    trspk_dash_batch_add_model16(
        p->batch_staging, model, (uint16_t)vid,
        seg, frame_idx,
        use_bake, p->cache);
}

/* === event_draw_model ====================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_draw_model(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashModel* model = cmd->_model_draw.model;
    if( !model || !game->sys_dash || !game->view_port || !game->camera )
        return;
    if( !p->cache )
        return;

    const int vid = cmd->_model_draw.visual_id;
    if( vid <= 0 )
        return;

    /* Determine animation segment + frame. */
    uint8_t  seg      = TRSPK_GPU_ANIM_NONE_IDX;
    uint16_t frame_i  = 0u;
    if( cmd->_model_draw.use_animation )
    {
        seg     = cmd->_model_draw.animation_index == 1 ? TRSPK_GPU_ANIM_SECONDARY_IDX
                                                        : TRSPK_GPU_ANIM_PRIMARY_IDX;
        frame_i = (uint16_t)cmd->_model_draw.frame_index;
    }

    /* Look up GPU pose from ResourceCache (covers both batch and per-instance). */
    const TRSPK_ModelPose* pose = trspk_resource_cache_get_pose_for_instance_draw(
        p->cache,
        (TRSPK_ModelId)vid,
        (TRSPK_ModelId)vid,
        cmd->_model_draw.use_animation,
        (int)cmd->_model_draw.animation_index,
        (int)cmd->_model_draw.frame_index);

    if( !pose || !pose->valid || pose->vbo == 0u )
    {
        /* Lazy load: animate if needed, emplace into LRU, create GPU VBO, set pose. */
        if( cmd->_model_draw.use_animation )
            dashmodel_animate(
                model,
                cmd->_model_draw.animation_frame,
                cmd->_model_draw.animation_framemap);

        d3d8_fixed_emplace_and_register(p, dev, model, (uint16_t)vid, seg, frame_i);

        pose = trspk_resource_cache_get_pose_for_instance_draw(
            p->cache,
            (TRSPK_ModelId)vid,
            (TRSPK_ModelId)vid,
            cmd->_model_draw.use_animation,
            (int)cmd->_model_draw.animation_index,
            (int)cmd->_model_draw.frame_index);
        if( !pose || !pose->valid || pose->vbo == 0u )
            return;
    }

    IDirect3DVertexBuffer8* vbo =
        reinterpret_cast<IDirect3DVertexBuffer8*>((uintptr_t)pose->vbo);
    const UINT vbo_offset = pose->vbo_offset; /* BaseVertexIndex */
    const int face_count  = (int)(pose->element_count / 3u);
    if( !vbo || face_count <= 0 )
        return;

    /* --- Depth-sort face order. ------------------------------------------ */
    struct DashPosition draw_position = cmd->_model_draw.position;
    const int face_order_count = dash3d_prepare_projected_face_order(
        game->sys_dash, model, &draw_position, game->view_port, game->camera);
    const int* face_order =
        dash3d_projected_face_order(game->sys_dash, (int*)&face_order_count);
    if( face_order_count <= 0 || !face_order )
        return;

    {
        const int kcmd = (int)TORIRS_GFX_DRAW_MODEL;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log(
                "cmd MODEL_DRAW vid=%d pos=(%d,%d,%d) yaw=%d faces=%d ordered=%d",
                vid,
                draw_position.x, draw_position.y, draw_position.z,
                (int)draw_position.yaw,
                face_count,
                face_order_count);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }

    /* --- Build world matrix (camera-relative position + yaw rotation). ---- */
    const float yaw_rad = (draw_position.yaw * 2.0f * (float)M_PI) / 2048.0f;
    const float cos_yaw = cosf(yaw_rad);
    const float sin_yaw = sinf(yaw_rad);

    D3DMATRIX world;
    memset(&world, 0, sizeof(world));
    world._11 =  cos_yaw; world._12 = 0.0f; world._13 = -sin_yaw; world._14 = 0.0f;
    world._21 =  0.0f;    world._22 = 1.0f; world._23 =  0.0f;    world._24 = 0.0f;
    world._31 =  sin_yaw; world._32 = 0.0f; world._33 =  cos_yaw; world._34 = 0.0f;
    world._41 = (float)draw_position.x;
    world._42 = (float)draw_position.y;
    world._43 = (float)draw_position.z;
    world._44 = 1.0f;

    /* Dedup world matrix. */
    uint16_t world_idx = (uint16_t)p->pass_state.worlds.size();
    for( size_t wi = 0; wi < p->pass_state.worlds.size(); ++wi )
    {
        if( memcmp(&p->pass_state.worlds[wi], &world, sizeof(D3DMATRIX)) == 0 )
        {
            world_idx = (uint16_t)wi;
            goto world_found;
        }
    }
    p->pass_state.worlds.push_back(world);
world_found:;

    /* --- Stage face indices into ib_scratch. ----------------------------- */

    if( p->tex_mode == D3D8_FIXED_TEX_ATLAS )
    {
        /* Atlas mode: one subdraw covers all sorted faces of this model. */
        const UINT pool_start = (UINT)p->pass_state.ib_scratch.size();
        for( int fi = 0; fi < face_order_count; ++fi )
        {
            const int f = face_order[fi];
            if( f < 0 || f >= face_count )
                continue;
            p->pass_state.ib_scratch.push_back((uint16_t)(f * 3 + 0));
            p->pass_state.ib_scratch.push_back((uint16_t)(f * 3 + 1));
            p->pass_state.ib_scratch.push_back((uint16_t)(f * 3 + 2));
        }
        const UINT pool_end = (UINT)p->pass_state.ib_scratch.size();
        if( pool_end <= pool_start )
            return;

        D3D8FixedSubDraw sd{};
        sd.vbo                 = vbo;
        sd.vbo_offset_vertices = vbo_offset;
        sd.world_idx           = world_idx;
        sd.tex_id              = -1;
        sd.opaque              = true;
        sd.anim_signed         = 0.0f;
        sd.pool_start_indices  = pool_start;
        sd.index_count         = pool_end - pool_start;
        p->pass_state.subdraws.push_back(sd);
    }
    else
    {
        /* Per-id mode: split into texture runs, one subdraw per run. */
        const faceint_t* ftex = dashmodel_face_textures_const(model);

        auto eff_tex = [&](int f) -> int {
            if( !ftex || f < 0 || f >= face_count )
                return -1;
            int raw = (int)ftex[f];
            if( raw < 0 )
                return -1;
            if( p->texture_by_id.find(raw) == p->texture_by_id.end() )
                return -1;
            return raw;
        };

        int run_start = 0;
        int run_tex   = eff_tex(face_order[0]);

        for( int i = 1; i <= face_order_count; ++i )
        {
            const int t = (i < face_order_count) ? eff_tex(face_order[i]) : INT_MIN;
            if( i < face_order_count && t == run_tex )
                continue;

            const UINT pool_start = (UINT)p->pass_state.ib_scratch.size();
            for( int k = run_start; k < i; ++k )
            {
                const int f = face_order[k];
                if( f >= 0 && f < face_count )
                {
                    p->pass_state.ib_scratch.push_back((uint16_t)(f * 3 + 0));
                    p->pass_state.ib_scratch.push_back((uint16_t)(f * 3 + 1));
                    p->pass_state.ib_scratch.push_back((uint16_t)(f * 3 + 2));
                }
            }
            const UINT pool_end = (UINT)p->pass_state.ib_scratch.size();

            if( pool_end > pool_start )
            {
                bool    opaque     = true;
                float   anim_sgn   = 0.0f;
                if( run_tex >= 0 )
                {
                    auto oi = p->texture_opaque_by_id.find(run_tex);
                    if( oi != p->texture_opaque_by_id.end() )
                        opaque = oi->second;
                    auto ai = p->texture_anim_speed_by_id.find(run_tex);
                    if( ai != p->texture_anim_speed_by_id.end() )
                        anim_sgn = ai->second;
                }

                D3D8FixedSubDraw sd{};
                sd.vbo                 = vbo;
                sd.vbo_offset_vertices = vbo_offset;
                sd.world_idx           = world_idx;
                sd.tex_id              = run_tex;
                sd.opaque              = opaque;
                sd.anim_signed         = anim_sgn;
                sd.pool_start_indices  = pool_start;
                sd.index_count         = pool_end - pool_start;
                p->pass_state.subdraws.push_back(sd);
            }

            run_start = i;
            run_tex   = t;
        }
    }
}

/* === event_state_begin_3d ================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_begin_3d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
    {
        const int kcmd = (int)cmd->kind;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log("cmd STATE_BEGIN_3D");
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }
    /* Reset pass buffers (discard any stale subdraws from a previous 3D pass). */
    d3d8_fixed_reset_pass(p);
}

/* === event_state_end_3d ==================================================== */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_end_3d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
    {
        const int kcmd = (int)cmd->kind;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log(
                "cmd STATE_END_3D subdraws=%zu",
                p->pass_state.subdraws.size());
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }
    d3d8_fixed_submit_pass(p, dev);
}

/* === Sprite / font load (unchanged, per-id) ================================ */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_sprite_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashSprite* sp = cmd->_sprite_load.sprite;
    {
        const int kcmd = (int)TORIRS_GFX_RES_SPRITE_LOAD;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log(
                "cmd SPRITE_LOAD element_id=%d atlas_index=%d sp=%p",
                cmd->_sprite_load.element_id,
                cmd->_sprite_load.atlas_index,
                (void*)sp);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }
    if( !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
        return;

    uint64_t sk = torirs_sprite_cache_key(
        cmd->_sprite_load.element_id, cmd->_sprite_load.atlas_index);
    auto it = p->sprite_by_slot.find(sk);
    if( it != p->sprite_by_slot.end() && it->second )
    {
        it->second->Release();
        p->sprite_by_slot.erase(it);
    }

    IDirect3DTexture8* st = nullptr;
    HRESULT hsp = dev->CreateTexture(
        (UINT)sp->width, (UINT)sp->height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &st);
    if( FAILED(hsp) || !st )
        return;

    D3DLOCKED_RECT lr;
    if( FAILED(st->LockRect(0, &lr, nullptr, 0)) )
    {
        st->Release();
        return;
    }
    for( int py = 0; py < sp->height; ++py )
    {
        uint32_t* dst = (uint32_t*)((unsigned char*)lr.pBits + py * lr.Pitch);
        for( int px = 0; px < sp->width; ++px )
        {
            uint32_t pix  = (uint32_t)sp->pixels_argb[py * sp->width + px];
            uint8_t  a_hi = (uint8_t)((pix >> 24) & 0xFFu);
            uint32_t rgb  = pix & 0x00FFFFFFu;
            uint8_t  a    = (a_hi != 0) ? a_hi : (rgb != 0u ? 0xFFu : 0u);
            dst[px] = (pix & 0x00FFFFFFu) | ((uint32_t)a << 24);
        }
    }
    st->UnlockRect(0);
    p->sprite_by_slot[sk]      = st;
    p->sprite_size_by_slot[sk] = std::make_pair(sp->width, sp->height);
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_sprite_unload(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
    {
        const int kcmd = (int)TORIRS_GFX_RES_SPRITE_UNLOAD;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log(
                "cmd SPRITE_UNLOAD element_id=%d atlas_index=%d",
                cmd->_sprite_load.element_id,
                cmd->_sprite_load.atlas_index);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }
    uint64_t sk = torirs_sprite_cache_key(
        cmd->_sprite_load.element_id, cmd->_sprite_load.atlas_index);
    auto it = p->sprite_by_slot.find(sk);
    if( it != p->sprite_by_slot.end() )
    {
        if( it->second )
            it->second->Release();
        p->sprite_by_slot.erase(it);
    }
    p->sprite_size_by_slot.erase(sk);
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_font_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashPixFont* f  = cmd->_font_load.font;
    int                 fid = cmd->_font_load.font_id;
    {
        const int kcmd = (int)TORIRS_GFX_RES_FONT_LOAD;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log("cmd FONT_LOAD font_id=%d", fid);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }
    if( !f || !f->atlas || p->font_atlas_by_id.count(fid) )
        return;

    struct DashFontAtlas* a = f->atlas;
    IDirect3DTexture8*    at = nullptr;
    HRESULT hfa = dev->CreateTexture(
        (UINT)a->atlas_width, (UINT)a->atlas_height, 1, 0,
        D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &at);
    if( FAILED(hfa) || !at )
        return;

    D3DLOCKED_RECT lr;
    if( FAILED(at->LockRect(0, &lr, nullptr, 0)) )
    {
        at->Release();
        return;
    }
    for( int row = 0; row < a->atlas_height; ++row )
        memcpy(
            (unsigned char*)lr.pBits + row * lr.Pitch,
            a->rgba_pixels + row * a->atlas_width,
            (size_t)a->atlas_width * 4u);
    at->UnlockRect(0);
    p->font_atlas_by_id[fid] = at;
}

/* === 2D draw events (flush 3D pass + font before switching) ================ */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_clear_rect(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    p->flush_3d_pass_if_needed();
    p->flush_font();

    const int cx = cmd->_clear_rect.x;
    const int cy = cmd->_clear_rect.y;
    const int cw = cmd->_clear_rect.w;
    const int ch = cmd->_clear_rect.h;
    {
        const int kcmd = (int)TORIRS_GFX_STATE_CLEAR_RECT;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log("cmd CLEAR_RECT %d,%d %dx%d", cx, cy, cw, ch);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }
    if( cw <= 0 || ch <= 0 )
        return;

    D3DVIEWPORT8 saved;
    dev->GetViewport(&saved);
    D3DVIEWPORT8 cvp = { (DWORD)cx, (DWORD)cy, (DWORD)cw, (DWORD)ch, 0.0f, 1.0f };
    if( (int)cvp.X + (int)cvp.Width  > renderer->width )
        cvp.Width  = (DWORD)(renderer->width  - (int)cvp.X);
    if( (int)cvp.Y + (int)cvp.Height > renderer->height )
        cvp.Height = (DWORD)(renderer->height - (int)cvp.Y);
    dev->SetViewport(&cvp);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0);
    dev->SetViewport(&saved);
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_draw_sprite(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    {
        const int kcmd = (int)TORIRS_GFX_DRAW_SPRITE;
        if( d3d8_fixed_should_log_cmd(p, kcmd) )
        {
            trspk_d3d8_fixed_log(
                "cmd SPRITE_DRAW element=%d atlas=%d dst=%d,%d %dx%d",
                cmd->_sprite_draw.element_id,
                cmd->_sprite_draw.atlas_index,
                cmd->_sprite_draw.dst_bb_x,
                cmd->_sprite_draw.dst_bb_y,
                cmd->_sprite_draw.dst_bb_w,
                cmd->_sprite_draw.dst_bb_h);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }

    struct DashSprite* sp = cmd->_sprite_draw.sprite;
    if( !sp )
        return;

    uint64_t sk = torirs_sprite_cache_key(
        cmd->_sprite_draw.element_id, cmd->_sprite_draw.atlas_index);
    auto tex_it = p->sprite_by_slot.find(sk);
    if( tex_it == p->sprite_by_slot.end() || !tex_it->second )
        return;

    const int iw = cmd->_sprite_draw.src_bb_w > 0 ? cmd->_sprite_draw.src_bb_w : sp->width;
    const int ih = cmd->_sprite_draw.src_bb_h > 0 ? cmd->_sprite_draw.src_bb_h : sp->height;
    const int ix = cmd->_sprite_draw.src_bb_x;
    const int iy = cmd->_sprite_draw.src_bb_y;
    if( ix < 0 || iy < 0 || ix + iw > sp->width || iy + ih > sp->height )
        return;

    int tw = sp->width, th = sp->height;
    auto sz_it = p->sprite_size_by_slot.find(sk);
    if( sz_it != p->sprite_size_by_slot.end() )
    {
        tw = sz_it->second.first;
        th = sz_it->second.second;
    }

    p->flush_3d_pass_if_needed();
    p->flush_font();
    d3d8_fixed_ensure_pass(p, dev, PASS_2D);
    dev->SetTexture(0, tex_it->second);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);

    float px[4], py[4];
    D3DVIEWPORT8 saved_vp   = {};
    bool sprite_vp_override = false;

    if( cmd->_sprite_draw.rotated )
    {
        const int dw = cmd->_sprite_draw.dst_bb_w;
        const int dh = cmd->_sprite_draw.dst_bb_h;
        if( dw <= 0 || dh <= 0 || iw <= 0 || ih <= 0 )
            return;

        D3DVIEWPORT8 spriteVP = {
            (DWORD)(cmd->_sprite_draw.dst_bb_x > 0 ? cmd->_sprite_draw.dst_bb_x : 0),
            (DWORD)(cmd->_sprite_draw.dst_bb_y > 0 ? cmd->_sprite_draw.dst_bb_y : 0),
            (DWORD)dw, (DWORD)dh, 0.0f, 1.0f
        };
        if( (int)spriteVP.X + (int)spriteVP.Width  > renderer->width )
            spriteVP.Width  = (DWORD)(renderer->width  - (int)spriteVP.X);
        if( (int)spriteVP.Y + (int)spriteVP.Height > renderer->height )
            spriteVP.Height = (DWORD)(renderer->height - (int)spriteVP.Y);

        if( (int)spriteVP.Width > 0 && (int)spriteVP.Height > 0 )
        {
            dev->GetViewport(&saved_vp);
            dev->SetViewport(&spriteVP);
            sprite_vp_override = true;
        }

        const float pivot_x =
            (float)cmd->_sprite_draw.dst_bb_x + (float)cmd->_sprite_draw.dst_anchor_x;
        const float pivot_y =
            (float)cmd->_sprite_draw.dst_bb_y + (float)cmd->_sprite_draw.dst_anchor_y;
        const int ang    = cmd->_sprite_draw.rotation_r2pi2048 & 2047;
        const float ang_f = (float)ang * (float)(2.0 * M_PI / 2048.0);
        const float ca = cosf(ang_f), sa = sinf(ang_f);
        const int   sax = cmd->_sprite_draw.src_anchor_x;
        const int   say = cmd->_sprite_draw.src_anchor_y;

        struct { int lx, ly; } corners[4] = { {0,0}, {iw,0}, {iw,ih}, {0,ih} };
        for( int k = 0; k < 4; ++k )
        {
            float Lx = (float)(corners[k].lx - sax);
            float Ly = (float)(corners[k].ly - say);
            px[k] = pivot_x + ca * Lx - sa * Ly;
            py[k] = pivot_y + sa * Lx + ca * Ly;
        }
    }
    else
    {
        const float x0 = (float)(cmd->_sprite_draw.dst_bb_x + sp->crop_x);
        const float y0 = (float)(cmd->_sprite_draw.dst_bb_y + sp->crop_y);
        px[0] = px[3] = x0;
        px[1] = px[2] = x0 + (float)iw;
        py[0] = py[1] = y0;
        py[2] = py[3] = y0 + (float)ih;
    }

    const float twf = (float)tw, thf = (float)th;
    const float u0 = (float)ix / twf, v0 = (float)iy / thf;
    const float u1 = (float)(ix + iw) / twf, v1 = (float)(iy + ih) / thf;
    DWORD white = 0xFFFFFFFF;

    D3D8UiVertex v[6] = {
        { px[0], py[0], 0.0f, 1.0f, white, u0, v0 },
        { px[1], py[1], 0.0f, 1.0f, white, u1, v0 },
        { px[2], py[2], 0.0f, 1.0f, white, u1, v1 },
        { px[0], py[0], 0.0f, 1.0f, white, u0, v0 },
        { px[2], py[2], 0.0f, 1.0f, white, u1, v1 },
        { px[3], py[3], 0.0f, 1.0f, white, u0, v1 },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, v, sizeof(D3D8UiVertex));
    if( sprite_vp_override )
        dev->SetViewport(&saved_vp);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_draw_font(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;

    struct DashPixFont* f   = cmd->_font_draw.font;
    const uint8_t*      text = cmd->_font_draw.text;
    {
        const int kcmd = (int)TORIRS_GFX_DRAW_FONT;
        if( d3d8_fixed_should_log_cmd(p, kcmd) && text )
        {
            char preview[32];
            int pi = 0;
            for( int ci = 0; ci < 20 && text[ci] && pi < (int)sizeof(preview) - 1; ++ci )
            {
                unsigned char c = text[ci];
                if( c >= 32u && c < 127u )
                    preview[pi++] = (char)c;
            }
            preview[pi] = '\0';
            trspk_d3d8_fixed_log(
                "cmd FONT_DRAW font_id=%d xy=%d,%d \"%s\"",
                cmd->_font_draw.font_id,
                cmd->_font_draw.x,
                cmd->_font_draw.y,
                preview);
            d3d8_fixed_mark_cmd_logged(p, kcmd);
        }
    }
    if( !f || !f->atlas || !text || f->height2d <= 0 )
        return;

    const int fid = cmd->_font_draw.font_id;
    if( p->font_atlas_by_id.find(fid) == p->font_atlas_by_id.end() )
        return;

    /* Flush 3D pass before first font draw to maintain ordering. */
    p->flush_3d_pass_if_needed();

    if( p->current_font_id != fid )
    {
        p->flush_font();
        p->current_font_id = fid;
    }

    struct DashFontAtlas* atlas = f->atlas;
    const float inv_aw = 1.0f / (float)atlas->atlas_width;
    const float inv_ah = 1.0f / (float)atlas->atlas_height;

    const int length    = (int)strlen((const char*)text);
    int color_rgb = cmd->_font_draw.color_rgb;
    float cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
    float cg = (float)((color_rgb >>  8) & 0xFF) / 255.0f;
    float cb = (float)( color_rgb        & 0xFF) / 255.0f;
    float ca = 1.0f;
    int pen_x  = cmd->_font_draw.x;
    int base_y = cmd->_font_draw.y;

    for( int i = 0; i < length; i++ )
    {
        if( text[i] == '@' && i + 5 <= length && text[i + 4] == '@' )
        {
            int new_color = dashfont_evaluate_color_tag((const char*)&text[i + 1]);
            if( new_color >= 0 )
            {
                color_rgb = new_color;
                cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
                cg = (float)((color_rgb >>  8) & 0xFF) / 255.0f;
                cb = (float)( color_rgb        & 0xFF) / 255.0f;
            }
            i += (i + 6 <= length && text[i + 5] == ' ') ? 5 : 4;
            continue;
        }

        int c = dashfont_charcode_to_glyph(text[i]);
        if( c < DASH_FONT_CHAR_COUNT )
        {
            int gw = atlas->glyph_w[c];
            int gh = atlas->glyph_h[c];
            if( gw > 0 && gh > 0 )
            {
                float sx0 = (float)(pen_x + f->char_offset_x[c]);
                float sy0 = (float)(base_y + f->char_offset_y[c]);
                float sx1 = sx0 + (float)gw;
                float sy1 = sy0 + (float)gh;
                float gu0 = (float)atlas->glyph_x[c] * inv_aw;
                float gv0 = (float)atlas->glyph_y[c] * inv_ah;
                float gu1 = (float)(atlas->glyph_x[c] + gw) * inv_aw;
                float gv1 = (float)(atlas->glyph_y[c] + gh) * inv_ah;
                DWORD diff = ((DWORD)(ca*255.f) << 24) | ((DWORD)(cr*255.f) << 16) |
                             ((DWORD)(cg*255.f) <<  8) |  (DWORD)(cb*255.f);
                D3D8UiVertex q[6] = {
                    { sx0, sy0, 0, 1, diff, gu0, gv0 },
                    { sx1, sy0, 0, 1, diff, gu1, gv0 },
                    { sx1, sy1, 0, 1, diff, gu1, gv1 },
                    { sx0, sy0, 0, 1, diff, gu0, gv0 },
                    { sx1, sy1, 0, 1, diff, gu1, gv1 },
                    { sx0, sy1, 0, 1, diff, gu0, gv1 },
                };
                p->pending_font_verts.insert(p->pending_font_verts.end(), q, q + 6);
            }
            int adv = f->char_advance[c];
            pen_x += (adv > 0) ? adv : 4;
        }
        else
        {
            pen_x += 4;
        }
    }
}

/* === State transitions (no-op except the begin/end 3D handlers) ============ */

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_begin_2d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
    const int kcmd = (int)cmd->kind;
    if( d3d8_fixed_should_log_cmd(p, kcmd) )
    {
        trspk_d3d8_fixed_log("cmd STATE_BEGIN_2D");
        d3d8_fixed_mark_cmd_logged(p, kcmd);
    }
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_end_2d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
    const int kcmd = (int)cmd->kind;
    if( d3d8_fixed_should_log_cmd(p, kcmd) )
    {
        trspk_d3d8_fixed_log("cmd STATE_END_2D");
        d3d8_fixed_mark_cmd_logged(p, kcmd);
    }
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_none_or_default(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
    const int kcmd = (int)cmd->kind;
    if( d3d8_fixed_should_log_cmd(p, kcmd) )
    {
        trspk_d3d8_fixed_log("cmd kind=%d (no-op)", kcmd);
        d3d8_fixed_mark_cmd_logged(p, kcmd);
    }
}
