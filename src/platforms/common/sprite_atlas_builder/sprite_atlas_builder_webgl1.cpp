#include "platforms/common/sprite_atlas_builder/sprite_atlas_builder.h"
#include "platforms/common/torirs_sprite_argb_rgba.h"
#include "platforms/webgl1/webgl1_internal.h"

/**
 * SpriteAtlasBuilderSubmitWebGL1 — upload each SpriteAtlasEntry to GPU.
 *
 * For every entry:
 *  - Converts ARGB pixels to RGBA8 (via torirs_copy_sprite_argb_pixels_to_rgba8).
 *  - Creates a GL_TEXTURE_2D texture (RGBA8, LINEAR filter, CLAMP_TO_EDGE).
 *  - Registers the texture as a standalone entry in renderer->sprite_cache.
 *
 * Entries whose (element_id, atlas_index) are already registered in
 * sprite_cache are released and replaced, matching the behaviour of
 * wg1_frame_event_sprite_load.
 *
 * GPU resource lifetime: the sprite_cache holds the GLuint texture name;
 * call sprite_cache.unload_sprite() and glDeleteTextures to free it.
 */
void
SpriteAtlasBuilderSubmitWebGL1(
    SpriteAtlasBuilder& builder,
    WebGL1RenderCtx* ctx)
{
    if( !ctx || !ctx->renderer || builder.empty() )
        return;

    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;
    std::vector<uint8_t>& rgba = r->rgba_scratch;

    for( const SpriteAtlasEntry& e : builder.entries() )
    {
        if( !e.pixels_argb || e.width <= 0 || e.height <= 0 )
            continue;

        // Release and unload any existing standalone entry for this slot.
        {
            auto* existing = r->sprite_cache.get_sprite(e.element_id, e.atlas_index);
            if( existing && existing->atlas_texture && !existing->is_batched )
                glDeleteTextures(1, &existing->atlas_texture);
            r->sprite_cache.unload_sprite(e.element_id, e.atlas_index);
        }

        // ARGB → RGBA conversion.
        const int pixel_count = e.width * e.height;
        rgba.resize((size_t)pixel_count * 4u);
        torirs_copy_sprite_argb_pixels_to_rgba8(e.pixels_argb, rgba.data(), pixel_count);

        // Create GL texture.
        GLuint tex = 0;
        glGenTextures(1, &tex);
        if( !tex )
        {
            fprintf(
                stderr,
                "[sprite_atlas_builder_webgl1] glGenTextures failed for "
                "element_id=%d atlas_index=%d\n",
                e.element_id,
                e.atlas_index);
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            e.width,
            e.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        r->sprite_cache.register_standalone(e.element_id, e.atlas_index, tex);
    }
}
