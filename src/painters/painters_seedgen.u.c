#ifndef PAINTERS_SEEDGEN_U_C
#define PAINTERS_SEEDGEN_U_C

/*
 * PainterSeedGen — incremental perimeter-seed generator shared by all painters.
 *
 * Its own file because it is a self-contained coroutine over a rectangle and
 * nothing else: it reads no painter, tile, element or command state, and the
 * three functions below are the whole of it. Sitting in the middle of
 * painters.c it read as part of the command encoding above it and the draw-box
 * resolution below it, which it is not.
 *
 * WHO USES IT, since the answer is not "the benchmarks": both real drains.
 * painter_paint_bucket (painters_bucket.u.c) reseeds from here every time its
 * bucket queue empties with tiles still marked READY, and
 * painter_paint_world3d (painters_world3d.u.c) does the same off its link
 * list. That is the ordinary traversal restart, not instrumentation, so this
 * file is NOT behind a debug gate — compiling it out blanks the world. The
 * telemetry that watches it (the SEED rows) is the part that is gated, and
 * that lives in debug/painters_debug.u.c.
 *
 * Replaces the materialized seeds[] + seed_seen[] buffers and the O(L*R^2)
 * upfront build that executed a full-buffer memset on every inner (dx,dz)
 * iteration.
 *
 * Yields (sx, sz, level, phase) in the same order as the original nested loop:
 *   for phase in [1, 2]:
 *     for level in [0, L):
 *       for dx in [-R, 0]:
 *         for dz in [-R, 0]:
 *           up to 4 symmetrically reflected candidates, de-duped by coordinate check
 *
 * Included by painters.c, once, above every drain that seeds from it.
 */
struct PainterSeedGen
{
    int eye_ix, eye_iz;
    int min_draw_x, max_draw_x;
    int min_draw_z, max_draw_z;
    int L, R;
    int phase;  /* 1..2; > 2 means exhausted */
    int level;
    int dx;     /* runs -R..0 */
    int dz;     /* runs -R..0 */
    int sub;    /* 0..3: which of the up-to-4 candidates within (dx,dz) */
};

/** Seed radius must cover every tile in the draw box relative to the eye so an
 * offset orbit centre still gets perimeter seeds. */
static int
painter_seed_radius_for_box(
    int eye_sx,
    int eye_sz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int radius)
{
    int r = radius;
    int d;
    if( max_draw_x > min_draw_x )
    {
        d = eye_sx - min_draw_x;
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
        d = eye_sx - (max_draw_x - 1);
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
    }
    if( max_draw_z > min_draw_z )
    {
        d = eye_sz - min_draw_z;
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
        d = eye_sz - (max_draw_z - 1);
        if( d < 0 )
            d = -d;
        if( d > r )
            r = d;
    }
    return r < 1 ? 1 : r;
}

static void
seed_gen_init(
    struct PainterSeedGen* g,
    int eye_ix,
    int eye_iz,
    int min_draw_x,
    int max_draw_x,
    int min_draw_z,
    int max_draw_z,
    int L,
    int radius)
{
    g->eye_ix     = eye_ix;
    g->eye_iz     = eye_iz;
    g->min_draw_x = min_draw_x;
    g->max_draw_x = max_draw_x;
    g->min_draw_z = min_draw_z;
    g->max_draw_z = max_draw_z;
    g->L          = L;
    g->R          = (radius < 1) ? 1 : radius;
    g->phase      = 1;
    g->level      = 0;
    g->dx         = -(g->R);
    g->dz         = -(g->R);
    g->sub        = 0;
}

/*
 * Advance to the next valid seed. Returns 1 and writes (sx,sz,level,phase), or
 * returns 0 when all perimeter seeds have been exhausted.
 */
static int
seed_gen_next(
    struct PainterSeedGen* g,
    int* out_sx,
    int* out_sz,
    int* out_level,
    int* out_phase)
{
    while( g->phase <= 2 )
    {
        int right_x = g->eye_ix + g->dx;
        int left_x  = g->eye_ix - g->dx;
        int fwd_z   = g->eye_iz + g->dz;
        int bwd_z   = g->eye_iz - g->dz;

        while( g->sub < 4 )
        {
            int sub = g->sub++;
            int sx  = (sub < 2) ? right_x : left_x;
            int sz  = (sub & 1) ? bwd_z : fwd_z;

            /* Skip symmetric duplicates (replaces the per-(dx,dz) seed_seen memset). */
            if( sub == 1 && bwd_z == fwd_z ) continue;                         /* dz == 0 */
            if( sub == 2 && left_x == right_x ) continue;                      /* dx == 0 */
            if( sub == 3 && (left_x == right_x || bwd_z == fwd_z) ) continue; /* dx|dz = 0 */

            /* Check against the draw rectangle (equivalent to original per-candidate checks
             * plus the emit_seed tile-bounds guard). */
            if( sx < g->min_draw_x || sx >= g->max_draw_x ) continue;
            if( sz < g->min_draw_z || sz >= g->max_draw_z ) continue;

            *out_sx    = sx;
            *out_sz    = sz;
            *out_level = g->level;
            *out_phase = g->phase;
            return 1;
        }

        /* Advance: dz → dx → level → phase */
        g->sub = 0;
        if( ++g->dz > 0 )
        {
            g->dz = -(g->R);
            if( ++g->dx > 0 )
            {
                g->dx = -(g->R);
                if( ++g->level >= g->L )
                {
                    g->level = 0;
                    g->phase++;
                }
            }
        }
    }
    return 0;
}

#endif /* PAINTERS_SEEDGEN_U_C */
