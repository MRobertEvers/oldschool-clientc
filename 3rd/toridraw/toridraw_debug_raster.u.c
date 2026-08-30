#ifndef TORIDRAW_DEBUG_RASTER_U_C
#define TORIDRAW_DEBUG_RASTER_U_C

/*
 * NDJSON emitter for the raster path.
 *
 * Included by toridraw_raster.u.c once struct ToriDraw_RasterDebugStats and
 * struct ToriDrawModelRasterContext are in scope, and after the clip and
 * texture-plane counters it reports (g_toridraw_clip_recip_oob,
 * g_toridraw_tex_plane_max_shift, g_toridraw_tex_plane_rejected) have been
 * declared earlier in the translation unit.
 *
 * Compiled out with the rest of the log; see toridraw_debug_log.h.
 */

#include "toridraw_debug_log.h"

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
};

#if defined(TORIDRAW_DEBUG_NDJSON) && TORIDRAW_DEBUG_NDJSON

/**
 * Per-model face accounting: what the raster drew, what it dropped and why.
 *
 * `anomaly` is the caller's own summary of the skip counters, reused rather
 * than recomputed so the record and the TORIDRAW_RASTER_DEBUG print below it
 * cannot disagree about what counts as one.
 */
static void
toridraw_dbg_record_raster_faces(
    const struct ToriDraw_RasterDebugStats* s,
    const struct ToriDrawModelRasterContext* ctx,
    const void* model_ptr,
    bool anomaly)
{
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
}

#define TORIDRAW_DBG_RECORD_RASTER_FACES(s, ctx, model_ptr, anomaly) \
    toridraw_dbg_record_raster_faces((s), (ctx), (model_ptr), (anomaly))

#else

#define TORIDRAW_DBG_RECORD_RASTER_FACES(s, ctx, model_ptr, anomaly) \
    ((void)(s), (void)(ctx), (void)(model_ptr), (void)(anomaly))

#endif /* TORIDRAW_DEBUG_NDJSON */

/*
 * The TORIDRAW_RASTER_DEBUG counters and printer.
 *
 * Last, because the printer calls the record macro above: it emits its
 * NDJSON record first and its human line second, from the same counters.
 */
static inline void
toridraw_raster_debug_note_texture(struct ToriDraw_RasterDebugStats* s, int texture_id)
{
    for( int i = 0; i < s->tex_id_count; i++ )
        if( s->tex_ids[i] == texture_id )
            return;
    if( s->tex_id_count < (int)(sizeof(s->tex_ids) / sizeof(s->tex_ids[0])) )
        s->tex_ids[s->tex_id_count++] = texture_id;
}

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
    if( TORIDRAW_DBG_ENABLED() )
        return true;
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

    TORIDRAW_DBG_RECORD_RASTER_FACES(s, ctx, model_ptr, anomaly);

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

#endif /* TORIDRAW_DEBUG_RASTER_U_C */
