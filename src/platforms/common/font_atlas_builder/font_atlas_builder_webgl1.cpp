#include "platforms/common/font_atlas_builder/font_atlas_builder.h"
#include "platforms/webgl1/webgl1_internal.h"

/**
 * FontAtlasBuilderSubmitWebGL1 — upload each FontAtlasEntry to GPU.
 *
 * For every entry:
 *  - Creates a GL_TEXTURE_2D texture (RGBA8, NEAREST filter, CLAMP_TO_EDGE)
 *    from the caller-supplied RGBA8 pixel data.
 *  - Registers the texture as a standalone font entry in renderer->font_cache.
 *
 * Entries whose font_id is already registered in font_cache are skipped,
 * matching the guard in wg1_font_load_impl.
 *
 * GPU resource lifetime: font_cache holds the GLuint texture name;
 * glDeleteTextures must be called when the cache entry is released.
 */
void
FontAtlasBuilderSubmitWebGL1(
    FontAtlasBuilder& builder,
    WebGL1RenderCtx* ctx)
{
    if( !ctx || !ctx->renderer || builder.empty() )
        return;

    struct Platform2_SDL2_Renderer_WebGL1* r = ctx->renderer;

    for( const FontAtlasEntry& e : builder.entries() )
    {
        if( !e.pixels_rgba || e.width <= 0 || e.height <= 0 )
            continue;
        if( e.font_id < 0 || e.font_id >= GpuFontCache<GLuint>::kMaxFonts )
            continue;

        // Skip if this font_id is already in the cache.
        if( r->font_cache.get_font(e.font_id) )
            continue;

        // Create GL texture.
        GLuint tex = 0;
        glGenTextures(1, &tex);
        if( !tex )
        {
            fprintf(
                stderr,
                "[font_atlas_builder_webgl1] glGenTextures failed for font_id=%d\n",
                e.font_id);
            continue;
        }

        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
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
            e.pixels_rgba);
        glBindTexture(GL_TEXTURE_2D, 0);

        r->font_cache.register_font(e.font_id, tex);
    }
}
