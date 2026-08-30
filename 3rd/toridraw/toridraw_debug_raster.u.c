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

#endif /* TORIDRAW_DEBUG_RASTER_U_C */
