#ifndef TORIDRAW_TRIANGLE_RSZBUF_U_C
#define TORIDRAW_TRIANGLE_RSZBUF_U_C

/*
 * One rev-241 face to the rszbuf rasterizer, one entry per face kind, as
 * tri.flat.u.c, tri.gouraud.u.c and the tri.texture_*.u.c files are for the
 * painter [class174.method5688 -> method5704].
 *
 * Each kind's Impl draws the projected triangle, and hands a face the walk
 * flagged near-clipped to that kind's NearClipImpl [class174.method5742],
 * which gathers the polygon with tri.rszbuf.clip.u.c and draws it as one
 * triangle or two. The flag is the walk's (RSZBUF_FACE_NEAR_CLIPPED, set in
 * camera space), not a sentinel test here: the client decides it once per
 * face, before the draw.
 *
 * `shade_*` are the client's colour words for the face: a flat face's are A,
 * 0 and -1 (toridraw_rszbuf_face_colors), because the clip lerps them. Depth
 * keys take the face's bias [class174.field2408 * 2] off the camera depth
 * before the key.
 *
 * A false return is the client throwing (a clip edge past its reciprocal
 * table): the walk draws no more of the model.
 */

#include "graphics/raster/rszbuf/rszbuf_model.h"
#include "graphics/raster/rszbuf/rszbuf_raster.h"
#include "graphics/raster/rszbuf/rszbuf_texture_planes.h"
#include "impl/raster/dispatch/tri.rszbuf.clip.u.c"
#include "impl/raster/rszbuf/raster.rszbuf.flat.zbuf.scalar.u.c"
#include "impl/raster/rszbuf/raster.rszbuf.gouraud.zbuf.scalar.u.c"
#include "impl/raster/rszbuf/raster.rszbuf.texture.linear.zbuf.scalar.u.c"
#include "impl/raster/rszbuf/raster.rszbuf.texture.perspective.zbuf.lerp8.scalar.u.c"

#include <stdbool.h>

/** The depth keys of a projected face's three corners. */
static inline void
toridraw_rszbuf_face_keys(
    const struct ToriDraw_RsZbufModel* rz,
    int a,
    int b,
    int c,
    int depth_bias,
    float key[3])
{
    RSZBUF_NO_FMA
    key[0] = rszbuf_depth_key(rz->depth[a] - (float)depth_bias);
    key[1] = rszbuf_depth_key(rz->depth[b] - (float)depth_bias);
    key[2] = rszbuf_depth_key(rz->depth[c] - (float)depth_bias);
}

/** The clipped polygon's point `i` as the rasterizer's float coordinates. */
#define RSZBUF_CLIP_Y(i) ((float)g_toridraw_rszbuf_clip_y[i])
#define RSZBUF_CLIP_X(i) ((float)g_toridraw_rszbuf_clip_x[i])

/* ------------------------------------------------------------------ flat */

static inline bool
ToriDraw_RsZbufTriangleFaceFlatNearClipImpl(
    struct ToriDraw_RsZbufRaster* r,
    const struct ToriDraw_RsZbufModel* rz,
    int a,
    int b,
    int c,
    int depth_bias,
    int shade_a,
    int shade_b,
    int shade_c,
    int32_t rgb)
{
    float key[4];
    int const clipped_count = ToriDraw_RsZbufTriangleClip(
        rz, r->center_x, r->center_y, r->projection_scale, a, b, c, shade_a, shade_b, shade_c);

    if( clipped_count < 0 )
        return false;
    ToriDraw_RsZbufTriangleClipKeys(depth_bias, key);
    r->clip_x = ToriDraw_RsZbufTriangleClipSpansX(clipped_count, r->clip_width);

    if( clipped_count == 3 || clipped_count == 4 )
        rszbuf_triangle_flat(
            r, RSZBUF_CLIP_Y(0), RSZBUF_CLIP_Y(1), RSZBUF_CLIP_Y(2), RSZBUF_CLIP_X(0),
            RSZBUF_CLIP_X(1), RSZBUF_CLIP_X(2), key[0], key[1], key[2], rgb);
    if( clipped_count == 4 )
        rszbuf_triangle_flat(
            r, RSZBUF_CLIP_Y(0), RSZBUF_CLIP_Y(2), RSZBUF_CLIP_Y(3), RSZBUF_CLIP_X(0),
            RSZBUF_CLIP_X(2), RSZBUF_CLIP_X(3), key[0], key[2], key[3], rgb);
    return true;
}

/** A flat face; `rgb` is colour A through the palette [field2375]. */
static inline bool
ToriDraw_RsZbufTriangleFaceFlat(
    struct ToriDraw_RsZbufRaster* r,
    const struct ToriDraw_RsZbufModel* rz,
    int a,
    int b,
    int c,
    int depth_bias,
    bool near_clipped,
    bool clip_x,
    int shade_a,
    int shade_b,
    int shade_c,
    int32_t rgb)
{
    float key[3];

    if( near_clipped )
        return ToriDraw_RsZbufTriangleFaceFlatNearClipImpl(
            r, rz, a, b, c, depth_bias, shade_a, shade_b, shade_c, rgb);

    toridraw_rszbuf_face_keys(rz, a, b, c, depth_bias, key);
    r->clip_x = clip_x;
    rszbuf_triangle_flat(
        r, rz->screen_y[a], rz->screen_y[b], rz->screen_y[c], rz->screen_x[a], rz->screen_x[b],
        rz->screen_x[c], key[0], key[1], key[2], rgb);
    return true;
}

/* --------------------------------------------------------------- gouraud */

static inline bool
ToriDraw_RsZbufTriangleFaceGouraudNearClipImpl(
    struct ToriDraw_RsZbufRaster* r,
    const struct ToriDraw_RsZbufModel* rz,
    int a,
    int b,
    int c,
    int depth_bias,
    int shade_a,
    int shade_b,
    int shade_c)
{
    float key[4];
    int const* const shade = g_toridraw_rszbuf_clip_shade;
    int const clipped_count = ToriDraw_RsZbufTriangleClip(
        rz, r->center_x, r->center_y, r->projection_scale, a, b, c, shade_a, shade_b, shade_c);

    if( clipped_count < 0 )
        return false;
    ToriDraw_RsZbufTriangleClipKeys(depth_bias, key);
    r->clip_x = ToriDraw_RsZbufTriangleClipSpansX(clipped_count, r->clip_width);

    if( clipped_count == 3 || clipped_count == 4 )
        rszbuf_triangle_gouraud(
            r, RSZBUF_CLIP_Y(0), RSZBUF_CLIP_Y(1), RSZBUF_CLIP_Y(2), RSZBUF_CLIP_X(0),
            RSZBUF_CLIP_X(1), RSZBUF_CLIP_X(2), key[0], key[1], key[2], shade[0], shade[1],
            shade[2]);
    if( clipped_count == 4 )
        rszbuf_triangle_gouraud(
            r, RSZBUF_CLIP_Y(0), RSZBUF_CLIP_Y(2), RSZBUF_CLIP_Y(3), RSZBUF_CLIP_X(0),
            RSZBUF_CLIP_X(2), RSZBUF_CLIP_X(3), key[0], key[2], key[3], shade[0], shade[2],
            shade[3]);
    return true;
}

static inline bool
ToriDraw_RsZbufTriangleFaceGouraud(
    struct ToriDraw_RsZbufRaster* r,
    const struct ToriDraw_RsZbufModel* rz,
    int a,
    int b,
    int c,
    int depth_bias,
    bool near_clipped,
    bool clip_x,
    int shade_a,
    int shade_b,
    int shade_c)
{
    float key[3];

    if( near_clipped )
        return ToriDraw_RsZbufTriangleFaceGouraudNearClipImpl(
            r, rz, a, b, c, depth_bias, shade_a, shade_b, shade_c);

    toridraw_rszbuf_face_keys(rz, a, b, c, depth_bias, key);
    r->clip_x = clip_x;
    rszbuf_triangle_gouraud(
        r, rz->screen_y[a], rz->screen_y[b], rz->screen_y[c], rz->screen_x[a], rz->screen_x[b],
        rz->screen_x[c], key[0], key[1], key[2], shade_a, shade_b, shade_c);
    return true;
}

/* -------------------------------------------------------------- textured */

/**
 * The textured face's mapping triangle, by its p/m/n vertices, in the camera-
 * space ints [field2368 / field2362 / field2398].
 */
static inline void
toridraw_rszbuf_texture_frame(
    const struct ToriDraw_RsZbufModel* rz,
    int p,
    int m,
    int n,
    struct ToriDraw_RsZbufTextureFrame* frame)
{
    int const vertex[3] = { p, m, n };

    for( int i = 0; i < 3; i++ )
    {
        frame->x[i] = rz->camera_x[vertex[i]];
        frame->y[i] = rz->camera_y[vertex[i]];
        frame->z[i] = rz->camera_z[vertex[i]];
    }
}

/** What a textured face draws with besides its corners. */
struct ToriDraw_RsZbufFaceTexture
{
    struct ToriDraw_RsZbufTextureFrame frame;
    const int* texels;
    bool opaque;
    /** Tiles take the linear span [class162.method5171], models the 8-pixel
     *  perspective fit [method5170]. */
    bool linear;
};

static inline void
toridraw_rszbuf_texture_triangle(
    struct ToriDraw_RsZbufRaster* r,
    const struct ToriDraw_RsZbufFaceTexture* texture,
    float y_a,
    float y_b,
    float y_c,
    float x_a,
    float x_b,
    float x_c,
    float key_a,
    float key_b,
    float key_c,
    int shade_a,
    int shade_b,
    int shade_c)
{
    if( texture->linear )
        rszbuf_triangle_texture_linear(
            r, y_a, y_b, y_c, x_a, x_b, x_c, key_a, key_b, key_c, shade_a, shade_b, shade_c,
            &texture->frame, texture->texels, texture->opaque);
    else
        rszbuf_triangle_texture(
            r, y_a, y_b, y_c, x_a, x_b, x_c, key_a, key_b, key_c, shade_a, shade_b, shade_c,
            &texture->frame, texture->texels, texture->opaque);
}

/**
 * A textured face behind the plane. A gouraud-lit one takes the clip's shades;
 * a flat-lit one takes the first point's shade for a triangle, and its own
 * colour A for both halves of a quad -- the client's two branches differ.
 */
static inline bool
ToriDraw_RsZbufTriangleFaceTextureNearClipImpl(
    struct ToriDraw_RsZbufRaster* r,
    const struct ToriDraw_RsZbufModel* rz,
    const struct ToriDraw_RsZbufFaceTexture* texture,
    int a,
    int b,
    int c,
    int depth_bias,
    bool flat_shaded,
    int shade_a,
    int shade_b,
    int shade_c)
{
    float key[4];
    int const* const shade = g_toridraw_rszbuf_clip_shade;
    int const clipped_count = ToriDraw_RsZbufTriangleClip(
        rz, r->center_x, r->center_y, r->projection_scale, a, b, c, shade_a, shade_b, shade_c);

    if( clipped_count < 0 )
        return false;
    ToriDraw_RsZbufTriangleClipKeys(depth_bias, key);
    r->clip_x = ToriDraw_RsZbufTriangleClipSpansX(clipped_count, r->clip_width);

    if( clipped_count == 3 )
    {
        int const s1 = flat_shaded ? shade[0] : shade[1];
        int const s2 = flat_shaded ? shade[0] : shade[2];

        toridraw_rszbuf_texture_triangle(
            r, texture, RSZBUF_CLIP_Y(0), RSZBUF_CLIP_Y(1), RSZBUF_CLIP_Y(2), RSZBUF_CLIP_X(0),
            RSZBUF_CLIP_X(1), RSZBUF_CLIP_X(2), key[0], key[1], key[2], shade[0], s1, s2);
        return true;
    }
    if( clipped_count != 4 )
        return true;

    {
        int const first[3] = { flat_shaded ? shade_a : shade[0], flat_shaded ? shade_a : shade[1],
                               flat_shaded ? shade_a : shade[2] };
        int const second[3] = { flat_shaded ? shade_a : shade[0], flat_shaded ? shade_a : shade[2],
                                flat_shaded ? shade_a : shade[3] };

        toridraw_rszbuf_texture_triangle(
            r, texture, RSZBUF_CLIP_Y(0), RSZBUF_CLIP_Y(1), RSZBUF_CLIP_Y(2), RSZBUF_CLIP_X(0),
            RSZBUF_CLIP_X(1), RSZBUF_CLIP_X(2), key[0], key[1], key[2], first[0], first[1],
            first[2]);
        toridraw_rszbuf_texture_triangle(
            r, texture, RSZBUF_CLIP_Y(0), RSZBUF_CLIP_Y(2), RSZBUF_CLIP_Y(3), RSZBUF_CLIP_X(0),
            RSZBUF_CLIP_X(2), RSZBUF_CLIP_X(3), key[0], key[2], key[3], second[0], second[1],
            second[2]);
    }
    return true;
}

/** A textured face; a flat-lit one draws colour A at every corner. */
static inline bool
ToriDraw_RsZbufTriangleFaceTexture(
    struct ToriDraw_RsZbufRaster* r,
    const struct ToriDraw_RsZbufModel* rz,
    const struct ToriDraw_RsZbufFaceTexture* texture,
    int a,
    int b,
    int c,
    int depth_bias,
    bool near_clipped,
    bool clip_x,
    bool flat_shaded,
    int shade_a,
    int shade_b,
    int shade_c)
{
    float key[3];

    if( near_clipped )
        return ToriDraw_RsZbufTriangleFaceTextureNearClipImpl(
            r, rz, texture, a, b, c, depth_bias, flat_shaded, shade_a, shade_b, shade_c);

    toridraw_rszbuf_face_keys(rz, a, b, c, depth_bias, key);
    r->clip_x = clip_x;
    toridraw_rszbuf_texture_triangle(
        r, texture, rz->screen_y[a], rz->screen_y[b], rz->screen_y[c], rz->screen_x[a],
        rz->screen_x[b], rz->screen_x[c], key[0], key[1], key[2], shade_a,
        flat_shaded ? shade_a : shade_b, flat_shaded ? shade_a : shade_c);
    return true;
}

#undef RSZBUF_CLIP_Y
#undef RSZBUF_CLIP_X

#endif
