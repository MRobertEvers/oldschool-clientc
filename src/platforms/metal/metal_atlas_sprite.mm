// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

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
    if( !ctx->uiPipeState || !ctx->spriteQuadBuf )
        return;
    metal_ensure_pipe(ctx, kMTLPipeUI);

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

    float px[4];
    float py[4];
    if( cmd->_sprite_draw.rotated )
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
            { 0,  0  },
            { iw, 0  },
            { iw, ih },
            { 0,  ih },
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

    const float fbw = (float)(ctx->win_width > 0 ? ctx->win_width : ctx->renderer->width);
    const float fbh = (float)(ctx->win_height > 0 ? ctx->win_height : ctx->renderer->height);
    auto to_clip = [&](float xp, float yp, float* ocx, float* ocy) {
        *ocx = 2.0f * xp / fbw - 1.0f;
        *ocy = 1.0f - 2.0f * yp / fbh;
    };

    float c0x, c0y, c1x, c1y, c2x, c2y, c3x, c3y;
    to_clip(px[0], py[0], &c0x, &c0y);
    to_clip(px[1], py[1], &c1x, &c1y);
    to_clip(px[2], py[2], &c2x, &c2y);
    to_clip(px[3], py[3], &c3x, &c3y);

    const float u0 = (float)ix / tw;
    const float v0 = (float)iy / th;
    const float u1 = (float)(ix + iw) / tw;
    const float v1 = (float)(iy + ih) / th;

    float verts[6 * 4] = {
        c0x, c0y, u0, v0, c1x, c1y, u1, v0, c2x, c2y, u1, v1,
        c0x, c0y, u0, v0, c2x, c2y, u1, v1, c3x, c3y, u0, v1,
    };

    const bool rotated_clip = cmd->_sprite_draw.rotated && cmd->_sprite_draw.dst_bb_w > 0 &&
                              cmd->_sprite_draw.dst_bb_h > 0;
    if( rotated_clip )
    {
        MTLScissorRect sc = metal_clamped_scissor_from_logical_dst_bb(
            ctx->renderer->width,
            ctx->renderer->height,
            ctx->win_width,
            ctx->win_height,
            cmd->_sprite_draw.dst_bb_x,
            cmd->_sprite_draw.dst_bb_y,
            cmd->_sprite_draw.dst_bb_w,
            cmd->_sprite_draw.dst_bb_h);
        [ctx->encoder setScissorRect:sc];
    }

    NSUInteger slotOffset = ctx->sprite_slot * kSpriteSlotBytes;
    memcpy((char*)ctx->spriteQuadBuf.contents + slotOffset, verts, sizeof(verts));
    [ctx->encoder setVertexBuffer:ctx->spriteQuadBuf offset:slotOffset atIndex:0];
    [ctx->encoder setFragmentTexture:spriteTex atIndex:0];
    [ctx->encoder setFragmentSamplerState:ctx->uiSamplerNearest atIndex:0];
    [ctx->encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];

    if( rotated_clip )
    {
        MTLScissorRect scMax = {
            0, 0, (NSUInteger)ctx->renderer->width, (NSUInteger)ctx->renderer->height
        };
        [ctx->encoder setScissorRect:scMax];
    }
    ++ctx->sprite_slot;
}

void
metal_frame_event_sprite_draw(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    metal_sprite_draw_impl(ctx, cmd);
}

