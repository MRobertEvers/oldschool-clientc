#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_raster_kernel_internal.h"
#include "toridraw_types.h"
#include "graphics/winding.h"

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
    /* scene->sm_face_xy: the six screen coordinates and the near-clip flag the
     * depth sort already had in registers. Only the sorted painter walk reads
     * it, and only for faces that sort accepted. */
    int* face_xy;
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

    /* Non-NULL only when TORIDRAW_RASTER_DEBUG >= 1. */
    struct ToriDraw_RasterDebugStats* raster_debug;
    /* #region agent log */
    int ordered_faces;
    /* #endregion */
};

/** Per-model raster counters: one bucket per skip reason plus drawn faces.
 *  Mirrors the sort-side ToriDraw_FaceSortDebugStats, same env-var pattern. */
struct ToriDraw_RasterDebugStats
{
    int drawn;
    int skipped_type;       /* face_infos raw_type == 2 (hidden) or outside 0..3 */
    int skipped_hidden;     /* color_c == TORIDRAWHSL16_HIDDEN sentinel */
    int skipped_alpha;      /* non-textured face with alpha <= 1 (fully transparent) */
    int skipped_near_clip;  /* near-clipped vertex but !allow_near_clip or no ortho buf */
    int skipped_tex_miss;   /* face_textures != -1 but texture not yet loaded */
    int skipped_tex_coord;  /* malformed explicit coord/frame indices */
    int index_oob;          /* face vertex index >= num_vertices (logic error) */
    int type_hist[16];      /* histogram of raw face_infos values 0..15 */

    /* #region agent log */
    /* Which kernel each drawn face actually reached, so a wrong-pixels report
     * can be attributed to shading or to texturing without guessing. */
    int drawn_gouraud;
    int drawn_flat;
    int drawn_textured;
    /* Largest per-face hsl16 vertex delta among drawn gouraud faces. The
     * colour gradient multiplies this by dy, so it decides whether the
     * barycentric step can overflow. */
    int max_color_delta;
    /* Distinct texture ids the drawn faces referenced (first few only). */
    int tex_ids[8];
    int tex_id_count;
    /* #endregion */
};

/* #region agent log */
static inline void
toridraw_raster_debug_note_texture(struct ToriDraw_RasterDebugStats* s, int texture_id)
{
    for( int i = 0; i < s->tex_id_count; i++ )
        if( s->tex_ids[i] == texture_id )
            return;
    if( s->tex_id_count < (int)(sizeof(s->tex_ids) / sizeof(s->tex_ids[0])) )
        s->tex_ids[s->tex_id_count++] = texture_id;
}
/* #endregion */

static inline int
toridraw_raster_debug_level(void)
{
    static int level = -1;
    if( level < 0 )
    {
        const char* value = getenv("TORIDRAW_RASTER_DEBUG");
        if( !value || value[0] == '\0' || value[0] == '0' )
            level = 0;
        else if( value[0] == '2' || strcmp(value, "verbose") == 0 || strcmp(value, "all") == 0 )
            level = 2;
        else
            level = 1;
    }
    return level;
}

/*
 * TORIRS_RASTER_TEX_MODE_DEBUG, resolved once.
 *
 * Its call site sits on the per-face non-plane/affine path, so a per-call
 * getenv() walks environ with a
 * strncmp per entry for every textured triangle in the frame. That measured
 * two million calls in four hundred frames of an idle scene, which is the same
 * reason TORIRS_RASTER_TEX_DEBUG is cached a few hundred lines down.
 */
static inline int
toridraw_raster_tex_mode_debug(void)
{
    static int enabled = -1;
    if( enabled < 0 )
        enabled = getenv("TORIRS_RASTER_TEX_MODE_DEBUG") ? 1 : 0;
    return enabled;
}

static inline bool
toridraw_raster_debug_enabled(void)
{
    /* #region agent log */
    if( toridraw_dbg_enabled() )
        return true;
    /* #endregion */
    return toridraw_raster_debug_level() != 0;
}

static inline void
toridraw_raster_debug_print(
    const struct ToriDraw_RasterDebugStats* s,
    const struct ToriDrawModelRasterContext* ctx,
    const void* model_ptr)
{
    /* Level 1: only report models with anomalies.
     * Level 2 (verbose): report every model. */
    bool anomaly = s->skipped_type > 0 || s->skipped_hidden > 0 || s->skipped_alpha > 0 ||
                   s->skipped_near_clip > 0 || s->skipped_tex_miss > 0 ||
                   s->skipped_tex_coord > 0 || s->index_oob > 0;

    /* #region agent log */
    if( toridraw_dbg_enabled() )
    {
        static int gate_nearclip = 0;
        static int gate_texmiss = 0;
        static int gate_recip = 0;
        static int gate_other = 0;
        static int last_recip_oob = 0;
        bool const recip_oob = g_toridraw_clip_recip_oob != last_recip_oob;
        bool const near_clip_loss = s->skipped_near_clip > 0;
        bool const tex_loss = s->skipped_tex_miss > 0;
        bool const big = ctx->num_faces >= 2000;
        const char* hyp = recip_oob ? "F" : (near_clip_loss ? "B" : (tex_loss ? "E" : "BE"));
        bool emit = false;

        last_recip_oob = g_toridraw_clip_recip_oob;
        if( recip_oob )
            emit = toridraw_dbg_gate(&gate_recip, 200);
        else if( near_clip_loss )
            emit = toridraw_dbg_gate(&gate_nearclip, 200);
        else if( tex_loss )
            emit = toridraw_dbg_gate(&gate_texmiss, 200);
        else if( big || anomaly )
            emit = toridraw_dbg_gate(&gate_other, 120);

        if( emit )
        {
            char data[900];
            char tex_buf[96];
            int tex_pos = 0;

            tex_buf[0] = '\0';
            for( int i = 0; i < s->tex_id_count && tex_pos < (int)sizeof(tex_buf) - 10; i++ )
                tex_pos += snprintf(
                    tex_buf + tex_pos,
                    sizeof(tex_buf) - (size_t)tex_pos,
                    "%s%d",
                    i ? "," : "",
                    s->tex_ids[i]);

            snprintf(
                data,
                sizeof(data),
                "{\"model\":\"%p\",\"faces\":%d,\"vertices\":%d,\"ordered\":%d,"
                "\"drawn\":%d,\"skip_type\":%d,\"skip_hidden\":%d,\"skip_alpha\":%d,"
                "\"skip_near_clip\":%d,\"skip_tex_miss\":%d,\"skip_tex_coord\":%d,"
                "\"index_oob\":%d,"
                "\"allow_near_clip\":%d,\"near_clipped\":%d,"
                "\"near_plane_z\":%d,\"clip_recip_oob\":%d,"
                "\"drawn_gouraud\":%d,\"drawn_flat\":%d,\"drawn_textured\":%d,"
                "\"max_color_delta\":%d,\"tex_ids\":[%s],"
                "\"tex_plane_max_shift\":%d,\"tex_plane_rejected\":%d}",
                model_ptr,
                ctx->num_faces,
                ctx->num_vertices,
                ctx->ordered_faces,
                s->drawn,
                s->skipped_type,
                s->skipped_hidden,
                s->skipped_alpha,
                s->skipped_near_clip,
                s->skipped_tex_miss,
                s->skipped_tex_coord,
                s->index_oob,
                (int)ctx->allow_near_clip,
                (int)ctx->near_clipped,
                ctx->near_plane_z,
                g_toridraw_clip_recip_oob,
                s->drawn_gouraud,
                s->drawn_flat,
                s->drawn_textured,
                s->max_color_delta,
                tex_buf,
                g_toridraw_tex_plane_max_shift,
                g_toridraw_tex_plane_rejected);
            toridraw_dbg_log(
                hyp,
                "toridraw_raster.u.c:raster_faces",
                recip_oob
                    ? "near-clip depth span exceeded the reciprocal table"
                    : (near_clip_loss
                           ? "faces dropped: near-clip not allowed for this model"
                           : (tex_loss ? "faces dropped: texture not resident"
                                       : "raster face accounting")),
                data);
        }
    }
    /* #endregion */

    if( toridraw_raster_debug_level() < 2 && !anomaly )
        return;

    /* Compact type histogram: only print buckets that are non-zero. */
    char hist_buf[128];
    int hist_pos = 0;
    hist_buf[0] = '\0';
    for( int t = 0; t < 16 && hist_pos < (int)sizeof(hist_buf) - 12; t++ )
    {
        if( s->type_hist[t] )
            hist_pos +=
                snprintf(hist_buf + hist_pos, sizeof(hist_buf) - (size_t)hist_pos,
                         " t%d=%d", t, s->type_hist[t]);
    }

    fprintf(
        stderr,
        "raster_face: model=%p faces=%d vertices=%d "
        "drawn=%d skip_type=%d skip_hidden=%d skip_alpha=%d "
        "skip_near_clip=%d skip_tex_miss=%d skip_tex_coord=%d index_oob=%d "
        "allow_near_clip=%d near_clipped=%d face_infos=%s type_hist[%s]\n",
        model_ptr,
        ctx->num_faces,
        ctx->num_vertices,
        s->drawn,
        s->skipped_type,
        s->skipped_hidden,
        s->skipped_alpha,
        s->skipped_near_clip,
        s->skipped_tex_miss,
        s->skipped_tex_coord,
        s->index_oob,
        (int)ctx->allow_near_clip,
        (int)ctx->near_clipped,
        ctx->face_infos ? "yes" : "no",
        hist_buf);
}

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

#define TORIDRAW_STOCK_FLAT_ARGS(ctx, face)                                                     \
    (ctx)->pixel_buffer, (face)->face_index, (ctx)->face_indices_a, (ctx)->face_indices_b,       \
        (ctx)->face_indices_c, (ctx)->vertex_x, (ctx)->vertex_y, (ctx)->vertex_z,                \
        (ctx)->orthographic_vertex_x_nullable, (ctx)->orthographic_vertex_y_nullable,            \
        (ctx)->orthographic_vertex_z_nullable, (ctx)->colors_a, (ctx)->face_alphas_nullable,     \
        (ctx)->near_plane_z, (ctx)->camera_cot16, (ctx)->offset_x, (ctx)->offset_y,              \
        (ctx)->stride, (ctx)->screen_width, (ctx)->screen_height, (ctx)->allow_near_clip,        \
        (ctx)->near_clipped

#define TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face)                                                  \
    (ctx)->pixel_buffer, (face)->face_index, (ctx)->face_indices_a, (ctx)->face_indices_b,       \
        (ctx)->face_indices_c, (ctx)->vertex_x, (ctx)->vertex_y, (ctx)->vertex_z,                \
        (ctx)->orthographic_vertex_x_nullable, (ctx)->orthographic_vertex_y_nullable,            \
        (ctx)->orthographic_vertex_z_nullable, (ctx)->colors_a, (ctx)->colors_b, (ctx)->colors_c,\
        (ctx)->face_alphas_nullable, (ctx)->near_plane_z, (ctx)->camera_cot16,                   \
        (ctx)->offset_x, (ctx)->offset_y, (ctx)->stride, (ctx)->screen_width,                    \
        (ctx)->screen_height, (ctx)->allow_near_clip, (ctx)->near_clipped

#define TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face)                                                  \
    (ctx)->pixel_buffer, (ctx)->stride, (ctx)->screen_width, (ctx)->screen_height,               \
        (ctx)->camera_cot16, (face)->face_index, (face)->texture.frame.p,                        \
        (face)->texture.frame.m, (face)->texture.frame.n,                                       \
        (ctx)->face_indices_a, (ctx)->face_indices_b, (ctx)->face_indices_c, (ctx)->vertex_x,    \
        (ctx)->vertex_y, (ctx)->vertex_z, (ctx)->orthographic_vertex_x_nullable,                 \
        (ctx)->orthographic_vertex_y_nullable, (ctx)->orthographic_vertex_z_nullable

#define TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face)                                                  \
    (int*)(face)->texture.texels, (face)->texture.width, (ctx)->near_plane_z, (ctx)->offset_x,   \
        (ctx)->offset_y, (ctx)->allow_near_clip, (ctx)->near_clipped

#define TORIDRAW_DEFINE_STOCK_GOURAUD(name, draw_fn)                                             \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFaceSD* face)                                   \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        (void)user_data;                                                                         \
        (void)target;                                                                            \
        draw_fn(TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face));                                         \
    }

#define TORIDRAW_DEFINE_STOCK_FLAT(name, flat_fn)                                                \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFaceSD* face)                                   \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        (void)user_data;                                                                         \
        (void)target;                                                                            \
        flat_fn(TORIDRAW_STOCK_FLAT_ARGS(ctx, face));                                            \
    }

#ifndef TORIDRAW_PIXEL16
#define TORIDRAW_DEFINE_STOCK_TEXTURED_GOURAUD(name, opaque_fn, trans_fn, affine_fn)              \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFaceSD* face)                                   \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        bool const affine = ctx->target.affine_textures || face->texture.render_type != 0;       \
        (void)user_data;                                                                         \
        (void)target;                                                                            \
        if( affine )                                                                             \
            affine_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,                   \
                      (ctx)->colors_b, (ctx)->colors_c, (int*)(face)->texture.texels,             \
                      (face)->texture.width,                                                      \
                      (face)->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE,                    \
                      (ctx)->near_plane_z, (ctx)->offset_x, (ctx)->offset_y,                      \
                      (ctx)->allow_near_clip, (ctx)->near_clipped);                              \
        else if( face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE )                          \
            opaque_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,                   \
                      (ctx)->colors_b, (ctx)->colors_c,                                           \
                      TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                   \
        else                                                                                     \
            trans_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,                    \
                     (ctx)->colors_b, (ctx)->colors_c,                                            \
                     TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                    \
    }

#define TORIDRAW_DEFINE_STOCK_TEXTURED_FLAT(name, opaque_fn, trans_fn, affine_fn)                 \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFaceSD* face)                                   \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        bool const affine = ctx->target.affine_textures || face->texture.render_type != 0;       \
        (void)user_data;                                                                         \
        (void)target;                                                                            \
        if( affine )                                                                             \
            affine_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,                   \
                      (int*)(face)->texture.texels, (face)->texture.width,                        \
                      (face)->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE,                    \
                      (ctx)->near_plane_z, (ctx)->offset_x, (ctx)->offset_y,                      \
                      (ctx)->allow_near_clip, (ctx)->near_clipped);                              \
        else if( face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE )                          \
            opaque_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,                   \
                      TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                   \
        else                                                                                     \
            trans_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,                    \
                     TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                                    \
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

static const struct ToriDraw_RasterKernelSDVTable g_stock_branching_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_branching_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_branching_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_branching_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_branching_textured_flat,
    },
};

static const struct ToriDraw_RasterKernelSDVTable g_stock_scanline_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_scanline_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_scanline_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_scanline_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_scanline_textured_flat,
    },
};

static const struct ToriDraw_RasterKernelSDVTable g_stock_smooth_branching_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_smooth_branching_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_branching_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_branching_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_branching_textured_flat,
    },
};

static const struct ToriDraw_RasterKernelSDVTable g_stock_smooth_scanline_vtable = {
    .draw = {
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_smooth_scanline_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_scanline_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_scanline_textured_gouraud,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_scanline_textured_flat,
    },
};

static const struct ToriDraw_RasterKernelSDVTable g_stock_zbuffered_vtable = {
    .draw = {
#ifdef TORIDRAW_PIXEL16
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_zbuffered_unsupported,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_zbuffered_unsupported,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_zbuffered_unsupported,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_zbuffered_unsupported,
#else
        [TORIDRAW_RASTER_FACE_SD_GOURAUD] = toridraw_stock_zbuffered_gouraud,
        [TORIDRAW_RASTER_FACE_SD_FLAT] = toridraw_stock_zbuffered_flat,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED] = toridraw_stock_zbuffered_textured,
        [TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT] = toridraw_stock_zbuffered_textured,
#endif
    },
};

static const struct ToriDraw_RasterKernelSD g_stock_branching_kernel = {
    .vtable = &g_stock_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelSD g_stock_scanline_kernel = {
    .vtable = &g_stock_scanline_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelSD g_stock_smooth_branching_kernel = {
    .vtable = &g_stock_smooth_branching_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelSD g_stock_smooth_scanline_kernel = {
    .vtable = &g_stock_smooth_scanline_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING,
};

static const struct ToriDraw_RasterKernelSD g_stock_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD g_stock_smooth_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD g_stock_sorted_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

static const struct ToriDraw_RasterKernelSD g_stock_smooth_sorted_zbuffered_kernel = {
    .vtable = &g_stock_zbuffered_vtable,
    .flags = TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING |
             TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER,
};

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetBranching(void)
{
    return &g_stock_branching_kernel;
}

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetScanline(void)
{
    return &g_stock_scanline_kernel;
}

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothBranching(void)
{
    return &g_stock_smooth_branching_kernel;
}

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothScanline(void)
{
    return &g_stock_smooth_scanline_kernel;
}

static const struct ToriDraw_RasterKernelSD*
toridraw_stock_zbuffered_kernel(bool smooth, bool sorted)
{
    if( sorted )
        return smooth ? &g_stock_smooth_sorted_zbuffered_kernel
                      : &g_stock_sorted_zbuffered_kernel;
    return smooth ? &g_stock_smooth_zbuffered_kernel : &g_stock_zbuffered_kernel;
}

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetZBuffered(void)
{
    return toridraw_stock_zbuffered_kernel(false, false);
}

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetSmoothZBuffered(void)
{
    return toridraw_stock_zbuffered_kernel(true, false);
}

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

#ifndef TORIDRAW_PIXEL16
static inline void
toridraw_raster_note_texture_miss(int texture_id)
{
    static int skip_tally[TORIDRAW_TEXTURE_ID_CAPACITY];
    static int skip_total;
    static int debug_enabled = -1;

    if( debug_enabled < 0 )
        debug_enabled = getenv("TORIRS_RASTER_TEX_DEBUG") ? 1 : 0;
    if( !debug_enabled || texture_id < 0 || texture_id >= TORIDRAW_TEXTURE_ID_CAPACITY )
        return;

    if( ++skip_tally[texture_id] == 1 )
        fprintf(stderr, "raster_tex_skip: first miss id=%d\n", texture_id);
    if( ++skip_total % 500 == 1 )
        fprintf(stderr,
                "raster_tex_skip: total=%d id=%d (count=%d)\n",
                skip_total,
                texture_id,
                skip_tally[texture_id]);
}
#endif

/* Interface-backed face preparation used by every production face loop. */
static inline void
ToriDraw_RasterModelFaceKernel(
    int face,
    struct ToriDrawModelRasterContext* ctx)
{
    struct ToriDraw_RasterDebugStats* dbg = ctx->raster_debug;
    struct ToriDraw_RasterFaceSD prepared;
    int raw_type;
    int color_a;
    int color_b;
    int color_c;
    bool near_clipped;

    assert(face >= 0 && face < ctx->num_faces);

    raw_type = ctx->face_infos ? ctx->face_infos[face] : 0;
    if( dbg && raw_type >= 0 && raw_type < 16 )
        dbg->type_hist[raw_type]++;
    if( raw_type == 2 || raw_type < 0 || raw_type > 3 )
    {
        if( dbg )
            dbg->skipped_type++;
        return;
    }

    prepared.face_index = face;
    prepared.vertex[0] = ctx->face_indices_a[face];
    prepared.vertex[1] = ctx->face_indices_b[face];
    prepared.vertex[2] = ctx->face_indices_c[face];

    if( dbg && toridraw_raster_debug_level() >= 2 &&
        (prepared.vertex[0] < 0 || prepared.vertex[0] >= ctx->num_vertices ||
         prepared.vertex[1] < 0 || prepared.vertex[1] >= ctx->num_vertices ||
         prepared.vertex[2] < 0 || prepared.vertex[2] >= ctx->num_vertices) )
    {
        dbg->index_oob++;
        fprintf(stderr,
                "raster_index_oob: model=%p face=%d indices=(%d,%d,%d) num_vertices=%d\n",
                (void*)ctx->face_indices_a,
                face,
                prepared.vertex[0],
                prepared.vertex[1],
                prepared.vertex[2],
                ctx->num_vertices);
    }

    color_a = ctx->colors_a[face];
    color_b = ctx->colors_b[face];
    color_c = ctx->colors_c[face];
    if( color_c == TORIDRAWHSL16_HIDDEN )
    {
        if( dbg )
            dbg->skipped_hidden++;
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
                    if( dbg )
                        dbg->skipped_tex_miss++;
                    toridraw_raster_note_texture_miss(texture_id);
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
                if( dbg )
                    dbg->skipped_tex_miss++;
                toridraw_raster_note_texture_miss(texture_id);
                return;
            }

            if( coord != -1 )
            {
                if( coord < 0 || coord >= ctx->num_textured_faces )
                {
                    if( dbg )
                        dbg->skipped_tex_coord++;
                    return;
                }
                if( ctx->texture_render_types_nullable )
                    render_type = ctx->texture_render_types_nullable[coord] & 0xFF;

                if( render_type == 0 )
                {
                    if( !ctx->face_p_coordinate_nullable || !ctx->face_m_coordinate_nullable ||
                        !ctx->face_n_coordinate_nullable )
                    {
                        if( dbg )
                            dbg->skipped_tex_coord++;
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

            if( render_type != 0 && toridraw_raster_tex_mode_debug() )
            {
                static int complex_debug_count;
                if( complex_debug_count++ < 32 )
                    fprintf(stderr,
                            "raster_tex_mode: face=%d coord=%d type=%u affine=1\n",
                            face,
                            coord >= 0 ? coord : face,
                            (unsigned int)render_type);
            }

            if( p < 0 || p >= ctx->num_vertices || m < 0 || m >= ctx->num_vertices || n < 0 ||
                n >= ctx->num_vertices )
            {
                if( dbg )
                    dbg->skipped_tex_coord++;
                return;
            }

            near_clipped = toridraw_raster_face_is_near_clipped(ctx, face);
            prepared.near_clipped = near_clipped;
            if( near_clipped && !ctx->allow_near_clip )
            {
                if( dbg )
                    dbg->skipped_near_clip++;
                return;
            }
            if( !ctx->orthographic_vertex_x_nullable || !ctx->orthographic_vertex_y_nullable ||
                !ctx->orthographic_vertex_z_nullable )
            {
                if( dbg )
                    dbg->skipped_near_clip++;
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
            prepared.texture.gate = texture_opaque ? TORIDRAW_RASTER_TEXTURE_OPAQUE
                                                   : TORIDRAW_RASTER_TEXTURE_COLOR_KEY;
            prepared.texture.render_type = (unsigned int)render_type;
            prepared.texture.frame.p = p;
            prepared.texture.frame.m = m;
            prepared.texture.frame.n = n;

            if( dbg )
            {
                dbg->drawn++;
                dbg->drawn_textured++;
                toridraw_raster_debug_note_texture(dbg, texture_id);
            }

            ToriDraw_RasterKernelSDDispatch(&ctx->kernel, &ctx->target, &prepared);
            return;
        }
    }
#endif

    prepared.opacity =
        ctx->face_alphas_nullable ? 0xFF - ctx->face_alphas_nullable[face] : 0xFF;
    if( prepared.opacity <= 1 )
    {
        if( dbg )
            dbg->skipped_alpha++;
        return;
    }

    near_clipped = toridraw_raster_face_is_near_clipped(ctx, face);
    prepared.near_clipped = near_clipped;
    if( near_clipped &&
        (!ctx->allow_near_clip || !ctx->orthographic_vertex_x_nullable ||
         !ctx->orthographic_vertex_y_nullable || !ctx->orthographic_vertex_z_nullable) )
    {
        if( dbg )
            dbg->skipped_near_clip++;
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

    if( dbg )
    {
        dbg->drawn++;
        if( prepared.face_class == TORIDRAW_RASTER_FACE_SD_FLAT )
            dbg->drawn_flat++;
        else
        {
            int const dab = abs(color_a - color_b);
            int const dbc = abs(color_b - color_c);
            int const dca = abs(color_c - color_a);
            int const worst = dab > dbc ? (dab > dca ? dab : dca)
                                        : (dbc > dca ? dbc : dca);
            dbg->drawn_gouraud++;
            if( worst > dbg->max_color_delta )
                dbg->max_color_delta = worst;
        }
    }

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
            int clip_right =
                view_port->clip_right > 0 ? view_port->clip_right : view_port->width;
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
        ctx->camera_cot16 = toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048);
        ctx->texture_map = &ToriDraw_SceneTexState(scene)->texture_map;
        ctx->cache_texture_id = -1;
        ctx->cache_texels = NULL;
        ctx->cache_texture_size = 0;
        ctx->cache_texture_height = 0;
        ctx->cache_texture_opaque = 0;
        ctx->affine_textures = camera->texture_affine != 0;
        ctx->allow_near_clip = ToriDraw_ModelHasTextures(hnd);
        ctx->near_clipped = scene->near_clipped;
        ctx->raster_debug = NULL;
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

    if( ctx->near_clipped &&
        (ctx->vertex_x[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
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
    struct ToriDrawModelRasterContext* ctx,
    struct ToriDraw_RasterDebugStats* raster_debug_storage)
{
    struct ToriDraw_Model* model;
    int clip_left;
    int clip_top;

    model = model_as_full(hnd);
    context_from_handle(scene, hnd, view_port, camera, ctx);
    ctx->kernel = *kernel;
    ctx->face_xy = scene->sm_face_xy;
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
    ctx->target.near_clip_available =
        ctx->allow_near_clip && ctx->orthographic_vertex_x_nullable &&
        ctx->orthographic_vertex_y_nullable && ctx->orthographic_vertex_z_nullable;
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

    if( toridraw_raster_debug_enabled() )
    {
        memset(raster_debug_storage, 0, sizeof(*raster_debug_storage));
        ctx->raster_debug = raster_debug_storage;
    }
    else
        ctx->raster_debug = NULL;
}

#include "graphics/raster/texture/tex_tri_asm.h"
#include "toridraw_raster_batch.h"

#ifdef TORIDRAW_RASTER_BATCH

/*
 * Staged faces for the batched raster kernels.
 *
 * WHAT THIS IS FOR. Every drawn face used to be touched three times, and each
 * touch re-gathered the same data: the depth sort read the vertices to bucket
 * the face, ToriDraw_RasterModelFaceKernel read them again to classify it and
 * fill a fifteen-field prepared struct, and the kernel shim read them a third
 * time to push thirteen or more cdecl arguments at a symbol that by
 * construction cannot be inlined.
 *
 * All three are now one. The sort keeps the six screen coordinates it was
 * already holding (scene->sm_face_xy), this pass copies them into a row, and
 * the kernel reads the row directly -- so the call, the four register saves,
 * the argument marshal and the four screen constants happen once per RUN
 * instead of once per face.
 *
 * DRAW ORDER IS THE CONSTRAINT. This is a painter: there is no depth buffer,
 * so overlapping faces are correct only in the order the sorter produced. The
 * batch is therefore run-length, not a bucket. A face the batcher cannot take
 * FLUSHES what is staged before it is drawn itself, and so does a face of a
 * different CLASS, since the three classes are three different kernels.
 * Nothing is ever reordered.
 *
 * SIZE. 64 rows is 3 KB. The Pentium 4 this exists for has a 16 KB L1D, so a
 * chunk that big is still resident when the kernel reads it back, and a
 * staging buffer sized to max_faces would not be.
 */
#define TORIDRAW_RASTER_BATCH_ROWS 64
#define TORIDRAW_RASTER_BATCH_ROW_INTS TORIDRAW_GOURAUD_BATCH_ROW_INTS

enum ToriDraw_RasterBatchClass
{
    TORIDRAW_RASTER_BATCH_NONE = 0,
    TORIDRAW_RASTER_BATCH_GOURAUD,
    TORIDRAW_RASTER_BATCH_GOURAUD_ALPHA,
    TORIDRAW_RASTER_BATCH_FLAT_OPAQUE,
    TORIDRAW_RASTER_BATCH_FLAT_ALPHA,
    /* The textured four. Their rows are twice as wide and live in their own
     * buffer; everything from here down is "textured" to the run logic. */
    TORIDRAW_RASTER_BATCH_TEX_OPAQUE,
    TORIDRAW_RASTER_BATCH_TEX_TRANS,
    TORIDRAW_RASTER_BATCH_TEX_FLAT_OPAQUE,
    TORIDRAW_RASTER_BATCH_TEX_FLAT_TRANS
};

#define TORIDRAW_RASTER_BATCH_IS_TEX(k) ((k) >= TORIDRAW_RASTER_BATCH_TEX_OPAQUE)

/* Twenty of the twenty-four are the kernel's arguments, lane 20 is the gate,
 * and the last three are padding to keep the row 16-byte aligned. */
#define TORIDRAW_RASTER_TEXBATCH_ROW_INTS 24
#define TORIDRAW_RASTER_TEXBATCH_ROWS 32

struct ToriDraw_RasterBatch
{
    enum ToriDraw_RasterBatchClass kind;
    int count;
};

/*
 * What the classifier learned about a face, so the append does not learn it
 * again. The texture lookup in particular is a cache probe plus, on a miss, a
 * map lookup; doing it twice per face to keep the two functions independent
 * would cost more than the batching saves.
 */
struct ToriDraw_RasterBatchFace
{
    int alpha;                  /* untextured: 0xFF - authored alpha        */
    const int* texels;          /* textured: the resolved texture           */
    int texture_width;
    int gate;                   /* 1 = a zero texel keys out                */
    int p;                      /* textured: the frame's three vertices,    */
    int m;                      /*   which are NOT permuted by the y sort   */
    int n;
};

/* Not on the scene, because the raster pass is not re-entrant: one model at a
 * time, one thread, and the buffer is empty again before draw_faces returns.
 * Alignment is load-bearing -- the kernels read each row with movdqa. */
static _Alignas(16) int g_toridraw_raster_batch
    [TORIDRAW_RASTER_BATCH_ROWS * TORIDRAW_RASTER_BATCH_ROW_INTS];

/* The textured staging, separate because its row is twice as wide. Same 3 KB,
 * and only one of the two is live at a time -- a run is a single class. */
static _Alignas(16) int g_toridraw_raster_texbatch
    [TORIDRAW_RASTER_TEXBATCH_ROWS * TORIDRAW_RASTER_TEXBATCH_ROW_INTS];

/* The texel pointer travels in an int lane. Guaranteed by construction rather
 * than hoped for: this whole file is behind the i686 batch kernels. */
_Static_assert(sizeof(void*) == sizeof(int),
               "the batched texture row carries a texel pointer in an int lane");

/* The per-face path's own bisect knob, asked the same way. A face it would
 * have dropped must not be drawn here instead. */
static int
toridraw_raster_batch_skip_textured(void)
{
    static int skip = -1;
    if( skip < 0 )
        skip = getenv("TORIDRAW_SKIP_TEXTURED") ? 1 : 0;
    return skip;
}

static void
toridraw_raster_batch_flush(
    struct ToriDrawModelRasterContext* ctx,
    struct ToriDraw_RasterBatch* batch)
{
    assert(ctx);
    assert(batch);

    if( batch->count > 0 )
    {
        switch( batch->kind )
        {
        case TORIDRAW_RASTER_BATCH_GOURAUD:
            toridraw_gouraud_batch_opaque_s4_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_GOURAUD_ALPHA:
            toridraw_gouraud_batch_alpha_s4_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_FLAT_OPAQUE:
            toridraw_flat_batch_opaque_s4_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_FLAT_ALPHA:
            toridraw_flat_batch_alpha_s4_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
#ifdef TORIDRAW_TEXTRI_BATCH
        case TORIDRAW_RASTER_BATCH_TEX_OPAQUE:
            toridraw_textri_opaque_lerp8_v3_batch_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_TEX_TRANS:
            toridraw_textri_trans_lerp8_v3_batch_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_TEX_FLAT_OPAQUE:
            toridraw_textri_flat_opaque_lerp8_v3_batch_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_TEX_FLAT_TRANS:
            toridraw_textri_flat_trans_lerp8_v3_batch_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
#endif /* TORIDRAW_TEXTRI_BATCH */
        default:
            assert(0 && "a staged batch with no kernel to draw it");
            break;
        }
        batch->count = 0;
    }
    batch->kind = TORIDRAW_RASTER_BATCH_NONE;
}

/*
 * Which batch, if any, this face belongs in.
 *
 * Returning NONE is always SAFE, never merely slower: the caller flushes and
 * hands the face to the original per-face path, which re-derives everything
 * from scratch. So these gates only have to be right in one direction -- a
 * face accepted here must be one the old path would have sent to the same
 * kernel with the same numbers.
 *
 * The gates, in the order the old path applies them:
 *   type 2 and anything outside 0..3 is not drawn at all;
 *   a hidden face is not drawn;
 *   a textured face goes to a different kernel family entirely, and its miss
 *     bookkeeping (toridraw_raster_note_texture_miss, the one-entry texture
 *     cache) must stay where it is, so those are rejected rather than copied;
 *   opacity 1 or less is not drawn;
 *   a near-clipped face goes to the clip builder -- and the sort already
 *     answered that, so this is a lane read and not three gathered ones.
 *
 * All four classes are taken either way now. The blending gouraud kernel used
 * to have no asm twin, so an alpha gouraud face fell through to the per-face
 * path and cut every run it appeared in; it has one now.
 */
/*
 * The textured half of the classifier.
 *
 * Every gate here is one the per-face path applies, in the order it applies
 * them, and every rejection hands the face back to that path intact -- which
 * is what keeps the texture-miss bookkeeping, the near-plane clip builder and
 * the affine family where they already are rather than reimplemented twice.
 *
 * The one-entry texture cache is READ AND WRITTEN here, exactly as the
 * per-face path does it. That is deliberate: a face rejected below still
 * leaves the cache holding the texture it resolved, so the per-face path that
 * picks it up hits rather than misses, and the two walks agree about what the
 * cache contains at every point in the model.
 */
#ifdef TORIDRAW_TEXTRI_BATCH
static enum ToriDraw_RasterBatchClass
toridraw_raster_batch_classify_textured(
    struct ToriDrawModelRasterContext* ctx,
    int face,
    int texture_id,
    int color_c,
    struct ToriDraw_RasterBatchFace* out)
{
    const int* texels;
    int texture_size;
    int texture_height;
    int texture_opaque;
    int coord;
    int render_type = 0;
    int p;
    int m;
    int n;

    if( toridraw_raster_batch_skip_textured() )
        return TORIDRAW_RASTER_BATCH_NONE;

    /* The affine family is a different rasteriser with a different signature;
     * the batch kernels are the perspective one. */
    if( ctx->target.affine_textures )
        return TORIDRAW_RASTER_BATCH_NONE;

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
        /* A miss is the per-face path's to report -- it owns the tally and the
         * first-miss line. Handing the face back reports it exactly once. */
        if( !texture )
            return TORIDRAW_RASTER_BATCH_NONE;
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
        return TORIDRAW_RASTER_BATCH_NONE;
    /* The walk is instantiated for two widths and dispatches on them. */
    if( texture_size != 64 && texture_size != 128 )
        return TORIDRAW_RASTER_BATCH_NONE;

    coord = ctx->face_texture_coords ? ctx->face_texture_coords[face] : -1;
    if( coord != -1 )
    {
        if( coord < 0 || coord >= ctx->num_textured_faces )
            return TORIDRAW_RASTER_BATCH_NONE;
        if( ctx->texture_render_types_nullable )
            render_type = ctx->texture_render_types_nullable[coord] & 0xFF;
        if( render_type != 0 )
            return TORIDRAW_RASTER_BATCH_NONE;
        if( !ctx->face_p_coordinate_nullable || !ctx->face_m_coordinate_nullable ||
            !ctx->face_n_coordinate_nullable )
            return TORIDRAW_RASTER_BATCH_NONE;
        p = ctx->face_p_coordinate_nullable[coord];
        m = ctx->face_m_coordinate_nullable[coord];
        n = ctx->face_n_coordinate_nullable[coord];
    }
    else
    {
        if( ctx->texture_render_types_nullable && face < ctx->num_textured_faces )
            render_type = ctx->texture_render_types_nullable[face] & 0xFF;
        if( render_type != 0 )
            return TORIDRAW_RASTER_BATCH_NONE;
        p = ctx->face_indices_a[face];
        m = ctx->face_indices_b[face];
        n = ctx->face_indices_c[face];
    }

    if( p < 0 || p >= ctx->num_vertices || m < 0 || m >= ctx->num_vertices ||
        n < 0 || n >= ctx->num_vertices )
        return TORIDRAW_RASTER_BATCH_NONE;

    /* A near-clipped face is rebuilt by the clip builder, which produces
     * geometry this row cannot describe. The sort already answered the
     * question, so this is a lane read rather than three gathered ones. */
    if( ctx->near_clipped && ctx->face_xy[(size_t)face * 8 + 3] )
        return TORIDRAW_RASTER_BATCH_NONE;

    if( !ctx->orthographic_vertex_x_nullable || !ctx->orthographic_vertex_y_nullable ||
        !ctx->orthographic_vertex_z_nullable )
        return TORIDRAW_RASTER_BATCH_NONE;

    out->texels = texels;
    out->texture_width = texture_size;
    out->gate = texture_opaque ? 0 : 1;
    out->p = p;
    out->m = m;
    out->n = n;

    if( color_c == TORIDRAWHSL16_FLAT )
        return texture_opaque ? TORIDRAW_RASTER_BATCH_TEX_FLAT_OPAQUE
                              : TORIDRAW_RASTER_BATCH_TEX_FLAT_TRANS;
    return texture_opaque ? TORIDRAW_RASTER_BATCH_TEX_OPAQUE
                          : TORIDRAW_RASTER_BATCH_TEX_TRANS;
}
#endif /* TORIDRAW_TEXTRI_BATCH */

static enum ToriDraw_RasterBatchClass
toridraw_raster_batch_classify(
    struct ToriDrawModelRasterContext* ctx,
    int face,
    struct ToriDraw_RasterBatchFace* out)
{
    int raw_type;
    int color_c;
    int alpha;
    int texture_id;

    assert(ctx);
    assert(out);
    assert(face >= 0 && face < ctx->num_faces);

    raw_type = ctx->face_infos ? ctx->face_infos[face] : 0;
    if( raw_type == 2 || raw_type < 0 || raw_type > 3 )
        return TORIDRAW_RASTER_BATCH_NONE;

    color_c = ctx->colors_c[face];
    if( color_c == TORIDRAWHSL16_HIDDEN )
        return TORIDRAW_RASTER_BATCH_NONE;

    texture_id = ctx->face_textures ? ctx->face_textures[face] : -1;
    if( texture_id != -1 )
    {
#ifdef TORIDRAW_TEXTRI_BATCH
        return toridraw_raster_batch_classify_textured(
            ctx, face, texture_id, color_c, out);
#else
        /* No batch door for a textured face in this build; the per-face path
         * draws it, as it did before there was a batcher at all. */
        return TORIDRAW_RASTER_BATCH_NONE;
#endif
    }

    alpha = ctx->face_alphas_nullable ? 0xFF - ctx->face_alphas_nullable[face]
                                      : 0xFF;
    if( alpha <= 1 )
        return TORIDRAW_RASTER_BATCH_NONE;

    if( ctx->near_clipped && ctx->face_xy[(size_t)face * 8 + 3] )
        return TORIDRAW_RASTER_BATCH_NONE;

    out->alpha = alpha;
    if( color_c == TORIDRAWHSL16_FLAT )
        return alpha == 0xFF ? TORIDRAW_RASTER_BATCH_FLAT_OPAQUE
                             : TORIDRAW_RASTER_BATCH_FLAT_ALPHA;
    return alpha == 0xFF ? TORIDRAW_RASTER_BATCH_GOURAUD
                         : TORIDRAW_RASTER_BATCH_GOURAUD_ALPHA;
}

/*
 * Copy one classified face into the staging buffer.
 *
 * The six coordinates come straight out of the sort's stash -- eight
 * sequential ints out of a region a few hundred bytes wide -- instead of three
 * index loads and six dependent loads into the vertex arrays. The viewport
 * offset is the only arithmetic left, and it is here rather than in the sort
 * because the sort does not know the viewport.
 */
/* The permutation the sort applied, for the consumers carrying per-vertex
 * data of their own. One table, two callers. */
static const unsigned char g_toridraw_raster_batch_order[6][3] = {
    { 0, 1, 2 }, { 0, 2, 1 }, { 1, 2, 0 },
    { 1, 0, 2 }, { 2, 0, 1 }, { 2, 1, 0 }
};

/*
 * One classified textured face into the wide staging buffer.
 *
 * The three screen coordinates come out of the sort's stash already in y
 * order; the nine orthographic ones do NOT get permuted with them, and that is
 * the part worth stating. The texture frame is (uv origin, u end, v end) taken
 * from vertices p, m and n -- three ROLES, not the triangle's own three
 * corners -- so reordering them would move the texture on the face. The kernel
 * agrees: VSORT permutes x, y and shade, and copies the frame straight
 * through.
 */
#ifdef TORIDRAW_TEXTRI_BATCH
static void
toridraw_raster_batch_append_textured(
    struct ToriDrawModelRasterContext* ctx,
    int face,
    enum ToriDraw_RasterBatchClass kind,
    const struct ToriDraw_RasterBatchFace* info,
    struct ToriDraw_RasterBatch* batch)
{
    int const* const xy = &ctx->face_xy[(size_t)face * 8];
    int* const row = g_toridraw_raster_texbatch +
                     batch->count * TORIDRAW_RASTER_TEXBATCH_ROW_INTS;
    int const* const ox_arr = ctx->orthographic_vertex_x_nullable;
    int const* const oy_arr = ctx->orthographic_vertex_y_nullable;
    int const* const oz_arr = ctx->orthographic_vertex_z_nullable;

    row[0] = xy[0] + ctx->offset_x;
    row[1] = xy[1] + ctx->offset_x;
    row[2] = xy[2] + ctx->offset_x;
    row[3] = xy[4] + ctx->offset_y;
    row[4] = xy[5] + ctx->offset_y;
    row[5] = xy[6] + ctx->offset_y;

    row[6] = ox_arr[info->p];
    row[7] = ox_arr[info->m];
    row[8] = ox_arr[info->n];
    row[9] = oy_arr[info->p];
    row[10] = oy_arr[info->m];
    row[11] = oy_arr[info->n];
    row[12] = oz_arr[info->p];
    row[13] = oz_arr[info->m];
    row[14] = oz_arr[info->n];

    if( kind == TORIDRAW_RASTER_BATCH_TEX_FLAT_OPAQUE ||
        kind == TORIDRAW_RASTER_BATCH_TEX_FLAT_TRANS )
    {
        /* One shade, and the constant-shade walk reads only this lane. Lanes
         * 16 and 17 are left as the previous row left them on purpose:
         * writing them would be two stores per face for a value with no
         * consumer. */
        row[15] = ctx->colors_a[face];
    }
    else
    {
        /* Per-vertex shades, so they follow the sort's permutation. */
        unsigned char const* const o = g_toridraw_raster_batch_order[xy[7]];
        int const col[3] = { ctx->colors_a[face], ctx->colors_b[face],
                             ctx->colors_c[face] };
        row[15] = col[o[0]];
        row[16] = col[o[1]];
        row[17] = col[o[2]];
    }

    row[18] = (int)(intptr_t)info->texels;
    row[19] = info->texture_width;
    row[20] = info->gate;
    /* 21..23 pad the row to 16 bytes; TLOADROW never reads them. */

    batch->kind = kind;
    batch->count++;
}
#endif /* TORIDRAW_TEXTRI_BATCH */

static void
toridraw_raster_batch_append(
    struct ToriDrawModelRasterContext* ctx,
    int face,
    enum ToriDraw_RasterBatchClass kind,
    int alpha,
    struct ToriDraw_RasterBatch* batch)
{
    int const* const xy = &ctx->face_xy[(size_t)face * 8];
    int* const row =
        g_toridraw_raster_batch + batch->count * TORIDRAW_RASTER_BATCH_ROW_INTS;
    int const ox = ctx->offset_x;
    int const oy = ctx->offset_y;

    /* Already in y order -- the depth sort did that, with the y values it was
     * holding for the winding. Nothing here reorders anything; the viewport
     * offset is the only arithmetic left, and it is here rather than in the
     * sort because the sort does not know the viewport. */
    row[0] = xy[0] + ox;
    row[1] = xy[1] + ox;
    row[2] = xy[2] + ox;
    row[4] = xy[4] + oy;
    row[5] = xy[5] + oy;
    row[6] = xy[6] + oy;

    if( kind == TORIDRAW_RASTER_BATCH_GOURAUD ||
        kind == TORIDRAW_RASTER_BATCH_GOURAUD_ALPHA )
    {
        /* The colours are per-vertex, so they follow the same permutation the
         * sort applied to the coordinates. */
        unsigned char const* const o = g_toridraw_raster_batch_order[xy[7]];
        int const col[3] = { ctx->colors_a[face], ctx->colors_b[face],
                             ctx->colors_c[face] };

        row[8] = col[o[0]];
        row[9] = col[o[1]];
        row[10] = col[o[2]];
        /* Lane 11 is the colour group's spare; the blending kernel reads the
         * opacity out of it, and the opaque one never looks. */
        row[11] = alpha;
    }
    else
    {
        /* Flat carries one colour and its opacity, not three, so there is
         * nothing here to permute. */
        row[8] = ctx->colors_a[face];
        row[9] = alpha;
    }
    /* row[3], row[7], row[11] are the padding lanes the kernels' pshufd
     * carries through and nothing reads. Left alone on purpose: writing them
     * would be stores per face to produce a value with no consumer. */

    batch->kind = kind;
    batch->count++;
}

/*
 * The batched walk. Chosen only when every assumption it rests on holds:
 *
 *   - the kernel really is the stock BRANCHING one, since the scanline and
 *     smooth families are different rasterisers reached through their own
 *     vtables and these kernels are neither of them;
 *   - no debug stats are being collected, because every counter this walk
 *     would have to maintain lives in the per-face path and belongs there
 *     rather than reimplemented twice.
 */
static void
toridraw_raster_draw_faces_batched(
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx)
{
    struct ToriDraw_RasterBatch batch;
    int i;

    assert(scene);
    assert(ctx);

    batch.kind = TORIDRAW_RASTER_BATCH_NONE;
    batch.count = 0;

    for( i = 0; i < scene->tmp_face_order_count; i++ )
    {
        int const face = scene->tmp_face_order[i];
        struct ToriDraw_RasterBatchFace info;
        enum ToriDraw_RasterBatchClass kind;

        info.alpha = 0xFF;
        kind = toridraw_raster_batch_classify(ctx, face, &info);

        if( kind != TORIDRAW_RASTER_BATCH_NONE )
        {
            int const cap = TORIDRAW_RASTER_BATCH_IS_TEX(kind)
                                ? TORIDRAW_RASTER_TEXBATCH_ROWS
                                : TORIDRAW_RASTER_BATCH_ROWS;

            /* Order, not speed: a different class is a different kernel, and
             * what is staged was sorted BEFORE this face. */
            if( batch.count > 0 && batch.kind != kind )
                toridraw_raster_batch_flush(ctx, &batch);
#ifdef TORIDRAW_TEXTRI_BATCH
            if( TORIDRAW_RASTER_BATCH_IS_TEX(kind) )
                toridraw_raster_batch_append_textured(
                    ctx, face, kind, &info, &batch);
            else
#endif
                toridraw_raster_batch_append(
                    ctx, face, kind, info.alpha, &batch);
            if( batch.count == cap )
                toridraw_raster_batch_flush(ctx, &batch);
            continue;
        }

        toridraw_raster_batch_flush(ctx, &batch);
        ToriDraw_RasterModelFaceKernel(face, ctx);
    }

    toridraw_raster_batch_flush(ctx, &batch);
}
#endif /* TORIDRAW_RASTER_BATCH */

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

static inline void
toridraw_raster_draw_faces(
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

    if( ctx->kernel.flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
    {
        /* The sorter already culled back-facing faces. */
        ctx->ordered_faces = scene->tmp_face_order_count;
        if( skip_faces )
            return;
#ifdef TORIDRAW_RASTER_BATCH
        if( ctx->kernel.vtable == &g_stock_branching_vtable && !ctx->raster_debug &&
            toridraw_raster_batch_armed() )
        {
            toridraw_raster_draw_faces_batched(scene, ctx);
            return;
        }
#endif
        for( int i = 0; i < scene->tmp_face_order_count; i++ )
            ToriDraw_RasterModelFaceKernel(scene->tmp_face_order[i], ctx);
    }
    else
    {
        ctx->ordered_faces = ctx->num_faces;
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

static inline bool
ToriDraw_RasterPainter(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    struct ToriDraw_RasterDebugStats raster_debug_storage;
    struct ToriDrawModelRasterContext ctx;

    assert(!(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER));
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
        toridraw_raster_context_init(
            scene,
            hnd,
            view_port,
            camera,
            pixel_buffer,
            kernel,
            &ctx,
            &raster_debug_storage);
        toridraw_raster_draw_faces(scene, &ctx);
        if( ctx.raster_debug )
            toridraw_raster_debug_print(ctx.raster_debug, &ctx, (void*)model_as_full(hnd));
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
    struct ToriDraw_RasterDebugStats raster_debug_storage;
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
            scene,
            hnd,
            view_port,
            camera,
            pixel_buffer,
            kernel,
            &ctx,
            &raster_debug_storage);

        rows = ctx.target.clip_origin_y + ctx.screen_height;
        if( !ToriDraw_SceneHasZBuffer(scene, ctx.stride, rows) )
        {
            bool const provisioned = ToriDraw_SceneZBufferResize(scene, ctx.stride, rows);

            assert(provisioned);
            (void)provisioned;
        }
        assert(ToriDraw_SceneHasZBuffer(scene, ctx.stride, rows));

        ctx.zbuf_target.pixel_buffer = ctx.pixel_buffer;
        ctx.zbuf_target.zbuffer = scene->zbuffer + ctx.target.clip_origin_x +
                                  ctx.target.clip_origin_y * ctx.stride;
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
        if( ctx.raster_debug )
            toridraw_raster_debug_print(ctx.raster_debug, &ctx, (void*)model_as_full(hnd));
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
