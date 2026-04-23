// System ObjC/Metal headers must come before any game headers.
#include "platforms/common/sprite_atlas_builder/sprite_atlas_builder.h"
#include "platforms/common/torirs_sprite_argb_rgba.h"
#include "platforms/metal/metal_internal.h"

/**
 * SpriteAtlasBuilderSubmitMetal — upload each SpriteAtlasEntry to GPU.
 *
 * For every entry:
 *  - Converts ARGB pixels to RGBA8 (via torirs_copy_sprite_argb_pixels_to_rgba8).
 *  - Creates a new MTLTexture (RGBA8Unorm, StorageModeShared).
 *  - Registers the texture as a standalone entry in renderer->sprite_cache.
 *
 * Entries whose (element_id, atlas_index) are already registered in
 * sprite_cache are released and replaced, matching the behaviour of
 * metal_frame_event_sprite_load.
 *
 * GPU resource lifetime: the sprite_cache holds the bridge-retained texture;
 * call sprite_cache.unload_sprite() / release the cache entry to free it.
 */
void
SpriteAtlasBuilderSubmitMetal(
    SpriteAtlasBuilder& builder,
    MetalRenderCtx* ctx)
{
    if( !ctx || !ctx->device || !ctx->renderer || builder.empty() )
        return;

    std::vector<uint8_t>& rgba = ctx->renderer->rgba_scratch;

    for( const SpriteAtlasEntry& e : builder.entries() )
    {
        if( !e.pixels_argb || e.width <= 0 || e.height <= 0 )
            continue;

        // Unload any previously-registered standalone entry for this slot.
        {
            auto* existing = ctx->renderer->sprite_cache.get_sprite(e.element_id, e.atlas_index);
            if( existing && existing->atlas_texture && !existing->is_batched )
                CFRelease(existing->atlas_texture);
            ctx->renderer->sprite_cache.unload_sprite(e.element_id, e.atlas_index);
        }

        // ARGB → RGBA conversion.
        const int pixel_count = e.width * e.height;
        rgba.resize((size_t)pixel_count * 4u);
        torirs_copy_sprite_argb_pixels_to_rgba8(e.pixels_argb, rgba.data(), pixel_count);

        // Create MTLTexture.
        MTLTextureDescriptor* td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:(NSUInteger)e.width
                                                              height:(NSUInteger)e.height
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeShared;
        id<MTLTexture> tex = [ctx->device newTextureWithDescriptor:td];
        if( !tex )
        {
            fprintf(
                stderr,
                "[sprite_atlas_builder_metal] failed to create texture for "
                "element_id=%d atlas_index=%d (%dx%d)\n",
                e.element_id,
                e.atlas_index,
                e.width,
                e.height);
            continue;
        }

        [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)e.width, (NSUInteger)e.height)
               mipmapLevel:0
                 withBytes:rgba.data()
               bytesPerRow:(NSUInteger)e.width * 4u];

        ctx->renderer->sprite_cache.register_standalone(
            e.element_id, e.atlas_index, (__bridge_retained void*)tex);
    }
}
