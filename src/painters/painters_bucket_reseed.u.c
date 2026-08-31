#ifndef PAINTERS_BUCKET_RESEED_U_C
#define PAINTERS_BUCKET_RESEED_U_C

/*
 * The bucket drain's perimeter reseed — the wave-front stall path.
 *
 * WHAT A STALL IS. The classify pass pushes every READY tile into the queue up
 * front, so on an ordinary frame the queue empties only once tiles_remaining
 * has reached zero and the drain breaks before ever coming here. This is
 * reached only when the queue empties with tiles STILL READY, which one thing
 * causes: `bucket_gate_blocks(...) -> continue` pops a tile and drops it
 * without re-queuing it and without marking it DONE. Normally a neighbour
 * finishing re-pushes it. When the last tiles standing are all gate-blocked on
 * one another, nothing is left to do that, and the wave-front has stalled.
 * Walking the draw-box perimeter for a still-READY tile is the restart; phase 2
 * relaxes the adjacency gate so a deadlocked set cannot stall forever.
 *
 * HOW OFTEN, measured 2026-08-30 with TORIRS_PAINTER_STALL over five bench
 * scenes x three pitches x four bearings -- ~18,000 real paints and ~94 million
 * adjacency-gate deferrals: ZERO stalls. Not one configuration reached this
 * file. It was not always so: before b9967d49f (2026-08-10) the classify pass
 * did not enqueue, and this was the drain's primary engine -- which is what the
 * `seed_gen_next` frames in src/out.folded, profiled 2026-07-23, are.
 *
 * So this is a backstop with no measured traffic, and it is written as one:
 * ITS OWN FILE, and ONE LINE at the point of use. Zero observed is not zero
 * possible -- the deadlock above is still reachable, and without the restart
 * those tiles are simply never painted -- so it stays, but it does not get to
 * put thirty lines through the middle of the drain loop to say so.
 *
 * Included by painters_bucket.u.c, below bucket_push_if_active and the tile
 * step enum it uses, above the drain that calls it.
 */

/** Lazily built: a frame that never stalls never runs the generator at all. */
struct PainterBucketReseed
{
    struct PainterSeedGen gen;
    int initialized;
};

/**
 * Restart the wave-front from the draw-box perimeter.
 *
 * Returns 1 having pushed exactly one still-READY tile (and set
 * *check_adjacent for it), or 0 when the perimeter is exhausted — which ends
 * the drain with `tiles_remaining` tiles unpainted, the one outcome here that
 * is visible on screen.
 */
static int
painter_bucket_reseed_next(
    struct PainterBucketReseed* r,
    struct PainterBucketCtx* w,
    struct TilePaint* paints,
    int width,
    int level_stride,
    int levels,
    int camera_sx,
    int camera_sz,
    int camera_slevel,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int radius,
    int depth,
    int tiles_remaining,
    int tiles_in_box,
    int* check_adjacent,
    int64_t* perf_pushes,
    int64_t* perf_push_dedup)
{
    int sx;
    int sz;
    int level;
    int phase;

    assert(r);
    assert(w);
    assert(paints);
    assert(check_adjacent);
    assert(perf_pushes);
    assert(perf_push_dedup);

    PAINTER_DBG_STALL_NOTE(
        depth, camera_sx, camera_sz, camera_slevel, tiles_remaining, tiles_in_box);

    if( !r->initialized )
    {
        seed_gen_init(
            &r->gen,
            camera_sx,
            camera_sz,
            min_draw_x,
            max_draw_x,
            min_draw_z,
            max_draw_z,
            levels,
            painter_seed_radius_for_box(
                camera_sx, camera_sz, min_draw_x, max_draw_x, min_draw_z, max_draw_z, radius));
        r->initialized = 1;
    }

    while( seed_gen_next(&r->gen, &sx, &sz, &level, &phase) )
    {
        int tidx = sx + sz * width + level * level_stride;
        int dist;
        if( paints[tidx].step != PAINT_STEP_READY )
            continue;

        dist = abs(sx - camera_sx) + abs(sz - camera_sz);
        PAINTER_DBG_WEDGE_EVENTF(tidx, "SEED", "phase=%d d=%d", phase, dist);
        PAINTER_DBG_STALL_RESEEDED(sx, sz, level, phase);
        bucket_push_if_active(w, paints, tidx, dist, perf_pushes, perf_push_dedup);
        *check_adjacent = (phase == 1);
        TORIRS_PERF_COUNT(TORIRS_PERF_CTR_PAINTER_DRAIN_EVENTS, 1);
        return 1;
    }

    PAINTER_DBG_STALL_GIVEUP(tiles_remaining);
    return 0;
}

/**
 * The one-line façade, and the reason this is a macro rather than a plain call.
 *
 * The restart needs twenty values, every one of them already a local of
 * bucket_paint_world's drain: the grid strides, the camera, the draw box, the
 * perf accumulators, the stall census's frame context. Spelling them at the
 * call site would put the thirty lines back that moving the body out took
 * away. So the macro names them, once, here — it expands inside the drain and
 * reads the drain's own locals by those names. It is single-use and it lives
 * next to the only function it may be written in; the function below it takes
 * everything explicitly and is what any other caller would use.
 */
#define PAINTER_BUCKET_RESEED(r) \
    painter_bucket_reseed_next( \
        (r), w, paints, width, level_stride, levels, camera_sx, camera_sz, camera_slevel, \
        min_draw_x, max_draw_x, min_draw_z, max_draw_z, radius, stack->depth, \
        tiles_remaining, tiles_in_box, &check_adjacent, &perf_pushes, &perf_push_dedup)

#endif /* PAINTERS_BUCKET_RESEED_U_C */
