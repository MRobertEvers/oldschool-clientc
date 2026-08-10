#ifndef VERTEXINT_BITS
#define VERTEXINT_BITS 16
#endif

#include "toridraw_model.h"
#include "toridraw_model_internal.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TORIDRAW_PIXEL16
static const int g_empty_texture_texels[128 * 128] = { 0 };
#endif

enum DashModelRasterFlags
{
    RASTER_FLAG_GOURAUD_SMOOTH = 1 << 0,
    RASTER_FLAG_TEXTURE_AFFINE = 1 << 1,
};

enum FaceType
{
    FACE_TYPE_GOURAUD,
    FACE_TYPE_FLAT,
    FACE_TYPE_TEXTURED,
    FACE_TYPE_TEXTURED_FLAT_SHADE,
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
    int cache_texture_opaque;
    int cache_texture_alpha_blended;

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
                   s->skipped_near_clip > 0 || s->skipped_tex_miss > 0 || s->index_oob > 0;

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
                "\"skip_near_clip\":%d,\"skip_tex_miss\":%d,\"index_oob\":%d,"
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
        "skip_near_clip=%d skip_tex_miss=%d index_oob=%d "
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
        s->index_oob,
        (int)ctx->allow_near_clip,
        (int)ctx->near_clipped,
        ctx->face_infos ? "yes" : "no",
        hist_buf);
}

static inline void
ToriDraw_RasterModelFace(
    int face,
    struct ToriDrawModelRasterContext* ctx)
{
    assert(face >= 0 && face < ctx->num_faces);

    struct ToriDraw_Texture* texture = NULL;
    struct ToriDraw_RasterDebugStats* dbg = ctx->raster_debug;

    /* Render type is the raw byte (reference ModelData.light). 0=gouraud,
     * 1=flat, 2=hidden, 3=black/hidden; any other value is also hidden. */
    int raw_type = ctx->face_infos ? ctx->face_infos[face] : 0;
    if( dbg && raw_type >= 0 && raw_type < 16 )
        dbg->type_hist[raw_type]++;
    if( raw_type == 2 || raw_type < 0 || raw_type > 3 )
    {
        if( dbg )
            dbg->skipped_type++;
        return;
    }
    enum FaceType type = (enum FaceType)raw_type;

    /* Bounds-check vertex indices when debug is active (level 2). ASAN catches
     * heap overflows, but a wrong-yet-in-bounds index produces silent garbage. */
    if( dbg && toridraw_raster_debug_level() >= 2 )
    {
        int const va = (int)(uint16_t)ctx->face_indices_a[face];
        int const vb = (int)(uint16_t)ctx->face_indices_b[face];
        int const vc = (int)(uint16_t)ctx->face_indices_c[face];
        if( va >= ctx->num_vertices || vb >= ctx->num_vertices || vc >= ctx->num_vertices )
        {
            dbg->index_oob++;
            fprintf(stderr,
                "raster_index_oob: model=%p face=%d indices=(%d,%d,%d) num_vertices=%d\n",
                (void*)ctx->face_indices_a,
                face, va, vb, vc, ctx->num_vertices);
        }
    }

    int color_a = ctx->colors_a[face];
    int color_b = ctx->colors_b[face];
    int color_c = ctx->colors_c[face];

    // Faces with color_c == -2 are not drawn. As far as I can tell, these faces are used for
    // texture PNM coordinates that do not coincide with a visible face.
    // /Users/matthewevers/Documents/git_repos/OS1/src/main/java/jagex3/dash3d/ModelUnlit.java
    // OS1 skips faces with -2.
    if( color_c == TORIDRAWHSL16_HIDDEN )
    {
        // TODO: How to organize this.
        // See here
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/rs/model/ModelData.ts
        // .light

        // and
        // /Users/matthewevers/Documents/git_repos/rs-map-viewer/src/mapviewer/webgl/buffer/SceneBuffer.ts
        // getModelFaces
        if( dbg )
            dbg->skipped_hidden++;
        return;
        // color_c = 0;
    }

    int tp_vertex;
    int tm_vertex;
    int tn_vertex;

    int tp_x;
    int tp_y;
    int tp_z;
    int tm_x;
    int tm_y;
    int tm_z;
    int tn_x;
    int tn_y;
    int tn_z;

    int texture_id;
    int texture_face;
    int alpha = ctx->face_alphas_nullable ? (ctx->face_alphas_nullable[face]) : 0xFF;

    // TODO: See above comments. alpha overrides colors.
    // if( ctx->face_alphas_nullable && ctx->face_alphas_nullable[index] < 0 )
    // {
    //     return;
    // }

#ifndef TORIDRAW_PIXEL16
    const int* texels = g_empty_texture_texels;
    int texture_size = 0;
    int texture_opaque = true;
    int texture_alpha_blended = false;
    if( ctx->face_textures != NULL )
        texture_id = ctx->face_textures[face];
    else
        texture_id = -1;

    /* #region agent log */
    /* TORIDRAW_SKIP_TEXTURED=1: drop every textured face instead of drawing it.
     * A bisect knob for docs/qbd_toridraw_streaks_debug.md - it answers whether
     * a given artifact comes from the textured span path or from
     * gouraud/flat, without having to reason about which faces are which. */
    {
        static int skip_textured = -1;
        if( skip_textured < 0 )
            skip_textured = getenv("TORIDRAW_SKIP_TEXTURED") ? 1 : 0;
        if( skip_textured && texture_id != -1 )
            return;
    }
    /* #endregion */

    if( texture_id != -1 )
    {
        if( texture_id == ctx->cache_texture_id )
        {
            texels = ctx->cache_texels;
            texture_size = ctx->cache_texture_size;
            texture_opaque = ctx->cache_texture_opaque;
            texture_alpha_blended = ctx->cache_texture_alpha_blended;

            if( color_c == TORIDRAWHSL16_FLAT )
                goto textured_flat;
            else
                goto textured;
        }

        // gamma 0.8 is the default in os1
        texture = (texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY)
                      ? ToriDraw_TextureMapGet(ctx->texture_map, texture_id)
                      : NULL;
        // Texture not loaded (yet): skip the face. Textured faces store 0-127
        // lightness in colors_a/b/c, not HSL16, so a gouraud fallback would
        // draw garbage; the reference skips the face too.
        if( texture == NULL )
        {
            if( dbg )
                dbg->skipped_tex_miss++;
            /* TORIRS_RASTER_TEX_DEBUG=1: tally skipped textured faces.
             * Resolve the env var once - getenv() walks environ with a strncmp
             * per entry, and this branch is the steady state for every face of
             * every model whose textures have not streamed in yet. */
            static int skip_tally[TORIDRAW_TEXTURE_ID_CAPACITY];
            static int skip_total = 0;
            static int debug_enabled = -1;
            if( debug_enabled < 0 )
                debug_enabled = getenv("TORIRS_RASTER_TEX_DEBUG") ? 1 : 0;

            if( debug_enabled && texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY )
            {
                skip_tally[texture_id]++;
                if( ++skip_total % 500 == 1 )
                    fprintf(
                        stderr,
                        "raster_tex_skip: total=%d id=%d (count=%d)\n",
                        skip_total,
                        texture_id,
                        skip_tally[texture_id]);
            }
            return;
        }

        texels = texture->texels;
        texture_size = texture->width;
        texture_opaque = texture->opaque;
        texture_alpha_blended = texture->alpha_blended;

        ctx->cache_texture_id = texture_id;
        ctx->cache_texels = texels;
        ctx->cache_texture_size = texture_size;
        ctx->cache_texture_opaque = texture_opaque;
        ctx->cache_texture_alpha_blended = texture_alpha_blended;

        if( color_c == TORIDRAWHSL16_FLAT )
            goto textured_flat;
        else
            goto textured;
    }
    else
#endif
    {
        // Alpha is a signed byte, but for non-textured
        // faces, we treat it as unsigned.
        // TORIDRAWHSL16_FLAT / TORIDRAWHSL16_HIDDEN are reserved. See lighting code.
        if( ctx->face_alphas_nullable )
            alpha = 0xFF - alpha;
        /* Reference getModelFaces: skip fully/near-fully transparent faces. */
        if( alpha <= 1 )
        {
            if( dbg )
                dbg->skipped_alpha++;
            return;
        }

        if( color_c == TORIDRAWHSL16_FLAT )
        {
            type = FACE_TYPE_FLAT;
        }
        else
        {
            type = FACE_TYPE_GOURAUD;
        }
        /* Near-clip gate for non-textured faces: if any vertex landed behind the
         * near plane and the context has no ortho buffer (allow_near_clip=false),
         * the triangle functions silently return.  Count those separately so the
         * debug line exposes them rather than attributing them to "drawn". */
        if( dbg && ctx->near_clipped && !ctx->allow_near_clip )
        {
            int const xa = ctx->vertex_x[ctx->face_indices_a[face]];
            int const xb = ctx->vertex_x[ctx->face_indices_b[face]];
            int const xc = ctx->vertex_x[ctx->face_indices_c[face]];
            if( xa == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                xb == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                xc == TORIDRAW_SCREEN_X_NEAR_CLIPPED )
            {
                dbg->skipped_near_clip++;
                return;
            }
        }
        if( dbg )
        {
            dbg->drawn++;
            /* #region agent log */
            if( type == FACE_TYPE_FLAT )
                dbg->drawn_flat++;
            else
                dbg->drawn_gouraud++;
            /* Gouraud faces only. A flat face carries TORIDRAWHSL16_FLAT
             * (0xFF7F) in color_c as a selector, not a colour, so including
             * them pinned this at 65407 for every model and hid the real
             * spread. */
            if( type != FACE_TYPE_FLAT && color_c != TORIDRAWHSL16_FLAT )
            {
                int const dab = abs(color_a - color_b);
                int const dbc = abs(color_b - color_c);
                int const dca = abs(color_c - color_a);
                int const worst = dab > dbc ? (dab > dca ? dab : dca)
                                            : (dbc > dca ? dbc : dca);
                if( worst > dbg->max_color_delta )
                    dbg->max_color_delta = worst;
            }
            /* #endregion */
        }
        switch( type )
        {
        case FACE_TYPE_GOURAUD:
            if( (ctx->flags & RASTER_FLAG_GOURAUD_SMOOTH) != 0 )
            {
                ToriDraw_TriangleFaceGouraudSmooth(
                    ctx->pixel_buffer,
                    face,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    ctx->face_alphas_nullable,
                    ctx->near_plane_z,
                    ctx->camera_cot16,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else
            {
                ToriDraw_TriangleFaceGouraud(
                    ctx->pixel_buffer,
                    face,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    ctx->face_alphas_nullable,
                    ctx->near_plane_z,
                    ctx->camera_cot16,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }

            break;
        case FACE_TYPE_FLAT:
            // Skip triangle if any vertex was clipped

            ToriDraw_TriangleFaceFlat(
                ctx->pixel_buffer,
                face,
                ctx->face_indices_a,
                ctx->face_indices_b,
                ctx->face_indices_c,
                ctx->vertex_x,
                ctx->vertex_y,
                ctx->vertex_z,
                ctx->orthographic_vertex_x_nullable,
                ctx->orthographic_vertex_y_nullable,
                ctx->orthographic_vertex_z_nullable,
                ctx->colors_a,
                ctx->face_alphas_nullable,
                ctx->near_plane_z,
                ctx->camera_cot16,
                ctx->offset_x,
                ctx->offset_y,
                ctx->stride,
                ctx->screen_width,
                ctx->screen_height,
                ctx->allow_near_clip,
                ctx->near_clipped);

            break;
#ifndef TORIDRAW_PIXEL16
        case FACE_TYPE_TEXTURED:
        textured:;
            if( dbg )
            {
                dbg->drawn++;
                /* #region agent log */
                dbg->drawn_textured++;
                toridraw_raster_debug_note_texture(dbg, texture_id);
                /* #endregion */
            }
            assert(ctx->orthographic_vertex_x_nullable != NULL);
            assert(ctx->orthographic_vertex_y_nullable != NULL);
            assert(ctx->orthographic_vertex_z_nullable != NULL);

            if( ctx->face_texture_coords && ctx->face_texture_coords[face] != -1 )
            {
                assert(ctx->face_p_coordinate_nullable != NULL);
                assert(ctx->face_m_coordinate_nullable != NULL);
                assert(ctx->face_n_coordinate_nullable != NULL);

                texture_face = ctx->face_texture_coords[face];

                tp_vertex = ctx->face_p_coordinate_nullable[texture_face];
                tm_vertex = ctx->face_m_coordinate_nullable[texture_face];
                tn_vertex = ctx->face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = ctx->face_indices_a[texture_face];
                tm_vertex = ctx->face_indices_b[texture_face];
                tn_vertex = ctx->face_indices_c[texture_face];
            }
            // texture_id = ctx->face_textures[index];
            // texture_face = ctx->face_infos[index] >> 2;
            // texture_face = ctx->face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < ctx->num_vertices);
            assert(tm_vertex < ctx->num_vertices);
            assert(tn_vertex < ctx->num_vertices);

            if( (ctx->flags & RASTER_FLAG_TEXTURE_AFFINE) != 0 )
            {
                ToriDraw_TriangleFaceTextureBlendAffineV3(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    (int*)texels,
                    texture_size,
                    texture_opaque,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            /* Per-texel alpha: blend rather than colour-key. Checked before
             * opaque because such a texture is never marked opaque, and before
             * transparent because that path would threshold it into holes. */
            else if( texture_alpha_blended )
            {
                ToriDraw_TriangleFaceTextureBlendAlpha(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else if( texture_opaque )
            {
                ToriDraw_TriangleFaceTextureBlendOpaque(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else
            {
                ToriDraw_TriangleFaceTextureBlendTransparent(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    ctx->colors_b,
                    ctx->colors_c,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }

            break;
        case FACE_TYPE_TEXTURED_FLAT_SHADE:
        textured_flat:;
            if( dbg )
            {
                dbg->drawn++;
                /* #region agent log */
                dbg->drawn_textured++;
                toridraw_raster_debug_note_texture(dbg, texture_id);
                /* #endregion */
            }
            assert(ctx->orthographic_vertex_x_nullable != NULL);
            assert(ctx->orthographic_vertex_y_nullable != NULL);
            assert(ctx->orthographic_vertex_z_nullable != NULL);

            if( ctx->face_texture_coords && ctx->face_texture_coords[face] != -1 )
            {
                texture_face = ctx->face_texture_coords[face];

                tp_vertex = ctx->face_p_coordinate_nullable[texture_face];
                tm_vertex = ctx->face_m_coordinate_nullable[texture_face];
                tn_vertex = ctx->face_n_coordinate_nullable[texture_face];
            }
            else
            {
                texture_face = face;
                tp_vertex = ctx->face_indices_a[texture_face];
                tm_vertex = ctx->face_indices_b[texture_face];
                tn_vertex = ctx->face_indices_c[texture_face];
            }
            // texture_id = ctx->face_textures[index];
            // texture_face = ctx->face_infos[index] >> 2;
            // texture_face = ctx->face_texture_coords[index];

            assert(tp_vertex > -1);
            assert(tm_vertex > -1);
            assert(tn_vertex > -1);

            assert(tp_vertex < ctx->num_vertices);
            assert(tm_vertex < ctx->num_vertices);
            assert(tn_vertex < ctx->num_vertices);

            if( (ctx->flags & RASTER_FLAG_TEXTURE_AFFINE) != 0 )
            {
                ToriDraw_TriangleFaceTextureFlatAffineV3(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    (int*)texels,
                    texture_size,
                    texture_opaque,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else if( texture_opaque )
            {
                ToriDraw_TriangleFaceTextureFlatOpaque(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }
            else
            {
                ToriDraw_TriangleFaceTextureFlatTransparent(
                    ctx->pixel_buffer,
                    ctx->stride,
                    ctx->screen_width,
                    ctx->screen_height,
                    ctx->camera_cot16,
                    face,
                    tp_vertex,
                    tm_vertex,
                    tn_vertex,
                    ctx->face_indices_a,
                    ctx->face_indices_b,
                    ctx->face_indices_c,
                    ctx->vertex_x,
                    ctx->vertex_y,
                    ctx->vertex_z,
                    ctx->orthographic_vertex_x_nullable,
                    ctx->orthographic_vertex_y_nullable,
                    ctx->orthographic_vertex_z_nullable,
                    ctx->colors_a,
                    (int*)texels,
                    texture_size,
                    ctx->near_plane_z,
                    ctx->offset_x,
                    ctx->offset_y,
                    ctx->allow_near_clip,
                    ctx->near_clipped);
            }

            break;
#endif /* TORIDRAW_PIXEL16 */
        default:
            break;
        }
    }
}

static inline void
context_from_handle(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    bool smooth,
    struct ToriDrawModelRasterContext* ctx)
{
    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
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
        ctx->cache_texture_opaque = 0;
        ctx->cache_texture_alpha_blended = 0;
        ctx->flags = 0;
        if( smooth )
            ctx->flags |= RASTER_FLAG_GOURAUD_SMOOTH;
        if( false )
            ctx->flags |= RASTER_FLAG_TEXTURE_AFFINE;
        ctx->allow_near_clip = ToriDraw_ModelHasTextures(hnd);
        ctx->near_clipped = scene->near_clipped;
        ctx->raster_debug = NULL;
        break;
    }
    default:
        break;
    }
}

static inline void
ToriDraw_RasterWithFaceIndices(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    struct ToriDrawModelRasterContext ctx;
    context_from_handle(scene, hnd, view_port, camera, smooth, &ctx);
    {
        int clip_left = view_port->clip_left > 0 ? view_port->clip_left : 0;
        int clip_top = view_port->clip_top > 0 ? view_port->clip_top : 0;
        int stride = ctx.stride;
        ctx.pixel_buffer = pixel_buffer + clip_left + clip_top * stride;
    }

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
    ctx.ordered_faces = scene->tmp_face_order_count;
    /* #endregion */

    for( int i = 0; i < scene->tmp_face_order_count; i++ )
    {
        int face = scene->tmp_face_order[i];
        ToriDraw_RasterModelFace(face, &ctx);
    }

    if( ctx.raster_debug )
        toridraw_raster_debug_print(ctx.raster_debug, &ctx, (void*)model_as_full(hnd));
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
    {
        return ToriDraw_RasterWithFaceIndices(
            scene, hnd, view_port, camera, pixel_buffer, smooth);
    }
    default:
        assert(false && "Invalid model handle kind");
        return;
    }
}
