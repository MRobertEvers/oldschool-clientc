#include "painter_common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int prev;
    int next;
    int level;
    int x;
    int z;
    uint8_t mask;
    uint8_t primary_count;
    int8_t scenery_id[PAINTER_MAX_PRIMARY];
    uint8_t primary_extend_dirs[PAINTER_MAX_PRIMARY];
    uint8_t combined_extend_dirs;
    uint8_t draw_front;
    uint8_t draw_back;
    uint8_t draw_primaries;
} W3dSquare;

typedef struct
{
    PainterScenerySpec spec;
    int cycle;
    int distance;
} W3dScenery;

typedef struct
{
    uint8_t phase;
    int level;
    int x;
    int z;
} W3dSeed;

typedef struct PainterWorld3dWorkspace
{
    int gs;
    int levels;
    int radius;
    double eye_x;
    double eye_z;
    double yaw_deg;
    double fov_deg;
    int n_tiles;
    int n_scenery;
    int sent_idx;
    W3dSquare *squares;
    W3dScenery *sceneries;
    W3dSeed *seeds;
    int seeds_cap;
    int seeds_len;
    int min_draw_x;
    int max_draw_x;
    int min_draw_z;
    int max_draw_z;
    uint64_t frustum[PAINTER_FRUSTUM_WORDS];
} PainterWorld3dWorkspace;

static inline W3dSquare *
sq_at(PainterWorld3dWorkspace *w, int level, int x, int z)
{
    if( x < 0 || z < 0 || x >= w->gs || z >= w->gs || level < 0 || level >= w->levels )
        return NULL;
    ptrdiff_t idx = PAINTER_GRID_IDX(level, x, z, w->gs);
    return &w->squares[idx];
}

static void
link_unlink(PainterWorld3dWorkspace *w, int idx)
{
    W3dSquare *s = &w->squares[idx];
    if( s->prev < 0 )
        return;
    int p = s->prev;
    int n = s->next;
    w->squares[p].next = n;
    w->squares[n].prev = p;
    s->prev = -1;
    s->next = -1;
}

static void
link_push(PainterWorld3dWorkspace *w, int idx)
{
    int sent = w->sent_idx;
    link_unlink(w, idx);
    W3dSquare *s = &w->squares[idx];
    int tail = w->squares[sent].prev;
    s->prev = tail;
    s->next = sent;
    w->squares[tail].next = idx;
    w->squares[sent].prev = idx;
}

static int
link_pop(PainterWorld3dWorkspace *w)
{
    int sent = w->sent_idx;
    int head = w->squares[sent].next;
    if( head == sent )
        return -1;
    link_unlink(w, head);
    return head;
}

static int
link_is_empty(PainterWorld3dWorkspace *w)
{
    int sent = w->sent_idx;
    return w->squares[sent].next == sent;
}

static void
link_init_sentinel(PainterWorld3dWorkspace *w)
{
    int s = w->sent_idx;
    w->squares[s].prev = s;
    w->squares[s].next = s;
}

static void
emit_seed(
    PainterWorld3dWorkspace *w,
    uint8_t phase,
    int level,
    int cx,
    int cz,
    uint8_t *seen,
    int gs)
{
    if( cx < 0 || cz < 0 || cx >= gs || cz >= gs )
        return;
    int b = cz * gs + cx;
    int bi = b / 8;
    int bb = 1 << (b % 8);
    if( seen[bi] & (uint8_t)bb )
        return;
    seen[bi] |= (uint8_t)bb;
    assert(w->seeds_len < w->seeds_cap);
    w->seeds[w->seeds_len].phase = phase;
    w->seeds[w->seeds_len].level = level;
    w->seeds[w->seeds_len].x = cx;
    w->seeds[w->seeds_len].z = cz;
    w->seeds_len++;
}

static int
build_seeds(PainterWorld3dWorkspace *w, int eye_ix, int eye_iz)
{
    w->seeds_len = 0;
    int R = w->radius;
    int gs = w->gs;
    int L = w->levels;

    for( int phase = 1; phase <= 2; phase++ )
    {
        for( int level = 0; level < L; level++ )
        {
            for( int dx = -R; dx <= 0; dx++ )
            {
                int right_x = eye_ix + dx;
                int left_x = eye_ix - dx;
                if( right_x < w->min_draw_x && left_x >= w->max_draw_x )
                    continue;

                for( int dz = -R; dz <= 0; dz++ )
                {
                    int fwd_z = eye_iz + dz;
                    int bwd_z = eye_iz - dz;

                    uint8_t seen[PAINTER_MAX_GRID * PAINTER_MAX_GRID / 8 + 1];
                    memset(seen, 0, sizeof(seen));

                    if( right_x >= w->min_draw_x )
                    {
                        if( fwd_z >= w->min_draw_z )
                            emit_seed(w, (uint8_t)phase, level, right_x, fwd_z, seen, gs);
                        if( bwd_z < w->max_draw_z )
                            emit_seed(w, (uint8_t)phase, level, right_x, bwd_z, seen, gs);
                    }
                    if( left_x < w->max_draw_x )
                    {
                        if( fwd_z >= w->min_draw_z )
                            emit_seed(w, (uint8_t)phase, level, left_x, fwd_z, seen, gs);
                        if( bwd_z < w->max_draw_z )
                            emit_seed(w, (uint8_t)phase, level, left_x, bwd_z, seen, gs);
                    }
                }
            }
        }
    }
    return w->seeds_len;
}

void *
painter_world3d_alloc(const PainterConfig *cfg)
{
    assert(cfg->grid_size >= 1 && cfg->grid_size <= PAINTER_MAX_GRID);
    PainterWorld3dWorkspace *w =
        (PainterWorld3dWorkspace *)calloc(1, sizeof(PainterWorld3dWorkspace));
    if( !w )
        return NULL;

    int gs = cfg->grid_size;
    int lv = cfg->levels;
    int nt = gs * gs * lv;
    w->gs = gs;
    w->levels = lv;
    w->n_tiles = nt;
    w->n_scenery = cfg->scenery_count;
    w->sent_idx = nt;
    w->squares = (W3dSquare *)calloc((size_t)nt + 1u, sizeof(W3dSquare));
    w->sceneries = (W3dScenery *)calloc(
        (size_t)(w->n_scenery > 0 ? w->n_scenery : 1), sizeof(W3dScenery));
    int R = cfg->radius;
    if( R < 1 )
        R = 1;
    if( R > gs )
        R = gs;
    w->radius = R;
    /* Upper bound ~ 8 * levels * (radius+1)^2; also scale with grid for safety */
    int cap = 8 * lv * (gs + 4) * (gs + 4) + 2048;
    if( cap < 8192 )
        cap = 8192;
    w->seeds_cap = cap;
    w->seeds = (W3dSeed *)calloc((size_t)cap, sizeof(W3dSeed));

    if( !w->squares || !w->sceneries || !w->seeds )
    {
        painter_world3d_free(w);
        return NULL;
    }

    for( int i = 0; i < nt; i++ )
    {
        w->squares[i].prev = -1;
        w->squares[i].next = -1;
    }
    link_init_sentinel(w);

    /* Static grid topology from cfg->scenery (same as Python _build_grid) */
    for( int level = 0; level < lv; level++ )
    {
        for( int x = 0; x < gs; x++ )
        {
            for( int z = 0; z < gs; z++ )
            {
                W3dSquare *sq = sq_at(w, level, x, z);
                sq->level = level;
                sq->x = x;
                sq->z = z;
                sq->mask = 1;
                sq->primary_count = 0;
                sq->combined_extend_dirs = 0;
                memset(sq->scenery_id, -1, sizeof(sq->scenery_id));
            }
        }
    }

    for( int si = 0; si < w->n_scenery; si++ )
    {
        w->sceneries[si].spec = cfg->scenery[si];
        const PainterScenerySpec *sp = &w->sceneries[si].spec;
        int min_x = painter_scenery_min_x(sp);
        int max_x = painter_scenery_max_x(sp);
        int min_z = painter_scenery_min_z(sp);
        int max_z = painter_scenery_max_z(sp);
        for( int tx = min_x; tx <= max_x; tx++ )
        {
            for( int tz = min_z; tz <= max_z; tz++ )
            {
                W3dSquare *sq = sq_at(w, sp->level, tx, tz);
                if( !sq || sq->primary_count >= PAINTER_MAX_PRIMARY )
                    continue;
                unsigned spans = 0;
                if( tx > min_x )
                    spans |= PAINTER_SPAN_WEST;
                if( tx < max_x )
                    spans |= PAINTER_SPAN_EAST;
                if( tz > min_z )
                    spans |= PAINTER_SPAN_NORTH;
                if( tz < max_z )
                    spans |= PAINTER_SPAN_SOUTH;
                sq->scenery_id[sq->primary_count] = (int8_t)si;
                sq->primary_extend_dirs[sq->primary_count] = (uint8_t)spans;
                sq->combined_extend_dirs |= (uint8_t)spans;
                sq->primary_count++;
            }
        }
    }

    return w;
}

void painter_world3d_free(void *ws)
{
    PainterWorld3dWorkspace *w = (PainterWorld3dWorkspace *)ws;
    if( !w )
        return;
    free(w->squares);
    free(w->sceneries);
    free(w->seeds);
    free(w);
}

void painter_world3d_run(void *ws, const PainterConfig *cfg)
{
    PainterWorld3dWorkspace *w = (PainterWorld3dWorkspace *)ws;
    assert(w && cfg);
    assert(cfg->grid_size == w->gs && cfg->levels == w->levels);

    w->eye_x = cfg->eye_x;
    w->eye_z = cfg->eye_z;
    w->yaw_deg = cfg->yaw_deg;
    w->fov_deg = cfg->fov_deg;

    int eye_ix = (int)w->eye_x;
    int eye_iz = (int)w->eye_z;
    int R = cfg->radius;
    if( R < 1 )
        R = 1;
    if( R > w->gs )
        R = w->gs;
    w->radius = R;

    w->min_draw_x = eye_ix - R;
    if( w->min_draw_x < 0 )
        w->min_draw_x = 0;
    w->max_draw_x = eye_ix + R + 1;
    if( w->max_draw_x > w->gs )
        w->max_draw_x = w->gs;
    w->min_draw_z = eye_iz - R;
    if( w->min_draw_z < 0 )
        w->min_draw_z = 0;
    w->max_draw_z = eye_iz + R + 1;
    if( w->max_draw_z > w->gs )
        w->max_draw_z = w->gs;

    painter_compute_frustum_mask(
        w->frustum, w->gs, w->eye_x, w->eye_z, w->yaw_deg, w->fov_deg);

    for( int i = 0; i < w->n_tiles; i++ )
    {
        link_unlink(w, i);
        W3dSquare *sq = &w->squares[i];
        sq->mask = (uint8_t)painter_tile_visible_in_mask(w->frustum, sq->x, sq->z, w->gs);
        sq->draw_front = 0;
        sq->draw_back = 0;
        sq->draw_primaries = 0;
    }
    link_init_sentinel(w);

    for( int si = 0; si < w->n_scenery; si++ )
    {
        w->sceneries[si].cycle = 0;
        w->sceneries[si].distance = 0;
    }

    build_seeds(w, eye_ix, eye_iz);

    int cycle = 1;
    int tiles_remaining = 0;

    for( int level = 0; level < w->levels; level++ )
    {
        for( int x = w->min_draw_x; x < w->max_draw_x; x++ )
        {
            for( int z = w->min_draw_z; z < w->max_draw_z; z++ )
            {
                W3dSquare *sq = sq_at(w, level, x, z);
                if( sq && sq->mask )
                {
                    sq->draw_front = 1;
                    sq->draw_back = 1;
                    sq->draw_primaries = sq->primary_count > 0 ? 1u : 0u;
                    tiles_remaining++;
                }
            }
        }
    }

    int seed_idx = 0;
    int check_adjacent = 1;

    for( ;; )
    {
        if( link_is_empty(w) )
        {
            if( tiles_remaining == 0 )
                break;
            int seeded = 0;
            while( seed_idx < w->seeds_len )
            {
                W3dSeed *sd = &w->seeds[seed_idx++];
                W3dSquare *sq = sq_at(w, sd->level, sd->x, sd->z);
                if( sq && sq->draw_front )
                {
                    link_push(w, (int)(sq - w->squares));
                    check_adjacent = (sd->phase == 1);
                    seeded = 1;
                    break;
                }
            }
            if( !seeded )
                break;
        }

        int tidx = link_pop(w);
        if( tidx < 0 )
            break;
        W3dSquare *tile = &w->squares[tidx];

        if( !tile->draw_back )
            continue;

        int tx = tile->x;
        int tz = tile->z;
        int lv = tile->level;

        if( tile->draw_front )
        {
            if( check_adjacent )
            {
                if( lv > 0 )
                {
                    W3dSquare *below = sq_at(w, lv - 1, tx, tz);
                    if( below && below->draw_back )
                        continue;
                }

                if( tx <= eye_ix && tx > w->min_draw_x )
                {
                    W3dSquare *adj = sq_at(w, lv, tx - 1, tz);
                    if( adj && adj->draw_back
                        && (adj->draw_front
                            || (tile->combined_extend_dirs & PAINTER_SPAN_WEST) == 0) )
                        continue;
                }
                if( tx >= eye_ix && tx < w->max_draw_x - 1 )
                {
                    W3dSquare *adj = sq_at(w, lv, tx + 1, tz);
                    if( adj && adj->draw_back
                        && (adj->draw_front
                            || (tile->combined_extend_dirs & PAINTER_SPAN_EAST) == 0) )
                        continue;
                }
                if( tz <= eye_iz && tz > w->min_draw_z )
                {
                    W3dSquare *adj = sq_at(w, lv, tx, tz - 1);
                    if( adj && adj->draw_back
                        && (adj->draw_front
                            || (tile->combined_extend_dirs & PAINTER_SPAN_NORTH) == 0) )
                        continue;
                }
                if( tz >= eye_iz && tz < w->max_draw_z - 1 )
                {
                    W3dSquare *adj = sq_at(w, lv, tx, tz + 1);
                    if( adj && adj->draw_back
                        && (adj->draw_front
                            || (tile->combined_extend_dirs & PAINTER_SPAN_SOUTH) == 0) )
                        continue;
                }
            }
            else
            {
                check_adjacent = 1;
            }

            tile->draw_front = 0;

            unsigned spans = tile->combined_extend_dirs;
            if( spans )
            {
                if( tx < eye_ix && (spans & PAINTER_SPAN_EAST) )
                {
                    W3dSquare *adj = sq_at(w, lv, tx + 1, tz);
                    if( adj && adj->draw_back )
                        link_push(w, (int)(adj - w->squares));
                }
                if( tz < eye_iz && (spans & PAINTER_SPAN_SOUTH) )
                {
                    W3dSquare *adj = sq_at(w, lv, tx, tz + 1);
                    if( adj && adj->draw_back )
                        link_push(w, (int)(adj - w->squares));
                }
                if( tx > eye_ix && (spans & PAINTER_SPAN_WEST) )
                {
                    W3dSquare *adj = sq_at(w, lv, tx - 1, tz);
                    if( adj && adj->draw_back )
                        link_push(w, (int)(adj - w->squares));
                }
                if( tz > eye_iz && (spans & PAINTER_SPAN_NORTH) )
                {
                    W3dSquare *adj = sq_at(w, lv, tx, tz - 1);
                    if( adj && adj->draw_back )
                        link_push(w, (int)(adj - w->squares));
                }
            }
        }

        if( tile->draw_primaries )
        {
            tile->draw_primaries = 0;
            int buf_si[PAINTER_MAX_SCENERY];
            int buf_n = 0;

            for( int i = 0; i < tile->primary_count; i++ )
            {
                int si = tile->scenery_id[i];
                if( si < 0 )
                    continue;
                W3dScenery *sc = &w->sceneries[si];
                if( sc->cycle == cycle )
                    continue;

                int blocked = 0;
                const PainterScenerySpec *sp = &sc->spec;
                int min_x = painter_scenery_min_x(sp);
                int max_x = painter_scenery_max_x(sp);
                int min_z = painter_scenery_min_z(sp);
                int max_z = painter_scenery_max_z(sp);
                for( int lx = min_x; lx <= max_x && !blocked; lx++ )
                {
                    for( int lz = min_z; lz <= max_z; lz++ )
                    {
                        W3dSquare *other = sq_at(w, lv, lx, lz);
                        if( other && other->draw_front )
                        {
                            tile->draw_primaries = 1;
                            blocked = 1;
                            break;
                        }
                    }
                }
                if( !blocked )
                {
                    int dist_x =
                        (eye_ix - min_x) > (max_x - eye_ix) ? (eye_ix - min_x) : (max_x - eye_ix);
                    int dz_a = eye_iz - min_z;
                    int dz_b = max_z - eye_iz;
                    int dz = dz_a > dz_b ? dz_a : dz_b;
                    sc->distance = dist_x + dz;
                    if( buf_n < PAINTER_MAX_SCENERY )
                        buf_si[buf_n++] = si;
                }
            }

            while( buf_n > 0 )
            {
                int best_i = -1;
                int best_d = -999999;
                for( int i = 0; i < buf_n; i++ )
                {
                    int si = buf_si[i];
                    W3dScenery *sc = &w->sceneries[si];
                    if( sc->cycle == cycle )
                        continue;
                    if( sc->distance > best_d )
                    {
                        best_d = sc->distance;
                        best_i = i;
                    }
                }
                if( best_i < 0 )
                    break;
                int si = buf_si[best_i];
                buf_si[best_i] = buf_si[buf_n - 1];
                buf_n--;
                W3dScenery *far = &w->sceneries[si];
                far->cycle = cycle;
                const PainterScenerySpec *sp = &far->spec;
                int min_x = painter_scenery_min_x(sp);
                int max_x = painter_scenery_max_x(sp);
                int min_z = painter_scenery_min_z(sp);
                int max_z = painter_scenery_max_z(sp);
                for( int lx = min_x; lx <= max_x; lx++ )
                {
                    for( int lz = min_z; lz <= max_z; lz++ )
                    {
                        W3dSquare *occ = sq_at(w, lv, lx, lz);
                        if( occ && (lx != tx || lz != tz) && occ->draw_back )
                            link_push(w, (int)(occ - w->squares));
                    }
                }
            }

            if( tile->draw_primaries )
                continue;
        }

        if( !tile->draw_back )
            continue;

        if( tx <= eye_ix && tx > w->min_draw_x )
        {
            W3dSquare *adj = sq_at(w, lv, tx - 1, tz);
            if( adj && adj->draw_back )
                continue;
        }
        if( tx >= eye_ix && tx < w->max_draw_x - 1 )
        {
            W3dSquare *adj = sq_at(w, lv, tx + 1, tz);
            if( adj && adj->draw_back )
                continue;
        }
        if( tz <= eye_iz && tz > w->min_draw_z )
        {
            W3dSquare *adj = sq_at(w, lv, tx, tz - 1);
            if( adj && adj->draw_back )
                continue;
        }
        if( tz >= eye_iz && tz < w->max_draw_z - 1 )
        {
            W3dSquare *adj = sq_at(w, lv, tx, tz + 1);
            if( adj && adj->draw_back )
                continue;
        }

        tile->draw_back = 0;
        tiles_remaining--;

        if( lv < w->levels - 1 )
        {
            W3dSquare *above = sq_at(w, lv + 1, tx, tz);
            if( above && above->draw_back )
                link_push(w, (int)(above - w->squares));
        }
        if( tx < eye_ix )
        {
            W3dSquare *adj = sq_at(w, lv, tx + 1, tz);
            if( adj && adj->draw_back )
                link_push(w, (int)(adj - w->squares));
        }
        if( tz < eye_iz )
        {
            W3dSquare *adj = sq_at(w, lv, tx, tz + 1);
            if( adj && adj->draw_back )
                link_push(w, (int)(adj - w->squares));
        }
        if( tx > eye_ix )
        {
            W3dSquare *adj = sq_at(w, lv, tx - 1, tz);
            if( adj && adj->draw_back )
                link_push(w, (int)(adj - w->squares));
        }
        if( tz > eye_iz )
        {
            W3dSquare *adj = sq_at(w, lv, tx, tz - 1);
            if( adj && adj->draw_back )
                link_push(w, (int)(adj - w->squares));
        }
    }
}
