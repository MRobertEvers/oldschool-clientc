#ifndef TORIDRAW_RASTER_BATCH_U_C
#define TORIDRAW_RASTER_BATCH_U_C

/*
 * The batched whole-model walk, and the one place its compile-time gate lives.
 *
 * WHAT IT IS. A stage-3 walk that runs the sorted face order once, classifies
 * each face into one of eight run classes, stages consecutive same-class faces
 * into a row buffer, and flushes each run to a presorted-run assembly kernel --
 * one call per RUN instead of one per triangle. It is the only consumer of the
 * y-ordered stash (sm_face_x4 / y4) the face sort leaves behind, which is what
 * lets the assembly skip the six-way y-permutation ladder entirely.
 *
 * WHY THE GATE, AND WHY IT IS HERE. Nothing about this is a feature toggle.
 * The walk names symbols like toridraw_flat_opaque_s4_presorted_run_asm
 * DIRECTLY, and those exist only on lanes where the makefile assembled the .S
 * files -- TORIDRAW_RASTER_BATCH is derived from exactly that
 * (toridraw_raster_batch.h, off the two _PRESORTED_RUN macros the asm headers
 * define). On a scalar, no-SIMD or PIXEL16 build those symbols are absent and
 * the calls would not link. It is a fact about the lane, not a choice.
 *
 * So the gate belongs at the boundary where the assembly is named, and nowhere
 * else. Off that lane this file still defines the name, aliased to the stock
 * walk, so a kernel that wants the batched walk writes one unconditional line
 * and carries no #ifdef of its own:
 *
 *     .draw_model = toridraw_raster_walk_batched,
 *
 * The name is supplied on every lane by raster.batch.h, which aliases it to
 * the stock walk where this file compiles to nothing. That alias is
 * load-bearing beyond tidiness. "Does this kernel draw whole
 * models" is asked by identity against ToriDraw_RasterWalkPerFace, so on a lane
 * with no assembly the branching kernel correctly reports that it does NOT --
 * and the face sort is correctly told not to fill a stash nothing would read.
 *
 * TORIDRAW_RASTER_BATCH=0 is a separate RUNTIME A/B knob, and exists only where
 * this gate already passed; see toridraw_raster_batch_armed().
 */

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
 * already holding (scene->sm_face_x4/y4), this pass copies them into a row, and
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
#define TORIDRAW_RASTER_BATCH_ROW_INTS TORIDRAW_GOURAUD_RUN_ROW_INTS

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

/* The textured row's lanes are laid out by tex_tri_asm.h: twenty-four ints,
 * the texel pointer taking one lane on i686 and two on an LP64 host. */
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
            toridraw_gouraud_opaque_s4_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_GOURAUD_ALPHA:
            toridraw_gouraud_alpha_s4_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_FLAT_OPAQUE:
            toridraw_flat_opaque_s4_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_FLAT_ALPHA:
            toridraw_flat_alpha_s4_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, g_toridraw_raster_batch, batch->count);
            break;
#ifdef TORIDRAW_TEXTRI_PRESORTED_RUN
        case TORIDRAW_RASTER_BATCH_TEX_OPAQUE:
            toridraw_textri_opaque_lerp8_v3_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_TEX_TRANS:
            toridraw_textri_trans_lerp8_v3_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_TEX_FLAT_OPAQUE:
            toridraw_textri_flat_opaque_lerp8_v3_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
        case TORIDRAW_RASTER_BATCH_TEX_FLAT_TRANS:
            toridraw_textri_flat_trans_lerp8_v3_presorted_run_asm(
                ctx->pixel_buffer, ctx->stride, ctx->screen_width,
                ctx->screen_height, ctx->camera_cot16,
                g_toridraw_raster_texbatch, batch->count);
            break;
#endif /* TORIDRAW_TEXTRI_PRESORTED_RUN */
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
#ifdef TORIDRAW_TEXTRI_PRESORTED_RUN
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
    if( ctx->near_clipped && ctx->face_x4[(size_t)face * 4 + 3] )
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
#endif /* TORIDRAW_TEXTRI_PRESORTED_RUN */

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
#ifdef TORIDRAW_TEXTRI_PRESORTED_RUN
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

    if( ctx->near_clipped && ctx->face_x4[(size_t)face * 4 + 3] )
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
#ifdef TORIDRAW_TEXTRI_PRESORTED_RUN
static void
toridraw_raster_batch_append_textured(
    struct ToriDrawModelRasterContext* ctx,
    int face,
    enum ToriDraw_RasterBatchClass kind,
    const struct ToriDraw_RasterBatchFace* info,
    struct ToriDraw_RasterBatch* batch)
{
    int const* const x4 = &ctx->face_x4[(size_t)face * 4];
    int const* const y4 = &ctx->face_y4[(size_t)face * 4];
    int* const row = g_toridraw_raster_texbatch +
                     batch->count * TORIDRAW_RASTER_TEXBATCH_ROW_INTS;
    int const* const ox_arr = ctx->orthographic_vertex_x_nullable;
    int const* const oy_arr = ctx->orthographic_vertex_y_nullable;
    int const* const oz_arr = ctx->orthographic_vertex_z_nullable;

    row[TORIDRAW_TEXBATCH_LANE_X + 0] = x4[0] + ctx->offset_x;
    row[TORIDRAW_TEXBATCH_LANE_X + 1] = x4[1] + ctx->offset_x;
    row[TORIDRAW_TEXBATCH_LANE_X + 2] = x4[2] + ctx->offset_x;
    row[TORIDRAW_TEXBATCH_LANE_Y + 0] = y4[0] + ctx->offset_y;
    row[TORIDRAW_TEXBATCH_LANE_Y + 1] = y4[1] + ctx->offset_y;
    row[TORIDRAW_TEXBATCH_LANE_Y + 2] = y4[2] + ctx->offset_y;

    row[TORIDRAW_TEXBATCH_LANE_OX + 0] = ox_arr[info->p];
    row[TORIDRAW_TEXBATCH_LANE_OX + 1] = ox_arr[info->m];
    row[TORIDRAW_TEXBATCH_LANE_OX + 2] = ox_arr[info->n];
    row[TORIDRAW_TEXBATCH_LANE_OY + 0] = oy_arr[info->p];
    row[TORIDRAW_TEXBATCH_LANE_OY + 1] = oy_arr[info->m];
    row[TORIDRAW_TEXBATCH_LANE_OY + 2] = oy_arr[info->n];
    row[TORIDRAW_TEXBATCH_LANE_OZ + 0] = oz_arr[info->p];
    row[TORIDRAW_TEXBATCH_LANE_OZ + 1] = oz_arr[info->m];
    row[TORIDRAW_TEXBATCH_LANE_OZ + 2] = oz_arr[info->n];

    if( kind == TORIDRAW_RASTER_BATCH_TEX_FLAT_OPAQUE ||
        kind == TORIDRAW_RASTER_BATCH_TEX_FLAT_TRANS )
    {
        /* One shade, and the constant-shade walk reads only this lane. Lanes
         * 16 and 17 are left as the previous row left them on purpose:
         * writing them would be two stores per face for a value with no
         * consumer. */
        row[TORIDRAW_TEXBATCH_LANE_SHADE] = ctx->colors_a[face];
    }
    else
    {
        /* Per-vertex shades, so they follow the sort's permutation. */
        unsigned char const* const o = g_toridraw_ysort_order[y4[3]];
        int const col[3] = { ctx->colors_a[face], ctx->colors_b[face],
                             ctx->colors_c[face] };
        row[TORIDRAW_TEXBATCH_LANE_SHADE + 0] = col[o[0]];
        row[TORIDRAW_TEXBATCH_LANE_SHADE + 1] = col[o[1]];
        row[TORIDRAW_TEXBATCH_LANE_SHADE + 2] = col[o[2]];
    }

    TORIDRAW_TEXBATCH_SET_TEXELS(row, info->texels);
    row[TORIDRAW_TEXBATCH_LANE_TW] = info->texture_width;
    row[TORIDRAW_TEXBATCH_LANE_GATE] = info->gate;
    /* The rest pads the row to 16 bytes; no kernel reads it. */

    batch->kind = kind;
    batch->count++;
}
#endif /* TORIDRAW_TEXTRI_PRESORTED_RUN */

static void
toridraw_raster_batch_append(
    struct ToriDrawModelRasterContext* ctx,
    int face,
    enum ToriDraw_RasterBatchClass kind,
    int alpha,
    struct ToriDraw_RasterBatch* batch)
{
    int const* const x4 = &ctx->face_x4[(size_t)face * 4];
    int const* const y4 = &ctx->face_y4[(size_t)face * 4];
    int* const row =
        g_toridraw_raster_batch + batch->count * TORIDRAW_RASTER_BATCH_ROW_INTS;
    int const ox = ctx->offset_x;
    int const oy = ctx->offset_y;

    /* Already in y order -- the depth sort did that, with the y values it was
     * holding for the winding. Nothing here reorders anything; the viewport
     * offset is the only arithmetic left, and it is here rather than in the
     * sort because the sort does not know the viewport. */
    row[0] = x4[0] + ox;
    row[1] = x4[1] + ox;
    row[2] = x4[2] + ox;
    row[4] = y4[0] + oy;
    row[5] = y4[1] + oy;
    row[6] = y4[2] + oy;

    if( kind == TORIDRAW_RASTER_BATCH_GOURAUD ||
        kind == TORIDRAW_RASTER_BATCH_GOURAUD_ALPHA )
    {
        /* The colours are per-vertex, so they follow the same permutation the
         * sort applied to the coordinates. */
        unsigned char const* const o = g_toridraw_ysort_order[y4[3]];
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
 * How much of the frame actually reaches a presorted kernel.
 *
 * See the note on toridraw_raster_batch_stats_report. These are plain statics
 * rather than raster_debug fields because raster_debug DISABLES batching --
 * asking the debug path what the batched path does would answer about a walk
 * that never ran.
 */
static long g_toridraw_batch_staged;
static long g_toridraw_batch_fell_drawn;
static long g_toridraw_batch_fell_culled;

/* A face the per-face path will not draw at all costs nothing when the batcher
 * refuses it: there is no second sort because there is no second anything.
 * Separating these from the real fallthrough is the difference between a
 * number that means something and one that flatters or alarms. */
static int
toridraw_raster_batch_face_is_drawn(
    struct ToriDrawModelRasterContext* ctx,
    int face)
{
    int raw_type = ctx->face_infos ? ctx->face_infos[face] : 0;
    if( raw_type == 2 || raw_type < 0 || raw_type > 3 )
        return 0;
    if( ctx->colors_c[face] == TORIDRAWHSL16_HIDDEN )
        return 0;
    if( ctx->face_textures && ctx->face_textures[face] != -1 )
        return 1;             /* textured faces ignore authored alpha */
    if( ctx->face_alphas_nullable &&
        (0xFF - ctx->face_alphas_nullable[face]) <= 1 )
        return 0;
    return 1;
}

static void toridraw_raster_batch_stats_report(void);

/* Registered from atexit rather than called by the client, so nothing in the
 * shutdown path has to know this counter exists. Registration happens on the
 * first probe, which is before any face is classified. */
static int
toridraw_raster_batch_stats_enabled(void)
{
    static int on = -1;
    if( on < 0 )
    {
        on = getenv("TORIDRAW_BATCH_STATS") ? 1 : 0;
        if( on )
            atexit(toridraw_raster_batch_stats_report);
    }
    return on;
}

static void
toridraw_raster_batch_stats_report(void)
{
    long const drawn = g_toridraw_batch_staged + g_toridraw_batch_fell_drawn;
    fprintf(stderr,
            "batch_stats: presort_models=%ld staged=%ld fell_drawn=%ld "
            "fell_culled=%ld presorted=%.3f%%\n",
            g_toridraw_presort_models,
            g_toridraw_batch_staged,
            g_toridraw_batch_fell_drawn,
            g_toridraw_batch_fell_culled,
            drawn ? 100.0 * (double)g_toridraw_batch_staged / (double)drawn
                  : 0.0);
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
    int const stats = toridraw_raster_batch_stats_enabled();
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
#ifdef TORIDRAW_TEXTRI_PRESORTED_RUN
            if( TORIDRAW_RASTER_BATCH_IS_TEX(kind) )
                toridraw_raster_batch_append_textured(
                    ctx, face, kind, &info, &batch);
            else
#endif
                toridraw_raster_batch_append(
                    ctx, face, kind, info.alpha, &batch);
            if( batch.count == cap )
                toridraw_raster_batch_flush(ctx, &batch);
            if( stats )
                g_toridraw_batch_staged++;
            continue;
        }

        if( stats )
        {
            if( toridraw_raster_batch_face_is_drawn(ctx, face) )
                g_toridraw_batch_fell_drawn++;
            else
                g_toridraw_batch_fell_culled++;
        }

        toridraw_raster_batch_flush(ctx, &batch);
        ToriDraw_RasterModelFaceKernel(face, ctx);
    }

    toridraw_raster_batch_flush(ctx, &batch);
}

/*
 * The batched walk as a stage-3 entry, with the per-face walk behind it.
 *
 * A superset, not an alternative -- the same shape the prepared projection
 * kernels take. It runs the staged form only when the sort actually left a
 * y-ordered stash behind, and hands everything else to the default walk:
 *
 *   sm_face_xy_valid, and NOT toridraw_raster_batch_armed() again -- the sort
 *   is the only thing that knows whether it filled the buffer. It declines for
 *   a full-mode scene, where sm_face_x4/y4 are not even allocated, and for a
 *   caller whose table named no whole-model raster.
 *
 *   raster_debug, because every counter the batched form would have to
 *   maintain lives in the per-face path and belongs there rather than
 *   reimplemented twice.
 */
static void
toridraw_raster_walk_batched(
    void* user_data,
    struct ToriDraw_Scene* scene,
    struct ToriDrawModelRasterContext* ctx)
{
    if( (ctx->kernel.flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING) &&
        !TORIDRAW_DBG_RASTER_ARMED(ctx) && scene->sm_face_xy_valid &&
        !toridraw_raster_abl_nofaces() )
    {
        TORIDRAW_DBG_RASTER_ORDERED(ctx, scene->tmp_face_order_count);
        toridraw_raster_draw_faces_batched(scene, ctx);
        return;
    }
    ToriDraw_RasterWalkPerFace(user_data, scene, ctx);
}

#endif /* TORIDRAW_RASTER_BATCH */

#endif /* TORIDRAW_RASTER_BATCH_U_C */
