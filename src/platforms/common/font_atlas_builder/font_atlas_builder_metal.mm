// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/font_atlas_builder/font_atlas_builder.h"
#include "platforms/metal/metal_internal.h"

/**
 * FontAtlasBuilderSubmitMetal — upload each FontAtlasEntry to GPU.
 *
 * For every entry:
 *  - Creates a new MTLTexture (RGBA8Unorm, StorageModeShared) from the
 *    caller-supplied RGBA8 pixel data.
 *  - Registers the texture as a standalone font entry in renderer->font_cache.
 *
 * Entries whose font_id is already registered in font_cache are skipped,
 * matching the guard in metal_frame_event_font_load.
 *
 * GPU resource lifetime: font_cache holds the bridge-retained texture;
 * the cache must be shut down or the entry explicitly unloaded to free it.
 */
void
FontAtlasBuilderSubmitMetal(
    FontAtlasBuilder& builder,
    MetalRenderCtx* ctx)
{
    if( !ctx || !ctx->renderer || !ctx->renderer->mtl_device || builder.empty() )
        return;

    for( const FontAtlasEntry& e : builder.entries() )
    {
        if( !e.pixels_rgba || e.width <= 0 || e.height <= 0 )
            continue;

        // Skip if this font_id is already in the cache.
        if( ctx->renderer->font_cache.get_font(e.font_id) )
            continue;

        // Create MTLTexture.
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:(NSUInteger)e.width
                                                              height:(NSUInteger)e.height
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLDevice> device = (__bridge id<MTLDevice>)ctx->renderer->mtl_device;
        id<MTLTexture> tex = [device newTextureWithDescriptor:td];
        if( !tex )
        {
            fprintf(
                stderr,
                "[font_atlas_builder_metal] failed to create texture for "
                "font_id=%d (%dx%d)\n",
                e.font_id,
                e.width,
                e.height);
            continue;
        }

        [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)e.width, (NSUInteger)e.height)
               mipmapLevel:0
                 withBytes:e.pixels_rgba
               bytesPerRow:(NSUInteger)e.width * 4u];

        ctx->renderer->font_cache.register_font(e.font_id, (__bridge_retained void*)tex);
    }
}
