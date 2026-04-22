// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

// ---------------------------------------------------------------------------
// TORIRS_GFX_FONT_LOAD
// ---------------------------------------------------------------------------
void
metal_frame_event_font_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    struct DashPixFont* font = cmd->_font_load.font;
    const int font_id = cmd->_font_load.font_id;
    if( !font || !font->atlas )
        return;

    const uint32_t fbid = ctx->renderer->current_font_batch_id;
    if( fbid != 0 && ctx->renderer->font_cache.is_batch_open(fbid) )
    {
        struct DashFontAtlas* atlas = font->atlas;
        ctx->renderer->font_cache.add_to_batch(
            fbid, font_id, atlas->rgba_pixels, atlas->atlas_width, atlas->atlas_height);
        return;
    }

    if( !ctx->renderer->font_cache.get_font(font_id) )
    {
        struct DashFontAtlas* atlas = font->atlas;
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:(NSUInteger)atlas->atlas_width
                                                              height:(NSUInteger)atlas->atlas_height
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> atlasTex = [ctx->device newTextureWithDescriptor:td];
        [atlasTex
            replaceRegion:MTLRegionMake2D(
                              0, 0, (NSUInteger)atlas->atlas_width, (NSUInteger)atlas->atlas_height)
              mipmapLevel:0
                withBytes:atlas->rgba_pixels
              bytesPerRow:(NSUInteger)atlas->atlas_width * 4u];
        ctx->renderer->font_cache.register_font(font_id, (__bridge_retained void*)atlasTex);
    }
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_FONT_DRAW
// ---------------------------------------------------------------------------
void
metal_frame_event_font_draw(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx->fontPipeState || !ctx->fontVbo )
        return;
    struct DashPixFont* f = cmd->_font_draw.font;
    if( !f || !f->atlas || !cmd->_font_draw.text || f->height2d <= 0 )
        return;

    const int fid = cmd->_font_draw.font_id;
    auto* fontEntry = ctx->renderer->font_cache.get_font(fid);
    if( !fontEntry || !fontEntry->atlas_texture )
        return;
    if( ctx->bft2d )
        ctx->bft2d->set_font(
            fid,
            fontEntry->atlas_texture,
            ctx->b2d_order,
            &ctx->split_font_before_next_set_font);

    struct DashFontAtlas* atlas = f->atlas;
    const float inv_aw = 1.0f / (float)atlas->atlas_width;
    const float inv_ah = 1.0f / (float)atlas->atlas_height;

    const uint8_t* text = cmd->_font_draw.text;
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

                float u0 = (float)atlas->glyph_x[c] * inv_aw;
                float v0 = (float)atlas->glyph_y[c] * inv_ah;
                float u1 = (float)(atlas->glyph_x[c] + gw) * inv_aw;
                float v1 = (float)(atlas->glyph_y[c] + gh) * inv_ah;

                /* Match pre-batching Metal (git 732d9a97): window logical -> NDC via fbw_font. */
                const float fw = ctx->fbw_font > 0.0f ? ctx->fbw_font : 1.0f;
                const float fh = ctx->fbh_font > 0.0f ? ctx->fbh_font : 1.0f;
                float cx0 = 2.0f * sx0 / fw - 1.0f;
                float cy0 = 1.0f - 2.0f * sy0 / fh;
                float cx1 = 2.0f * sx1 / fw - 1.0f;
                float cy1 = 1.0f - 2.0f * sy1 / fh;

                float vq[6 * 8] = {
                    cx0, cy0, u0, v0, cr, cg, cb, ca, cx1, cy0, u1, v0, cr, cg, cb, ca,
                    cx1, cy1, u1, v1, cr, cg, cb, ca, cx0, cy0, u0, v0, cr, cg, cb, ca,
                    cx1, cy1, u1, v1, cr, cg, cb, ca, cx0, cy1, u0, v1, cr, cg, cb, ca,
                };
                if( ctx->bft2d )
                    ctx->bft2d->append_glyph_quad(vq);
            }
        }
        int adv = f->char_advance[c];
        if( adv <= 0 )
            adv = 4;
        pen_x += adv;
    }

    if( ctx->bft2d )
        ctx->bft2d->close_open_segment(ctx->b2d_order);
    ctx->split_sprite_before_next_enqueue = true;
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_BATCH_SPRITE_LOAD_START / END
// ---------------------------------------------------------------------------
void
metal_frame_event_batch_sprite_load_start(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t sbid = cmd->_batch.batch_id;
    ctx->renderer->current_sprite_batch_id = sbid;
    ctx->renderer->sprite_cache.begin_batch(sbid, 2048, 2048);
}

void
metal_frame_event_batch_sprite_load_end(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t sbid = cmd->_batch.batch_id;
    auto atlas = ctx->renderer->sprite_cache.finalize_batch(sbid);
    if( atlas.pixels && atlas.w > 0 && atlas.h > 0 )
    {
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:(NSUInteger)atlas.w
                                                              height:(NSUInteger)atlas.h
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> atlasTex = [ctx->device newTextureWithDescriptor:td];
        std::vector<uint8_t>& rgba = ctx->renderer->rgba_scratch;
        rgba.resize((size_t)atlas.w * (size_t)atlas.h * 4u);
        const uint32_t* src = atlas.pixels;
        for( int p = 0; p < atlas.w * atlas.h; ++p )
        {
            uint32_t pix = src[p];
            rgba[(size_t)p * 4u + 0u] = (uint8_t)((pix >> 16) & 0xFFu);
            rgba[(size_t)p * 4u + 1u] = (uint8_t)((pix >> 8) & 0xFFu);
            rgba[(size_t)p * 4u + 2u] = (uint8_t)(pix & 0xFFu);
            uint8_t a_hi = (uint8_t)((pix >> 24) & 0xFFu);
            uint32_t rgb = pix & 0x00FFFFFFu;
            rgba[(size_t)p * 4u + 3u] = (a_hi != 0) ? a_hi : (rgb != 0u ? 0xFFu : 0u);
        }
        [atlasTex replaceRegion:MTLRegionMake2D(0, 0, atlas.w, atlas.h)
                    mipmapLevel:0
                      withBytes:rgba.data()
                    bytesPerRow:(NSUInteger)atlas.w * 4u];
        ctx->renderer->sprite_cache.set_batch_atlas_handle(sbid, (__bridge_retained void*)atlasTex);
    }
    ctx->renderer->current_sprite_batch_id = 0;
}

// ---------------------------------------------------------------------------
// TORIRS_GFX_BATCH_FONT_LOAD_START / END
// ---------------------------------------------------------------------------
void
metal_frame_event_batch_font_load_start(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t fbid = cmd->_batch.batch_id;
    ctx->renderer->current_font_batch_id = fbid;
    ctx->renderer->font_cache.begin_batch(fbid, 1024, 1024);
}

void
metal_frame_event_batch_font_load_end(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const uint32_t fbid = cmd->_batch.batch_id;
    auto atlas = ctx->renderer->font_cache.finalize_batch(fbid);
    if( atlas.pixels && atlas.w > 0 && atlas.h > 0 )
    {
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:(NSUInteger)atlas.w
                                                              height:(NSUInteger)atlas.h
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> atlasTex = [ctx->device newTextureWithDescriptor:td];
        [atlasTex replaceRegion:MTLRegionMake2D(0, 0, atlas.w, atlas.h)
                    mipmapLevel:0
                      withBytes:atlas.pixels
                    bytesPerRow:(NSUInteger)atlas.w * 4u];
        ctx->renderer->font_cache.set_batch_atlas_handle(fbid, (__bridge_retained void*)atlasTex);
    }
    ctx->renderer->current_font_batch_id = 0;
}

