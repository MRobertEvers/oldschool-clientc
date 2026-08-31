#ifndef TORIDRAW_FACESORT_BUCKET_FULL_SCALAR_U_C
#define TORIDRAW_FACESORT_BUCKET_FULL_SCALAR_U_C

/*
 * THE FULL-SCENE BUCKET SORT.
 *
 * Relocated verbatim out of toridraw_render.u.c, which owned it only because
 * that is where it was written. Not one line of it changed in the move: this
 * file is a cut, and the commit that made it is reviewable by reading the two
 * line numbers rather than the eight hundred lines between them.
 *
 * Included at the point it used to occupy, so the unity build sees exactly the
 * declarations it saw before -- everything above it in toridraw_render.u.c is
 * still above it.
 */

/*
 * THE FOUR LARGE-MODEL SORT LOOPS, AND WHY THERE ARE FOUR.
 *
 * Two questions used to be asked per face inside one loop: does this model
 * reach behind the near plane, and does its depth range need coarser buckets.
 * Neither changes within a model, so both are answered once -- by the
 * dispatcher at the bottom -- and each answer picks a loop that does not
 * contain the question. Same split as the small-scene family further down this
 * file, and the same reason the flags are not threaded down and left to the
 * optimizer to unswitch: it held at -O2/-O3 and did not at -O1.
 *
 * `depth_shift` is the one that used to need an always_inline and a literal 0
 * argument to fold away. It does not any more: the two unshifted loops have no
 * shift to fold, so their inner loop is byte-for-byte what it was before the
 * large-model support existed, without depending on the inliner to make it so.
 * Only a model that actually needs coarser buckets reaches a `_shift` loop,
 * where the shift is a loop-invariant register rather than a constant -- see
 * the note at its computation for why that is a resolution budget and not a
 * hard range limit.
 *
 * What the four share is below them being separate: the accept step and the
 * return encoding are one function each, because those are the same work in
 * every variant. The flags change what a face COSTS, not what happens to one
 * that survives.
 */

/** The three-way near-plane sentinel test, for the two loops that can see one. */
static inline bool
bucket_face_clip_candidate(
    const int* RESTRICT vx,
    uint32_t a,
    uint32_t b,
    uint32_t c)
{
    return vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED || vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
           vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED;
}

/*
 * One surviving face into its depth bucket, and the model's depth span with
 * it -- the span is the accept's bookkeeping, so it moves where the accept
 * happens rather than being repeated at four call sites.
 *
 * Returns the bucket's new occupancy, or 0 when the bucket was already full.
 * The configured stride fixes bucket capacity per depth level. Nothing bounded
 * the count before, so a model with more than 512 front-facing triangles at
 * one quantized depth (a large flat wall seen edge-on, a terrain patch) wrote
 * into the next depth's bucket and silently corrupted the draw order. Drop the
 * overflow instead.
 */
static inline int
bucket_accept_face(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_stride,
    int depth_avg,
    int f,
    int* RESTRICT min_d,
    int* RESTRICT max_d)
{
    const int count = face_depth_bucket_counts[depth_avg];

    if( count >= depth_stride )
        return 0;

    /* Every depth bucket is a slice of one allocation, so an overrun corrupts
     * the next depth's faces rather than tripping a sanitizer. Assert the
     * slice, not the block. */
    assert(count >= 0 && count < depth_stride);
    assert(f >= 0 && f <= 0x7FFF && "face index must fit faceint_t");

    face_depth_bucket_counts[depth_avg] = count + 1;
    face_depth_buckets[depth_avg * depth_stride + count] = (faceint_t)f;

    if( depth_avg < *min_d )
        *min_d = depth_avg;
    if( depth_avg > *max_d )
        *max_d = depth_avg;

    return count + 1;
}

/** The packed span every variant returns; 0 for a model that accepted nothing. */
static inline int
bucket_sort_bounds(
    int min_d,
    int max_d)
{
    if( min_d > max_d )
        return 0;
    return (min_d) | (max_d << 16);
}

/* No near clip, no shift: the plain case, and the one most models take. */
static int
bucket_sort_by_average_depth_plain(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    int min_d = depth_levels;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        int depth_avg;

        if( !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        bucket_accept_face(
            face_depth_buckets,
            face_depth_bucket_counts,
            depth_stride,
            depth_avg,
            f,
            &min_d,
            &max_d);
    }

    return bucket_sort_bounds(min_d, max_d);
}

/*
 * Near clip, no shift.
 *
 * A clipped vertex has sentinel x and undivided y, so this triangle's
 * screen-space winding does not exist yet. The reference buckets it
 * unconditionally and performs the real winding test after building the
 * near-plane polygon.
 */
static int
bucket_sort_by_average_depth_clipped(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    int min_d = depth_levels;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        int depth_avg;

        if( !bucket_face_clip_candidate(vx, a, b, c) &&
            !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        bucket_accept_face(
            face_depth_buckets,
            face_depth_bucket_counts,
            depth_stride,
            depth_avg,
            f,
            &min_d,
            &max_d);
    }

    return bucket_sort_bounds(min_d, max_d);
}

/* Coarser buckets, no near clip: only a model whose depth range overflows the
 * table gets here, and the shift is what buys it the range. */
static int
bucket_sort_by_average_depth_shift(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
    int depth_shift,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    int min_d = depth_levels;
    int max_d = 0;

    assert(depth_shift > 0);

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        int depth_avg;

        if( !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = (div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth) >> depth_shift;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        bucket_accept_face(
            face_depth_buckets,
            face_depth_bucket_counts,
            depth_stride,
            depth_avg,
            f,
            &min_d,
            &max_d);
    }

    return bucket_sort_bounds(min_d, max_d);
}

/* Coarser buckets and a near clip: the rarest of the four. */
static int
bucket_sort_by_average_depth_shift_clipped(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
    int depth_shift,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    int min_d = depth_levels;
    int max_d = 0;

    assert(depth_shift > 0);

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        int depth_avg;

        if( !bucket_face_clip_candidate(vx, a, b, c) &&
            !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = (div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth) >> depth_shift;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        bucket_accept_face(
            face_depth_buckets,
            face_depth_bucket_counts,
            depth_stride,
            depth_avg,
            f,
            &min_d,
            &max_d);
    }

    return bucket_sort_bounds(min_d, max_d);
}

/* The only place the three questions are asked. Once per model, not per face. */
static inline int
bucket_sort_by_average_depth(
    faceint_t* RESTRICT face_depth_buckets,
    faceint_t* RESTRICT face_depth_bucket_counts,
    int depth_levels,
    int depth_stride,
    int depth_shift,
    TORIDRAW_DBG_SORT_PARAM bool near_clipped,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    TORIDRAW_DBG_SORT_LARGE_TAKEOVER(
        debug_stats,
        face_depth_buckets,
        face_depth_bucket_counts,
        depth_levels,
        depth_stride,
        depth_shift,
        near_clipped,
        model_min_depth,
        num_faces,
        vx,
        vy,
        vz,
        face_a,
        face_b,
        face_c);

    if( depth_shift )
    {
        if( near_clipped )
            return bucket_sort_by_average_depth_shift_clipped(
                face_depth_buckets,
                face_depth_bucket_counts,
                depth_levels,
                depth_stride,
                depth_shift,
                model_min_depth,
                num_faces,
                vx,
                vy,
                vz,
                face_a,
                face_b,
                face_c);
        return bucket_sort_by_average_depth_shift(
            face_depth_buckets,
            face_depth_bucket_counts,
            depth_levels,
            depth_stride,
            depth_shift,
            model_min_depth,
            num_faces,
            vx,
            vy,
            vz,
            face_a,
            face_b,
            face_c);
    }

    if( near_clipped )
        return bucket_sort_by_average_depth_clipped(
            face_depth_buckets,
            face_depth_bucket_counts,
            depth_levels,
            depth_stride,
            model_min_depth,
            num_faces,
            vx,
            vy,
            vz,
            face_a,
            face_b,
            face_c);
    return bucket_sort_by_average_depth_plain(
        face_depth_buckets,
        face_depth_bucket_counts,
        depth_levels,
        depth_stride,
        model_min_depth,
        num_faces,
        vx,
        vy,
        vz,
        face_a,
        face_b,
        face_c);
}

/**
 * One traversal of the depth buckets that does everything the old
 * parition_faces_by_priority() + the accumulation half of
 * sort_face_draw_order() did between them: fills the per-priority face
 * buckets, sums the per-priority depths, and lays out the two flexible-priority
 * arrays. Both old loops visited the same faces in the same order and unpacked
 * the same priority nibble, so folding them is order-preserving; the running
 * index they each used (face_priority_bucket_counts[prio] and counts[prio])
 * was always the same number.
 */
static inline void
partition_and_accumulate_faces_by_priority(
    faceint_t* face_priority_buckets,
    faceint_t* face_priority_bucket_counts,
    int* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* counts,
    int depth_levels,
    int depth_stride,
    int priority_stride,
    int flex_capacity,
    int model_face_count,
    faceint_t* face_depth_buckets,
    faceint_t* face_depth_bucket_counts,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    if( depth_upper_bound >= depth_levels )
        depth_upper_bound = depth_levels - 1;

    for( int depth = depth_upper_bound; depth >= depth_lower_bound; depth-- )
    {
        int face_count = (int)face_depth_bucket_counts[depth];
        if( face_count == 0 )
            continue;

        /* A count above the stride means the bucket writer already spilled
         * into the next depth level's slice of the same allocation. */
        assert(face_count > 0 && face_count <= depth_stride);

        faceint_t* faces = &face_depth_buckets[depth * depth_stride];
        for( int i = 0; i < face_count; i++ )
        {
            faceint_t face_idx = faces[i];
            int prio = faceprio_unpack(face_priorities, face_idx);
            int n;

            assert(face_idx >= 0 && face_idx < model_face_count);
            assert(prio >= 0 && prio < 12 && "face priority indexes counts[12]");

            n = counts[prio];
            /* Same shape of hazard as the depth buckets: the twelve priority
             * runs are slices of one block, so overflowing one silently
             * rewrites the next priority's faces. */
            assert(n >= 0 && n < priority_stride);

            face_priority_buckets[prio * priority_stride + n] = face_idx;

            if( prio < 10 )
            {
                priority_depths[prio] += depth;
            }
            else
            {
                /* depth occupies the low 16 bits and the face index the high
                 * 16, so both have to be representable or the pair decodes as
                 * a different face at a different depth. */
                assert(depth >= 0 && depth <= 0xFFFF);
                assert(n < flex_capacity);

                if( prio == 10 )
                    flex_prio11_face_to_depth[n] = depth | (face_idx << 16);
                else
                    flex_prio12_face_to_depth[n] = depth | (face_idx << 16);
            }

            counts[prio] = n + 1;
            face_priority_bucket_counts[prio] = (faceint_t)(n + 1);
        }
    }
}

/**
 * Emission half of the old sort_face_draw_order(): interleaves the flexible
 * priorities with the fixed priority runs. Takes the counts and depth sums that
 * partition_and_accumulate_faces_by_priority() already produced.
 */
static inline int
sort_face_draw_order(
    int* priority_depths,
    int* flex_prio11_face_to_depth,
    int* flex_prio12_face_to_depth,
    int* face_draw_order,
    faceint_t* face_priority_buckets,
    int* counts,
    int priority_stride,
    int flex_capacity,
    int max_faces)
{
    int average_depth1_2 = 0;
    int count1_2 = counts[1] + counts[2];
    if( count1_2 > 0 )
        average_depth1_2 = (priority_depths[1] + priority_depths[2]) / count1_2;
    int average_depth3_4 = 0;
    int count3_4 = counts[3] + counts[4];
    if( count3_4 > 0 )
        average_depth3_4 = (priority_depths[3] + priority_depths[4]) / count3_4;
    int average_depth6_8 = 0;
    int count6_8 = counts[6] + counts[8];
    if( count6_8 > 0 )
        average_depth6_8 = (priority_depths[6] + priority_depths[8]) / count6_8;

    /* Priority 11 is appended onto priority 10 and the pair is then walked as
     * one run, so the merged length has to fit the array that receives it. */
    assert(counts[10] >= 0 && counts[11] >= 0);
    assert(counts[10] + counts[11] <= flex_capacity);

    for( int i = 0; i < counts[11]; i++ )
    {
        flex_prio11_face_to_depth[counts[10] + i] = flex_prio12_face_to_depth[i];
    }
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

    TORIDRAW_DBG_SORT_COUNTS(
        counts, flex_prio11_face_to_depth, average_depth1_2, average_depth3_4, average_depth6_8);

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth1_2 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 0; prio < 3; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = face_priority_buckets[prio * priority_stride + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth3_4 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 3; prio < 5; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = face_priority_buckets[prio * priority_stride + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth6_8 )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 5; prio < 10; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = face_priority_buckets[prio * priority_stride + i];
        }
    }

    while( flexible_face_index < counts[10] )
    {
        face_draw_order[order_index++] = flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    /* The order array is sized for the model's faces. Exceeding it means some
     * face was emitted more than once, which is also how a face goes missing. */
    assert(order_index <= max_faces);

    return order_index;
}

static inline void
ToriDraw_ComputeProjectedFaceOrder(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort)
{
    /* Full mode has no sm_face_x4/y4 to fill: the buffer is allocated only
     * for a small-mode scene (toridraw.c), and only the small sorter
     * stamps it. Saying so here is what stops the batched walk reading a
     * NULL pointer, or a stash left behind by an earlier small model. */
    (void)presort;
    scene->sm_face_xy_valid = 0;
    TORIDRAW_DBG_SORT_LOCALS
    faceint_t* fia = NULL;
    faceint_t* fib = NULL;
    faceint_t* fic = NULL;
    uint8_t* face_priorities = NULL;
    int face_count = 0;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        fia = m->face_indices_a;
        fib = m->face_indices_b;
        fic = m->face_indices_c;
        /* A model that resolves itself per pixel has no use for face render
         * priorities, and honouring them actively defeats the depth test: a
         * priority pins a face into a draw band regardless of depth, which is
         * the painter's-algorithm crutch the z-buffer exists to replace. The
         * two together give the priority's answer, not the depth test's -- so
         * opting a model in drops them. See TORIDRAW_MODEL_FLAG_ZBUFFER.
         *
         * TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY drops them on its own, for an
         * imported model whose priorities its authoring client never read. */
        face_priorities = m->face_priorities;
        if( toridraw_ignore_priorities() )
            face_priorities = NULL;
        if( m->flags & (TORIDRAW_MODEL_FLAG_ZBUFFER | TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY) )
            face_priorities = NULL;
        face_count = m->face_count;
        break;
    }
    default:
        assert(0);
        break;
    }

    const struct ToriDraw_BoundsCylinder* bc = model_bounds_cylinder(hnd);
    int model_min_depth = bc ? bc->min_z_depth_any_rotation : 0;
    /* model_min_depth is reused below to carry the observed depth range, so
     * keep the bias the bucket sort actually applied. */
    int const bias = model_min_depth;

    TORIDRAW_DBG_SORT_ARM();

    /* No clear here. The bucket-count table arrives all-zero -- calloc'd at
     * scene creation, and each sort below re-zeroes exactly the buckets it
     * dirtied once its consumer has walked them, so the invariant holds from
     * model to model. The reference engine bounds this clear by the model's
     * depth diameter for the same reason: a full-width clear is
     * depth_levels-sized, and on the 16K tier that is 32KB zeroed per model
     * to bucket a median of ~19 faces -- ~30MB of memset a frame, 6.9% of
     * steady-state CPU on the XP lane, all of it evicting 2x a P4's L1D. */

    /*
     * How much depth precision this model has to give up to be sortable.
     *
     * The bucket sort quantises a face's average depth into a fixed table, and
     * a model spans [0, 2*bias] of it -- so a model whose bias exceeds half the
     * table cannot be represented AT ALL: every face falls outside the buckets,
     * the sort emits nothing, and the model vanishes while still picking. That
     * is a hard cliff, and it is reached by exactly the things least able to
     * afford it: physically large imports, and any model an animation has
     * stretched (the QBD hit a bias of 54,402 against a 16,384-level table).
     *
     * Shifting the quantisation right by just enough makes the table a budget
     * on PRECISION instead of a limit on SIZE. A large model sorts into coarser
     * depth bands -- the only cost is that two faces closer together than one
     * band may tie, which is a sort-order nicety, not a visibility cliff -- and
     * everything that already fit keeps a shift of zero and buckets exactly as
     * it did before, bit for bit.
     *
     * Derived rather than tuned: the smallest shift for which 2*bias+1 fits.
     */
    int depth_shift = 0;
    int64_t span = (int64_t)model_min_depth * 2 + 1;
    while( span > (int64_t)scene->depth_levels && depth_shift < 30 )
    {
        span >>= 1;
        depth_shift++;
    }

    int bounds = bucket_sort_by_average_depth(
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        scene->depth_levels,
        scene->depth_stride,
        depth_shift,
        TORIDRAW_DBG_SORT_ARG scene->near_clipped,
        model_min_depth,
        face_count,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        fia,
        fib,
        fic);

    model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( !face_priorities )
    {
        int order_index = 0;
        for( int depth = model_max_depth; depth < scene->depth_levels && depth >= model_min_depth;
             depth-- )
        {
            int bucket_count = (int)scene->tmp_depth_face_count[depth];
            if( bucket_count == 0 )
                continue;

            faceint_t* faces = &scene->tmp_depth_faces[depth * scene->depth_stride];
            for( int j = 0; j < bucket_count; j++ )
            {
                scene->tmp_face_order[order_index++] = faces[j];
            }
        }
        scene->tmp_face_order_count = order_index;

        /* Restore the all-zero invariant: re-zero exactly the buckets this
         * model dirtied. The sort's returned bounds are the ACTUAL touched
         * range -- every accepted bucket write updated them -- not the
         * bias-derived estimate, which animation can stretch past. An empty
         * sort returns 0 (min=max=0): a 2-byte clear of an already-zero
         * bucket. */
        assert(model_min_depth >= 0);
        assert(model_max_depth < scene->depth_levels);
        assert(model_min_depth <= model_max_depth);
        memset(
            &scene->tmp_depth_face_count[model_min_depth],
            0,
            (size_t)(model_max_depth - model_min_depth + 1) *
                sizeof(scene->tmp_depth_face_count[0]));

        TORIDRAW_DBG_SORT_PRINT(debug_stats, scene, hnd, order_index);
        TORIDRAW_DBG_CHECK_FACE_ORDER(scene, hnd, face_priorities, face_count, bias);
        return;
    }

    memset(scene->tmp_priority_depth_sum, 0, 12 * sizeof(int));
    memset(scene->tmp_priority_face_count, 0, 12 * sizeof(faceint_t));

    int counts[12] = { 0 };

    partition_and_accumulate_faces_by_priority(
        scene->tmp_priority_faces,
        scene->tmp_priority_face_count,
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        counts,
        scene->depth_levels,
        scene->depth_stride,
        scene->priority_stride,
        scene->flex_prio_capacity,
        face_count,
        scene->tmp_depth_faces,
        scene->tmp_depth_face_count,
        face_priorities,
        model_min_depth,
        model_max_depth);

    /* Same invariant restore as the no-priority path: the partition above only
     * reads the bucket counts over [model_min_depth, model_max_depth], so once
     * it returns the dirtied range can be re-zeroed. */
    assert(model_min_depth >= 0);
    assert(model_max_depth < scene->depth_levels);
    assert(model_min_depth <= model_max_depth);
    memset(
        &scene->tmp_depth_face_count[model_min_depth],
        0,
        (size_t)(model_max_depth - model_min_depth + 1) * sizeof(scene->tmp_depth_face_count[0]));

    scene->tmp_face_order_count = sort_face_draw_order(
        scene->tmp_priority_depth_sum,
        scene->tmp_flex_prio11_face_to_depth,
        scene->tmp_flex_prio12_face_to_depth,
        scene->tmp_face_order,
        scene->tmp_priority_faces,
        counts,
        scene->priority_stride,
        scene->flex_prio_capacity,
        scene->max_faces);
    TORIDRAW_DBG_SORT_PRINT(debug_stats, scene, hnd, scene->tmp_face_order_count);
    TORIDRAW_DBG_CHECK_FACE_ORDER(scene, hnd, face_priorities, face_count, bias);
}

#endif /* TORIDRAW_FACESORT_BUCKET_FULL_SCALAR_U_C */
