#ifndef TORIDRAW_KERNELS_SD_COMPAT_FLAT_U_C
#define TORIDRAW_KERNELS_SD_COMPAT_FLAT_U_C

/*
 * Compatibility SD painter: every face ONE flat fill.
 *
 * For a device too slow for the game's own raster (the web device check's
 * compatibility mode, torirs_devicecheck.js): no gouraud, no texture mapping.
 *
 *   untextured   the face's lit colour; a gouraud face takes the mean of its
 *                three corners' lightness under the first corner's hue and
 *                saturation (one fill, no interpolation)
 *   textured     the texture's average colour, lit by the face the way the
 *                reference flattens a ground texture under
 *                Scene.lowDetailTextures (Scene.adjustHslLightness:
 *                hue and saturation kept, lightness (avg & 127) * light >> 7,
 *                clamped to 2..126)
 *   see-through  a colour-keyed texture (RGB 0 not drawn) is averaged over
 *                the texels that ARE drawn and filled at their coverage as
 *                alpha: a fence of 40% bars draws at 40%
 *
 * A face that reaches behind the near plane is dropped rather than clipped:
 * clipping is work, and such a face is a sliver at the screen's edge.
 *
 * Included from toridraw_raster.u.c with the other SD kernels; selected by
 * ToriDraw_RasterSetCompat / TORIDRAW_RASTER_COMPAT=1, through
 * ToriDraw_KernelGetStock (table.software_compat.u.c).
 */

#include "osrs/palette.h"

/* One texture's flat stand-in, computed the first time a face asks and kept
 * while the texture's texels stay the same block. */
struct ToriDraw_CompatTexture
{
    const int* texels;
    int hsl;
    int alpha;
};

static struct ToriDraw_CompatTexture g_compat_textures[TORIDRAW_TEXTURE_ID_CAPACITY];

static const struct ToriDraw_CompatTexture*
toridraw_compat_texture(const struct ToriDraw_RasterTextureSD* texture)
{
    struct ToriDraw_CompatTexture* slot;
    int const count = texture->width * texture->height;
    bool const keyed = texture->gate != TORIDRAW_RASTER_TEXTURE_OPAQUE;
    long red = 0;
    long green = 0;
    long blue = 0;
    int drawn = 0;

    assert(texture->texture_id >= 0);
    assert(texture->texture_id < TORIDRAW_TEXTURE_ID_CAPACITY);
    assert(texture->texels);
    assert(count > 0);
    slot = &g_compat_textures[texture->texture_id];
    if( slot->texels == texture->texels )
        return slot;
    for( int i = 0; i < count; i++ )
    {
        int const rgb = texture->texels[i] & 0xFFFFFF;
        if( keyed && rgb == 0 )
            continue;
        red += (rgb >> 16) & 0xFF;
        green += (rgb >> 8) & 0xFF;
        blue += rgb & 0xFF;
        drawn++;
    }
    if( drawn == 0 )
    {
        slot->hsl = 0;
        slot->alpha = 0;
    }
    else
    {
        slot->hsl = palette_rgb_to_hsl16(
            (int)(((red / drawn) << 16) | ((green / drawn) << 8) | (blue / drawn)));
        slot->alpha = keyed ? (drawn * 0xFF) / count : 0xFF;
    }
    slot->texels = texture->texels;
    return slot;
}

/* Scene.adjustHslLightness, the reference's flattened texture colour. */
static inline int
toridraw_compat_adjust_lightness(
    int hsl,
    int light)
{
    int l = ((hsl & 127) * light) >> 7;
    if( l < 2 )
        l = 2;
    else if( l > 126 )
        l = 126;
    return (hsl & 0xFF80) + l;
}

static void
toridraw_compat_flat_face(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    struct ToriDrawModelRasterContext* ctx = target->internal;
    int const f = face->face_index;
    int const a = ctx->face_indices_a[f];
    int const b = ctx->face_indices_b[f];
    int const c = ctx->face_indices_c[f];
    int const x1 = ctx->vertex_x[a];
    int const x2 = ctx->vertex_x[b];
    int const x3 = ctx->vertex_x[c];
    int hsl;
    int alpha;

    (void)user_data;
    if( x1 == TORIDRAW_SCREEN_X_NEAR_CLIPPED || x2 == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
        x3 == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
        return;
    if( face->face_class == TORIDRAW_RASTER_FACE_SD_TEXTURED ||
        face->face_class == TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT )
    {
        const struct ToriDraw_CompatTexture* t = toridraw_compat_texture(&face->texture);
        int const light = (face->shade[0] + face->shade[1] + face->shade[2]) / 3;

        if( t->alpha == 0 )
            return;
        hsl = toridraw_compat_adjust_lightness(t->hsl, light);
        alpha = t->alpha;
    }
    else
    {
        int const l = ((face->shade[0] & 127) + (face->shade[1] & 127) + (face->shade[2] & 127)) / 3;

        hsl = (face->shade[0] & 0xFF80) | l;
        alpha = face->opacity;
    }
    ToriDraw_TriangleFlatBranching(
        ctx->pixel_buffer,
        ctx->stride,
        ctx->screen_width,
        ctx->screen_height,
        x1 + ctx->offset_x,
        x2 + ctx->offset_x,
        x3 + ctx->offset_x,
        ctx->vertex_y[a] + ctx->offset_y,
        ctx->vertex_y[b] + ctx->offset_y,
        ctx->vertex_y[c] + ctx->offset_y,
        hsl,
        alpha);
}

static const struct ToriDraw_RasterKernelSDVTable g_compat_flat_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_compat_flat_face,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_compat_flat_face,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_compat_flat_face,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_compat_flat_face,
    },
};

static struct ToriDraw_RasterKernelSD g_compat_flat_kernel = {
    .name = "compat_flat",
    .draw_model = ToriDraw_RasterWalkPerFace,
    .vtable = &g_compat_flat_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
    .zbuffered_variant = &g_stock_sorted_zbuffered_kernel,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetCompatFlat(void)
{
    return &g_compat_flat_kernel;
}

#endif /* TORIDRAW_KERNELS_SD_COMPAT_FLAT_U_C */
