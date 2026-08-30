#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

#include "graphics/winding.h"
#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_raster_kernel_internal.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

/*
 * The kernel families this file's face callbacks draw through. toridraw.c
 * includes the same set, in the same order and under the same PIXEL16 gate,
 * ahead of this file, and every one of them carries an include guard -- so all
 * of this is a no-op in the unity build. It is here so the file states the
 * dependencies it actually calls into and parses on its own, which is what the
 * editor's index does with a .u.c.
 */
// clang-format off
#include "triangles/toridraw_triangle_clip.u.c"
#include "triangles/toridraw_triangle_face_alpha.u.c"
#include "graphics/raster/scanline/scanline.u.c"
#include "triangles/toridraw_triangle_flat.u.c"
#include "triangles/toridraw_triangle_gouraud.u.c"
#ifndef TORIDRAW_PIXEL16
#include "triangles/toridraw_triangle_texture_opaque.u.c"
#include "triangles/toridraw_triangle_texture_transparent.u.c"
#include "triangles/toridraw_triangle_texture_affine.u.c"
#include "triangles/toridraw_triangle_zbuf.u.c"
#endif
// clang-format on

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct ToriDrawModelRasterContext
{
    uint32_t bench_flag;
    toripixel_t* pixel_buffer;
    int* face_infos;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    int num_faces;
    int* vertex_x;
    int* vertex_y;
    int* vertex_z;
    int* orthographic_vertex_x_nullable;
    int* orthographic_vertex_y_nullable;
    int* orthographic_vertex_z_nullable;
    int num_vertices;
    faceint_t* face_textures;
    faceint_t* face_texture_coords;
    int face_texture_coords_length;
    faceint_t* face_p_coordinate_nullable;
    faceint_t* face_m_coordinate_nullable;
    faceint_t* face_n_coordinate_nullable;
    uint8_t* texture_render_types_nullable;
    int num_textured_faces;
    hsl16_t* colors_a;
    hsl16_t* colors_b;
    hsl16_t* colors_c;
    /* scene->sm_face_x4 / sm_face_y4: the six screen coordinates, the
     * near-clip flag and the y-sort permutation the depth sort already had in
     * registers. Only the sorted painter walk reads them, and only for faces
     * the sort accepted. */
    int* face_x4;
    int* face_y4;
    alphaint_t* face_alphas_nullable;
    int offset_x;
    int offset_y;
    int near_plane_z;
    int screen_width;
    int screen_height;
    int stride;
    int camera_cot16;
    struct ToriDraw_TextureMap* texture_map;
    bool affine_textures;
    bool allow_near_clip;
    /* Whether this model's projection could park TORIDRAW_SCREEN_X_NEAR_CLIPPED
     * in screen_vertices_x at all — scene->near_clipped, set by ToriDraw_Project.
     * The per-face sentinel tests below are gated on it, both to skip them
     * entirely for the common model and because the no-clip projection kernel
     * omits the -5001 nudge, so a genuine -5000 is possible when it is false. */
    bool near_clipped;

    /* Last texture resolved through texture_map, memoized. A model's faces are
     * drawn depth-bucketed but almost always share one or two texture ids, so
     * this turns the bounds check plus four dependent loads into one compare.
     * Lives for a single model's raster pass (the context is a stack local
     * rebuilt per model), so a texture swap between models is always seen. */
    int cache_texture_id;
    const int* cache_texels;
    int cache_texture_size;
    int cache_texture_height;
    int cache_texture_opaque;

    /* Fixed depth-raster state. Only ToriDraw_RasterZ initializes or reads it;
     * the painter path never provisions a depth buffer. */
#ifndef TORIDRAW_PIXEL16
    struct ToriDraw_ZbufTarget zbuf_target;
    struct ToriDraw_ZbufFaceSource zbuf_source;
#endif

    /* The public, pass-stable descriptor and complete per-call kernel.
     * Built-in callbacks recover this context through target.internal;
     * application callbacks use only the normalized public fields. */
    struct ToriDraw_RasterTarget target;
    struct ToriDraw_RasterKernelSD kernel;

    /* The raster-debug slot: the counters, and the face count they describe.
     * Present only in a build that asked for the facility -- see
     * toridraw_debug.u.c, which toridraw_render.u.c includes above this file. */
    TORIDRAW_DBG_RASTER_CONTEXT_FIELDS
};

static inline bool
toridraw_raster_face_is_near_clipped(
    const struct ToriDrawModelRasterContext* ctx,
    int face)
{
    if( !ctx->near_clipped )
        return false;

    return ctx->vertex_x[ctx->face_indices_a[face]] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
           ctx->vertex_x[ctx->face_indices_b[face]] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
           ctx->vertex_x[ctx->face_indices_c[face]] == TORIDRAW_SCREEN_X_NEAR_CLIPPED;
}

#ifndef TORIDRAW_PIXEL16
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
static inline void
toridraw_stock_draw_zbuffered_solid(
    struct ToriDrawModelRasterContext* ctx,
    const struct ToriDraw_RasterFaceSD* face,
    int mode)
{
    ToriDraw_TriangleFaceZBuffered(
        &ctx->zbuf_target,
        &ctx->zbuf_source,
        face->face_index,
        mode,
        face->shade[0],
        face->shade[1],
        face->shade[2],
        face->opacity,
        TORIDRAW_ZBUF_TEX_OPAQUE,
        0,
        0,
        0,
        NULL,
        0,
        ctx->allow_near_clip,
        ctx->near_clipped);
}

static void
toridraw_stock_zbuffered_gouraud(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    struct ToriDrawModelRasterContext* ctx = target->internal;

    (void)user_data;
    toridraw_stock_draw_zbuffered_solid(ctx, face, TORIDRAW_ZBUF_MODE_GOURAUD);
}

static void
toridraw_stock_zbuffered_flat(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    struct ToriDrawModelRasterContext* ctx = target->internal;

    (void)user_data;
    toridraw_stock_draw_zbuffered_solid(ctx, face, TORIDRAW_ZBUF_MODE_FLAT);
}

static void
toridraw_stock_zbuffered_textured(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    struct ToriDrawModelRasterContext* ctx = target->internal;
    int const gate = face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE
                         ? TORIDRAW_ZBUF_TEX_OPAQUE
                         : TORIDRAW_ZBUF_TEX_TRANSPARENT;

    (void)user_data;
    ToriDraw_TriangleFaceZBuffered(
        &ctx->zbuf_target,
        &ctx->zbuf_source,
        face->face_index,
        TORIDRAW_ZBUF_MODE_TEXTURE,
        face->shade[0],
        face->shade[1],
        face->shade[2],
        face->opacity,
        gate,
        face->texture.frame.p,
        face->texture.frame.m,
        face->texture.frame.n,
        (int*)face->texture.texels,
        face->texture.width,
        ctx->allow_near_clip,
        ctx->near_clipped);
}
#else
static void
toridraw_stock_zbuffered_unsupported(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    (void)user_data;
    (void)target;
    (void)face;
    assert(false && "The SD z-buffer raster is unavailable in Pixel16");
}
#endif

#define TORIDRAW_STOCK_FLAT_ARGS(ctx, face)                                                        \
    (ctx)->pixel_buffer, (face)->face_index, (ctx)->face_indices_a, (ctx)->face_indices_b,         \
        (ctx)->face_indices_c, (ctx)->vertex_x, (ctx)->vertex_y, (ctx)->vertex_z,                  \
        (ctx)->orthographic_vertex_x_nullable, (ctx)->orthographic_vertex_y_nullable,              \
        (ctx)->orthographic_vertex_z_nullable, (ctx)->colors_a, (ctx)->face_alphas_nullable,       \
        (ctx)->near_plane_z, (ctx)->camera_cot16, (ctx)->offset_x, (ctx)->offset_y, (ctx)->stride, \
        (ctx)->screen_width, (ctx)->screen_height, (ctx)->allow_near_clip, (ctx)->near_clipped

#define TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face)                                                     \
    (ctx)->pixel_buffer, (face)->face_index, (ctx)->face_indices_a, (ctx)->face_indices_b,         \
        (ctx)->face_indices_c, (ctx)->vertex_x, (ctx)->vertex_y, (ctx)->vertex_z,                  \
        (ctx)->orthographic_vertex_x_nullable, (ctx)->orthographic_vertex_y_nullable,              \
        (ctx)->orthographic_vertex_z_nullable, (ctx)->colors_a, (ctx)->colors_b, (ctx)->colors_c,  \
        (ctx)->face_alphas_nullable, (ctx)->near_plane_z, (ctx)->camera_cot16, (ctx)->offset_x,    \
        (ctx)->offset_y, (ctx)->stride, (ctx)->screen_width, (ctx)->screen_height,                 \
        (ctx)->allow_near_clip, (ctx)->near_clipped

#define TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face)                                                     \
    (ctx)->pixel_buffer, (ctx)->stride, (ctx)->screen_width, (ctx)->screen_height,                 \
        (ctx)->camera_cot16, (face)->face_index, (face)->texture.frame.p, (face)->texture.frame.m, \
        (face)->texture.frame.n, (ctx)->face_indices_a, (ctx)->face_indices_b,                     \
        (ctx)->face_indices_c, (ctx)->vertex_x, (ctx)->vertex_y, (ctx)->vertex_z,                  \
        (ctx)->orthographic_vertex_x_nullable, (ctx)->orthographic_vertex_y_nullable,              \
        (ctx)->orthographic_vertex_z_nullable

#define TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face)                                                     \
    (int*)(face)->texture.texels, (face)->texture.width, (ctx)->near_plane_z, (ctx)->offset_x,     \
        (ctx)->offset_y, (ctx)->allow_near_clip, (ctx)->near_clipped

#define TORIDRAW_DEFINE_STOCK_GOURAUD(name, draw_fn)                                               \
    static void name(                                                                              \
        void* user_data,                                                                           \
        const struct ToriDraw_RasterTarget* target,                                                \
        const struct ToriDraw_RasterFaceSD* face)                                                  \
    {                                                                                              \
        struct ToriDrawModelRasterContext* ctx = target->internal;                                 \
        (void)user_data;                                                                           \
        (void)target;                                                                              \
        draw_fn(TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face));                                           \
    }

#define TORIDRAW_DEFINE_STOCK_FLAT(name, flat_fn)                                                  \
    static void name(                                                                              \
        void* user_data,                                                                           \
        const struct ToriDraw_RasterTarget* target,                                                \
        const struct ToriDraw_RasterFaceSD* face)                                                  \
    {                                                                                              \
        struct ToriDrawModelRasterContext* ctx = target->internal;                                 \
        (void)user_data;                                                                           \
        (void)target;                                                                              \
        flat_fn(TORIDRAW_STOCK_FLAT_ARGS(ctx, face));                                              \
    }

#ifndef TORIDRAW_PIXEL16
#define TORIDRAW_DEFINE_STOCK_TEXTURED_GOURAUD(name, opaque_fn, trans_fn, affine_fn)               \
    static void name(                                                                              \
        void* user_data,                                                                           \
        const struct ToriDraw_RasterTarget* target,                                                \
        const struct ToriDraw_RasterFaceSD* face)                                                  \
    {                                                                                              \
        struct ToriDrawModelRasterContext* ctx = target->internal;                                 \
        bool const affine = ctx->target.affine_textures || face->texture.render_type != 0;         \
        (void)user_data;                                                                           \
        (void)target;                                                                              \
        if( affine )                                                                               \
            affine_fn(                                                                             \
                TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face),                                            \
                (ctx)->colors_a,                                                                   \
                (ctx)->colors_b,                                                                   \
                (ctx)->colors_c,                                                                   \
                (int*)(face)->texture.texels,                                                      \
                (face)->texture.width,                                                             \
                (face)->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE,                            \
                (ctx)->near_plane_z,                                                               \
                (ctx)->offset_x,                                                                   \
                (ctx)->offset_y,                                                                   \
                (ctx)->allow_near_clip,                                                            \
                (ctx)->near_clipped);                                                              \
        else if( face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE )                            \
            opaque_fn(                                                                             \
                TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face),                                            \
                (ctx)->colors_a,                                                                   \
                (ctx)->colors_b,                                                                   \
                (ctx)->colors_c,                                                                   \
                TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                           \
        else                                                                                       \
            trans_fn(                                                                              \
                TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face),                                            \
                (ctx)->colors_a,                                                                   \
                (ctx)->colors_b,                                                                   \
                (ctx)->colors_c,                                                                   \
                TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                           \
    }

#define TORIDRAW_DEFINE_STOCK_TEXTURED_FLAT(name, opaque_fn, trans_fn, affine_fn)                  \
    static void name(                                                                              \
        void* user_data,                                                                           \
        const struct ToriDraw_RasterTarget* target,                                                \
        const struct ToriDraw_RasterFaceSD* face)                                                  \
    {                                                                                              \
        struct ToriDrawModelRasterContext* ctx = target->internal;                                 \
        bool const affine = ctx->target.affine_textures || face->texture.render_type != 0;         \
        (void)user_data;                                                                           \
        (void)target;                                                                              \
        if( affine )                                                                               \
            affine_fn(                                                                             \
                TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face),                                            \
                (ctx)->colors_a,                                                                   \
                (int*)(face)->texture.texels,                                                      \
                (face)->texture.width,                                                             \
                (face)->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE,                            \
                (ctx)->near_plane_z,                                                               \
                (ctx)->offset_x,                                                                   \
                (ctx)->offset_y,                                                                   \
                (ctx)->allow_near_clip,                                                            \
                (ctx)->near_clipped);                                                              \
        else if( face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE )                            \
            opaque_fn(                                                                             \
                TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face),                                            \
                (ctx)->colors_a,                                                                   \
                TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                           \
        else                                                                                       \
            trans_fn(                                                                              \
                TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face),                                            \
                (ctx)->colors_a,                                                                   \
                TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                           \
    }

TORIDRAW_DEFINE_STOCK_TEXTURED_GOURAUD(
    toridraw_stock_branching_textured_gouraud,
    ToriDraw_TriangleFaceTextureBlendOpaqueBranching,
    ToriDraw_TriangleFaceTextureBlendTransparentBranching,
    ToriDraw_TriangleFaceTextureBlendAffineV3Branching)
TORIDRAW_DEFINE_STOCK_TEXTURED_FLAT(
    toridraw_stock_branching_textured_flat,
    ToriDraw_TriangleFaceTextureFlatOpaqueBranching,
    ToriDraw_TriangleFaceTextureFlatTransparentBranching,
    ToriDraw_TriangleFaceTextureFlatAffineV3Branching)
TORIDRAW_DEFINE_STOCK_TEXTURED_GOURAUD(
    toridraw_stock_scanline_textured_gouraud,
    ToriDraw_TriangleFaceTextureBlendOpaqueScanline,
    ToriDraw_TriangleFaceTextureBlendTransparentScanline,
    ToriDraw_TriangleFaceTextureBlendAffineV3Scanline)
TORIDRAW_DEFINE_STOCK_TEXTURED_FLAT(
    toridraw_stock_scanline_textured_flat,
    ToriDraw_TriangleFaceTextureFlatOpaqueScanline,
    ToriDraw_TriangleFaceTextureFlatTransparentScanline,
    ToriDraw_TriangleFaceTextureFlatAffineV3Scanline)
#endif

TORIDRAW_DEFINE_STOCK_GOURAUD(
    toridraw_stock_branching_gouraud,
    ToriDraw_TriangleFaceGouraudBranching)
TORIDRAW_DEFINE_STOCK_GOURAUD(
    toridraw_stock_scanline_gouraud,
    ToriDraw_TriangleFaceGouraudScanline)
TORIDRAW_DEFINE_STOCK_GOURAUD(
    toridraw_stock_smooth_branching_gouraud,
    ToriDraw_TriangleFaceGouraudSmoothBranching)
TORIDRAW_DEFINE_STOCK_GOURAUD(
    toridraw_stock_smooth_scanline_gouraud,
    ToriDraw_TriangleFaceGouraudSmoothScanline)
TORIDRAW_DEFINE_STOCK_FLAT(
    toridraw_stock_branching_flat,
    ToriDraw_TriangleFaceFlatBranching)
TORIDRAW_DEFINE_STOCK_FLAT(
    toridraw_stock_scanline_flat,
    ToriDraw_TriangleFaceFlatScanline)

#ifdef TORIDRAW_PIXEL16
static void
toridraw_stock_unreachable_textured(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    (void)user_data;
    (void)target;
    (void)face;
    assert(false && "Pixel16 stock classification emitted a textured face");
}
#define toridraw_stock_branching_textured_gouraud toridraw_stock_unreachable_textured
#define toridraw_stock_branching_textured_flat toridraw_stock_unreachable_textured
#define toridraw_stock_scanline_textured_gouraud toridraw_stock_unreachable_textured
#define toridraw_stock_scanline_textured_flat toridraw_stock_unreachable_textured
#endif

/* Both defined below, once the walks exist; named here so the prebaked
 * kernels can point at them. ToriDraw_RasterWalkPerFace is the stock stage-3
 * implementation and is public: a kernel that only wants to supply face
 * callbacks names it and gets the whole normalizing walk for free. */
void
ToriDraw_RasterWalkPerFace(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx);

#include "graphics/raster/batch/raster.batch.h"

/*
 * The prebaked SD raster kernels, one file each.
 *
 * Here rather than higher up: each names the stock callbacks defined above,
 * and the texture slots are macros that resolve to an unreachable stub under
 * TORIDRAW_PIXEL16, so the kernels have to be assembled after that choice.
 */
// clang-format off
#include "kernels/sd.branching.u.c"
#include "kernels/sd.branching_perface.u.c"
#include "kernels/sd.scanline.u.c"
#include "kernels/sd.smooth_branching.u.c"
#include "kernels/sd.smooth_scanline.u.c"
#include "kernels/sd.zbuffered.u.c"
// clang-format on

#undef toridraw_stock_scanline_textured_flat
#undef toridraw_stock_scanline_textured_gouraud
#undef toridraw_stock_branching_textured_flat
#undef toridraw_stock_branching_textured_gouraud
#undef TORIDRAW_DEFINE_STOCK_TEXTURED_FLAT
#undef TORIDRAW_DEFINE_STOCK_TEXTURED_GOURAUD
#undef TORIDRAW_DEFINE_STOCK_FLAT
#undef TORIDRAW_DEFINE_STOCK_GOURAUD
#undef TORIDRAW_STOCK_TEXTURE_TAIL
#undef TORIDRAW_STOCK_TEXTURE_ARGS
#undef TORIDRAW_STOCK_GOURAUD_ARGS
#undef TORIDRAW_STOCK_FLAT_ARGS

/* Interface-backed face preparation used by every production face loop. */
static inline void
ToriDraw_RasterModelFaceKernel(
    int face,
    struct ToriDrawModelRasterContext* ctx)
{
    TORIDRAW_DBG_RASTER_LOCAL(ctx)
    struct ToriDraw_RasterFaceSD prepared;
    int raw_type;
    int color_a;
    int color_b;
    int color_c;
    bool near_clipped;

    assert(face >= 0 && face < ctx->num_faces);

    raw_type = ctx->face_infos ? ctx->face_infos[face] : 0;
    TORIDRAW_DBG_RASTER_TYPE(dbg, raw_type);
    if( raw_type == 2 || raw_type < 0 || raw_type > 3 )
    {
        TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_type);
        return;
    }

    prepared.face_index = face;
    prepared.vertex[0] = ctx->face_indices_a[face];
    prepared.vertex[1] = ctx->face_indices_b[face];
    prepared.vertex[2] = ctx->face_indices_c[face];

    TORIDRAW_DBG_RASTER_INDEX_OOB(dbg, ctx, face, prepared.vertex);

    color_a = ctx->colors_a[face];
    color_b = ctx->colors_b[face];
    color_c = ctx->colors_c[face];
    if( color_c == TORIDRAWHSL16_HIDDEN )
    {
        TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_hidden);
        return;
    }

#ifndef TORIDRAW_PIXEL16
    {
        int texture_id = ctx->face_textures ? ctx->face_textures[face] : -1;

        /* Preserve the existing textured-path bisect knob. */
        static int skip_textured = -1;
        if( skip_textured < 0 )
            skip_textured = getenv("TORIDRAW_SKIP_TEXTURED") ? 1 : 0;
        if( skip_textured && texture_id != -1 )
            return;

        if( texture_id != -1 )
        {
            const int* texels;
            int texture_size;
            int texture_height;
            int texture_opaque;
            int coord = ctx->face_texture_coords ? ctx->face_texture_coords[face] : -1;
            int render_type = 0;
            int p;
            int m;
            int n;

            if( texture_id == ctx->cache_texture_id )
            {
                texels = ctx->cache_texels;
                texture_size = ctx->cache_texture_size;
                texture_height = ctx->cache_texture_height;
                texture_opaque = ctx->cache_texture_opaque;
            }
            else
            {
                struct ToriDraw_Texture* texture =
                    (texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY)
                        ? ToriDraw_TextureMapGet(ctx->texture_map, texture_id)
                        : NULL;
                if( !texture )
                {
                    TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_tex_miss);
                    TORIDRAW_DBG_RASTER_TEX_MISS(texture_id);
                    return;
                }
                texels = texture->texels;
                texture_size = texture->width;
                texture_height = texture->height;
                texture_opaque = texture->opaque;
                ctx->cache_texture_id = texture_id;
                ctx->cache_texels = texels;
                ctx->cache_texture_size = texture_size;
                ctx->cache_texture_height = texture_height;
                ctx->cache_texture_opaque = texture_opaque;
            }

            if( !texels || texture_size <= 0 || texture_height <= 0 )
            {
                TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_tex_miss);
                TORIDRAW_DBG_RASTER_TEX_MISS(texture_id);
                return;
            }

            if( coord != -1 )
            {
                if( coord < 0 || coord >= ctx->num_textured_faces )
                {
                    TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_tex_coord);
                    return;
                }
                if( ctx->texture_render_types_nullable )
                    render_type = ctx->texture_render_types_nullable[coord] & 0xFF;

                if( render_type == 0 )
                {
                    if( !ctx->face_p_coordinate_nullable || !ctx->face_m_coordinate_nullable ||
                        !ctx->face_n_coordinate_nullable )
                    {
                        TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_tex_coord);
                        return;
                    }
                    p = ctx->face_p_coordinate_nullable[coord];
                    m = ctx->face_m_coordinate_nullable[coord];
                    n = ctx->face_n_coordinate_nullable[coord];
                    prepared.texture.frame_fallback = false;
                }
                else
                {
                    /* Non-plane records contain axes, not indices. Stock keeps
                     * its affine face-frame fallback while retaining the byte. */
                    p = prepared.vertex[0];
                    m = prepared.vertex[1];
                    n = prepared.vertex[2];
                    prepared.texture.frame_fallback = true;
                }
            }
            else
            {
                /* Preserve the historical internal fallback: when the face
                 * index names a mapping record, its type may still force the
                 * affine family even though A/B/C provide the safe frame. */
                if( ctx->texture_render_types_nullable && face < ctx->num_textured_faces )
                    render_type = ctx->texture_render_types_nullable[face] & 0xFF;
                p = prepared.vertex[0];
                m = prepared.vertex[1];
                n = prepared.vertex[2];
                prepared.texture.frame_fallback = true;
            }

            TORIDRAW_DBG_RASTER_TEX_MODE(face, coord, render_type);

            if( p < 0 || p >= ctx->num_vertices || m < 0 || m >= ctx->num_vertices || n < 0 ||
                n >= ctx->num_vertices )
            {
                TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_tex_coord);
                return;
            }

            near_clipped = toridraw_raster_face_is_near_clipped(ctx, face);
            prepared.near_clipped = near_clipped;
            if( near_clipped && !ctx->allow_near_clip )
            {
                TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_near_clip);
                return;
            }
            if( !ctx->orthographic_vertex_x_nullable || !ctx->orthographic_vertex_y_nullable ||
                !ctx->orthographic_vertex_z_nullable )
            {
                TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_near_clip);
                return;
            }

            prepared.face_class = color_c == TORIDRAWHSL16_FLAT
                                      ? TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT
                                      : TORIDRAW_RASTER_FACE_SD_TEXTURED;
            prepared.shade[0] = color_a;
            prepared.shade[1] = color_c == TORIDRAWHSL16_FLAT ? color_a : color_b;
            prepared.shade[2] = color_c == TORIDRAWHSL16_FLAT ? color_a : color_c;
            /* Stock texture kernels historically ignore authored face alpha. */
            prepared.opacity = 0xFF;
            prepared.texture.texture_id = texture_id;
            prepared.texture.texels = texels;
            prepared.texture.width = texture_size;
            prepared.texture.height = texture_height;
            prepared.texture.gate =
                texture_opaque ? TORIDRAW_RASTER_TEXTURE_OPAQUE : TORIDRAW_RASTER_TEXTURE_COLOR_KEY;
            prepared.texture.render_type = (unsigned int)render_type;
            prepared.texture.frame.p = p;
            prepared.texture.frame.m = m;
            prepared.texture.frame.n = n;

            TORIDRAW_DBG_RASTER_DREW_TEXTURED(dbg, texture_id);

            ToriDraw_RasterKernelSDDispatch(&ctx->kernel, &ctx->target, &prepared);
            return;
        }
    }
#endif

    prepared.opacity = ctx->face_alphas_nullable ? 0xFF - ctx->face_alphas_nullable[face] : 0xFF;
    if( prepared.opacity <= 1 )
    {
        TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_alpha);
        return;
    }

    near_clipped = toridraw_raster_face_is_near_clipped(ctx, face);
    prepared.near_clipped = near_clipped;
    if( near_clipped &&
        (!ctx->allow_near_clip || !ctx->orthographic_vertex_x_nullable ||
         !ctx->orthographic_vertex_y_nullable || !ctx->orthographic_vertex_z_nullable) )
    {
        TORIDRAW_DBG_RASTER_BUMP(dbg, skipped_near_clip);
        return;
    }

    if( color_c == TORIDRAWHSL16_FLAT )
    {
        prepared.face_class = TORIDRAW_RASTER_FACE_SD_FLAT;
        prepared.shade[0] = color_a;
        prepared.shade[1] = color_a;
        prepared.shade[2] = color_a;
    }
    else
    {
        prepared.face_class = TORIDRAW_RASTER_FACE_SD_GOURAUD;
        prepared.shade[0] = color_a;
        prepared.shade[1] = color_b;
        prepared.shade[2] = color_c;
    }

    TORIDRAW_DBG_RASTER_DREW_SHADED(
        dbg,
        prepared.face_class == TORIDRAW_RASTER_FACE_SD_FLAT,
        color_a,
        color_b,
        color_c);

    ToriDraw_RasterKernelSDDispatch(&ctx->kernel, &ctx->target, &prepared);
}

static inline void
context_from_handle(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    struct ToriDrawModelRasterContext* ctx)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        ctx->num_faces = m->face_count;
        ctx->face_infos = m->face_infos;
        ctx->face_indices_a = m->face_indices_a;
        ctx->face_indices_b = m->face_indices_b;
        ctx->face_indices_c = m->face_indices_c;
        ctx->num_faces = m->face_count;
        ctx->vertex_x = scene->screen_vertices_x;
        ctx->vertex_y = scene->screen_vertices_y;
        ctx->vertex_z = scene->screen_vertices_z;
        ctx->orthographic_vertex_x_nullable = scene->orthographic_vertices_x;
        ctx->orthographic_vertex_y_nullable = scene->orthographic_vertices_y;
        ctx->orthographic_vertex_z_nullable = scene->orthographic_vertices_z;
        ctx->num_vertices = m->vertex_count;
        ctx->face_textures = m->face_textures;
        ctx->face_texture_coords = m->face_texture_coords;
        ctx->face_texture_coords_length = m->textured_face_count;
        ctx->face_p_coordinate_nullable = m->textured_p_coordinate;
        ctx->face_m_coordinate_nullable = m->textured_m_coordinate;
        ctx->face_n_coordinate_nullable = m->textured_n_coordinate;
        ctx->texture_render_types_nullable = m->texture_render_types;
        ctx->num_textured_faces = m->textured_face_count;
        ctx->colors_a = m->face_colors_a;
        ctx->colors_b = m->face_colors_b;
        ctx->colors_c = m->face_colors_c;
        ctx->face_alphas_nullable = m->face_alphas;
        /* Rebase clip so left/top become 0: existing x_start<0 / y<0 clamps apply.
         * Caller advances pixel_buffer by clip_left + clip_top*stride. */
        {
            int clip_left = view_port->clip_left;
            int clip_top = view_port->clip_top;
            int clip_right = view_port->clip_right > 0 ? view_port->clip_right : view_port->width;
            int clip_bottom =
                view_port->clip_bottom > 0 ? view_port->clip_bottom : view_port->height;
            if( clip_left < 0 )
                clip_left = 0;
            if( clip_top < 0 )
                clip_top = 0;
            if( clip_right < clip_left )
                clip_right = clip_left;
            if( clip_bottom < clip_top )
                clip_bottom = clip_top;
            ctx->screen_width = clip_right - clip_left;
            ctx->screen_height = clip_bottom - clip_top;
            /* Projection origin = center of the (rebased) clip rect. The
             * perspective texture kernels anchor their uv basis at
             * screen_width/2, screen_height/2 with no way to shift it, so the
             * geometry has to use that same origin or every textured face is
             * skewed by the difference. Full-surface viewports (clip == the
             * whole target) are unaffected: there the clip center already is
             * width/2, height/2. */
            ctx->offset_x = ctx->screen_width >> 1;
            ctx->offset_y = ctx->screen_height >> 1;
        }
        /* Must match the plane ToriDraw_Project actually clipped against: the
         * near-clip builders lerp their new vertices onto it, and a plane the
         * projection never used would place them where nothing was cut. */
        ctx->near_plane_z = scene->projection_near_plane_z;
        ctx->stride = view_port->stride ? view_port->stride : view_port->width;
        ctx->camera_cot16 =
            toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048);
        ctx->texture_map = &ToriDraw_SceneTexState(scene)->texture_map;
        ctx->cache_texture_id = -1;
        ctx->cache_texels = NULL;
        ctx->cache_texture_size = 0;
        ctx->cache_texture_height = 0;
        ctx->cache_texture_opaque = 0;
        ctx->affine_textures = camera->texture_affine != 0;
        ctx->allow_near_clip = ToriDraw_ModelHasTextures(hnd);
        ctx->near_clipped = scene->near_clipped;
        TORIDRAW_DBG_RASTER_DISARM(ctx);
        break;
    }
    default:
        break;
    }
}

/**
 * Is this face facing the camera?
 *
 * Only the model-order walk asks. On the sorted path the depth bucketer answers it
 * — it drops back-facing triangles before they reach a kernel — so a walk that
 * skips the sort has to do the cull itself or it draws the model's inside
 * surfaces as well as its outside ones. Same test, same sign convention, and the
 * same near-clip exemption: a face with a vertex behind the eye has no
 * screen-space winding yet, so it is kept and the near-clip rebuild decides.
 */
static inline bool
toridraw_raster_face_front_facing(
    const struct ToriDrawModelRasterContext* ctx,
    int face)
{
    int const a = ctx->face_indices_a[face];
    int const b = ctx->face_indices_b[face];
    int const c = ctx->face_indices_c[face];

    if( ctx->near_clipped && (ctx->vertex_x[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                              ctx->vertex_x[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                              ctx->vertex_x[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED) )
        return true;

    return toridraw_winding_2d_front_facing(
        ctx->vertex_x[a],
        ctx->vertex_y[a],
        ctx->vertex_x[b],
        ctx->vertex_y[b],
        ctx->vertex_x[c],
        ctx->vertex_y[c]);
}

static inline void
toridraw_raster_context_init(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel,
    struct ToriDrawModelRasterContext* ctx
    TORIDRAW_DBG_RASTER_STORAGE_PARAM)
{
    struct ToriDraw_Model* model;
    int clip_left;
    int clip_top;

    model = model_as_full(hnd);
    context_from_handle(scene, hnd, view_port, camera, ctx);
    ctx->kernel = *kernel;
    ctx->face_x4 = scene->sm_face_x4;
    ctx->face_y4 = scene->sm_face_y4;
    clip_left = view_port->clip_left > 0 ? view_port->clip_left : 0;
    clip_top = view_port->clip_top > 0 ? view_port->clip_top : 0;
    ctx->pixel_buffer = pixel_buffer + clip_left + clip_top * ctx->stride;

    memset(&ctx->target, 0, sizeof(ctx->target));
    ctx->target.pixel_buffer = ctx->pixel_buffer;
    ctx->target.width = ctx->screen_width;
    ctx->target.height = ctx->screen_height;
    ctx->target.stride = ctx->stride;
    ctx->target.clip_origin_x = clip_left;
    ctx->target.clip_origin_y = clip_top;
    ctx->target.projection_center_x = ctx->offset_x;
    ctx->target.projection_center_y = ctx->offset_y;
    ctx->target.near_plane_z = ctx->near_plane_z;
    ctx->target.camera_cot16 = ctx->camera_cot16;
    ctx->target.model_mid_z = scene->projected_vertex.z;
    ctx->target.parallel_projection = toridraw_proj_is_parallel(camera->proj_mode);
    ctx->target.affine_textures = ctx->affine_textures;
    ctx->target.near_clip_available = ctx->allow_near_clip && ctx->orthographic_vertex_x_nullable &&
                                      ctx->orthographic_vertex_y_nullable &&
                                      ctx->orthographic_vertex_z_nullable;
    ctx->target.vertex_count = ctx->num_vertices;
    ctx->target.screen_vertices_x = ctx->vertex_x;
    ctx->target.screen_vertices_y = ctx->vertex_y;
    ctx->target.screen_vertices_z = ctx->vertex_z;
    ctx->target.orthographic_vertices_x = ctx->orthographic_vertex_x_nullable;
    ctx->target.orthographic_vertices_y = ctx->orthographic_vertex_y_nullable;
    ctx->target.orthographic_vertices_z = ctx->orthographic_vertex_z_nullable;
    ctx->target.posed_vertices_x = model->vertices_x;
    ctx->target.posed_vertices_y = model->vertices_y;
    ctx->target.posed_vertices_z = model->vertices_z;
    ctx->target.bind_vertices_x =
        model->original_vertices_x ? model->original_vertices_x : model->vertices_x;
    ctx->target.bind_vertices_y =
        model->original_vertices_y ? model->original_vertices_y : model->vertices_y;
    ctx->target.bind_vertices_z =
        model->original_vertices_z ? model->original_vertices_z : model->vertices_z;
    ctx->target.internal = ctx;

    TORIDRAW_DBG_RASTER_ARM(ctx);
}

#include "graphics/raster/texture/tex_tri_asm.h"
#include "toridraw_raster_batch.h"

/* ABLATION SUPPORT (measurement only) -- see the TORIRS_ABL_NOFACES arm in
 * toridraw_raster_draw_faces. Read once; off is one predicted branch. */
static int
toridraw_raster_abl_nofaces(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_ABL_NOFACES") ? 1 : 0;
    return armed;
}

/* The batched whole-model walk. One file, one gate, at the boundary where the
 * presorted-run assembly is actually named. */
#include "graphics/raster/batch/raster.batch.u.c"

void
ToriDraw_RasterWalkPerFace(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx)
{
    /* ABLATION (TORIRS_ABL_NOFACES=1, measurement only): the model was
     * projected and its faces sorted; the whole per-face walk -- classify,
     * gather, prepare, dispatch, fill -- is withheld. Subtracting this from
     * the TORIRS_ABL_NOKERNEL arm leaves the per-face overhead a staged
     * pipeline is trying to delete, without the fill it is not trying to
     * touch. ordered_faces is still published so the debug stats keep
     * describing the same walk. */
    int const skip_faces = toridraw_raster_abl_nofaces();

    (void)user_data;

    if( ctx->kernel.flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
    {
        /* The sorter already culled back-facing faces. */
        TORIDRAW_DBG_RASTER_ORDERED(ctx, scene->tmp_face_order_count);
        if( skip_faces )
            return;
        for( int i = 0; i < scene->tmp_face_order_count; i++ )
            ToriDraw_RasterModelFaceKernel(scene->tmp_face_order[i], ctx);
    }
    else
    {
        TORIDRAW_DBG_RASTER_ORDERED(ctx, ctx->num_faces);
        if( skip_faces )
            return;
        for( int face = 0; face < ctx->num_faces; face++ )
        {
            if( !toridraw_raster_face_front_facing(ctx, face) )
                continue;
            ToriDraw_RasterModelFaceKernel(face, ctx);
        }
    }
}

/*
 * Stage 3 is one call. Which walk runs is the kernel's own business.
 *
 * A NULL draw_model is the stock walk, not a contract violation -- the same
 * defaulting the projection and face_sort slots use, and what lets a kernel
 * that only supplies the four leaf callbacks stay a valid aggregate
 * initializer.
 */
static inline void
toridraw_raster_draw_faces(
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx)
{
    if( ctx->kernel.draw_model )
        ctx->kernel.draw_model(ctx->kernel.user_data, scene, ctx);
    else
        ToriDraw_RasterWalkPerFace(ctx->kernel.user_data, scene, ctx);
}

static inline bool
ToriDraw_RasterPainter(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    TORIDRAW_DBG_RASTER_STORAGE
    struct ToriDrawModelRasterContext ctx;

    assert(!(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER));
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
        toridraw_raster_context_init(
            scene, hnd, view_port, camera, pixel_buffer, kernel, &ctx
                TORIDRAW_DBG_RASTER_STORAGE_ARG);
        toridraw_raster_draw_faces(scene, &ctx);
        TORIDRAW_DBG_RASTER_PRINT(&ctx, (void*)model_as_full(hnd));
        return true;
    default:
        assert(false && "Invalid model handle kind");
        return false;
    }
}

#ifndef TORIDRAW_PIXEL16
static inline bool
ToriDraw_RasterZ(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    TORIDRAW_DBG_RASTER_STORAGE
    struct ToriDrawModelRasterContext ctx;
    int rows;

    assert(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER);
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
        toridraw_raster_context_init(
            scene, hnd, view_port, camera, pixel_buffer, kernel, &ctx
                TORIDRAW_DBG_RASTER_STORAGE_ARG);

        rows = ctx.target.clip_origin_y + ctx.screen_height;
        if( !ToriDraw_SceneHasZBuffer(scene, ctx.stride, rows) )
        {
            bool const provisioned = ToriDraw_SceneZBufferResize(scene, ctx.stride, rows);

            assert(provisioned);
            (void)provisioned;
        }
        assert(ToriDraw_SceneHasZBuffer(scene, ctx.stride, rows));

        ctx.zbuf_target.pixel_buffer = ctx.pixel_buffer;
        ctx.zbuf_target.zbuffer =
            scene->zbuffer + ctx.target.clip_origin_x + ctx.target.clip_origin_y * ctx.stride;
        ctx.zbuf_target.stride = ctx.stride;
        ctx.zbuf_target.screen_width = ctx.screen_width;
        ctx.zbuf_target.screen_height = ctx.screen_height;
        ctx.zbuf_target.camera_cot16 = ctx.camera_cot16;
        ctx.zbuf_target.offset_x = ctx.offset_x;
        ctx.zbuf_target.offset_y = ctx.offset_y;
        ctx.zbuf_target.near_plane_z = ctx.near_plane_z;
        ctx.zbuf_target.model_mid_z = scene->projected_vertex.z;
        ctx.zbuf_target.parallel = toridraw_proj_is_parallel(camera->proj_mode);

        ctx.zbuf_source.face_indices_a = ctx.face_indices_a;
        ctx.zbuf_source.face_indices_b = ctx.face_indices_b;
        ctx.zbuf_source.face_indices_c = ctx.face_indices_c;
        ctx.zbuf_source.screen_vertices_x = ctx.vertex_x;
        ctx.zbuf_source.screen_vertices_y = ctx.vertex_y;
        ctx.zbuf_source.screen_vertices_z = ctx.vertex_z;
        ctx.zbuf_source.orthographic_vertices_x = ctx.orthographic_vertex_x_nullable;
        ctx.zbuf_source.orthographic_vertices_y = ctx.orthographic_vertex_y_nullable;
        ctx.zbuf_source.orthographic_vertices_z = ctx.orthographic_vertex_z_nullable;

        ctx.target.zbuffer = ctx.zbuf_target.zbuffer;
        ctx.target.depth_test = true;

        /* The reset is what confines depth testing to this model. */
        toridraw_zbuf_reset(
            ctx.zbuf_target.zbuffer,
            ctx.zbuf_target.stride,
            ctx.screen_width,
            ctx.screen_height,
            ctx.vertex_x,
            ctx.vertex_y,
            ctx.num_vertices,
            ctx.offset_x,
            ctx.offset_y,
            ctx.near_clipped,
            ctx.zbuf_target.parallel);

        toridraw_raster_draw_faces(scene, &ctx);
        TORIDRAW_DBG_RASTER_PRINT(&ctx, (void*)model_as_full(hnd));
        return true;
    default:
        assert(false && "Invalid model handle kind");
        return false;
    }
}
#else
static inline bool
ToriDraw_RasterZ(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER);
    assert(false && "The SD z-buffer raster is unavailable in Pixel16");
    (void)scene;
    (void)hnd;
    (void)view_port;
    (void)camera;
    (void)pixel_buffer;
    return false;
}
#endif
