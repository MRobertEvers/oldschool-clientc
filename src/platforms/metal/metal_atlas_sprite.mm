// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

extern "C" {
#include "graphics/dash_math.h"
}

void
metal_frame_event_sprite_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashSprite* sp = cmd->_sprite_load.sprite;
    if( !sp || !sp->pixels_argb || sp->width <= 0 || sp->height <= 0 )
        return;

    const int sp_el = cmd->_sprite_load.element_id;
    const int sp_ai = cmd->_sprite_load.atlas_index;
    {
        auto* existing = ctx->renderer->sprite_cache.get_sprite(sp_el, sp_ai);
        if( existing && existing->atlas_texture && !existing->is_batched )
            CFRelease(existing->atlas_texture);
        ctx->renderer->sprite_cache.unload_sprite(sp_el, sp_ai);
    }

    const uint32_t sbid = ctx->renderer->current_sprite_batch_id;
    if( sbid != 0 && ctx->renderer->sprite_cache.is_batch_open(sbid) )
    {
        ctx->renderer->sprite_cache.add_to_batch(
            sbid, sp_el, sp_ai, (const uint32_t*)sp->pixels_argb, sp->width, sp->height);
        return;
    }

    const int sw = sp->width;
    const int sh = sp->height;
    std::vector<uint8_t>& rgba = ctx->renderer->rgba_scratch;
    rgba.resize((size_t)sw * (size_t)sh * 4u);
    for( int p = 0; p < sw * sh; ++p )
    {
        uint32_t pix = (uint32_t)sp->pixels_argb[p];
        uint8_t a_hi = (uint8_t)((pix >> 24) & 0xFFu);
        uint32_t rgb = pix & 0x00FFFFFFu;
        uint8_t a = 0;
        if( a_hi != 0 )
            a = a_hi;
        else if( rgb != 0u )
            a = 0xFFu;
        rgba[(size_t)p * 4u + 0u] = (uint8_t)((pix >> 16) & 0xFFu);
        rgba[(size_t)p * 4u + 1u] = (uint8_t)((pix >> 8) & 0xFFu);
        rgba[(size_t)p * 4u + 2u] = (uint8_t)(pix & 0xFFu);
        rgba[(size_t)p * 4u + 3u] = a;
    }
    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:(NSUInteger)sw
                                                          height:(NSUInteger)sh
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> spTex = [ctx->device newTextureWithDescriptor:td];
    [spTex replaceRegion:MTLRegionMake2D(0, 0, sw, sh)
             mipmapLevel:0
               withBytes:rgba.data()
             bytesPerRow:(NSUInteger)sw * 4u];
    ctx->renderer->sprite_cache.register_standalone(sp_el, sp_ai, (__bridge_retained void*)spTex);
    fprintf(
        stderr,
        "[metal] TORIRS_GFX_SPRITE_LOAD applied: element_id=%d atlas_index=%d "
        "texture=%dx%d\n",
        sp_el,
        sp_ai,
        sw,
        sh);
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_SPRITE_UNLOAD
// ---------------------------------------------------------------------------
void
metal_frame_event_sprite_unload(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int unl_el = cmd->_sprite_load.element_id;
    const int unl_ai = cmd->_sprite_load.atlas_index;
    auto* e = ctx->renderer->sprite_cache.get_sprite(unl_el, unl_ai);
    if( e && e->atlas_texture && !e->is_batched )
        CFRelease(e->atlas_texture);
    ctx->renderer->sprite_cache.unload_sprite(unl_el, unl_ai);
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_SPRITE_DRAW
// ---------------------------------------------------------------------------
void
metal_sprite_draw_impl(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashSprite* sp = cmd->_sprite_draw.sprite;
    if( !sp || sp->width <= 0 || sp->height <= 0 )
        return;

    const int draw_el = cmd->_sprite_draw.element_id;
    const int draw_ai = cmd->_sprite_draw.atlas_index;
    auto* spriteEntry = ctx->renderer->sprite_cache.get_sprite(draw_el, draw_ai);
    if( !spriteEntry || !spriteEntry->atlas_texture )
        return;
    id<MTLTexture> spriteTex = (__bridge id<MTLTexture>)spriteEntry->atlas_texture;

    const int iw = cmd->_sprite_draw.src_bb_w > 0 ? cmd->_sprite_draw.src_bb_w : sp->width;
    const int ih = cmd->_sprite_draw.src_bb_h > 0 ? cmd->_sprite_draw.src_bb_h : sp->height;
    const int ix = cmd->_sprite_draw.src_bb_x;
    const int iy = cmd->_sprite_draw.src_bb_y;
    if( ix < 0 || iy < 0 || ix + iw > sp->width || iy + ih > sp->height )
    {
        fprintf(
            stderr,
            "[metal] SPRITE_DRAW skipped: src_bb out of bounds "
            "element_id=%d ix=%d iy=%d iw=%d ih=%d sprite=%dx%d\n",
            cmd->_sprite_draw.element_id,
            ix,
            iy,
            iw,
            ih,
            sp->width,
            sp->height);
        return;
    }
    const float tw = (float)sp->width;
    const float th = (float)sp->height;

    const bool rotated = cmd->_sprite_draw.rotated;
    if( rotated )
    {
        if( !ctx->uiInverseRotPipeState )
            return;
    }
    else
    {
        if( !ctx->uiPipeState )
            return;
    }

    float px[4];
    float py[4];
    float log_u[4];
    float log_v[4];
    SpriteInverseRotParams inv_params{};

    if( rotated )
    {
        const int dw = cmd->_sprite_draw.dst_bb_w;
        const int dh = cmd->_sprite_draw.dst_bb_h;
        if( dw <= 0 || dh <= 0 || iw <= 0 || ih <= 0 )
        {
            fprintf(
                stderr,
                "[metal] SPRITE_DRAW skipped: bad dst/src size "
                "element_id=%d dw=%d dh=%d iw=%d ih=%d\n",
                cmd->_sprite_draw.element_id,
                dw,
                dh,
                iw,
                ih);
            return;
        }
        const int dst_bb_x = cmd->_sprite_draw.dst_bb_x;
        const int dst_bb_y = cmd->_sprite_draw.dst_bb_y;
        px[0] = px[3] = (float)dst_bb_x;
        px[1] = px[2] = (float)(dst_bb_x + dw);
        py[0] = py[1] = (float)dst_bb_y;
        py[2] = py[3] = (float)(dst_bb_y + dh);
        log_u[0] = log_u[3] = (float)dst_bb_x;
        log_u[1] = log_u[2] = (float)(dst_bb_x + dw);
        log_v[0] = log_v[1] = (float)dst_bb_y;
        log_v[2] = log_v[3] = (float)(dst_bb_y + dh);

        inv_params.dst_origin_x = (float)dst_bb_x;
        inv_params.dst_origin_y = (float)dst_bb_y;
        inv_params.dst_anchor_x = (float)cmd->_sprite_draw.dst_anchor_x;
        inv_params.dst_anchor_y = (float)cmd->_sprite_draw.dst_anchor_y;
        inv_params.src_anchor_x = (float)cmd->_sprite_draw.src_anchor_x;
        inv_params.src_anchor_y = (float)cmd->_sprite_draw.src_anchor_y;
        inv_params.src_size_x = (float)iw;
        inv_params.src_size_y = (float)ih;

        const NSUInteger tex_w = spriteTex.width;
        const NSUInteger tex_h = spriteTex.height;
        const float atlas_x = spriteEntry->u0 * (float)tex_w;
        const float atlas_y = spriteEntry->v0 * (float)tex_h;
        inv_params.src_crop_x = (float)ix + atlas_x;
        inv_params.src_crop_y = (float)iy + atlas_y;
        inv_params.tex_size_x = (float)tex_w;
        inv_params.tex_size_y = (float)tex_h;

        const int ang = cmd->_sprite_draw.rotation_r2pi2048 & 2047;
        inv_params.cos_val = (float)dash_cos(ang) / 65536.0f;
        inv_params.sin_val = (float)dash_sin(ang) / 65536.0f;
        inv_params._pad0 = 0.0f;
        inv_params._pad1 = 0.0f;
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

    /* Match pre-batching Metal: logical window coords -> NDC for full-drawable spriteVp
       (not letterboxed through ui_gl_vp; see git 732d9a97 metal_frame_event_sprite_draw). */
    float fbw = (float)(ctx->win_width > 0 ? ctx->win_width : ctx->renderer->width);
    float fbh = (float)(ctx->win_height > 0 ? ctx->win_height : ctx->renderer->height);
    if( fbw <= 0.0f )
        fbw = 1.0f;
    if( fbh <= 0.0f )
        fbh = 1.0f;
    auto to_clip = [&](float xp, float yp, float* ocx, float* ocy) {
        *ocx = 2.0f * xp / fbw - 1.0f;
        *ocy = 1.0f - 2.0f * yp / fbh;
    };

    float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
    to_clip(px[0], py[0], &c0x, &c0y);
    to_clip(px[1], py[1], &c1x, &c1y);
    to_clip(px[2], py[2], &c2x, &c2y);
    to_clip(px[3], py[3], &c3x, &c3y);

    float verts[6 * 4];
    if( rotated )
    {
        verts[0] = c0x;
        verts[1] = c0y;
        verts[2] = log_u[0];
        verts[3] = log_v[0];
        verts[4] = c1x;
        verts[5] = c1y;
        verts[6] = log_u[1];
        verts[7] = log_v[1];
        verts[8] = c2x;
        verts[9] = c2y;
        verts[10] = log_u[2];
        verts[11] = log_v[2];
        verts[12] = c0x;
        verts[13] = c0y;
        verts[14] = log_u[0];
        verts[15] = log_v[0];
        verts[16] = c2x;
        verts[17] = c2y;
        verts[18] = log_u[2];
        verts[19] = log_v[2];
        verts[20] = c3x;
        verts[21] = c3y;
        verts[22] = log_u[3];
        verts[23] = log_v[3];
    }
    else
    {
        const float u0 = (float)ix / tw;
        const float v0 = (float)iy / th;
        const float u1 = (float)(ix + iw) / tw;
        const float v1 = (float)(iy + ih) / th;
        verts[0] = c0x;
        verts[1] = c0y;
        verts[2] = u0;
        verts[3] = v0;
        verts[4] = c1x;
        verts[5] = c1y;
        verts[6] = u1;
        verts[7] = v0;
        verts[8] = c2x;
        verts[9] = c2y;
        verts[10] = u1;
        verts[11] = v1;
        verts[12] = c0x;
        verts[13] = c0y;
        verts[14] = u0;
        verts[15] = v0;
        verts[16] = c2x;
        verts[17] = c2y;
        verts[18] = u1;
        verts[19] = v1;
        verts[20] = c3x;
        verts[21] = c3y;
        verts[22] = u0;
        verts[23] = v1;
    }

    SpriteQuadVertex six[6];
    for( int i = 0; i < 6; ++i )
    {
        six[i].cx = verts[i * 4 + 0];
        six[i].cy = verts[i * 4 + 1];
        six[i].u = verts[i * 4 + 2];
        six[i].v = verts[i * 4 + 3];
    }

    if( ctx->bsp2d )
    {
        if( rotated )
            ctx->bsp2d->enqueue(
                (__bridge void*)spriteTex,
                six,
                nullptr,
                ctx->b2d_order,
                &ctx->split_sprite_before_next_enqueue,
                &inv_params);
        else
            ctx->bsp2d->enqueue(
                (__bridge void*)spriteTex,
                six,
                nullptr,
                ctx->b2d_order,
                &ctx->split_sprite_before_next_enqueue);
    }
    ctx->split_font_before_next_set_font = true;
}

void
metal_frame_event_sprite_draw(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    metal_sprite_draw_impl(ctx, cmd);
}
