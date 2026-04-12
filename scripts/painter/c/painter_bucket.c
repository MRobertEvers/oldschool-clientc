#include "painter_common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Manhattan distance from eye to any tile is in [0, 2*(gs-1)]; gs <= PAINTER_MAX_GRID. */
#define PAINTER_BUCKET_DIST_RANGE (2 * PAINTER_MAX_GRID + 1)

typedef struct
{
    int x, z, level;
    int idx;
    int dist;
    int bucket_next; /* intrusive singly-linked list per distance bucket; -1 = end */
    uint8_t step;
    uint8_t mask;
    uint8_t in_heap;
    uint8_t scenery_count;
    uint8_t combined_extend_dirs;
    int8_t scenery_id[PAINTER_MAX_PRIMARY];
} BucketTile;

typedef struct
{
    PainterScenerySpec spec;
    uint8_t step;
    int n_tiles;
    int tiles[PAINTER_MAX_TILES_PER_SCENERY];
} BucketScenery;

typedef struct PainterBucketWorkspace
{
    int gs;
    int levels;
    double eye_x;
    double eye_z;
    double yaw_deg;
    double fov_deg;
    int min_draw_x;
    int max_draw_x;
    int min_draw_z;
    int max_draw_z;
    int n_tiles;
    int n_scenery;
    BucketTile *tiles;
    BucketScenery *sceneries;
    int buckets[PAINTER_BUCKET_DIST_RANGE];
    int bucket_max;
    uint64_t frustum[PAINTER_FRUSTUM_WORDS];
} PainterBucketWorkspace;

static inline BucketTile *
tile_at(PainterBucketWorkspace *w, int level, int x, int z)
{
    if( x < 0 || z < 0 || x >= w->gs || z >= w->gs || level < 0 || level >= w->levels )
        return NULL;
    ptrdiff_t idx = PAINTER_GRID_IDX(level, x, z, w->gs);
    return &w->tiles[idx];
}

static void
bucket_reset(PainterBucketWorkspace *w)
{
    for( int i = 0; i < PAINTER_BUCKET_DIST_RANGE; i++ )
        w->buckets[i] = -1;
    w->bucket_max = -1;
}

static void
bucket_push(PainterBucketWorkspace *w, int ti)
{
    BucketTile *t = &w->tiles[ti];
    int d = t->dist;
    assert(d >= 0 && d < PAINTER_BUCKET_DIST_RANGE);
    t->bucket_next = w->buckets[d];
    w->buckets[d] = ti;
    if( d > w->bucket_max )
        w->bucket_max = d;
}

/* Pop farthest distance first; LIFO within a bucket (order within a distance band is irrelevant). */
static int
bucket_pop(PainterBucketWorkspace *w)
{
    while( w->bucket_max >= 0 )
    {
        int head = w->buckets[w->bucket_max];
        if( head < 0 )
        {
            w->bucket_max--;
            continue;
        }
        w->buckets[w->bucket_max] = w->tiles[head].bucket_next;
        w->tiles[head].bucket_next = -1;
        return head;
    }
    return -1;
}

void *
painter_bucket_alloc(const PainterConfig *cfg)
{
    assert(cfg->grid_size >= 1 && cfg->grid_size <= PAINTER_MAX_GRID);
    assert(cfg->levels >= 1 && cfg->levels <= 8);
    PainterBucketWorkspace *w = (PainterBucketWorkspace *)calloc(1, sizeof(PainterBucketWorkspace));
    if( !w )
        return NULL;
    w->gs = cfg->grid_size;
    w->levels = cfg->levels;
    w->eye_x = cfg->eye_x;
    w->eye_z = cfg->eye_z;
    w->yaw_deg = cfg->yaw_deg;
    w->fov_deg = cfg->fov_deg;
    w->n_tiles = cfg->levels * cfg->grid_size * cfg->grid_size;
    w->n_scenery = cfg->scenery_count;
    w->tiles = (BucketTile *)calloc((size_t)w->n_tiles, sizeof(BucketTile));
    w->sceneries = (BucketScenery *)calloc((size_t)w->n_scenery, sizeof(BucketScenery));
    if( !w->tiles || (w->n_scenery > 0 && !w->sceneries) )
    {
        painter_bucket_free(w);
        return NULL;
    }
    return w;
}

void painter_bucket_free(void *ws)
{
    PainterBucketWorkspace *w = (PainterBucketWorkspace *)ws;
    if( !w )
        return;
    free(w->tiles);
    free(w->sceneries);
    free(w);
}

static int
is_north(PainterBucketWorkspace *w, BucketTile *t)
{
    return t->z < (int)w->eye_z;
}

static int
is_east(PainterBucketWorkspace *w, BucketTile *t)
{
    return t->x > (int)w->eye_x;
}

static int
is_south(PainterBucketWorkspace *w, BucketTile *t)
{
    return t->z > (int)w->eye_z;
}

static int
is_west(PainterBucketWorkspace *w, BucketTile *t)
{
    return t->x < (int)w->eye_x;
}

static void
push_tile(PainterBucketWorkspace *w, int ti)
{
    BucketTile *t = &w->tiles[ti];
    if( t->in_heap )
        return;
    bucket_push(w, ti);
    t->in_heap = 1;
}

void painter_bucket_run(void *ws, const PainterConfig *cfg)
{
    PainterBucketWorkspace *w = (PainterBucketWorkspace *)ws;
    assert(w && cfg);
    assert(cfg->grid_size == w->gs && cfg->levels == w->levels);

    w->eye_x = cfg->eye_x;
    w->eye_z = cfg->eye_z;
    w->yaw_deg = cfg->yaw_deg;
    w->fov_deg = cfg->fov_deg;

    painter_compute_frustum_mask(
        w->frustum, w->gs, w->eye_x, w->eye_z, w->yaw_deg, w->fov_deg);

    int gs = w->gs;
    int lvls = w->levels;
    int eye_ix = (int)w->eye_x;
    int eye_iz = (int)w->eye_z;
    int R = cfg->radius;
    if( R < 1 )
        R = 1;
    if( R > gs )
        R = gs;
    w->min_draw_x = eye_ix - R;
    if( w->min_draw_x < 0 )
        w->min_draw_x = 0;
    w->max_draw_x = eye_ix + R + 1;
    if( w->max_draw_x > gs )
        w->max_draw_x = gs;
    w->min_draw_z = eye_iz - R;
    if( w->min_draw_z < 0 )
        w->min_draw_z = 0;
    w->max_draw_z = eye_iz + R + 1;
    if( w->max_draw_z > gs )
        w->max_draw_z = gs;

    int ti = 0;
    for( int l = 0; l < lvls; l++ )
    {
        for( int z = 0; z < gs; z++ )
        {
            for( int x = 0; x < gs; x++ )
            {
                BucketTile *t = &w->tiles[ti];
                t->x = x;
                t->z = z;
                t->level = l;
                t->idx = ti;
                t->step = PAINTER_STEP_DONE;
                t->mask = 0;
                t->dist = 0;
                t->bucket_next = -1;
                t->in_heap = 0;
                t->scenery_count = 0;
                t->combined_extend_dirs = 0;
                memset(t->scenery_id, -1, sizeof(t->scenery_id));
                ti++;
            }
        }
    }

    for( int l = 0; l < lvls; l++ )
    {
        for( int z = w->min_draw_z; z < w->max_draw_z; z++ )
        {
            for( int x = w->min_draw_x; x < w->max_draw_x; x++ )
            {
                BucketTile *t = tile_at(w, l, x, z);
                if( !t )
                    continue;
                t->mask = (uint8_t)painter_tile_visible_in_mask(w->frustum, x, z, gs);
                t->step = t->mask ? PAINTER_STEP_NOT_VISITED : PAINTER_STEP_DONE;
                t->dist = abs(x - eye_ix) + abs(z - eye_iz);
            }
        }
    }

    for( int si = 0; si < w->n_scenery; si++ )
    {
        BucketScenery *sc = &w->sceneries[si];
        sc->spec = cfg->scenery[si];
        sc->step = PAINTER_STEP_NOT_VISITED;
        sc->n_tiles = 0;
        const PainterScenerySpec *sp = &sc->spec;
        int min_x = painter_scenery_min_x(sp);
        int max_x = painter_scenery_max_x(sp);
        int min_z = painter_scenery_min_z(sp);
        int max_z = painter_scenery_max_z(sp);
        for( int tx = min_x; tx <= max_x; tx++ )
        {
            for( int tz = min_z; tz <= max_z; tz++ )
            {
                BucketTile *t = tile_at(w, sp->level, tx, tz);
                if( !t )
                    continue;
                assert(sc->n_tiles < PAINTER_MAX_TILES_PER_SCENERY);
                sc->tiles[sc->n_tiles++] = (int)(t - w->tiles);

                if( t->scenery_count < PAINTER_MAX_PRIMARY )
                {
                    t->scenery_id[t->scenery_count++] = (int8_t)si;
                }

                unsigned spans = 0;
                if( tx > min_x )
                    spans |= PAINTER_SPAN_WEST;
                if( tx < max_x )
                    spans |= PAINTER_SPAN_EAST;
                if( tz > min_z )
                    spans |= PAINTER_SPAN_NORTH;
                if( tz < max_z )
                    spans |= PAINTER_SPAN_SOUTH;
                t->combined_extend_dirs |= (uint8_t)spans;
            }
        }
    }

    bucket_reset(w);
    for( int l = 0; l < lvls; l++ )
    {
        for( int z = w->min_draw_z; z < w->max_draw_z; z++ )
        {
            for( int x = w->min_draw_x; x < w->max_draw_x; x++ )
            {
                BucketTile *t = tile_at(w, l, x, z);
                if( !t || !t->mask )
                    continue;
                int i = (int)(t - w->tiles);
                bucket_push(w, i);
                t->in_heap = 1;
            }
        }
    }

    for( ;; )
    {
        int e_tile = bucket_pop(w);
        if( e_tile < 0 )
            break;
        BucketTile *tile = &w->tiles[e_tile];
        tile->in_heap = 0;

        if( tile->step == PAINTER_STEP_DONE )
            continue;

        if( tile->step == PAINTER_STEP_NOT_VISITED )
        {
            if( tile->level > 0 )
            {
                BucketTile *below = tile_at(w, tile->level - 1, tile->x, tile->z);
                if( below && below->step != PAINTER_STEP_DONE )
                    continue;
            }

            if( is_north(w, tile) )
            {
                BucketTile *n = tile_at(w, tile->level, tile->x, tile->z - 1);
                if( n && n->step != PAINTER_STEP_DONE )
                {
                    if( (tile->combined_extend_dirs & PAINTER_SPAN_NORTH) == 0 )
                        continue;
                }
            }
            if( is_east(w, tile) )
            {
                BucketTile *n = tile_at(w, tile->level, tile->x + 1, tile->z);
                if( n && n->step != PAINTER_STEP_DONE )
                {
                    if( (tile->combined_extend_dirs & PAINTER_SPAN_EAST) == 0 )
                        continue;
                }
            }
            if( is_south(w, tile) )
            {
                BucketTile *n = tile_at(w, tile->level, tile->x, tile->z + 1);
                if( n && n->step != PAINTER_STEP_DONE )
                {
                    if( (tile->combined_extend_dirs & PAINTER_SPAN_SOUTH) == 0 )
                        continue;
                }
            }
            if( is_west(w, tile) )
            {
                BucketTile *n = tile_at(w, tile->level, tile->x - 1, tile->z);
                if( n && n->step != PAINTER_STEP_DONE )
                {
                    if( (tile->combined_extend_dirs & PAINTER_SPAN_WEST) == 0 )
                        continue;
                }
            }
        }

        tile->step = PAINTER_STEP_BASE;

        int visit_sc[PAINTER_MAX_PRIMARY];
        int n_visit = 0;
        for( int i = 0; i < tile->scenery_count; i++ )
        {
            int si = tile->scenery_id[i];
            if( si < 0 )
                continue;
            BucketScenery *sc = &w->sceneries[si];
            if( sc->step == PAINTER_STEP_DONE )
                continue;
            int all_base = 1;
            for( int j = 0; j < sc->n_tiles; j++ )
            {
                BucketTile *u = &w->tiles[sc->tiles[j]];
                if( u->step < PAINTER_STEP_BASE )
                {
                    all_base = 0;
                    break;
                }
            }
            if( all_base && n_visit < PAINTER_MAX_PRIMARY )
                visit_sc[n_visit++] = si;
        }

        int some_drawn = 0;
        for( int vi = 0; vi < n_visit; vi++ )
        {
            BucketScenery *sc = &w->sceneries[visit_sc[vi]];
            sc->step = PAINTER_STEP_DONE;
            for( int j = 0; j < sc->n_tiles; j++ )
            {
                push_tile(w, sc->tiles[j]);
                some_drawn = 1;
            }
        }
        if( some_drawn )
            continue;

        int all_sc_done = 1;
        for( int i = 0; i < tile->scenery_count; i++ )
        {
            int si = tile->scenery_id[i];
            if( si < 0 )
                continue;
            if( w->sceneries[si].step != PAINTER_STEP_DONE )
            {
                all_sc_done = 0;
                break;
            }
        }
        if( !all_sc_done )
            continue;

        tile->step = PAINTER_STEP_DONE;

        if( tile->level < w->levels - 1 )
        {
            BucketTile *up = tile_at(w, tile->level + 1, tile->x, tile->z);
            if( up && up->step != PAINTER_STEP_DONE )
                push_tile(w, (int)(up - w->tiles));
        }

        if( is_north(w, tile) )
        {
            BucketTile *n = tile_at(w, tile->level, tile->x, tile->z + 1);
            if( n && n->step != PAINTER_STEP_DONE )
                push_tile(w, (int)(n - w->tiles));
        }
        if( is_east(w, tile) )
        {
            BucketTile *n = tile_at(w, tile->level, tile->x - 1, tile->z);
            if( n && n->step != PAINTER_STEP_DONE )
                push_tile(w, (int)(n - w->tiles));
        }
        if( is_south(w, tile) )
        {
            BucketTile *n = tile_at(w, tile->level, tile->x, tile->z - 1);
            if( n && n->step != PAINTER_STEP_DONE )
                push_tile(w, (int)(n - w->tiles));
        }
        if( is_west(w, tile) )
        {
            BucketTile *n = tile_at(w, tile->level, tile->x + 1, tile->z);
            if( n && n->step != PAINTER_STEP_DONE )
                push_tile(w, (int)(n - w->tiles));
        }
    }
}
