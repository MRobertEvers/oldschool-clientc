#ifndef TORIDRAW_FACESORT_DISPATCH_U_C
#define TORIDRAW_FACESORT_DISPATCH_U_C

/*
 * WHAT BOTH SORT LANES SHARE: the model inputs, the two emitters, the entries.
 *
 * sort_model_inputs gathers what either lane needs to know about the model
 * once; face_order_small_bucket and face_order_small_flat turn each lane's
 * result into scene->tmp_face_order; and the two public entries sit on top.
 *
 * This is stage 2's dispatch, so it is the one of the three files that a new
 * sort lane has to touch -- the lanes themselves are additive.
 *
 * Relocated verbatim; see the note in the full-scene file.
 */

/*
 * What both sort lanes need to know about the model, gathered once.
 *
 * The two lanes below run completely different algorithms over the same six
 * facts, and reading them off the handle is neither of their business -- it is
 * a switch on the handle kind and three policy questions about priorities, and
 * it was the top third of the one function they used to share.
 */
struct sort_model_inputs
{
    faceint_t* fia;
    faceint_t* fib;
    faceint_t* fic;
    /* NULL when this model's faces are not banded by render priority. */
    uint8_t* face_priorities;
    int face_count;
    /* -1 = not a two-triangle terrain tile; see ToriDraw_Model.tile_sort_kernel. */
    int tile2_rot;
    int model_min_depth;
};

static inline void
sort_model_inputs(
    struct ToriDraw_ModelHandle hnd,
    struct sort_model_inputs* in)
{
    const struct ToriDraw_BoundsCylinder* bc;

    assert(in);

    in->fia = NULL;
    in->fib = NULL;
    in->fic = NULL;
    in->face_priorities = NULL;
    in->face_count = 0;
    in->tile2_rot = -1;

    switch( hnd.kind )
    {
    case TORIDRAWMK_MODEL:
    case TORIDRAWMK_MODEL_HD:
    case TORIDRAWMK_MODEL_SHARED:
    case TORIDRAWMK_MODEL_LENT_FACES:
    {
        struct ToriDraw_Model* m = model_as_full(hnd);
        in->fia = m->face_indices_a;
        in->fib = m->face_indices_b;
        in->fic = m->face_indices_c;
        /* A model that resolves itself per pixel has no use for face render
         * priorities, and honouring them actively defeats the depth test: a
         * priority pins a face into a draw band regardless of depth, which is
         * the painter's-algorithm crutch the z-buffer exists to replace. The
         * two together give the priority's answer, not the depth test's -- so
         * opting a model in drops them. See TORIDRAW_MODEL_FLAG_ZBUFFER.
         *
         * TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY drops them on its own, for an
         * imported model whose priorities its authoring client never read. */
        in->face_priorities = m->face_priorities;
        if( toridraw_ignore_priorities() )
            in->face_priorities = NULL;
        if( m->flags & (TORIDRAW_MODEL_FLAG_ZBUFFER | TORIDRAW_MODEL_FLAG_NO_FACE_PRIORITY) )
            in->face_priorities = NULL;
        in->face_count = m->face_count;
        /* TORIDRAWMK_MODEL only. The tile kernel reads vx[0..3] on the promise
         * that this model's four projected vertices are its own and that its two
         * faces are the tile triples -- a promise world_decode_tile makes about a
         * model it owns outright, and not one the lent-faces or the shared regimes
         * are in a position to keep. */
        if( hnd.kind == TORIDRAWMK_MODEL && m->tile_sort_kernel &&
            toridraw_face_sort_tile2_armed() )
        {
            assert(m->vertex_count == 4);
            assert(m->face_count == 2);
            in->tile2_rot = m->tile_sort_kernel - 1;
        }
        break;
    }
    default:
        assert(0);
        break;
    }

    bc = model_bounds_cylinder(hnd);
    in->model_min_depth = bc ? bc->min_z_depth_any_rotation : 0;
}

/*
 * THE FLAT LANE: SIMD cull into composite keys, then bitonic or radix.
 *
 * The keys come back already in draw order, so the emit is a truncation -- the
 * low sixteen bits of each key are the face index -- or, for a model that
 * bands its faces, the same priority fold the bucket lane ends with, reading
 * (face, depth) pairs off the key array instead of off the depth table.
 */
static inline void
face_order_small_flat(
    struct ToriDraw_Scene* scene,
    const struct sort_model_inputs* in,
    bool presort)
{
    int const n = toridraw_face_sort_flat(
        scene,
        presort,
        scene->near_clipped,
        in->model_min_depth,
        in->face_count,
        in->tile2_rot,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        in->fia,
        in->fib,
        in->fic);
    const uint32_t* keys = scene->sm_sort_keys;

    if( !in->face_priorities )
    {
        for( int i = 0; i < n; i++ )
            scene->tmp_face_order[i] = (int)(keys[i] & 0xFFFF);
        scene->tmp_face_order_count = n;
        return;
    }

    {
        int priority_depths[12] = { 0 };
        int counts[12] = { 0 };

        partition_and_accumulate_faces_by_priority_keys(
            scene, keys, n, priority_depths, counts, in->face_priorities);
        scene->tmp_face_order_count =
            sort_face_draw_order_small(scene, scene->tmp_face_order, priority_depths, counts);
    }
}

/*
 * THE BUCKET LANE: the CSR depth sort, and the reference the flat lane is held
 * to order-for-order by toridraw_face_sort_flat_test.
 *
 * It is also the only lane the debug counters exist in, which is why the
 * dispatcher hands it `debug_stats` and sends every instrumented run here.
 */
static inline void
face_order_small_bucket(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    const struct sort_model_inputs* in,
    TORIDRAW_DBG_SORT_PARAM bool presort)
{
    int bounds = bucket_sort_by_average_depth_small(
        scene,
        TORIDRAW_DBG_SORT_ARG presort,
        scene->near_clipped,
        in->model_min_depth,
        in->face_count,
        scene->screen_vertices_x,
        scene->screen_vertices_y,
        scene->screen_vertices_z,
        in->fia,
        in->fib,
        in->fic);

    int model_min_depth = bounds & 0xFFFF;
    int model_max_depth = bounds >> 16;

    if( bounds == 0 )
    {
        /* bounds == 0 is ambiguous: it means "nothing accepted", and it is
         * also what a model wholly inside bucket 0 encodes. In that second
         * case the prefix sum ran and dirtied [0, 1], so restore regardless --
         * zeroing two already-zero ints in the first case is free. */
        sm_depth_offset_restore(scene, model_min_depth, model_max_depth);
        scene->tmp_face_order_count = 0;
        TORIDRAW_DBG_SORT_PRINT(debug_stats, scene, hnd, 0);
        return;
    }

    if( !in->face_priorities )
    {
        int order_index = 0;
        for( int depth = model_max_depth; depth < scene->depth_levels && depth >= model_min_depth;
             depth-- )
        {
            int start = scene->sm_depth_offset[depth];
            int end = scene->sm_depth_offset[depth + 1];
            for( int j = start; j < end; j++ )
                scene->tmp_face_order[order_index++] = scene->sm_faces_by_depth[j];
        }
        scene->tmp_face_order_count = order_index;

        sm_depth_offset_restore(scene, model_min_depth, model_max_depth);
        TORIDRAW_DBG_SORT_PRINT(debug_stats, scene, hnd, order_index);
        return;
    }

    {
        int priority_depths[12] = { 0 };
        int counts[12] = { 0 };

        partition_and_accumulate_faces_by_priority_small(
            scene, priority_depths, counts, in->face_priorities, model_min_depth, model_max_depth);

        /* Last reader of sm_depth_offset; the sort below works from
         * sm_prio_faces and counts. */
        sm_depth_offset_restore(scene, model_min_depth, model_max_depth);

        scene->tmp_face_order_count =
            sort_face_draw_order_small(scene, scene->tmp_face_order, priority_depths, counts);
        TORIDRAW_DBG_SORT_PRINT(debug_stats, scene, hnd, scene->tmp_face_order_count);
    }
}

/*
 * Read the model once, pick a lane once.
 *
 * `flat` is the caller's choice of algorithm; the debug counters live only in
 * the bucket lane, so a run that asks for them goes there whatever `flat` says.
 * That override is the reason the arming happens up here and not inside the
 * lane that uses it.
 */
static inline void
toridraw_compute_projected_face_order_small(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort,
    int flat)
{
    TORIDRAW_DBG_SORT_LOCALS
    struct sort_model_inputs in;

    sort_model_inputs(hnd, &in);

    TORIDRAW_DBG_SORT_ARM();

    if( flat && !TORIDRAW_DBG_SORT_ARMED() )
        face_order_small_flat(scene, &in, presort);
    else
        face_order_small_bucket(scene, hnd, &in, TORIDRAW_DBG_SORT_ARG presort);
}

/* The plain entry: the sort the environment / ToriDraw_FaceSortSetFlat name. */
static inline void
ToriDraw_ComputeProjectedFaceOrderSmall(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    bool presort)
{
    toridraw_compute_projected_face_order_small(
        scene, hnd, presort, toridraw_face_sort_flat_armed());
}


#endif /* TORIDRAW_FACESORT_DISPATCH_U_C */
