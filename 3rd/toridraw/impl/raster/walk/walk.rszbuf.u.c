#ifndef TORIDRAW_WALK_RSZBUF_U_C
#define TORIDRAW_WALK_RSZBUF_U_C

/*
 * The rev-241 depth-buffered model walk [class174.method5800 and what it
 * calls; tiles per class158.method5289 / method5294].
 *
 * Under `::zbuf 1` the client draws the scene twice: an OPAQUE pass that draws
 * every face without alpha, then a TRANSLUCENT pass, in the painter's order,
 * that draws only the faces with alpha (tiles not at all). A model's mode
 * decides how its faces are ordered and which buffers they may write
 * [class167, chosen by class140.method1206 and its overrides]:
 *
 *   model order    every face in index order, colour and depth written. The
 *                  default in the opaque pass, and in the translucent pass for
 *                  a model with no face alpha [field2295].
 *   sorted         back to front by depth bucket, no render priorities, colour
 *                  and depth written. The default for a model with face alpha
 *                  in the translucent pass [field2291].
 *   two-pass       colour only, in render-priority order -- so the painter's
 *                  rules decide how the model layers against itself -- then
 *                  depth only [field2293]. Every player (class52), and an
 *                  npc (class77) whose type carries opcode 147, which clears
 *                  its zbuf flag [class431.field5435, true by default]
 *                  (TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS).
 *
 * The client's fourth mode (colour only, model order [field2294]) belongs to
 * one scene-wide model this renderer never draws, and is not here.
 *
 * The pass is the kernel's (kernels/sd.rszbuffered.u.c names one per pass);
 * the mode is the model's. The walk culls and orders the faces and hands each
 * to the kernel's face callbacks below, which pass it to the per-kind dispatch
 * in impl/raster/dispatch/tri.rszbuf.u.c -- where the near clip lives, as it
 * does for every other kernel.
 *
 * Where the client throws -- a depth bucket or a render-priority band past its
 * static array, a near-clip edge past its reciprocal table -- it catches the
 * exception around the whole model and draws no more of it. The walk stops at
 * the same points (`abandoned`).
 */

// clang-format off
#include "graphics/raster/rszbuf/rszbuf_model.h"
#include "graphics/raster/rszbuf/rszbuf_raster.h"
#include "graphics/shared_tables.h"
#include "impl/raster/dispatch/tri.rszbuf.u.c"
// clang-format on

enum ToriDraw_RsZbufPass
{
    TORIDRAW_RSZBUF_PASS_OPAQUE = 0,
    TORIDRAW_RSZBUF_PASS_TRANSLUCENT = 1,
};

/** What a pass's kernel carries as its user_data. */
struct ToriDraw_RsZbufPassSpec
{
    enum ToriDraw_RsZbufPass pass;
};

/** One model's walk: the face callbacks' user_data. */
struct ToriDraw_RsZbufWalk
{
    const struct ToriDraw_Model* model;
    struct ToriDraw_RsZbufModel* rz;
    struct ToriDrawModelRasterContext* ctx;
    struct ToriDraw_RsZbufRaster raster;
    enum ToriDraw_RsZbufPass pass;
    bool terrain;
    /** Faces are grouped by depth bucket (rszbuf_bucket_faces has run). */
    bool bucketed;
    /** The fullest bucket, for the client's capacity test. */
    int largest_bucket;
    /** The client threw part way; draw no more of this model. */
    bool abandoned;
};

/* ------------------------------------------------------------ helpers */

/** A face's alpha byte as the rasterizer reads it: 255 is 253
 *  [class174.method5688: a byte -1 reads as 253]. */
static inline int
rszbuf_face_alpha(
    const struct ToriDrawModelRasterContext* ctx,
    int face)
{
    int alpha;

    if( !ctx->face_alphas_nullable )
        return 0;
    alpha = ctx->face_alphas_nullable[face] & 0xFF;
    return alpha == 0xFF ? 253 : alpha;
}

/** The face's depth bias in camera z [class174.field2408 * 2]. */
static inline int
rszbuf_face_depth_bias(
    const struct ToriDraw_Model* model,
    int face)
{
    return model->face_z_offsets ? model->face_z_offsets[face] * 2 : 0;
}

/**
 * The client's three colour words for a face [field2363 / field2401 /
 * field2402]. Its lighting writes A and C = -1 for a flat face and never
 * writes B, which keeps the 0 it was allocated with; the near clip lerps all
 * three, so they are given as the client holds them.
 */
static inline void
rszbuf_face_colors(
    const struct ToriDrawModelRasterContext* ctx,
    int face,
    int color[3])
{
    color[0] = ctx->colors_a[face];
    if( ctx->colors_c[face] == TORIDRAWHSL16_FLAT )
    {
        color[1] = 0;
        color[2] = -1;
        return;
    }
    color[1] = ctx->colors_b[face];
    color[2] = ctx->colors_c[face];
}

static inline bool
rszbuf_face_flag(
    const struct ToriDraw_RsZbufWalk* walk,
    int face,
    unsigned flag)
{
    return (walk->rz->face_flags[face] & flag) != 0;
}

/* ----------------------------------------------------- face callbacks */

static void
toridraw_rszbuf_flat(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    struct ToriDraw_RsZbufWalk* const walk = user_data;
    int const f = face->face_index;
    int color[3];

    (void)target;
    rszbuf_face_colors(walk->ctx, f, color);
    if( !ToriDraw_RsZbufTriangleFaceFlat(
            &walk->raster, walk->rz, face->vertex[0], face->vertex[1], face->vertex[2],
            rszbuf_face_depth_bias(walk->model, f), face->near_clipped,
            rszbuf_face_flag(walk, f, RSZBUF_FACE_CLIP_X), color[0], color[1], color[2],
            (int32_t)walk->raster.palette[color[0] & 0xFFFF]) )
        walk->abandoned = true;
}

static void
toridraw_rszbuf_gouraud(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    struct ToriDraw_RsZbufWalk* const walk = user_data;
    int const f = face->face_index;
    int color[3];

    (void)target;
    rszbuf_face_colors(walk->ctx, f, color);
    if( !ToriDraw_RsZbufTriangleFaceGouraud(
            &walk->raster, walk->rz, face->vertex[0], face->vertex[1], face->vertex[2],
            rszbuf_face_depth_bias(walk->model, f), face->near_clipped,
            rszbuf_face_flag(walk, f, RSZBUF_FACE_CLIP_X), color[0], color[1], color[2]) )
        walk->abandoned = true;
}

static void
toridraw_rszbuf_textured_impl(
    struct ToriDraw_RsZbufWalk* walk,
    const struct ToriDraw_RasterFaceSD* face,
    bool flat_shaded)
{
    struct ToriDraw_RsZbufFaceTexture texture;
    int const f = face->face_index;
    int color[3];

    toridraw_rszbuf_texture_frame(
        walk->rz, face->texture.frame.p, face->texture.frame.m, face->texture.frame.n,
        &texture.frame);
    texture.texels = face->texture.texels;
    texture.opaque = face->texture.gate == TORIDRAW_RASTER_TEXTURE_OPAQUE;
    texture.linear = walk->terrain;

    rszbuf_face_colors(walk->ctx, f, color);
    if( !ToriDraw_RsZbufTriangleFaceTexture(
            &walk->raster, walk->rz, &texture, face->vertex[0], face->vertex[1], face->vertex[2],
            rszbuf_face_depth_bias(walk->model, f), face->near_clipped,
            rszbuf_face_flag(walk, f, RSZBUF_FACE_CLIP_X), flat_shaded, color[0], color[1],
            color[2]) )
        walk->abandoned = true;
}

static void
toridraw_rszbuf_textured(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    (void)target;
    toridraw_rszbuf_textured_impl(user_data, face, false);
}

static void
toridraw_rszbuf_textured_flat(
    void* user_data,
    const struct ToriDraw_RasterTarget* target,
    const struct ToriDraw_RasterFaceSD* face)
{
    (void)target;
    toridraw_rszbuf_textured_impl(user_data, face, true);
}

/* ------------------------------------------------------------ the walk */

/**
 * Resolve one face and hand it to the kernel's callback for its class
 * [class174.method5688, method5704's choice of setup]. The alpha test here is
 * the pass filter for a two-pass model, whose faces method5800 does not
 * filter; for every other model it agrees with the flags.
 */
static void
rszbuf_draw_face(
    struct ToriDraw_RsZbufWalk* walk,
    int f)
{
    struct ToriDrawModelRasterContext* const ctx = walk->ctx;
    struct ToriDraw_RasterFaceSD face;
    int const alpha = walk->terrain ? 0 : rszbuf_face_alpha(ctx, f);
    bool const flat = ctx->colors_c[f] == TORIDRAWHSL16_FLAT;
    int const texture_id = ctx->face_textures ? ctx->face_textures[f] : -1;

    if( walk->pass == TORIDRAW_RSZBUF_PASS_TRANSLUCENT ? alpha == 0 : alpha != 0 )
        return;
    walk->raster.face_alpha = alpha;

    memset(&face, 0, sizeof(face));
    face.face_index = f;
    face.vertex[0] = ctx->face_indices_a[f];
    face.vertex[1] = ctx->face_indices_b[f];
    face.vertex[2] = ctx->face_indices_c[f];
    face.shade[0] = ctx->colors_a[f];
    face.shade[1] = flat ? ctx->colors_a[f] : ctx->colors_b[f];
    face.shade[2] = flat ? ctx->colors_a[f] : ctx->colors_c[f];
    face.opacity = 0xFF - alpha;
    face.near_clipped = rszbuf_face_flag(walk, f, RSZBUF_FACE_NEAR_CLIPPED);

    if( texture_id == -1 )
    {
        face.face_class = flat ? TORIDRAW_RASTER_FACE_SD_FLAT : TORIDRAW_RASTER_FACE_SD_GOURAUD;
    }
    else
    {
        struct ToriDraw_Texture* texture =
            (ctx->texture_map && texture_id >= 0 && texture_id < TORIDRAW_TEXTURE_ID_CAPACITY)
                ? ToriDraw_TextureMapGet(ctx->texture_map, texture_id)
                : NULL;
        int const coord = ctx->face_texture_coords ? ctx->face_texture_coords[f] : -1;

        /* The client's spans address a 128-wide texture and nothing else; a
         * texture not yet resident is skipped, as the painter skips it. */
        if( !texture || !texture->texels || texture->width != 128 )
            return;

        face.face_class =
            flat ? TORIDRAW_RASTER_FACE_SD_TEXTURED_FLAT : TORIDRAW_RASTER_FACE_SD_TEXTURED;
        face.texture.texture_id = texture_id;
        face.texture.texels = texture->texels;
        face.texture.width = texture->width;
        face.texture.height = texture->height;
        face.texture.gate =
            texture->opaque ? TORIDRAW_RASTER_TEXTURE_OPAQUE : TORIDRAW_RASTER_TEXTURE_COLOR_KEY;

        /* The mapping triangle the face names [field2355 -> field2410 /
         * field2412 / field2413], else its own corners. */
        if( coord >= 0 && coord < ctx->num_textured_faces && ctx->face_p_coordinate_nullable )
        {
            face.texture.frame.p = ctx->face_p_coordinate_nullable[coord];
            face.texture.frame.m = ctx->face_m_coordinate_nullable[coord];
            face.texture.frame.n = ctx->face_n_coordinate_nullable[coord];
        }
        else
        {
            face.texture.frame_fallback = true;
            face.texture.frame.p = face.vertex[0];
            face.texture.frame.m = face.vertex[1];
            face.texture.frame.n = face.vertex[2];
        }
        /* A mapping vertex the model does not have is an index the client
         * reads out of bounds: it throws. */
        if( face.texture.frame.p < 0 || face.texture.frame.p >= ctx->num_vertices ||
            face.texture.frame.m < 0 || face.texture.frame.m >= ctx->num_vertices ||
            face.texture.frame.n < 0 || face.texture.frame.n >= ctx->num_vertices )
        {
            walk->abandoned = true;
            return;
        }
    }

    ToriDraw_RasterKernelSDDispatch(&ctx->kernel, &ctx->target, &face);
}

/**
 * Cull and flag every face for this pass [class174.method5800, before the
 * draw; a tile's own tests in class158]: hidden faces; faces of the other pass
 * (not for a two-pass model, whose faces rszbuf_draw_face filters, nor a tile,
 * which is only drawn in the opaque pass); back faces -- in camera space when a
 * corner is behind the near plane, on screen otherwise; and whether a face
 * needs its spans clamped to the viewport.
 */
static void
rszbuf_flag_faces(
    struct ToriDraw_RsZbufWalk* walk,
    bool two_pass)
{
    RSZBUF_NO_FMA
    const struct ToriDrawModelRasterContext* const ctx = walk->ctx;
    struct ToriDraw_RsZbufModel* const rz = walk->rz;
    float const width = (float)walk->raster.clip_width;

    for( int f = 0; f < ctx->num_faces; f++ )
    {
        uint8_t flags = 0;

        if( ctx->colors_c[f] == TORIDRAWHSL16_HIDDEN )
        {
            rz->face_flags[f] = RSZBUF_FACE_CULLED;
            continue;
        }
        if( !two_pass && !walk->terrain )
        {
            bool const has_alpha = ctx->face_alphas_nullable && ctx->face_alphas_nullable[f] != 0;
            if( walk->pass == TORIDRAW_RSZBUF_PASS_TRANSLUCENT ? !has_alpha : has_alpha )
            {
                rz->face_flags[f] = RSZBUF_FACE_CULLED;
                continue;
            }
        }

        int const a = ctx->face_indices_a[f];
        int const b = ctx->face_indices_b[f];
        int const c = ctx->face_indices_c[f];
        float const xa = rz->screen_x[a];
        float const xb = rz->screen_x[b];
        float const xc = rz->screen_x[c];

        if( rz->any_behind_near &&
            (xa == RSZBUF_BEHIND_NEAR_PLANE || xb == RSZBUF_BEHIND_NEAR_PLANE ||
             xc == RSZBUF_BEHIND_NEAR_PLANE) )
        {
            /* The face's normal against the eye, in the camera-space ints. */
            int const ab_x = toridraw_wrap_sub(rz->camera_x[a], rz->camera_x[b]);
            int const cb_x = toridraw_wrap_sub(rz->camera_x[c], rz->camera_x[b]);
            int const ab_y = toridraw_wrap_sub(rz->camera_y[a], rz->camera_y[b]);
            int const cb_y = toridraw_wrap_sub(rz->camera_y[c], rz->camera_y[b]);
            int const ab_z = toridraw_wrap_sub(rz->camera_z[a], rz->camera_z[b]);
            int const cb_z = toridraw_wrap_sub(rz->camera_z[c], rz->camera_z[b]);
            int const normal_x =
                toridraw_wrap_sub(toridraw_wrap_mul(ab_y, cb_z), toridraw_wrap_mul(cb_y, ab_z));
            int const normal_y =
                toridraw_wrap_sub(toridraw_wrap_mul(cb_x, ab_z), toridraw_wrap_mul(ab_x, cb_z));
            int const normal_z =
                toridraw_wrap_sub(toridraw_wrap_mul(ab_x, cb_y), toridraw_wrap_mul(cb_x, ab_y));
            int const facing = toridraw_wrap_add(
                toridraw_wrap_add(
                    toridraw_wrap_mul(rz->camera_z[b], normal_z),
                    toridraw_wrap_mul(rz->camera_x[b], normal_x)),
                toridraw_wrap_mul(rz->camera_y[b], normal_y));

            flags |= RSZBUF_FACE_NEAR_CLIPPED;
            if( (float)facing <= 0.0f )
                flags |= RSZBUF_FACE_CULLED;
        }
        else
        {
            float const ya = rz->screen_y[a];
            float const yb = rz->screen_y[b];
            float const yc = rz->screen_y[c];

            if( (yc - yb) * (xa - xb) - (ya - yb) * (xc - xb) <= 0.0f )
                flags |= RSZBUF_FACE_CULLED;
            if( xa < 0.0f || xb < 0.0f || xc < 0.0f || xa > width || xb > width || xc > width )
                flags |= RSZBUF_FACE_CLIP_X;
        }
        rz->face_flags[f] = flags;
    }
}

/** Every unculled face, in index order [class174.method5694]. */
static void
rszbuf_draw_in_model_order(struct ToriDraw_RsZbufWalk* walk)
{
    for( int f = 0; f < walk->ctx->num_faces && !walk->abandoned; f++ )
        if( !rszbuf_face_flag(walk, f, RSZBUF_FACE_CULLED) )
            rszbuf_draw_face(walk, f);
}

static inline int
rszbuf_face_bucket(
    const struct ToriDraw_RsZbufWalk* walk,
    int f)
{
    const struct ToriDrawModelRasterContext* const ctx = walk->ctx;
    const int* const bucket_depth = walk->rz->bucket_depth;

    return (bucket_depth[ctx->face_indices_a[f]] + bucket_depth[ctx->face_indices_b[f]] +
            bucket_depth[ctx->face_indices_c[f]]) /
               3 +
           walk->rz->depth_offset;
}

/**
 * Group the unculled faces by the bucket of their average depth, index order
 * within a bucket [the fill in class174.method5697]. False where the fill
 * indexes outside the client's 6000 buckets and throws.
 *
 * Done once per model. A two-pass model fills the client's buckets twice
 * without emptying them, so the second fill holds every bucket's faces twice
 * over; the draws replay that by walking each bucket `fills` times rather than
 * storing it twice.
 */
static bool
rszbuf_bucket_faces(struct ToriDraw_RsZbufWalk* walk)
{
    struct ToriDraw_RsZbufModel* const rz = walk->rz;
    int const face_count = walk->ctx->num_faces;
    int start = 0;

    for( int b = 0; b < rz->depth_range; b++ )
        rz->bucket_count[b] = 0;
    walk->largest_bucket = 0;

    for( int f = 0; f < face_count; f++ )
    {
        if( rszbuf_face_flag(walk, f, RSZBUF_FACE_CULLED) )
            continue;
        int const bucket = rszbuf_face_bucket(walk, f);
        if( bucket < 0 || bucket >= RSZBUF_DEPTH_BUCKETS )
            return false;
        /* Filled but never drawn by the client. Its count there is never
         * cleared either, which is not reproduced. */
        if( bucket >= rz->depth_range )
            continue;
        if( ++rz->bucket_count[bucket] > walk->largest_bucket )
            walk->largest_bucket = rz->bucket_count[bucket];
    }

    for( int b = 0; b < rz->depth_range; b++ )
    {
        rz->bucket_start[b] = start;
        start += rz->bucket_count[b];
        rz->bucket_count[b] = 0;
    }
    for( int f = 0; f < face_count; f++ )
    {
        if( rszbuf_face_flag(walk, f, RSZBUF_FACE_CULLED) )
            continue;
        int const bucket = rszbuf_face_bucket(walk, f);
        if( bucket >= rz->depth_range )
            continue;
        rz->face_order[rz->bucket_start[bucket] + rz->bucket_count[bucket]++] = f;
    }
    walk->bucketed = true;
    return true;
}

/**
 * Faces in render-priority bands, the flexible bands 10 and 11 cut in by depth
 * ahead of bands 0, 3 and 5, at the average depths of bands 1-2, 3-4 and 6-8
 * [class174.method5726].
 */
static void
rszbuf_draw_by_priority(
    struct ToriDraw_RsZbufWalk* walk,
    int fills)
{
    const struct ToriDraw_Model* const model = walk->model;
    struct ToriDraw_RsZbufModel* const rz = walk->rz;
    int band_count[RSZBUF_PRIORITY_BANDS] = { 0 };
    int band_depth_sum[RSZBUF_PRIORITY_BANDS] = { 0 };

#define RSZBUF_BAND(band) (rz->priority_faces + (band) * RSZBUF_PRIORITY_CAPACITY)
#define RSZBUF_BAND_DEPTH(band) (rz->priority_depth + (band) * RSZBUF_PRIORITY_CAPACITY)

    for( int bucket = rz->depth_range - 1; bucket >= 0; bucket-- )
    {
        const int* const faces = rz->face_order + rz->bucket_start[bucket];
        for( int fill = 0; fill < fills; fill++ )
        {
            for( int i = 0; i < rz->bucket_count[bucket]; i++ )
            {
                int const f = faces[i];
                int const band = ToriDraw_ModelGetFacePriority(model->face_priorities, f);
                if( band >= RSZBUF_PRIORITY_BANDS || band_count[band] >= RSZBUF_PRIORITY_CAPACITY )
                {
                    walk->abandoned = true;
                    return;
                }
                int const at = band_count[band]++;
                RSZBUF_BAND(band)[at] = f;
                if( band < 10 )
                    band_depth_sum[band] = toridraw_wrap_add(band_depth_sum[band], bucket);
                else
                    RSZBUF_BAND_DEPTH(band)[at] = bucket;
            }
        }
    }

#define RSZBUF_BAND_AVERAGE(low, high)                                                             \
    (band_count[low] > 0 || band_count[high] > 0                                                   \
         ? toridraw_wrap_add(band_depth_sum[low], band_depth_sum[high]) /                          \
               (band_count[low] + band_count[high])                                                \
         : 0)

    int const average_1_2 = RSZBUF_BAND_AVERAGE(1, 2);
    int const average_3_4 = RSZBUF_BAND_AVERAGE(3, 4);
    int const average_6_8 = RSZBUF_BAND_AVERAGE(6, 8);

#undef RSZBUF_BAND_AVERAGE

    /* A cursor over band 10, then band 11, in the order they were filled. */
    int flex_index = 0;
    int flex_count = band_count[10];
    const int* flex_faces = RSZBUF_BAND(10);
    const int* flex_depths = RSZBUF_BAND_DEPTH(10);
    if( flex_index == flex_count )
    {
        flex_count = band_count[11];
        flex_faces = RSZBUF_BAND(11);
        flex_depths = RSZBUF_BAND_DEPTH(11);
    }
    int flex_depth = flex_index < flex_count ? flex_depths[flex_index] : -1000;

#define RSZBUF_DRAW_NEXT_FLEX()                                                                    \
    do                                                                                             \
    {                                                                                              \
        rszbuf_draw_face(walk, flex_faces[flex_index++]);                                          \
        if( flex_index == flex_count && flex_faces != RSZBUF_BAND(11) )                            \
        {                                                                                          \
            flex_index = 0;                                                                        \
            flex_count = band_count[11];                                                           \
            flex_faces = RSZBUF_BAND(11);                                                          \
            flex_depths = RSZBUF_BAND_DEPTH(11);                                                   \
        }                                                                                          \
        flex_depth = flex_index < flex_count ? flex_depths[flex_index] : -1000;                    \
    } while( 0 )

    for( int band = 0; band < 10 && !walk->abandoned; band++ )
    {
        while( band == 0 && flex_depth > average_1_2 && !walk->abandoned )
            RSZBUF_DRAW_NEXT_FLEX();
        while( band == 3 && flex_depth > average_3_4 && !walk->abandoned )
            RSZBUF_DRAW_NEXT_FLEX();
        while( band == 5 && flex_depth > average_6_8 && !walk->abandoned )
            RSZBUF_DRAW_NEXT_FLEX();
        for( int i = 0; i < band_count[band] && !walk->abandoned; i++ )
            rszbuf_draw_face(walk, RSZBUF_BAND(band)[i]);
    }
    while( flex_depth != -1000 && !walk->abandoned )
        RSZBUF_DRAW_NEXT_FLEX();

#undef RSZBUF_DRAW_NEXT_FLEX
#undef RSZBUF_BAND
#undef RSZBUF_BAND_DEPTH
}

/**
 * Back to front by depth bucket, or by render priority [class174.method5697].
 * `fills` is how many times the client has filled its buckets for this model,
 * this fill included.
 */
static void
rszbuf_draw_by_depth(
    struct ToriDraw_RsZbufWalk* walk,
    bool use_priorities,
    int fills)
{
    struct ToriDraw_RsZbufModel* const rz = walk->rz;

    if( !walk->bucketed && !rszbuf_bucket_faces(walk) )
    {
        walk->abandoned = true;
        return;
    }
    /* The client's bucket is a char[512]; the 513th face throws. */
    if( walk->largest_bucket * fills > RSZBUF_BUCKET_CAPACITY )
    {
        walk->abandoned = true;
        return;
    }

    if( use_priorities && walk->model->face_priorities )
    {
        rszbuf_draw_by_priority(walk, fills);
        return;
    }
    for( int bucket = rz->depth_range - 1; bucket >= 0 && !walk->abandoned; bucket-- )
    {
        const int* const faces = rz->face_order + rz->bucket_start[bucket];
        for( int fill = 0; fill < fills && !walk->abandoned; fill++ )
            for( int i = 0; i < rz->bucket_count[bucket] && !walk->abandoned; i++ )
                rszbuf_draw_face(walk, faces[i]);
    }
}

/** The masks the rasterizer holds between draws [class175.field2465 /
 *  field2442]: kept on the scratch too, because a model the client abandons
 *  part way leaves them as they were for whatever draws next. */
static inline void
rszbuf_set_write_masks(
    struct ToriDraw_RsZbufWalk* walk,
    int color_mask,
    int depth_mask)
{
    walk->rz->color_write_mask = color_mask;
    walk->rz->depth_write_mask = depth_mask;
    walk->raster.color_write_mask = color_mask;
    walk->raster.depth_write_mask = depth_mask;
}

/** The kernel's draw_model: one model, one pass. */
static void
toridraw_rszbuf_walk(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx)
{
    const struct ToriDraw_RsZbufPassSpec* const spec = user_data;
    const struct ToriDraw_Model* const model = ToriDraw_ModelRead(scene->active_hnd);
    struct ToriDraw_RsZbufModel* const rz = scene->rszbuf;
    struct ToriDraw_RsZbufWalk walk;

    assert(spec);
    assert(model);
    assert(rz);

    memset(&walk, 0, sizeof(walk));
    walk.model = model;
    walk.rz = rz;
    walk.ctx = ctx;
    walk.pass = spec->pass;
    walk.terrain = (model->flags & TORIDRAW_MODEL_FLAG_TERRAIN) != 0;

    bool const two_pass = (model->flags & TORIDRAW_MODEL_FLAG_RSZBUF_TWO_PASS) != 0;

    /* The client draws no tile in the translucent pass, and no model there
     * that has no face alpha [class137.method5019, class174.method5690]. */
    if( walk.pass == TORIDRAW_RSZBUF_PASS_TRANSLUCENT &&
        (walk.terrain || !ctx->face_alphas_nullable) )
        return;
    if( rz->depth_range >= RSZBUF_DEPTH_BUCKETS )
        return;

    walk.raster.pixels = ctx->pixel_buffer;
    walk.raster.zbuffer = ctx->zbuf_target.zbuffer;
    walk.raster.stride = ctx->stride;
    walk.raster.clip_width = ctx->screen_width;
    walk.raster.clip_height = ctx->screen_height;
    walk.raster.center_x = rz->center_x;
    walk.raster.center_y = rz->center_y;
    walk.raster.projection_scale = rz->projection_scale;
    walk.raster.gouraud_blocks4 = true;
    walk.raster.palette = g_hsl16_to_pixel_table;
    walk.raster.color_write_mask = rz->color_write_mask;
    walk.raster.depth_write_mask = rz->depth_write_mask;

    /* The face callbacks recover the walk from user_data. */
    ctx->kernel.user_data = &walk;

    rszbuf_flag_faces(&walk, two_pass);

    if( walk.terrain )
    {
        /* A tile is not a model: no mode, and it writes through whatever
         * masks are live. */
        rszbuf_draw_in_model_order(&walk);
        return;
    }

    if( two_pass )
    {
        rszbuf_set_write_masks(&walk, -1, 0);
        rszbuf_draw_by_depth(&walk, true, 1);
        if( walk.abandoned )
            return;
        rszbuf_set_write_masks(&walk, 0, -1);
        rszbuf_draw_by_depth(&walk, true, 2);
        if( walk.abandoned )
            return;
        rszbuf_set_write_masks(&walk, -1, -1);
        return;
    }

    rszbuf_set_write_masks(&walk, -1, -1);
    if( walk.pass == TORIDRAW_RSZBUF_PASS_OPAQUE || !ctx->face_alphas_nullable )
        rszbuf_draw_in_model_order(&walk);
    else
        rszbuf_draw_by_depth(&walk, false, 1);
}

#endif
