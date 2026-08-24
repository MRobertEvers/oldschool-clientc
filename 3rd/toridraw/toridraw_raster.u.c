#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_raster_kernel_internal.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum DashModelRasterFlags
{
    RASTER_FLAG_GOURAUD_SMOOTH = 1 << 0,
    RASTER_FLAG_TEXTURE_AFFINE = 1 << 1,
};

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
    alphaint_t* face_alphas_nullable;
    int offset_x;
    int offset_y;
    int near_plane_z;
    int screen_width;
    int screen_height;
    int stride;
    int camera_cot16;
    struct ToriDraw_TextureMap* texture_map;
    int flags;
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

    /* Depth-tested raster for this model. NULL unless the model carries
     * TORIDRAW_MODEL_FLAG_ZBUFFER and the scene has a z-buffer; when set, every
     * face of the model routes to the zbuf family instead of the stock kernels.
     * The buffer has already been reset for this model, which is what keeps the
     * test confined to it (triangles/toridraw_triangle_zbuf.u.c). */
#ifndef TORIDRAW_PIXEL16
    struct ToriDraw_ZbufTarget zbuf_target;
    struct ToriDraw_ZbufFaceSource zbuf_source;
#endif
    bool zbuffered;

    /* The public, pass-stable descriptor and the four slots resolved once at
     * model entry. Built-in callbacks recover this context through
     * target.internal; application callbacks use only the normalized public
     * fields. */
    struct ToriDraw_RasterTarget target;
    struct ToriDraw_ResolvedRasterKernel kernel;

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
static inline void
toridraw_stock_draw_zbuffered(
    struct ToriDrawModelRasterContext* ctx,
    const struct ToriDraw_RasterFace* face)
{
    int mode;
    int gate = TORIDRAW_ZBUF_TEX_OPAQUE;
    const int* texels = NULL;
    int texture_size = 0;
    int p = 0;
    int m = 0;
    int n = 0;

    if( face->face_class == TORIDRAW_RASTER_FACE_GOURAUD )
        mode = TORIDRAW_ZBUF_MODE_GOURAUD;
    else if( face->face_class == TORIDRAW_RASTER_FACE_FLAT )
        mode = TORIDRAW_ZBUF_MODE_FLAT;
    else
    {
        mode = TORIDRAW_ZBUF_MODE_TEXTURE;
        gate = face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE
                   ? TORIDRAW_ZBUF_TEX_OPAQUE
                   : TORIDRAW_ZBUF_TEX_TRANSPARENT;
        texels = face->texture.texels;
        texture_size = face->texture.width;
        p = face->texture.mapping.vertex_frame.p;
        m = face->texture.mapping.vertex_frame.m;
        n = face->texture.mapping.vertex_frame.n;
    }

    ToriDraw_TriangleFaceZBuffered(
        &ctx->zbuf_target,
        &ctx->zbuf_source,
        face->face_index,
        mode,
        face->shade[0],
        face->shade[1],
        face->shade[2],
        face->opacity,
        gate,
        p,
        m,
        n,
        (int*)texels,
        texture_size,
        ctx->allow_near_clip,
        ctx->near_clipped);
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
        (ctx)->camera_cot16, (face)->face_index, (face)->texture.mapping.vertex_frame.p,         \
        (face)->texture.mapping.vertex_frame.m, (face)->texture.mapping.vertex_frame.n,          \
        (ctx)->face_indices_a, (ctx)->face_indices_b, (ctx)->face_indices_c, (ctx)->vertex_x,    \
        (ctx)->vertex_y, (ctx)->vertex_z, (ctx)->orthographic_vertex_x_nullable,                 \
        (ctx)->orthographic_vertex_y_nullable, (ctx)->orthographic_vertex_z_nullable

#define TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face)                                                  \
    (int*)(face)->texture.texels, (face)->texture.width, (ctx)->near_plane_z, (ctx)->offset_x,   \
        (ctx)->offset_y, (ctx)->allow_near_clip, (ctx)->near_clipped

#define TORIDRAW_DEFINE_STOCK_GOURAUD(name, normal_fn, smooth_fn)                                \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFace* face)                                     \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        (void)user_data;                                                                         \
        (void)target;                                                                            \
        if( ctx->zbuffered )                                                                     \
        {                                                                                        \
            /* Compiled out with the depth family in the 16-bit target. */                       \
            /* NOLINTNEXTLINE */                                                                 \
            toridraw_stock_draw_zbuffered(ctx, face);                                            \
            return;                                                                              \
        }                                                                                        \
        if( (ctx->flags & RASTER_FLAG_GOURAUD_SMOOTH) != 0 )                                    \
            smooth_fn(TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face));                                   \
        else                                                                                     \
            normal_fn(TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face));                                   \
    }

#ifdef TORIDRAW_PIXEL16
#undef TORIDRAW_DEFINE_STOCK_GOURAUD
#define TORIDRAW_DEFINE_STOCK_GOURAUD(name, normal_fn, smooth_fn)                                \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFace* face)                                     \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        (void)user_data;                                                                         \
        if( (ctx->flags & RASTER_FLAG_GOURAUD_SMOOTH) != 0 )                                    \
            smooth_fn(TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face));                                   \
        else                                                                                     \
            normal_fn(TORIDRAW_STOCK_GOURAUD_ARGS(ctx, face));                                   \
    }
#endif

#define TORIDRAW_DEFINE_STOCK_FLAT(name, flat_fn)                                                \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFace* face)                                     \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        (void)user_data;                                                                         \
        (void)target;                                                                            \
        if( ctx->zbuffered )                                                                     \
        {                                                                                        \
            /* NOLINTNEXTLINE */                                                                 \
            toridraw_stock_draw_zbuffered(ctx, face);                                            \
            return;                                                                              \
        }                                                                                        \
        flat_fn(TORIDRAW_STOCK_FLAT_ARGS(ctx, face));                                            \
    }

#ifdef TORIDRAW_PIXEL16
#undef TORIDRAW_DEFINE_STOCK_FLAT
#define TORIDRAW_DEFINE_STOCK_FLAT(name, flat_fn)                                                \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFace* face)                                     \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        (void)user_data;                                                                         \
        flat_fn(TORIDRAW_STOCK_FLAT_ARGS(ctx, face));                                            \
    }
#endif

#ifndef TORIDRAW_PIXEL16
#define TORIDRAW_DEFINE_STOCK_TEXTURED(name, blend_opaque_fn, blend_trans_fn,                    \
                                       blend_affine_fn, flat_opaque_fn, flat_trans_fn,            \
                                       flat_affine_fn)                                            \
    static void name(void* user_data, const struct ToriDraw_RasterTarget* target,                \
                     const struct ToriDraw_RasterFace* face)                                     \
    {                                                                                            \
        struct ToriDrawModelRasterContext* ctx = target->internal;                               \
        bool const flat = face->face_class == TORIDRAW_RASTER_FACE_TEXTURED_FLAT;                \
        bool const affine = ctx->target.affine_textures || face->texture.render_type != 0;       \
        (void)user_data;                                                                         \
        (void)target;                                                                            \
        if( ctx->zbuffered )                                                                     \
        {                                                                                        \
            toridraw_stock_draw_zbuffered(ctx, face);                                            \
            return;                                                                              \
        }                                                                                        \
        if( flat )                                                                               \
        {                                                                                        \
            if( affine )                                                                         \
                flat_affine_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,          \
                               (int*)(face)->texture.texels, (face)->texture.width,               \
                               (face)->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE,            \
                               (ctx)->near_plane_z, (ctx)->offset_x, (ctx)->offset_y,             \
                               (ctx)->allow_near_clip, (ctx)->near_clipped);                      \
            else if( face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE )                       \
                flat_opaque_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,          \
                               TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                           \
            else                                                                                 \
                flat_trans_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,           \
                              TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                            \
        }                                                                                        \
        else                                                                                     \
        {                                                                                        \
            if( affine )                                                                         \
                blend_affine_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,         \
                                (ctx)->colors_b, (ctx)->colors_c,                                 \
                                (int*)(face)->texture.texels, (face)->texture.width,              \
                                (face)->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE,           \
                                (ctx)->near_plane_z, (ctx)->offset_x, (ctx)->offset_y,            \
                                (ctx)->allow_near_clip, (ctx)->near_clipped);                     \
            else if( face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE )                       \
                blend_opaque_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,         \
                                (ctx)->colors_b, (ctx)->colors_c,                                 \
                                TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                          \
            else                                                                                 \
                blend_trans_fn(TORIDRAW_STOCK_TEXTURE_ARGS(ctx, face), (ctx)->colors_a,          \
                               (ctx)->colors_b, (ctx)->colors_c,                                  \
                               TORIDRAW_STOCK_TEXTURE_TAIL(ctx, face));                           \
        }                                                                                        \
    }

TORIDRAW_DEFINE_STOCK_TEXTURED(
    toridraw_stock_branching_textured,
    ToriDraw_TriangleFaceTextureBlendOpaqueBranching,
    ToriDraw_TriangleFaceTextureBlendTransparentBranching,
    ToriDraw_TriangleFaceTextureBlendAffineV3Branching,
    ToriDraw_TriangleFaceTextureFlatOpaqueBranching,
    ToriDraw_TriangleFaceTextureFlatTransparentBranching,
    ToriDraw_TriangleFaceTextureFlatAffineV3Branching)
TORIDRAW_DEFINE_STOCK_TEXTURED(
    toridraw_stock_scanline_textured,
    ToriDraw_TriangleFaceTextureBlendOpaqueScanline,
    ToriDraw_TriangleFaceTextureBlendTransparentScanline,
    ToriDraw_TriangleFaceTextureBlendAffineV3Scanline,
    ToriDraw_TriangleFaceTextureFlatOpaqueScanline,
    ToriDraw_TriangleFaceTextureFlatTransparentScanline,
    ToriDraw_TriangleFaceTextureFlatAffineV3Scanline)
#endif

TORIDRAW_DEFINE_STOCK_GOURAUD(
    toridraw_stock_branching_gouraud,
    ToriDraw_TriangleFaceGouraudBranching,
    ToriDraw_TriangleFaceGouraudSmoothBranching)
TORIDRAW_DEFINE_STOCK_GOURAUD(
    toridraw_stock_scanline_gouraud,
    ToriDraw_TriangleFaceGouraudScanline,
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
    const struct ToriDraw_RasterFace* face)
{
    (void)user_data;
    (void)target;
    (void)face;
    assert(false && "Pixel16 stock classification emitted a textured face");
}
#define toridraw_stock_branching_textured toridraw_stock_unreachable_textured
#define toridraw_stock_scanline_textured toridraw_stock_unreachable_textured
#endif

static const struct ToriDraw_RasterKernelVTable g_stock_branching_vtable = {
    .draw_gouraud = toridraw_stock_branching_gouraud,
    .draw_flat = toridraw_stock_branching_flat,
    .draw_textured = toridraw_stock_branching_textured,
    .draw_textured_flat = toridraw_stock_branching_textured,
};

static const struct ToriDraw_RasterKernelVTable g_stock_scanline_vtable = {
    .draw_gouraud = toridraw_stock_scanline_gouraud,
    .draw_flat = toridraw_stock_scanline_flat,
    .draw_textured = toridraw_stock_scanline_textured,
    .draw_textured_flat = toridraw_stock_scanline_textured,
};

static const struct ToriDraw_RasterKernel g_stock_branching_kernel = {
    .vtable = &g_stock_branching_vtable,
    .domains = TORIDRAW_RASTER_KERNEL_STOCK,
};

static const struct ToriDraw_RasterKernel g_stock_scanline_kernel = {
    .vtable = &g_stock_scanline_vtable,
    .domains = TORIDRAW_RASTER_KERNEL_STOCK,
};

const struct ToriDraw_RasterKernel*
ToriDraw_RasterKernelGetBranching(void)
{
    return &g_stock_branching_kernel;
}

const struct ToriDraw_RasterKernel*
ToriDraw_RasterKernelGetScanline(void)
{
    return &g_stock_scanline_kernel;
}

#undef toridraw_stock_scanline_textured
#undef toridraw_stock_branching_textured
#undef TORIDRAW_DEFINE_STOCK_TEXTURED
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
    struct ToriDraw_RasterFace prepared;
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
                    prepared.texture.mapping_payload = TORIDRAW_RASTER_MAPPING_VERTEX_FRAME;
                }
                else
                {
                    /* Non-plane records contain axes, not indices. Stock keeps
                     * its affine face-frame fallback while retaining the byte. */
                    p = prepared.vertex[0];
                    m = prepared.vertex[1];
                    n = prepared.vertex[2];
                    prepared.texture.mapping_payload =
                        TORIDRAW_RASTER_MAPPING_STOCK_FACE_FALLBACK;
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
                prepared.texture.mapping_payload =
                    TORIDRAW_RASTER_MAPPING_STOCK_FACE_FALLBACK;
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
                                      ? TORIDRAW_RASTER_FACE_TEXTURED_FLAT
                                      : TORIDRAW_RASTER_FACE_TEXTURED;
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
            prepared.texture.clamp_s = false;
            prepared.texture.clamp_t = false;
            prepared.texture.render_type = (unsigned int)render_type;
            prepared.texture.mapping.vertex_frame.p = p;
            prepared.texture.mapping.vertex_frame.m = m;
            prepared.texture.mapping.vertex_frame.n = n;
            prepared.texture.modulate = false;
            prepared.texture.tint_r = 0;
            prepared.texture.tint_g = 0;
            prepared.texture.tint_b = 0;
            prepared.texture.texture_neutral = 0;

            if( dbg )
            {
                dbg->drawn++;
                dbg->drawn_textured++;
                toridraw_raster_debug_note_texture(dbg, texture_id);
            }

            struct ToriDraw_ResolvedRasterSlot* slot =
                &ctx->kernel.slots[prepared.face_class];
            slot->function(slot->user_data, &ctx->target, &prepared);
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
        prepared.face_class = TORIDRAW_RASTER_FACE_FLAT;
        prepared.shade[0] = color_a;
        prepared.shade[1] = color_a;
        prepared.shade[2] = color_a;
    }
    else
    {
        prepared.face_class = TORIDRAW_RASTER_FACE_GOURAUD;
        prepared.shade[0] = color_a;
        prepared.shade[1] = color_b;
        prepared.shade[2] = color_c;
    }

    if( dbg )
    {
        dbg->drawn++;
        if( prepared.face_class == TORIDRAW_RASTER_FACE_FLAT )
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

    {
        struct ToriDraw_ResolvedRasterSlot* slot = &ctx->kernel.slots[prepared.face_class];
        slot->function(slot->user_data, &ctx->target, &prepared);
    }
}

static inline void
context_from_handle(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    bool smooth,
    /* Draw depth-tested whatever the model's own flag says. This is how
     * ToriDraw_RenderZBuffered opts a model in without writing to it: the entry
     * point is the request, so the flag is not consulted and the scene's
     * MODEL_ZBUFFER permission is not required either. */
    bool force_zbuffer,
    struct ToriDrawModelRasterContext* ctx)
{
    /* Set before the switch so a handle kind this function does not fill still
     * leaves the caller with a defined answer: the depth-tested path is opt-in,
     * and "unset" must read as "off" rather than as whatever the stack held. */
    ctx->zbuffered = false;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
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
        ctx->flags = 0;
        if( smooth )
            ctx->flags |= RASTER_FLAG_GOURAUD_SMOOTH;
        if( camera->texture_affine )
            ctx->flags |= RASTER_FLAG_TEXTURE_AFFINE;
        ctx->allow_near_clip = ToriDraw_ModelHasTextures(hnd);
        ctx->near_clipped = scene->near_clipped;
        ctx->raster_debug = NULL;
        ctx->zbuffered = false;
#ifndef TORIDRAW_PIXEL16
        /* Opt-in per model; the scene's buffer is sized here on first use, so a
         * caller that never draws a z-buffered model never pays for one. A
         * failed allocation is not an error — the model simply draws by face
         * order, exactly as it did before the flag existed. */
        if( force_zbuffer || (m->flags & TORIDRAW_MODEL_FLAG_ZBUFFER) != 0 )
        {
            int const clip_top = view_port->clip_top > 0 ? view_port->clip_top : 0;
            int const rows = clip_top + ctx->screen_height;
            int const stride = ctx->stride;

            if( !ToriDraw_SceneHasZBuffer(scene, stride, rows) &&
                (force_zbuffer || (scene->flags & TORIDRAW_SCENE_MODEL_ZBUFFER) != 0) )
                ToriDraw_SceneZBufferResize(scene, stride, rows);

            /* A forced request that produced no buffer would silently draw by
             * face order, which is the one thing the caller asked not to do. */
            assert(!force_zbuffer || ToriDraw_SceneHasZBuffer(scene, stride, rows));

            if( ToriDraw_SceneHasZBuffer(scene, stride, rows) )
            {
                ctx->zbuffered = true;
                ctx->zbuf_target.zbuffer = scene->zbuffer;
                ctx->zbuf_target.stride = stride;
                ctx->zbuf_target.screen_width = ctx->screen_width;
                ctx->zbuf_target.screen_height = ctx->screen_height;
                ctx->zbuf_target.camera_cot16 = ctx->camera_cot16;
                ctx->zbuf_target.offset_x = ctx->offset_x;
                ctx->zbuf_target.offset_y = ctx->offset_y;
                ctx->zbuf_target.near_plane_z = ctx->near_plane_z;
                ctx->zbuf_target.model_mid_z = scene->projected_vertex.z;
                ctx->zbuf_target.parallel = toridraw_proj_is_parallel(camera->proj_mode);

                ctx->zbuf_source.face_indices_a = ctx->face_indices_a;
                ctx->zbuf_source.face_indices_b = ctx->face_indices_b;
                ctx->zbuf_source.face_indices_c = ctx->face_indices_c;
                ctx->zbuf_source.screen_vertices_x = ctx->vertex_x;
                ctx->zbuf_source.screen_vertices_y = ctx->vertex_y;
                ctx->zbuf_source.screen_vertices_z = ctx->vertex_z;
                ctx->zbuf_source.orthographic_vertices_x =
                    ctx->orthographic_vertex_x_nullable;
                ctx->zbuf_source.orthographic_vertices_y =
                    ctx->orthographic_vertex_y_nullable;
                ctx->zbuf_source.orthographic_vertices_z =
                    ctx->orthographic_vertex_z_nullable;
            }
        }
#endif
        break;
    }
    default:
        break;
    }
}

/**
 * Is this face facing the camera?
 *
 * Only the unsorted walk asks. On the sorted path the depth bucketer answers it
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

    long long const dx1 = (long long)ctx->vertex_x[a] - ctx->vertex_x[b];
    long long const dy1 = (long long)ctx->vertex_y[a] - ctx->vertex_y[b];
    long long const dx2 = (long long)ctx->vertex_x[c] - ctx->vertex_x[b];
    long long const dy2 = (long long)ctx->vertex_y[c] - ctx->vertex_y[b];

    return toridraw_winding_front_facing(dx1 * dy2 - dy1 * dx2);
}

static inline void
ToriDraw_RasterWithFaceIndices(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth,
    bool force_zbuffer,
    /* Ignore scene->tmp_face_order and walk the model's own face order. Only
     * meaningful with force_zbuffer: without a depth buffer the face order IS
     * the visibility answer. */
    bool unsorted)
{
    const struct ToriDraw_RasterKernel* terminal;
    struct ToriDraw_ResolvedRasterKernel resolved;
    struct ToriDrawModelRasterContext ctx;
    struct ToriDraw_Model* model;

    if( !ToriDraw_RasterPassBegin(scene) )
        return;

    terminal = ToriDraw_RasterGetScanline() ? ToriDraw_RasterKernelGetScanline()
                                            : ToriDraw_RasterKernelGetBranching();
    if( !ToriDraw_RasterKernelResolve(
            scene->raster_kernel,
            terminal,
            TORIDRAW_RASTER_KERNEL_STOCK,
            &resolved) )
        goto cleanup;

    model = model_as_full(hnd);
    context_from_handle(scene, hnd, view_port, camera, smooth, force_zbuffer, &ctx);
    ctx.kernel = resolved;
    {
        int clip_left = view_port->clip_left > 0 ? view_port->clip_left : 0;
        int clip_top = view_port->clip_top > 0 ? view_port->clip_top : 0;
        int stride = ctx.stride;
        ctx.pixel_buffer = pixel_buffer + clip_left + clip_top * stride;
#ifndef TORIDRAW_PIXEL16
        /* Rebased by the same amount as the frame buffer, so one offset walks
         * both — see the layout note on ToriDraw_Scene.zbuffer. */
        if( ctx.zbuffered )
        {
            ctx.zbuf_target.pixel_buffer = ctx.pixel_buffer;
            ctx.zbuf_target.zbuffer += clip_left + clip_top * stride;
        }
#endif

        memset(&ctx.target, 0, sizeof(ctx.target));
        ctx.target.domain = TORIDRAW_RASTER_KERNEL_STOCK;
        ctx.target.pixel_buffer = ctx.pixel_buffer;
#ifndef TORIDRAW_PIXEL16
        ctx.target.zbuffer = ctx.zbuffered ? ctx.zbuf_target.zbuffer : NULL;
#endif
        ctx.target.width = ctx.screen_width;
        ctx.target.height = ctx.screen_height;
        ctx.target.stride = ctx.stride;
        ctx.target.clip_origin_x = clip_left;
        ctx.target.clip_origin_y = clip_top;
        ctx.target.projection_center_x = ctx.offset_x;
        ctx.target.projection_center_y = ctx.offset_y;
        ctx.target.near_plane_z = ctx.near_plane_z;
        ctx.target.camera_cot16 = ctx.camera_cot16;
        ctx.target.model_mid_z = scene->projected_vertex.z;
        ctx.target.parallel_projection = toridraw_proj_is_parallel(camera->proj_mode);
        ctx.target.smooth_shading = (ctx.flags & RASTER_FLAG_GOURAUD_SMOOTH) != 0;
        ctx.target.affine_textures = (ctx.flags & RASTER_FLAG_TEXTURE_AFFINE) != 0;
        ctx.target.depth_test = ctx.zbuffered;
        ctx.target.near_clip_available =
            ctx.allow_near_clip && ctx.orthographic_vertex_x_nullable &&
            ctx.orthographic_vertex_y_nullable && ctx.orthographic_vertex_z_nullable;
        ctx.target.vertex_count = ctx.num_vertices;
        ctx.target.screen_vertices_x = ctx.vertex_x;
        ctx.target.screen_vertices_y = ctx.vertex_y;
        ctx.target.screen_vertices_z = ctx.vertex_z;
        ctx.target.orthographic_vertices_x = ctx.orthographic_vertex_x_nullable;
        ctx.target.orthographic_vertices_y = ctx.orthographic_vertex_y_nullable;
        ctx.target.orthographic_vertices_z = ctx.orthographic_vertex_z_nullable;
        ctx.target.posed_vertices_x = model->vertices_x;
        ctx.target.posed_vertices_y = model->vertices_y;
        ctx.target.posed_vertices_z = model->vertices_z;
        ctx.target.bind_vertices_x =
            model->original_vertices_x ? model->original_vertices_x : model->vertices_x;
        ctx.target.bind_vertices_y =
            model->original_vertices_y ? model->original_vertices_y : model->vertices_y;
        ctx.target.bind_vertices_z =
            model->original_vertices_z ? model->original_vertices_z : model->vertices_z;
        ctx.target.internal = &ctx;
    }

#ifndef TORIDRAW_PIXEL16
    /* Reset before the first face, not after the last: this is what confines
     * the depth test to this model, so it has to happen even if the model turns
     * out to draw nothing. */
    if( ctx.zbuffered )
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
#endif

    struct ToriDraw_RasterDebugStats raster_debug_storage;
    if( toridraw_raster_debug_enabled() )
    {
        memset(&raster_debug_storage, 0, sizeof(raster_debug_storage));
        ctx.raster_debug = &raster_debug_storage;
    }
    else
    {
        ctx.raster_debug = NULL;
    }
    /* #region agent log */
    ctx.ordered_faces = unsorted ? ctx.num_faces : scene->tmp_face_order_count;
    /* #endregion */

    if( unsorted )
    {
        for( int face = 0; face < ctx.num_faces; face++ )
        {
            if( !toridraw_raster_face_front_facing(&ctx, face) )
                continue;
            ToriDraw_RasterModelFaceKernel(face, &ctx);
        }
    }
    else
    {
        for( int i = 0; i < scene->tmp_face_order_count; i++ )
        {
            int face = scene->tmp_face_order[i];
            ToriDraw_RasterModelFaceKernel(face, &ctx);
        }
    }

    if( ctx.raster_debug )
        toridraw_raster_debug_print(ctx.raster_debug, &ctx, (void*)model_as_full(hnd));

cleanup:
    ToriDraw_RasterPassEnd(scene);
}

static inline void
ToriDraw_Raster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    {
        return ToriDraw_RasterWithFaceIndices(
            scene, hnd, view_port, camera, pixel_buffer, smooth, false, false);
    }
    default:
        assert(false && "Invalid model handle kind");
        return;
    }
}

/** ToriDraw_Raster with the depth buffer forced on and the face order thrown
 *  away. See ToriDraw_RenderZBuffered, which is the only caller. */
static inline void
ToriDraw_RasterZBuffered(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    {
        return ToriDraw_RasterWithFaceIndices(
            scene, hnd, view_port, camera, pixel_buffer, smooth, true, true);
    }
    default:
        assert(false && "Invalid model handle kind");
        return;
    }
}
