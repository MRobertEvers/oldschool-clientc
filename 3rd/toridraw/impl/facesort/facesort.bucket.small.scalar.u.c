#ifndef TORIDRAW_FACESORT_BUCKET_SMALL_SCALAR_U_C
#define TORIDRAW_FACESORT_BUCKET_SMALL_SCALAR_U_C

/*
 * THE SMALL-SCENE BUCKET SORT: the CSR variant, sized off max_faces.
 *
 * The same algorithm as its full-scene twin over different scratch -- a
 * compressed sparse row layout instead of the dense depth_levels x
 * depth_stride table -- and the scene TIER is what picks between them.
 *
 * Relocated verbatim; see the note in the full-scene file.
 */

/*
 * Restore the all-zero invariant over exactly the buckets one model dirtied:
 * [min_depth, max_depth] from the counting pass, plus the end sentinel the
 * prefix sum wrote at max_depth + 1. Call this once every consumer of
 * sm_depth_offset has walked it, on every exit that reached the prefix sum.
 */
static inline void
sm_depth_offset_restore(
    struct ToriDraw_Scene* scene,
    int min_depth,
    int max_depth)
{
    assert(scene);
    assert(scene->sm_depth_offset);
    assert(min_depth >= 0);
    assert(max_depth < scene->depth_levels);
    assert(min_depth <= max_depth);

    memset(
        &scene->sm_depth_offset[min_depth], 0, (size_t)(max_depth - min_depth + 2) * sizeof(int));
}

/*
 * Hand the raster pass what the sort loop already has.
 *
 * Every one of these six values was just loaded to compute the winding, and
 * every one of them used to be read a second time further down the frame --
 * through face_indices_a/b/c into screen_vertices_x/y, which is three loads to
 * get an index and six dependent loads to use it, per face, to recover what
 * was sitting in registers in the caller.
 *
 * ORDERED BY Y, here, once, for every kernel downstream. Every raster kernel
 * used to open with a six-way compare ladder to put the three vertices in y
 * order, and then a permuting copy to act on the answer. Both are deleted by
 * doing it here: these three y values are already in registers -- the winding
 * needed them -- and the ladder is a mispredict per triangle on a part that
 * pays twenty pipeline stages for one.
 *
 * The `<=` tie-breaks are transcribed exactly from the C wrappers the kernels
 * came from. Two triangles that tie differently stop tiling with each other,
 * so this is part of the contract and not a comparison order to tidy up --
 * which is why the ladder lives in one function and not once per sort variant.
 */
static inline void
sm_stash_face_xy_sorted(
    struct ToriDraw_Scene* scene,
    int f,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    uint32_t a,
    uint32_t b,
    uint32_t c)
{
    int* const x4 = &scene->sm_face_x4[(size_t)f * 4];
    int* const y4 = &scene->sm_face_y4[(size_t)f * 4];
    int const ya = vy[a];
    int const yb = vy[b];
    int const yc = vy[c];
    int const perm = (ya <= yb && ya <= yc) ? ((yb <= yc) ? 0 : 1)
                     : (yb <= yc)           ? ((yc <= ya) ? 2 : 3)
                                            : ((ya <= yb) ? 4 : 5);
    unsigned char const* const o = g_toridraw_ysort_order[perm];
    int const px[3] = { vx[a], vx[b], vx[c] };
    int const py[3] = { ya, yb, yc };

    x4[0] = px[o[0]];
    x4[1] = px[o[1]];
    x4[2] = px[o[2]];
    /* Not a clip candidate; the flag is the other stash below. */
    x4[3] = 0;
    y4[0] = py[o[0]];
    y4[1] = py[o[1]];
    y4[2] = py[o[2]];
    /* The permutation itself, for the consumers that carry per-vertex data of
     * their own -- gouraud's three colours, and the texture frame's three
     * vertex indices. */
    y4[3] = perm;
}

/* A clip candidate never had its coordinates read -- the winding is skipped
 * for it -- so only the flag is written, and the flag is what stops anything
 * reading the rest. */
static inline void
sm_stash_face_clipped(
    struct ToriDraw_Scene* scene,
    int f)
{
    scene->sm_face_x4[(size_t)f * 4 + 3] = 1;
}

/*
 * The half of the CSR sort that has nothing to do with which loop filled the
 * counts: prefix sum, end sentinel, cursor seed, scatter. Identical for every
 * variant below, which is why it is here and not spelled out five times --
 * the flags change what a FACE costs, not what the table does afterwards.
 *
 * Returns the packed [min_d, max_d] the caller passes on, or 0 for a model
 * that accepted nothing.
 */
static inline int
sm_bucket_sort_finish(
    struct ToriDraw_Scene* scene,
    int num_faces,
    int min_d,
    int max_d)
{
    int total = 0;

    if( min_d > max_d )
        return 0;

    TORIDRAW_DBG_SPAN_RATIO(num_faces, min_d, max_d);

    /* Prefix sum over the model's span only. Buckets outside it are zero and
     * stay zero; no consumer reads them, because every consumer is bounded by
     * the same [min_d, max_d] this returns. */
    for( int d = min_d; d <= max_d; d++ )
    {
        int count = scene->sm_depth_offset[d];
        scene->sm_depth_offset[d] = total;
        total += count;
    }

    /* End sentinel. Consumers read sm_depth_offset[depth + 1] for depth up to
     * max_d, so max_d + 1 must hold the end of the last bucket -- which is why
     * the array is calloc'd depth_levels + 1 long. */
    assert(max_d + 1 <= scene->depth_levels);
    scene->sm_depth_offset[max_d + 1] = total;

    /* The scatter below bumps sm_depth_cursor only over [min_d, max_d], so
     * that is all that needs seeding. */
    memcpy(
        &scene->sm_depth_cursor[min_d],
        &scene->sm_depth_offset[min_d],
        (size_t)(max_d - min_d + 1) * sizeof(int));

    for( int f = 0; f < num_faces; f++ )
    {
        int depth_avg = scene->sm_face_depth[f];
        if( depth_avg < 0 )
            continue;

        int write = scene->sm_depth_cursor[depth_avg]++;
        scene->sm_faces_by_depth[write] = (faceint_t)f;
    }

    return (min_d) | (max_d << 16);
}

/*
 * THE FOUR SORT LOOPS, AND WHY THERE ARE FOUR OF THEM.
 *
 * Two questions used to be asked per face inside one loop: does this model
 * reach behind the near plane, and is anything going to read the y-ordered
 * stash. Neither changes within a model, and both are cheap to answer once, so
 * they are answered once -- by the dispatcher at the bottom -- and each answer
 * picks a loop that does not contain the question. Same reasoning as the
 * near-clip projection family further down this file, and the same reason the
 * flags are not threaded down and left to the optimizer to unswitch: it held
 * at -O2/-O3 and did not at -O1.
 *
 * The stash matters most. It has exactly one consumer -- the batched software
 * raster walk -- and a caller that will not run it (every D3D9 renderer sorts
 * back-to-front on the CPU and then hands the faces to the GPU) would
 * otherwise pay seven stores and a six-way compare per drawn face to fill a
 * buffer nobody loads. Worse, the bucket sort is the A/B baseline the flat
 * sort is measured against, so leaving that cost in would read as a cost of
 * the OLD pipeline and be credited to the new one.
 *
 * The counting pass in each loop touches only buckets in this model's depth
 * span, so sm_depth_offset arrives all-zero -- calloc'd at scene creation, and
 * every exit that dirties it re-zeroes that span once its consumers are done.
 * A full-width clear is 64KB per model at DEPTH_16K, to bucket a median of ~19
 * faces, and it evicts 8x a P4's L1D on the way past. The dispatcher asserts
 * the invariant; none of these four re-establish it.
 */

/* No near clip, no stash: the plain case, and the one most models take. */
static int
bucket_sort_by_average_depth_small_plain(
    struct ToriDraw_Scene* scene,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    const int depth_levels = scene->depth_levels;
    int min_d = depth_levels;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        int depth_avg;

        scene->sm_face_depth[f] = -1;

        if( !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        scene->sm_face_depth[f] = (faceint_t)depth_avg;
        scene->sm_depth_offset[depth_avg]++;

        if( depth_avg < min_d )
            min_d = depth_avg;
        if( depth_avg > max_d )
            max_d = depth_avg;
    }

    return sm_bucket_sort_finish(scene, num_faces, min_d, max_d);
}

/*
 * Near clip, no stash.
 *
 * A clipped vertex has sentinel x and undivided y, so this triangle's
 * screen-space winding does not exist yet. The reference buckets it
 * unconditionally and performs the real winding test after building the
 * near-plane polygon.
 */
static int
bucket_sort_by_average_depth_small_clipped(
    struct ToriDraw_Scene* scene,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    const int depth_levels = scene->depth_levels;
    int min_d = depth_levels;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        bool clip_candidate;
        int depth_avg;

        scene->sm_face_depth[f] = -1;

        clip_candidate = vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                         vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                         vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED;
        if( !clip_candidate &&
            !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        scene->sm_face_depth[f] = (faceint_t)depth_avg;
        scene->sm_depth_offset[depth_avg]++;

        if( depth_avg < min_d )
            min_d = depth_avg;
        if( depth_avg > max_d )
            max_d = depth_avg;
    }

    return sm_bucket_sort_finish(scene, num_faces, min_d, max_d);
}

/* No near clip, stashing: every accepted face is a real triangle, so every
 * accepted face gets the y-ordered stash and nothing writes the clip flag. */
static int
bucket_sort_by_average_depth_small_stash(
    struct ToriDraw_Scene* scene,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    const int depth_levels = scene->depth_levels;
    int min_d = depth_levels;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        int depth_avg;

        scene->sm_face_depth[f] = -1;

        if( !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        scene->sm_face_depth[f] = (faceint_t)depth_avg;
        scene->sm_depth_offset[depth_avg]++;
        sm_stash_face_xy_sorted(scene, f, vx, vy, a, b, c);

        if( depth_avg < min_d )
            min_d = depth_avg;
        if( depth_avg > max_d )
            max_d = depth_avg;
    }

    return sm_bucket_sort_finish(scene, num_faces, min_d, max_d);
}

/* Near clip and stashing: the only loop that has to choose between the two
 * stashes, because it is the only one that can see a face with no winding. */
static int
bucket_sort_by_average_depth_small_stash_clipped(
    struct ToriDraw_Scene* scene,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    const int depth_levels = scene->depth_levels;
    int min_d = depth_levels;
    int max_d = 0;

    for( int f = 0; f < num_faces; f++ )
    {
        const uint32_t a = face_a[f];
        const uint32_t b = face_b[f];
        const uint32_t c = face_c[f];
        bool clip_candidate;
        int depth_avg;

        scene->sm_face_depth[f] = -1;

        clip_candidate = vx[a] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                         vx[b] == TORIDRAW_SCREEN_X_NEAR_CLIPPED ||
                         vx[c] == TORIDRAW_SCREEN_X_NEAR_CLIPPED;
        if( !clip_candidate &&
            !toridraw_winding_2d_front_facing(vx[a], vy[a], vx[b], vy[b], vx[c], vy[c]) )
            continue;

        depth_avg = div3_fast_fixedpoint(vz[a] + vz[b] + vz[c]) + model_min_depth;
        if( (unsigned int)depth_avg >= (unsigned int)depth_levels )
            continue;

        scene->sm_face_depth[f] = (faceint_t)depth_avg;
        scene->sm_depth_offset[depth_avg]++;
        if( clip_candidate )
            sm_stash_face_clipped(scene, f);
        else
            sm_stash_face_xy_sorted(scene, f, vx, vy, a, b, c);

        if( depth_avg < min_d )
            min_d = depth_avg;
        if( depth_avg > max_d )
            max_d = depth_avg;
    }

    return sm_bucket_sort_finish(scene, num_faces, min_d, max_d);
}

/*
 * The only place the three questions are asked. Once per model, not per face.
 *
 * `presort` is the caller's half of the stash decision and it is the half that
 * matters. Whether the batcher is armed is not asked here at all any more: it
 * chose the raster kernel, and `presort` is derived from that kernel's door --
 * so it happens once for the model too.
 */
static inline int
bucket_sort_by_average_depth_small(
    struct ToriDraw_Scene* scene,
    TORIDRAW_DBG_SORT_PARAM bool presort,
    bool near_clipped,
    int model_min_depth,
    int num_faces,
    const int* RESTRICT vx,
    const int* RESTRICT vy,
    const int* RESTRICT vz,
    const faceint_t* RESTRICT face_a,
    const faceint_t* RESTRICT face_b,
    const faceint_t* RESTRICT face_c)
{
    bool const stash_xy = presort;

    /* Recorded, not re-derived downstream: the walk that reads sm_face_x4/y4
     * asks this rather than asking the same three questions again and possibly
     * answering one of them differently. Written here, so no loop below has to
     * remember to. */
    scene->sm_face_xy_valid = stash_xy;
    if( stash_xy )
        TORIDRAW_BATCH_COUNT(g_toridraw_presort_models);

    /* Verifying the all-zero invariant is O(depth_levels) -- the very cost the
     * windowing exists to avoid -- so it is an assert and nothing else. */
    assert(sm_depth_offset_all_zero(scene));

    TORIDRAW_DBG_SORT_SMALL_TAKEOVER(
        debug_stats,
        scene,
        stash_xy,
        near_clipped,
        model_min_depth,
        num_faces,
        vx,
        vy,
        vz,
        face_a,
        face_b,
        face_c);

    if( stash_xy )
    {
        if( near_clipped )
            return bucket_sort_by_average_depth_small_stash_clipped(
                scene, model_min_depth, num_faces, vx, vy, vz, face_a, face_b, face_c);
        return bucket_sort_by_average_depth_small_stash(
            scene, model_min_depth, num_faces, vx, vy, vz, face_a, face_b, face_c);
    }

    if( near_clipped )
        return bucket_sort_by_average_depth_small_clipped(
            scene, model_min_depth, num_faces, vx, vy, vz, face_a, face_b, face_c);
    return bucket_sort_by_average_depth_small_plain(
        scene, model_min_depth, num_faces, vx, vy, vz, face_a, face_b, face_c);
}

#include "toridraw_face_sort_flat.u.c"

/**
 * Small-scene twin of partition_and_accumulate_faces_by_priority(): the same
 * fold of the old parition_faces_by_priority_small() and the accumulation half
 * of sort_face_draw_order_small() into one traversal.
 */
static inline void
partition_and_accumulate_faces_by_priority_small(
    struct ToriDraw_Scene* scene,
    int* priority_depths,
    int* counts,
    const uint8_t* face_priorities,
    int depth_lower_bound,
    int depth_upper_bound)
{
    const int depth_levels = scene->depth_levels;
    const int max_faces = scene->max_faces;

    if( depth_upper_bound >= depth_levels )
        depth_upper_bound = depth_levels - 1;

    memset(scene->sm_prio_count, 0, sizeof(scene->sm_prio_count));

    for( int depth = depth_upper_bound; depth >= depth_lower_bound; depth-- )
    {
        int start = scene->sm_depth_offset[depth];
        int end = scene->sm_depth_offset[depth + 1];
        for( int i = start; i < end; i++ )
        {
            faceint_t face_idx = scene->sm_faces_by_depth[i];
            int prio = faceprio_unpack(face_priorities, face_idx);
            int n;

            assert(face_idx >= 0 && face_idx < max_faces);
            assert(prio >= 0 && prio < 12 && "face priority indexes counts[12]");

            n = counts[prio];
            /* One allocation, thirteen slices: an overrun here rewrites the
             * next priority's faces where no sanitizer can see it. */
            assert(n >= 0 && n < max_faces);

            scene->sm_prio_faces[prio * max_faces + n] = face_idx;

            if( prio < 10 )
            {
                priority_depths[prio] += depth;
            }
            else
            {
                assert(depth >= 0 && depth <= 0xFFFF);
                assert(n < scene->flex_prio_capacity);

                if( prio == 10 )
                    scene->sm_flex_prio11_face_to_depth[n] = depth | (face_idx << 16);
                else
                    scene->sm_flex_prio12_face_to_depth[n] = depth | (face_idx << 16);
            }

            counts[prio] = n + 1;
            scene->sm_prio_count[prio] = n + 1;
        }
    }
}

static inline int
sort_face_draw_order_small(
    struct ToriDraw_Scene* scene,
    int* face_draw_order,
    int* priority_depths,
    int* counts)
{
    const int max_faces = scene->max_faces;

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

    assert(counts[10] >= 0 && counts[11] >= 0);
    assert(counts[10] + counts[11] <= scene->flex_prio_capacity);

    for( int i = 0; i < counts[11]; i++ )
    {
        scene->sm_flex_prio11_face_to_depth[counts[10] + i] =
            scene->sm_flex_prio12_face_to_depth[i];
    }
    counts[10] += counts[11];

    int flexible_face_index = 0;
    int order_index = 0;

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth1_2 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 0; prio < 3; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth3_4 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 3; prio < 5; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] &&
           (scene->sm_flex_prio11_face_to_depth[flexible_face_index] & 0xFFFF) > average_depth6_8 )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    for( int prio = 5; prio < 10; prio++ )
    {
        for( int i = 0; i < counts[prio]; i++ )
        {
            face_draw_order[order_index++] = scene->sm_prio_faces[prio * max_faces + i];
        }
    }

    while( flexible_face_index < counts[10] )
    {
        face_draw_order[order_index++] =
            scene->sm_flex_prio11_face_to_depth[flexible_face_index] >> 16;
        flexible_face_index++;
    }

    assert(order_index <= max_faces);

    return order_index;
}

#endif /* TORIDRAW_FACESORT_BUCKET_SMALL_SCALAR_U_C */
