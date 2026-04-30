/**
 * GFX event handlers — bodies from d3d8_old Render(); included by d3d8_fixed_state.cpp.
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

extern "C" {
#include "graphics/dash.h"
#include "graphics/dash_model_internal.h"
#include "graphics/shared_tables.h"
#include "graphics/uv_pnm.h"
#include "tori_rs_render.h"
}

#include "d3d8_fixed_internal.h"
#include "d3d8_fixed_state.h"
#include "trspk_d3d8_fixed.h"

#ifdef __cplusplus
#define TRSPK_D3D8_FIXED_EVT extern "C"
#else
#define TRSPK_D3D8_FIXED_EVT
#endif

#define D3D8_FIXED_EVENT_HEAD                                                                                           \
    D3D8FixedInternal* p = trspk_d3d8_fixed_priv(ctx->renderer);                                                        \
    if( !p || !p->device || !ctx->platform_renderer || !ctx->game )                                                     \
        return;                                                                                                         \
    IDirect3DDevice8* dev = p->device;                                                                                  \
    struct Platform2_Win32_Renderer_D3D8* renderer = ctx->platform_renderer;                                            \
    struct GGame* game = ctx->game;                                                                                     \
    const float u_clock = ctx->u_clock;                                                                                 \
    const int vp_w = ctx->frame_vp_w;                                                                                   \
    const int vp_h = ctx->frame_vp_h;                                                                                   \
    (void)renderer;                                                                                                     \
    (void)dev;                                                                                                          \
    (void)p

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_tex_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        int tex_id = cmd->_texture_load.texture_id;
        struct DashTexture* texture = cmd->_texture_load.texture_nullable;
        {
            const int kcmd = (int)TORIRS_GFX_RES_TEX_LOAD;
            if( d3d8_fixed_should_log_cmd(p, kcmd) )
            {
                if( texture && texture->texels )
                    trspk_d3d8_fixed_log(
                        "cmd TEXTURE_LOAD tex_id=%d %dx%d opaque=%d",
                        tex_id,
                        texture->width,
                        texture->height,
                        texture->opaque ? 1 : 0);
                else
                    trspk_d3d8_fixed_log(
                        "cmd TEXTURE_LOAD tex_id=%d (skip: null or no texels)",
                        tex_id);
                d3d8_fixed_mark_cmd_logged(p, kcmd);
            }
        }
        if( texture && texture->texels && false )
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD[%d]: w=%d h=%d stride_hint=%d texels=%p",
                tex_id,
                texture->width,
                texture->height,
                texture->width * 4,
                (void*)texture->texels);
        if( game->sys_dash && texture && texture->texels )
        {
            if( false )
                trspk_d3d8_fixed_log(
                    "TEXTURE_LOAD[%d]: dash3d_add_texture begin sys_dash=%p texels=%p",
                    tex_id,
                    (void*)game->sys_dash,
                    (void*)texture->texels);
            dash3d_add_texture(game->sys_dash, tex_id, texture);
            if( false )
                trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: dash3d_add_texture done", tex_id);
        }
        if( !texture || !texture->texels )
    return;
        auto old_it = p->texture_by_id.find(tex_id);
        if( false )
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD[%d]: evicting old=%p",
                tex_id,
                (void*)(old_it != p->texture_by_id.end() ? old_it->second : nullptr));
        if( old_it != p->texture_by_id.end() && old_it->second )
            old_it->second->Release();
        if( false )
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD[%d]: read anim tex=%p dir=%d speed=%d opaque=%d",
                tex_id,
                (void*)texture,
                texture->animation_direction,
                texture->animation_speed,
                texture->opaque);
        float anim_signed =
            d3d8_texture_animation_signed(texture->animation_direction, texture->animation_speed);
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: anim_signed=%.4f, inserting anim_speed map", tex_id, anim_signed);
        p->texture_anim_speed_by_id[tex_id] = anim_signed;
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: inserting opaque map", tex_id);
        p->texture_opaque_by_id[tex_id] = texture->opaque ? true : false;
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: maps updated", tex_id);
    
        IDirect3DTexture8* dt = nullptr;
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: CreateTexture call", tex_id);
        HRESULT htex = dev->CreateTexture(
            (UINT)texture->width,
            (UINT)texture->height,
            1,
            0,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &dt);
        if( htex != D3D_OK || !dt )
        {
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD CreateTexture failed hr=0x%08lX tex_id=%d size=%dx%d",
                (unsigned long)htex,
                tex_id,
                texture->width,
                texture->height);
    return;
        }
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: CreateTexture ok tex=%p", tex_id, (void*)dt);
        D3DLOCKED_RECT lr;
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: LockRect call", tex_id);
        HRESULT hlk = dt->LockRect(0, &lr, nullptr, 0);
        if( hlk != D3D_OK )
        {
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD LockRect failed hr=0x%08lX tex_id=%d",
                (unsigned long)hlk,
                tex_id);
            dt->Release();
    return;
        }
        if( false )
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD[%d]: LockRect ok pBits=%p Pitch=%ld",
                tex_id,
                lr.pBits,
                (long)lr.Pitch);
        if( false )
            trspk_d3d8_fixed_log(
                "TEXTURE_LOAD[%d]: memcpy %dx%d pitch=%ld bytes_per_row=%zu",
                tex_id,
                texture->width,
                texture->height,
                (long)lr.Pitch,
                (size_t)texture->width * 4u);
        for( int row = 0; row < texture->height; ++row )
            memcpy(
                (unsigned char*)lr.pBits + row * lr.Pitch,
                texture->texels + row * texture->width,
                (size_t)texture->width * 4u);
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: memcpy done", tex_id);
        dt->UnlockRect(0);
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: UnlockRect done", tex_id);
        p->texture_by_id[tex_id] = dt;
        if( false )
            trspk_d3d8_fixed_log("TEXTURE_LOAD[%d]: cached tex=%p", tex_id, (void*)dt);
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_res_model_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        struct DashModel* model = cmd->_model_load.model;
        uint64_t mk = cmd->_model_load.model_key;
        {
            const int kcmd = (int)TORIRS_GFX_RES_MODEL_LOAD;
            if( d3d8_fixed_should_log_cmd(p, kcmd) )
            {
                int fc = model ? dashmodel_face_count(model) : -1;
                trspk_d3d8_fixed_log(
                    "cmd MODEL_LOAD key=0x%016llX model=%p face_count=%d",
                    (unsigned long long)mk,
                    (void*)model,
                    fc);
                d3d8_fixed_mark_cmd_logged(p, kcmd);
            }
        }
        if( model && p->model_gpu_by_key.find(mk) == p->model_gpu_by_key.end() )
            d3d8_build_model_gpu(p, dev, model, mk);
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_res_model_unload(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        int mid = cmd->_model_load.visual_id;
        {
            const int kcmd = (int)TORIRS_GFX_RES_MODEL_UNLOAD;
            if( d3d8_fixed_should_log_cmd(p, kcmd) )
            {
                trspk_d3d8_fixed_log("cmd MODEL_UNLOAD model_id=%d", mid);
                d3d8_fixed_mark_cmd_logged(p, kcmd);
            }
        }
        if( mid <= 0 )
    return;
        for( auto it = p->batched_model_batch_by_key.begin();
             it != p->batched_model_batch_by_key.end(); )
        {
            if( model_id_from_model_cache_key(it->first) == mid )
                it = p->batched_model_batch_by_key.erase(it);
            else
                ++it;
        }
        for( auto it = p->model_gpu_by_key.begin(); it != p->model_gpu_by_key.end(); )
        {
            if( model_id_from_model_cache_key(it->first) == mid )
            {
                d3d8_release_model_gpu(it->second);
                it = p->model_gpu_by_key.erase(it);
            }
            else
                ++it;
        }
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_begin(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        uint32_t bid = cmd->_batch.batch_id;
        if( p->current_batch )
        {
            trspk_d3d8_fixed_log(
                "BATCH_MODEL_LOAD_START: replacing incomplete batch id=%u",
                (unsigned)p->current_batch->batch_id);
            d3d8_release_model_batch(p->current_batch);
            p->current_batch = nullptr;
        }
        p->current_batch = new D3D8ModelBatch();
        p->current_batch->batch_id = bid;
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_model_add(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        struct DashModel* model = cmd->_model_load.model;
        uint64_t mk = cmd->_model_load.model_key;
        if( !p->current_batch || !model )
    return;
        if( dashmodel_face_count(model) <= 0 )
    return;
        if( !dashmodel_face_colors_a_const(model) || !dashmodel_face_colors_b_const(model) ||
            !dashmodel_face_colors_c_const(model) )
    return;
        if( !dashmodel_vertices_x_const(model) || !dashmodel_vertices_y_const(model) ||
            !dashmodel_vertices_z_const(model) )
    return;
        if( !dashmodel_face_indices_a_const(model) || !dashmodel_face_indices_b_const(model) ||
            !dashmodel_face_indices_c_const(model) )
    return;
        const int fc = dashmodel_face_count(model);
        const faceint_t* ftex = dashmodel_face_textures_const(model);
        D3D8BatchModelEntry ent;
        ent.model_key = mk;
        ent.model_id = cmd->_model_load.visual_id;
        ent.vertex_base = p->current_batch->total_vertex_count;
        ent.face_count = fc;
        ent.per_face_raw_tex_id.resize((size_t)fc);
    
        for( int f = 0; f < fc; ++f )
        {
            int raw = ftex ? (int)ftex[f] : -1;
            ent.per_face_raw_tex_id[(size_t)f] = raw;
            D3D8WorldVertex tri[3];
            if( !fill_model_face_vertices_model_local(model, f, raw, tri) )
            {
                d3d8_vertex_fill_invisible(&tri[0]);
                d3d8_vertex_fill_invisible(&tri[1]);
                d3d8_vertex_fill_invisible(&tri[2]);
            }
            p->current_batch->pending_verts.push_back(tri[0]);
            p->current_batch->pending_verts.push_back(tri[1]);
            p->current_batch->pending_verts.push_back(tri[2]);
            /* Per-model-local indices; BaseVertexIndex on SetIndices supplies batch offset. */
            p->current_batch->pending_indices.push_back((uint16_t)(f * 3 + 0));
            p->current_batch->pending_indices.push_back((uint16_t)(f * 3 + 1));
            p->current_batch->pending_indices.push_back((uint16_t)(f * 3 + 2));
        }
        p->current_batch->total_vertex_count += fc * 3;
        p->current_batch->entries_by_key[mk] = std::move(ent);
        p->batched_model_batch_by_key[mk] = p->current_batch;
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_end(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        uint32_t bid = cmd->_batch.batch_id;
        if( !p->current_batch || p->current_batch->batch_id != bid )
    return;
        D3D8ModelBatch* batch = p->current_batch;
        p->current_batch = nullptr;
        if( batch->pending_verts.empty() )
        {
            delete batch;
    return;
        }
    
        const UINT vbytes = (UINT)(batch->pending_verts.size() * sizeof(D3D8WorldVertex));
        IDirect3DVertexBuffer8* vbo = nullptr;
        HRESULT hrvb = dev->CreateVertexBuffer(
            vbytes, D3DUSAGE_WRITEONLY, kFvfWorld, D3DPOOL_MANAGED, &vbo);
        if( hrvb != D3D_OK || !vbo )
        {
            trspk_d3d8_fixed_log(
                "BATCH_MODEL_LOAD_END: CreateVertexBuffer failed hr=0x%08lX",
                (unsigned long)hrvb);
            delete batch;
    return;
        }
        void* vdst = nullptr;
        HRESULT hvlock = vbo->Lock(0, vbytes, (BYTE**)&vdst, 0);
        if( hvlock != D3D_OK )
        {
            trspk_d3d8_fixed_log(
                "BATCH_MODEL_LOAD_END: VBO Lock failed hr=0x%08lX",
                (unsigned long)hvlock);
            vbo->Release();
            delete batch;
    return;
        }
        memcpy(vdst, batch->pending_verts.data(), vbytes);
        vbo->Unlock();
        batch->vbo = vbo;
        batch->pending_verts.clear();
    
        static unsigned s_batch_end_logs = 0;
        uint16_t max_idx = 0;
        if( !batch->pending_indices.empty() )
        {
            for( uint16_t ix : batch->pending_indices )
            {
                if( ix > max_idx )
                    max_idx = ix;
            }
        }
        if( s_batch_end_logs < 4u )
        {
            s_batch_end_logs++;
            trspk_d3d8_fixed_log(
                "BATCH_MODEL_LOAD_END[%u]: batch_id=%u total_vertex_count=%d "
                "index_count=%zu max_per_model_local_index=%u",
                s_batch_end_logs,
                (unsigned)bid,
                batch->total_vertex_count,
                batch->pending_indices.size(),
                (unsigned)max_idx);
        }
    
        const UINT ibytes = (UINT)(batch->pending_indices.size() * sizeof(uint16_t));
        IDirect3DIndexBuffer8* ibo = nullptr;
        HRESULT hrib =
            dev->CreateIndexBuffer(ibytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ibo);
        if( hrib != D3D_OK || !ibo )
        {
            trspk_d3d8_fixed_log(
                "BATCH_MODEL_LOAD_END: CreateIndexBuffer failed hr=0x%08lX",
                (unsigned long)hrib);
            batch->vbo->Release();
            batch->vbo = nullptr;
            delete batch;
    return;
        }
        void* idst = nullptr;
        HRESULT hilock = ibo->Lock(0, ibytes, (BYTE**)&idst, 0);
        if( hilock != D3D_OK )
        {
            trspk_d3d8_fixed_log(
                "BATCH_MODEL_LOAD_END: IBO Lock failed hr=0x%08lX",
                (unsigned long)hilock);
            ibo->Release();
            batch->vbo->Release();
            batch->vbo = nullptr;
            delete batch;
    return;
        }
        memcpy(idst, batch->pending_indices.data(), ibytes);
        ibo->Unlock();
        batch->ebo = ibo;
        batch->pending_indices.clear();
    
        p->batches_by_id[batch->batch_id] = batch;
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_clear(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        uint32_t bid = cmd->_batch.batch_id;
        auto bit = p->batches_by_id.find(bid);
        if( bit == p->batches_by_id.end() || !bit->second )
    return;
        D3D8ModelBatch* batch = bit->second;
        p->batches_by_id.erase(bit);
        for( auto it = p->batched_model_batch_by_key.begin();
             it != p->batched_model_batch_by_key.end(); )
        {
            if( it->second == batch )
                it = p->batched_model_batch_by_key.erase(it);
            else
                ++it;
        }
        d3d8_release_model_batch(batch);
    
}

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
                if( sp && sp->pixels_argb )
                    trspk_d3d8_fixed_log(
                        "cmd SPRITE_LOAD size=%dx%d",
                        sp->width,
                        sp->height);
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
            (UINT)sp->width,
            (UINT)sp->height,
            1,
            0,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &st);
        if( hsp != D3D_OK || !st )
        {
            trspk_d3d8_fixed_log(
                "SPRITE_LOAD CreateTexture failed hr=0x%08lX key=0x%016llX %dx%d",
                (unsigned long)hsp,
                (unsigned long long)sk,
                sp->width,
                sp->height);
    return;
        }
        D3DLOCKED_RECT lr;
        HRESULT hsl = st->LockRect(0, &lr, nullptr, 0);
        if( hsl != D3D_OK )
        {
            trspk_d3d8_fixed_log(
                "SPRITE_LOAD LockRect failed hr=0x%08lX key=0x%016llX",
                (unsigned long)hsl,
                (unsigned long long)sk);
            st->Release();
    return;
        }
        for( int py = 0; py < sp->height; ++py )
        {
            uint32_t* dst = (uint32_t*)((unsigned char*)lr.pBits + py * lr.Pitch);
            for( int px = 0; px < sp->width; ++px )
            {
                uint32_t pix = (uint32_t)sp->pixels_argb[py * sp->width + px];
                uint8_t a_hi = (uint8_t)((pix >> 24) & 0xFFu);
                uint32_t rgb = pix & 0x00FFFFFFu;
                uint8_t a = 0;
                if( a_hi != 0 )
                    a = a_hi;
                else if( rgb != 0u )
                    a = 0xFFu;
                dst[px] = (pix & 0x00FFFFFFu) | ((uint32_t)a << 24);
            }
        }
        st->UnlockRect(0);
        p->sprite_by_slot[sk] = st;
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
        struct DashPixFont* f = cmd->_font_load.font;
        int font_id = cmd->_font_load.font_id;
        {
            const int kcmd = (int)TORIRS_GFX_RES_FONT_LOAD;
            if( d3d8_fixed_should_log_cmd(p, kcmd) )
            {
                trspk_d3d8_fixed_log(
                    "cmd FONT_LOAD font_id=%d font=%p",
                    font_id,
                    (void*)f);
                if( f && f->atlas )
                    trspk_d3d8_fixed_log(
                        "cmd FONT_LOAD atlas %dx%d",
                        f->atlas->atlas_width,
                        f->atlas->atlas_height);
                d3d8_fixed_mark_cmd_logged(p, kcmd);
            }
        }
        if( !f || !f->atlas || p->font_atlas_by_id.count(font_id) )
    return;
        struct DashFontAtlas* a = f->atlas;
        IDirect3DTexture8* at = nullptr;
        HRESULT hfa = dev->CreateTexture(
            (UINT)a->atlas_width,
            (UINT)a->atlas_height,
            1,
            0,
            D3DFMT_A8R8G8B8,
            D3DPOOL_MANAGED,
            &at);
        if( hfa != D3D_OK || !at )
        {
            trspk_d3d8_fixed_log(
                "FONT_LOAD CreateTexture failed hr=0x%08lX font_id=%d atlas=%dx%d",
                (unsigned long)hfa,
                font_id,
                a->atlas_width,
                a->atlas_height);
    return;
        }
        D3DLOCKED_RECT lr;
        HRESULT hfal = at->LockRect(0, &lr, nullptr, 0);
        if( hfal != D3D_OK )
        {
            trspk_d3d8_fixed_log(
                "FONT_LOAD LockRect failed hr=0x%08lX font_id=%d",
                (unsigned long)hfal,
                font_id);
            at->Release();
    return;
        }
        for( int row = 0; row < a->atlas_height; ++row )
            memcpy(
                (unsigned char*)lr.pBits + row * lr.Pitch,
                a->rgba_pixels + row * a->atlas_width,
                (size_t)a->atlas_width * 4u);
        at->UnlockRect(0);
        p->font_atlas_by_id[font_id] = at;
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_clear_rect(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        p->flush_font();
        int cx = cmd->_clear_rect.x;
        int cy = cmd->_clear_rect.y;
        int cw = cmd->_clear_rect.w;
        int ch = cmd->_clear_rect.h;
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
        D3DVIEWPORT8 cvp = {
            (DWORD)cx,
            (DWORD)cy,
            (DWORD)cw,
            (DWORD)ch,
            0.0f,
            1.0f
        };
        if( (int)cvp.X + (int)cvp.Width > renderer->width )
            cvp.Width = (DWORD)(renderer->width - (int)cvp.X);
        if( (int)cvp.Y + (int)cvp.Height > renderer->height )
            cvp.Height = (DWORD)(renderer->height - (int)cvp.Y);
        dev->SetViewport(&cvp);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0x00000000, 1.0f, 0);
        dev->SetViewport(&saved);
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_draw_model(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        struct DashModel* model = cmd->_model_draw.model;
        if( !model || !game->sys_dash || !game->view_port || !game->camera )
    return;
        d3d8_fixed_ensure_pass(p, dev, PASS_3D);
        uint64_t mk_draw = cmd->_model_draw.model_key;
        int vertex_index_base = 0;
        UINT num_vertices_for_draw = 0;
        D3D8ModelGpu* gpu = nullptr;
        D3D8ModelGpu batched_wrap{};
        D3D8ModelBatch* batched_batch_ptr = nullptr;
    
        auto bmit = p->batched_model_batch_by_key.find(mk_draw);
        if( bmit != p->batched_model_batch_by_key.end() && bmit->second )
        {
            batched_batch_ptr = bmit->second;
            auto eit = batched_batch_ptr->entries_by_key.find(mk_draw);
            if( eit == batched_batch_ptr->entries_by_key.end() )
                return;
            const D3D8BatchModelEntry& ent = eit->second;
            vertex_index_base = ent.vertex_base;
            batched_wrap.vbo = batched_batch_ptr->vbo;
            batched_wrap.face_count = ent.face_count;
            batched_wrap.per_face_raw_tex_id = ent.per_face_raw_tex_id;
            gpu = &batched_wrap;
        }
        else
        {
            gpu = d3d8_lookup_or_build_model_gpu(p, dev, model, mk_draw);
            if( !gpu || !gpu->vbo )
                return;
        }
        if( !gpu || !gpu->vbo )
    return;
        /* D3D8 DIP NumVertices: count for this model only; BaseVertexIndex on SetIndices
         * selects the sub-range inside a shared batched VBO. */
        num_vertices_for_draw = (UINT)(gpu->face_count * 3);
    
        struct DashPosition draw_position = cmd->_model_draw.position;
        int face_order_count = dash3d_prepare_projected_face_order(
            game->sys_dash, model, &draw_position, game->view_port, game->camera);
        const int* face_order =
            dash3d_projected_face_order(game->sys_dash, &face_order_count);
        if( face_order_count <= 0 || !face_order )
    return;
        {
            const int kcmd = (int)TORIRS_GFX_DRAW_MODEL;
            if( d3d8_fixed_should_log_cmd(p, kcmd) )
            {
                trspk_d3d8_fixed_log(
                    "cmd MODEL_DRAW key=0x%016llX pos=(%d,%d,%d) yaw=%d gpu_faces=%d "
                    "order_count=%d",
                    (unsigned long long)cmd->_model_draw.model_key,
                    draw_position.x,
                    draw_position.y,
                    draw_position.z,
                    (int)draw_position.yaw,
                    gpu->face_count,
                    face_order_count);
                d3d8_fixed_mark_cmd_logged(p, kcmd);
            }
        }
    
        unsigned int ib_batches = 0;
        unsigned int draws_before = p->debug_model_draws;
        unsigned int tris_before = p->debug_triangles;
    
        const float yaw_rad_m =
            (draw_position.yaw * 2.0f * (float)M_PI) / 2048.0f;
        const float cos_yaw = cosf(yaw_rad_m);
        const float sin_yaw = sinf(yaw_rad_m);
    
        D3DMATRIX world;
        memset(&world, 0, sizeof(world));
        world._11 = cos_yaw;
        world._12 = 0.0f;
        world._13 = -sin_yaw;
        world._14 = 0.0f;
        world._21 = 0.0f;
        world._22 = 1.0f;
        world._23 = 0.0f;
        world._24 = 0.0f;
        world._31 = sin_yaw;
        world._32 = 0.0f;
        world._33 = cos_yaw;
        world._34 = 0.0f;
        world._41 = (float)draw_position.x;
        world._42 = (float)draw_position.y;
        world._43 = (float)draw_position.z;
        world._44 = 1.0f;
        dev->SetTransform(D3DTS_WORLD, &world);
    
        if( s_model_draw_debug < 2u )
        {
            trspk_d3d8_fixed_log(
                "MODEL_DRAW dbg[%u] batched=%d vertex_index_base=%d num_vertices_for_draw=%u "
                "buf=%dx%d vp=%dx%d pos=(%d,%d,%d) yaw=%d",
                s_model_draw_debug,
                batched_batch_ptr ? 1 : 0,
                vertex_index_base,
                (unsigned)num_vertices_for_draw,
                renderer->width,
                renderer->height,
                vp_w,
                vp_h,
                draw_position.x,
                draw_position.y,
                draw_position.z,
                (int)draw_position.yaw);
            trspk_d3d8_fixed_log(
                "MODEL_DRAW dbg world row3: %f %f %f %f",
                world._41,
                world._42,
                world._43,
                world._44);
            trspk_d3d8_fixed_log(
                "MODEL_DRAW dbg view row3: %f %f %f %f",
                p->frame_view._41,
                p->frame_view._42,
                p->frame_view._43,
                p->frame_view._44);
            trspk_d3d8_fixed_log(
                "MODEL_DRAW dbg proj row0: %f %f %f %f",
                p->frame_proj._11,
                p->frame_proj._12,
                p->frame_proj._13,
                p->frame_proj._14);
            trspk_d3d8_fixed_log(
                "MODEL_DRAW dbg proj row3: %f %f %f %f",
                p->frame_proj._41,
                p->frame_proj._42,
                p->frame_proj._43,
                p->frame_proj._44);
            if( !batched_batch_ptr )
            {
                const D3D8WorldVertex* fv = gpu->first_face_verts;
                const D3D8WorldVertex* lv = gpu->last_face_verts;
                float wsp[4], vsp[4], clip[4];
                float v0[4] = { fv[0].x, fv[0].y, fv[0].z, 1.0f };
                d3d8_mul_row_vec_d3dmatrix(v0, &world, wsp);
                d3d8_mul_row_vec_d3dmatrix(wsp, &p->frame_view, vsp);
                d3d8_mul_row_vec_d3dmatrix(vsp, &p->frame_proj, clip);
                const float wclip = clip[3];
                const float invw = (wclip != 0.0f) ? (1.0f / wclip) : 0.0f;
                trspk_d3d8_fixed_log(
                    "MODEL_DRAW dbg first_face v_local=(%f,%f,%f) clip=(%f,%f,%f,%f) "
                    "ndc=(%f,%f,%f)",
                    fv[0].x,
                    fv[0].y,
                    fv[0].z,
                    clip[0],
                    clip[1],
                    clip[2],
                    wclip,
                    clip[0] * invw,
                    clip[1] * invw,
                    clip[2] * invw);
                float vL[4] = { lv[0].x, lv[0].y, lv[0].z, 1.0f };
                d3d8_mul_row_vec_d3dmatrix(vL, &world, wsp);
                d3d8_mul_row_vec_d3dmatrix(wsp, &p->frame_view, vsp);
                d3d8_mul_row_vec_d3dmatrix(vsp, &p->frame_proj, clip);
                const float wclipL = clip[3];
                const float invwL = (wclipL != 0.0f) ? (1.0f / wclipL) : 0.0f;
                trspk_d3d8_fixed_log(
                    "MODEL_DRAW dbg last_face v0_local=(%f,%f,%f) clip=(%f,%f,%f,%f) "
                    "ndc=(%f,%f,%f)",
                    lv[0].x,
                    lv[0].y,
                    lv[0].z,
                    clip[0],
                    clip[1],
                    clip[2],
                    wclipL,
                    clip[0] * invwL,
                    clip[1] * invwL,
                    clip[2] * invwL);
            }
            else
            {
                trspk_d3d8_fixed_log(
                    "MODEL_DRAW dbg (batched VBO: no CPU first/last_face verts for clip)");
            }
            s_model_draw_debug++;
        }
    
        auto eff_tex = [&](int f) -> int {
            if( f < 0 || f >= gpu->face_count )
                return -1;
            int raw = gpu->per_face_raw_tex_id[(size_t)f];
            if( raw < 0 )
                return -1;
            if( p->texture_by_id.find(raw) == p->texture_by_id.end() )
                return -1;
            return raw;
        };
    
        dev->SetStreamSource(0, gpu->vbo, sizeof(D3D8WorldVertex));
    
        int run_start = 0;
        int run_tex = eff_tex(face_order[0]);
        for( int i = 1; i <= face_order_count; ++i )
        {
            int t = (i < face_order_count) ? eff_tex(face_order[i]) : INT_MIN;
            if( i < face_order_count && t == run_tex )
                continue;
    
            const int nfaces = i - run_start;
            if( nfaces <= 0 )
            {
                run_start = i;
                run_tex = t;
                continue;
            }
    
            size_t nidx = 0;
            for( int k = 0; k < nfaces; ++k )
            {
                const int f = face_order[run_start + k];
                if( f >= 0 && f < gpu->face_count )
                    nidx += 3u;
            }
            if( nidx == 0 )
            {
                run_start = i;
                run_tex = t;
                continue;
            }
    
            const UINT nidx_bytes = (UINT)(nidx * sizeof(uint16_t));
            if( p->ib_ring_write_offset + nidx_bytes > p->ib_ring_size_bytes )
                break;
            BYTE* lock_dst = nullptr;
            DWORD lock_flags =
                p->ib_ring_write_offset == 0 ? D3DLOCK_DISCARD : D3DLOCK_NOOVERWRITE;
            HRESULT hr_ib = p->ib_ring->Lock(
                (UINT)p->ib_ring_write_offset,
                nidx_bytes,
                (BYTE**)&lock_dst,
                lock_flags);
            if( hr_ib != D3D_OK )
            {
                trspk_d3d8_fixed_log(
                    "MODEL_DRAW ib_ring->Lock failed hr=0x%08lX off=%zu bytes=%u",
                    (unsigned long)hr_ib,
                    (size_t)p->ib_ring_write_offset,
                    (unsigned)nidx_bytes);
                break;
            }
            uint16_t* idst = (uint16_t*)lock_dst;
            size_t wpos = 0;
            for( int k = 0; k < nfaces; ++k )
            {
                const int f = face_order[run_start + k];
                if( f < 0 || f >= gpu->face_count )
                    continue;
                /* Per-model-local indices; device adds BaseVertexIndex from SetIndices. */
                idst[wpos++] = (uint16_t)(f * 3 + 0);
                idst[wpos++] = (uint16_t)(f * 3 + 1);
                idst[wpos++] = (uint16_t)(f * 3 + 2);
            }
            p->ib_ring->Unlock();
            HRESULT hr_si =
                dev->SetIndices(p->ib_ring, (UINT)vertex_index_base);
    
            if( run_tex < 0 )
            {
                dev->SetTexture(0, nullptr);
                dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
                d3d8_disable_texture_transform(dev);
                dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            }
            else
            {
                dev->SetTexture(0, p->texture_by_id[run_tex]);
                dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
                float speed = p->texture_anim_speed_by_id[run_tex];
                if( speed != 0.0f )
                {
                    if( speed > 0.0f )
                        d3d8_apply_texture_matrix_translate(dev, u_clock * speed, 0.0f);
                    else
                        d3d8_apply_texture_matrix_translate(dev, 0.0f, u_clock * speed);
                }
                else
                    d3d8_disable_texture_transform(dev);
    
                bool opaque = true;
                auto oi = p->texture_opaque_by_id.find(run_tex);
                if( oi != p->texture_opaque_by_id.end() )
                    opaque = oi->second;
                dev->SetRenderState(D3DRS_ALPHATESTENABLE, opaque ? FALSE : TRUE);
            }
    
            UINT start_index = (UINT)(p->ib_ring_write_offset / sizeof(uint16_t));
            const UINT tri_count = (UINT)(nidx / 3u);
            HRESULT hr_dip = dev->DrawIndexedPrimitive(
                D3DPT_TRIANGLELIST,
                0,
                num_vertices_for_draw,
                start_index,
                tri_count);
            if( s_dip_full_log < 4u )
            {
                trspk_d3d8_fixed_log(
                    "MODEL_DRAW dbg DIP SetIndices(base=%u) hr_si=0x%08lX "
                    "minIndex=0 NumVertices=%u startIndex=%u primCount=%u hr_dip=0x%08lX",
                    (unsigned)vertex_index_base,
                    (unsigned long)hr_si,
                    num_vertices_for_draw,
                    (unsigned)start_index,
                    (unsigned)tri_count,
                    (unsigned long)hr_dip);
                s_dip_full_log++;
            }
            if( FAILED(hr_dip) )
                trspk_d3d8_fixed_log(
                    "MODEL_DRAW DrawIndexedPrimitive failed hr=0x%08lX start_index=%u "
                    "tri_count=%u",
                    (unsigned long)hr_dip,
                    (unsigned)start_index,
                    (unsigned)tri_count);
            p->ib_ring_write_offset += nidx_bytes;
            p->debug_model_draws++;
            p->debug_triangles += tri_count;
            ib_batches++;
    
            run_start = i;
            run_tex = t;
        }
        if( false )
            trspk_d3d8_fixed_log(
                "MODEL_DRAW done key=0x%016llX ib_batches=%u draws_this=%u tris_this=%u",
                (unsigned long long)cmd->_model_draw.model_key,
                ib_batches,
                (unsigned)(p->debug_model_draws - draws_before),
                (unsigned)(p->debug_triangles - tris_before));
        d3d8_disable_texture_transform(dev);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    
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
                    "cmd SPRITE_DRAW element=%d atlas=%d rotated=%d dst=%d,%d %dx%d src=%d,%d "
                    "%dx%d",
                    cmd->_sprite_draw.element_id,
                    cmd->_sprite_draw.atlas_index,
                    cmd->_sprite_draw.rotated ? 1 : 0,
                    cmd->_sprite_draw.dst_bb_x,
                    cmd->_sprite_draw.dst_bb_y,
                    cmd->_sprite_draw.dst_bb_w,
                    cmd->_sprite_draw.dst_bb_h,
                    cmd->_sprite_draw.src_bb_x,
                    cmd->_sprite_draw.src_bb_y,
                    cmd->_sprite_draw.src_bb_w,
                    cmd->_sprite_draw.src_bb_h);
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
        int tw = sp->width;
        int th = sp->height;
        auto sz_it = p->sprite_size_by_slot.find(sk);
        if( sz_it != p->sprite_size_by_slot.end() )
        {
            tw = sz_it->second.first;
            th = sz_it->second.second;
        }
        const int iw =
            cmd->_sprite_draw.src_bb_w > 0 ? cmd->_sprite_draw.src_bb_w : sp->width;
        const int ih =
            cmd->_sprite_draw.src_bb_h > 0 ? cmd->_sprite_draw.src_bb_h : sp->height;
        const int ix = cmd->_sprite_draw.src_bb_x;
        const int iy = cmd->_sprite_draw.src_bb_y;
        if( ix < 0 || iy < 0 || ix + iw > sp->width || iy + ih > sp->height )
    return;
        p->flush_font();
        d3d8_fixed_ensure_pass(p, dev, PASS_2D);
        dev->SetTexture(0, tex_it->second);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    
        float px[4];
        float py[4];
        D3DVIEWPORT8 saved_vp = {};
        bool sprite_vp_overridden = false;
        if( cmd->_sprite_draw.rotated )
        {
            const int dw = cmd->_sprite_draw.dst_bb_w;
            const int dh = cmd->_sprite_draw.dst_bb_h;
            if( dw <= 0 || dh <= 0 || iw <= 0 || ih <= 0 )
                return;
            D3DVIEWPORT8 spriteVP = {
                (DWORD)(cmd->_sprite_draw.dst_bb_x > 0 ? cmd->_sprite_draw.dst_bb_x : 0),
                (DWORD)(cmd->_sprite_draw.dst_bb_y > 0 ? cmd->_sprite_draw.dst_bb_y : 0),
                (DWORD)dw,
                (DWORD)dh,
                0.0f,
                1.0f
            };
            if( (int)spriteVP.X + (int)spriteVP.Width > renderer->width )
                spriteVP.Width = (DWORD)(renderer->width - (int)spriteVP.X);
            if( (int)spriteVP.Y + (int)spriteVP.Height > renderer->height )
                spriteVP.Height = (DWORD)(renderer->height - (int)spriteVP.Y);
            if( (int)spriteVP.Width > 0 && (int)spriteVP.Height > 0 )
            {
                dev->GetViewport(&saved_vp);
                dev->SetViewport(&spriteVP);
                sprite_vp_overridden = true;
            }
            const int sax = cmd->_sprite_draw.src_anchor_x;
            const int say = cmd->_sprite_draw.src_anchor_y;
            const float pivot_x =
                (float)cmd->_sprite_draw.dst_bb_x + (float)cmd->_sprite_draw.dst_anchor_x;
            const float pivot_y =
                (float)cmd->_sprite_draw.dst_bb_y + (float)cmd->_sprite_draw.dst_anchor_y;
            const int ang = cmd->_sprite_draw.rotation_r2pi2048 & 2047;
            const float angle = (float)ang * (float)(2.0 * M_PI / 2048.0);
            const float ca = cosf(angle);
            const float sa = sinf(angle);
            struct
            {
                int lx, ly;
            } corners[4] = {
                { 0, 0 },
                { iw, 0 },
                { iw, ih },
                { 0, ih },
            };
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
            const int dst_x = cmd->_sprite_draw.dst_bb_x + sp->crop_x;
            const int dst_y = cmd->_sprite_draw.dst_bb_y + sp->crop_y;
            const float w = (float)iw;
            const float h = (float)ih;
            const float x0 = (float)dst_x;
            const float y0 = (float)dst_y;
            px[0] = px[3] = x0;
            px[1] = px[2] = x0 + w;
            py[0] = py[1] = y0;
            py[2] = py[3] = y0 + h;
        }
    
        const float twf = (float)tw;
        const float thf = (float)th;
        float u0 = (float)ix / twf;
        float v0 = (float)iy / thf;
        float u1 = (float)(ix + iw) / twf;
        float v1 = (float)(iy + ih) / thf;
    
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
        if( sprite_vp_overridden )
            dev->SetViewport(&saved_vp);
        dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_draw_font(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        struct DashPixFont* f = cmd->_font_draw.font;
        const uint8_t* text = cmd->_font_draw.text;
        {
            const int kcmd = (int)TORIRS_GFX_DRAW_FONT;
            if( d3d8_fixed_should_log_cmd(p, kcmd) && text )
            {
                char preview[48];
                int pi = 0;
                for( int ci = 0; ci < 28 && text[ci] && pi < (int)sizeof(preview) - 2; ++ci )
                {
                    unsigned char c = text[ci];
                    if( c >= 32u && c < 127u )
                        preview[pi++] = (char)c;
                    else
                    {
                        int w = snprintf(
                            preview + pi,
                            sizeof(preview) - (size_t)pi,
                            "\\x%02x",
                            (unsigned)c);
                        if( w > 0 )
                            pi += w;
                        if( pi >= (int)sizeof(preview) - 1 )
                            break;
                    }
                }
                preview[pi] = '\0';
                trspk_d3d8_fixed_log(
                    "cmd FONT_DRAW font_id=%d xy=%d,%d preview=\"%s\"",
                    cmd->_font_draw.font_id,
                    cmd->_font_draw.x,
                    cmd->_font_draw.y,
                    preview);
                d3d8_fixed_mark_cmd_logged(p, kcmd);
            }
        }
        if( !f || !f->atlas || !text || f->height2d <= 0 )
    return;
        int fid = cmd->_font_draw.font_id;
        if( p->font_atlas_by_id.find(fid) == p->font_atlas_by_id.end() )
    return;
        if( p->current_font_id != fid )
        {
            p->flush_font();
            p->current_font_id = fid;
        }
    
        struct DashFontAtlas* atlas = f->atlas;
        const float inv_aw = 1.0f / (float)atlas->atlas_width;
        const float inv_ah = 1.0f / (float)atlas->atlas_height;
    
        int length = (int)strlen((const char*)text);
        int color_rgb = cmd->_font_draw.color_rgb;
        float cr = (float)((color_rgb >> 16) & 0xFF) / 255.0f;
        float cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
        float cb = (float)(color_rgb & 0xFF) / 255.0f;
        float ca = 1.0f;
        int pen_x = cmd->_font_draw.x;
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
                    cg = (float)((color_rgb >> 8) & 0xFF) / 255.0f;
                    cb = (float)(color_rgb & 0xFF) / 255.0f;
                }
                if( i + 6 <= length && text[i + 5] == ' ' )
                    i += 5;
                else
                    i += 4;
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
    
                    DWORD diff = ((DWORD)(ca * 255.0f) << 24) |
                        ((DWORD)(cr * 255.0f) << 16) | ((DWORD)(cg * 255.0f) << 8) |
                        (DWORD)(cb * 255.0f);
    
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
                if( adv <= 0 )
                    adv = 4;
                pen_x += adv;
            }
            else
            {
                pen_x += 4;
            }
        }
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_begin_3d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        int k2 = (int)cmd->kind;
        if( d3d8_fixed_should_log_cmd(p, k2) )
        {
            trspk_d3d8_fixed_log("cmd kind=%d (no-op in D3D8 path)", k2);
            d3d8_fixed_mark_cmd_logged(p, k2);
        }
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_end_3d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        int k2 = (int)cmd->kind;
        if( d3d8_fixed_should_log_cmd(p, k2) )
        {
            trspk_d3d8_fixed_log("cmd kind=%d (no-op in D3D8 path)", k2);
            d3d8_fixed_mark_cmd_logged(p, k2);
        }
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_begin_2d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        int k2 = (int)cmd->kind;
        if( d3d8_fixed_should_log_cmd(p, k2) )
        {
            trspk_d3d8_fixed_log("cmd kind=%d (no-op in D3D8 path)", k2);
            d3d8_fixed_mark_cmd_logged(p, k2);
        }
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_state_end_2d(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        int k2 = (int)cmd->kind;
        if( d3d8_fixed_should_log_cmd(p, k2) )
        {
            trspk_d3d8_fixed_log("cmd kind=%d (no-op in D3D8 path)", k2);
            d3d8_fixed_mark_cmd_logged(p, k2);
        }
    
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_none_or_default(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    D3D8_FIXED_EVENT_HEAD;
        int k2 = (int)cmd->kind;
        if( d3d8_fixed_should_log_cmd(p, k2) )
        {
            trspk_d3d8_fixed_log("cmd kind=%d (no-op in D3D8 path)", k2);
            d3d8_fixed_mark_cmd_logged(p, k2);
        }
    
}


TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_res_anim_load(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    trspk_d3d8_fixed_event_none_or_default(ctx, cmd);
}

TRSPK_D3D8_FIXED_EVT void
trspk_d3d8_fixed_event_batch3d_anim_add(
    TRSPK_D3D8Fixed_EventContext* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    trspk_d3d8_fixed_event_none_or_default(ctx, cmd);
}

