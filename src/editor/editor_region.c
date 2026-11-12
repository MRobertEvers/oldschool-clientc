#include "editor/editor_region.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the client's overlay shapes ----------------------------------------------- */

/* engine/world_builder/world_decode_tile.c: table row = overlay shape + 1.
 * A row's vertex types (1..8 the corners and edge middles anticlockwise from
 * the SW corner, 9..12 the quarter-midlines, 13..16 the quarter-centres) and
 * its faces, each (is_overlay, a, b, c) over those vertices. */
static const int k_vertex_types[13][6] = {
    { 1, 3, 5, 7 },         { 1, 3, 5, 7 },         { 1, 3, 5, 7 },          { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 6 },      { 1, 3, 5, 7, 6 },      { 1, 3, 5, 7, 6 },       { 1, 3, 5, 7, 2, 6 },
    { 1, 3, 5, 7, 2, 8 },   { 1, 3, 5, 7, 2, 8 },   { 1, 3, 5, 7, 11, 12 },  { 1, 3, 5, 7, 11, 12 },
    { 1, 3, 5, 7, 13, 14 },
};
static const int k_vertex_counts[13] = { 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6 };
static const int k_faces[13][24] = {
    { 0, 1, 2, 3, 0, 0, 1, 3 },
    { 1, 1, 2, 3, 1, 0, 1, 3 },
    { 0, 1, 2, 3, 1, 0, 1, 3 },
    { 0, 0, 1, 2, 0, 0, 2, 4, 1, 0, 4, 3 },
    { 0, 0, 1, 4, 0, 0, 4, 3, 1, 1, 2, 4 },
    { 0, 0, 4, 3, 1, 0, 1, 2, 1, 0, 2, 4 },
    { 0, 1, 2, 4, 1, 0, 1, 4, 1, 0, 4, 3 },
    { 0, 4, 1, 2, 0, 4, 2, 5, 1, 0, 4, 5, 1, 0, 5, 3 },
    { 0, 4, 1, 2, 0, 4, 2, 3, 0, 4, 3, 5, 1, 0, 4, 5 },
    { 0, 0, 4, 5, 1, 4, 1, 2, 1, 4, 2, 3, 1, 4, 3, 5 },
    { 0, 0, 1, 5, 0, 1, 4, 5, 0, 1, 2, 4, 1, 0, 5, 3, 1, 5, 4, 3, 1, 4, 2, 3 },
    { 1, 0, 1, 5, 1, 1, 4, 5, 1, 1, 2, 4, 0, 0, 5, 3, 0, 5, 4, 3, 0, 4, 2, 3 },
    { 1, 0, 5, 4, 1, 0, 1, 5, 0, 0, 4, 3, 0, 4, 5, 3, 0, 5, 2, 3, 0, 1, 2, 5 },
};
static const int k_face_counts[13] = { 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 6, 6, 6 };
/* Each vertex type's place in the tile, in 128ths (x east, z north). */
static const int k_type_pos[17][2] = {
    { 0, 0 },    { 0, 0 },    { 64, 0 },  { 128, 0 },  { 128, 64 }, { 128, 128 }, { 64, 128 }, { 0, 128 }, { 0, 64 },
    { 64, 32 },  { 96, 64 },  { 64, 96 }, { 32, 64 },  { 32, 32 },  { 96, 32 },   { 96, 96 },  { 32, 96 },
};

static uint64_t g_masks[EDITOR_REGION_SHAPES][4];
static int g_masks_ready;

/* The client turns an edge-middle or quarter vertex type with the tile; the
 * corners stay and the faces' corner indices turn instead. */
static int
region_rotate_type(
    int type,
    int rotation)
{
    if( (type & 1) == 0 && type <= 8 )
        return ((type - rotation - rotation - 1) & 7) + 1;
    if( type > 8 && type <= 12 )
        return ((type - 9 - rotation) & 3) + 9;
    if( type > 12 )
        return ((type - 13 - rotation) & 3) + 13;
    return type;
}

/* Inside or on the triangle (a sample on a shared diagonal counts for both
 * halves, as it does for neither more than the other). */
static int
region_in_triangle(
    const double* a,
    const double* b,
    const double* c,
    double x,
    double z)
{
    double const d1 = (x - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (z - b[1]);
    double const d2 = (x - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (z - c[1]);
    double const d3 = (x - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (z - a[1]);
    int const negative = d1 < 0 || d2 < 0 || d3 < 0;
    int const positive = d1 > 0 || d2 > 0 || d3 > 0;
    return !(negative && positive);
}

static uint64_t
region_build_mask(
    int shape,
    int rotation)
{
    int const row = shape + 1;
    double pos[6][2];
    uint64_t mask = 0;

    for( int v = 0; v < k_vertex_counts[row]; v++ )
    {
        int const type = region_rotate_type(k_vertex_types[row][v], rotation);
        pos[v][0] = k_type_pos[type][0];
        pos[v][1] = k_type_pos[type][1];
    }
    for( int r = 0; r < EDITOR_REGION_MASK; r++ )
        for( int c = 0; c < EDITOR_REGION_MASK; c++ )
        {
            double const x = (c + 0.5) * 128.0 / EDITOR_REGION_MASK;
            double const z = (r + 0.5) * 128.0 / EDITOR_REGION_MASK;
            for( int f = 0; f < k_face_counts[row]; f++ )
            {
                const int* face = &k_faces[row][f * 4];
                int index[3];
                if( face[0] != 1 )
                    continue;
                for( int k = 0; k < 3; k++ )
                    index[k] = face[1 + k] < 4 ? (face[1 + k] - rotation) & 3 : face[1 + k];
                if( region_in_triangle(pos[index[0]], pos[index[1]], pos[index[2]], x, z) )
                {
                    mask |= (uint64_t)1 << (r * EDITOR_REGION_MASK + c);
                    break;
                }
            }
        }
    return mask;
}

static void
region_masks_init(void)
{
    if( g_masks_ready )
        return;
    for( int s = 0; s < EDITOR_REGION_SHAPES; s++ )
        for( int r = 0; r < 4; r++ )
            g_masks[s][r] = region_build_mask(s, r);
    g_masks_ready = 1;
}

uint64_t
Editor_RegionShapeMask(
    int shape,
    int rotation)
{
    assert(shape >= 0);
    assert(shape < EDITOR_REGION_SHAPES);
    assert(rotation >= 0);
    assert(rotation < 4);
    region_masks_init();
    return g_masks[shape][rotation];
}

int
Editor_RegionShapeTriangles(
    int shape,
    int rotation,
    double out[EDITOR_REGION_SHAPE_TRIANGLES][3][2])
{
    int const row = shape + 1;
    double pos[6][2];
    int n = 0;

    assert(shape >= 0);
    assert(shape < EDITOR_REGION_SHAPES);
    assert(rotation >= 0);
    assert(rotation < 4);
    assert(out);
    for( int v = 0; v < k_vertex_counts[row]; v++ )
    {
        int const type = region_rotate_type(k_vertex_types[row][v], rotation);
        pos[v][0] = k_type_pos[type][0] / 128.0;
        pos[v][1] = k_type_pos[type][1] / 128.0;
    }
    for( int f = 0; f < k_face_counts[row]; f++ )
    {
        const int* face = &k_faces[row][f * 4];
        if( face[0] != 1 )
            continue;
        for( int k = 0; k < 3; k++ )
        {
            int const index = face[1 + k] < 4 ? (face[1 + k] - rotation) & 3 : face[1 + k];
            out[n][k][0] = pos[index][0];
            out[n][k][1] = pos[index][1];
        }
        n++;
    }
    return n;
}

int
Editor_RegionPopcount(uint64_t v)
{
    int n = 0;
    while( v )
    {
        v &= v - 1;
        n++;
    }
    return n;
}

/* ---- the fit ------------------------------------------------------------------- */

/* The shapes a set may write, in tie order: the full tile, the half diagonal,
 * the half square first. NATURAL is the path's dirt set (the content's tracks
 * use every shape but the trapezium), PAVED the path's pavement set. */
static const int k_natural_shapes[] = { 0, 1, 6, 4, 5, 2, 3, 8, 7, 10, 9 };
static const int k_paved_shapes[] = { 0, 1, 6, 8, 7 };
static const int k_all_shapes[] = { 0, 1, 6, 4, 5, 2, 3, 8, 7, 10, 9, 11 };

static const int*
region_set_shapes(
    enum Editor_RegionSet set,
    int* out_count)
{
    assert(set >= 0);
    assert(set < EDITOR_REGION_SET_COUNT);
    if( set == EDITOR_REGION_PAVED )
    {
        *out_count = (int)(sizeof(k_paved_shapes) / sizeof(k_paved_shapes[0]));
        return k_paved_shapes;
    }
    if( set == EDITOR_REGION_ALL )
    {
        *out_count = (int)(sizeof(k_all_shapes) / sizeof(k_all_shapes[0]));
        return k_all_shapes;
    }
    *out_count = (int)(sizeof(k_natural_shapes) / sizeof(k_natural_shapes[0]));
    return k_natural_shapes;
}

/* How wrong `mask` is as a cover of `target`: a spilled sample weighs
 * `hardness`, a missed one 100 - hardness. At 50 it is 50 x the samples that
 * differ, the path's measure. */
static int
region_cost(
    uint64_t target,
    uint64_t mask,
    int hardness)
{
    return hardness * Editor_RegionPopcount(mask & ~target) + (100 - hardness) * Editor_RegionPopcount(target & ~mask);
}

int
Editor_RegionFit(
    enum Editor_RegionSet set,
    int hardness,
    uint64_t mask,
    int* out_shape,
    int* out_rotation)
{
    int shape_count;
    const int* shapes = region_set_shapes(set, &shape_count);
    int best = region_cost(mask, 0, hardness);
    int found = 0;

    assert(hardness >= 0);
    assert(hardness <= 100);
    assert(out_shape);
    assert(out_rotation);
    region_masks_init();
    for( int i = 0; i < shape_count; i++ )
        for( int r = 0; r < (shapes[i] == 0 ? 1 : 4); r++ )
        {
            int const cost = region_cost(mask, g_masks[shapes[i]][r], hardness);
            if( cost < best )
            {
                best = cost;
                *out_shape = shapes[i];
                *out_rotation = r;
                found = 1;
            }
        }
    return found;
}

/* ---- the grid ------------------------------------------------------------------ */

int
Editor_RegionGridInit(
    struct Editor_RegionGrid* grid,
    int x0,
    int z0,
    int w,
    int h)
{
    assert(grid);
    assert(w > 0);
    assert(h > 0);
    memset(grid, 0, sizeof(*grid));
    if( w > EDITOR_REGION_SPAN_MAX || h > EDITOR_REGION_SPAN_MAX )
        return 0;
    grid->x0 = x0;
    grid->z0 = z0;
    grid->w = w;
    grid->h = h;
    grid->tiles = ToriMem_Calloc(ToriMem_Editor(), (size_t)w * (size_t)h, sizeof(*grid->tiles));
    assert(grid->tiles);
    return 1;
}

void
Editor_RegionGridFree(struct Editor_RegionGrid* grid)
{
    if( !grid )
        return;
    if( grid->tiles )
        ToriMem_Free(ToriMem_Editor(), grid->tiles);
    grid->tiles = NULL;
    grid->w = 0;
    grid->h = 0;
}

/* Sample (c, r), absolute (world tile * 8 + sample), on. */
static void
region_sample_set(
    struct Editor_RegionGrid* grid,
    int c,
    int r)
{
    int const tx = (int)floor((double)c / EDITOR_REGION_MASK) - grid->x0;
    int const tz = (int)floor((double)r / EDITOR_REGION_MASK) - grid->z0;
    int const sc = c - (tx + grid->x0) * EDITOR_REGION_MASK;
    int const sr = r - (tz + grid->z0) * EDITOR_REGION_MASK;

    if( tx < 0 || tz < 0 || tx >= grid->w || tz >= grid->h )
        return;
    grid->tiles[(size_t)tz * grid->w + tx] |= (uint64_t)1 << (sr * EDITOR_REGION_MASK + sc);
}

/* The grid's sample range, absolute. */
static void
region_sample_bounds(
    const struct Editor_RegionGrid* grid,
    int* c0,
    int* r0,
    int* c1,
    int* r1)
{
    *c0 = grid->x0 * EDITOR_REGION_MASK;
    *r0 = grid->z0 * EDITOR_REGION_MASK;
    *c1 = (grid->x0 + grid->w) * EDITOR_REGION_MASK - 1;
    *r1 = (grid->z0 + grid->h) * EDITOR_REGION_MASK - 1;
}

void
Editor_RegionStraight(
    double start_x,
    double start_z,
    double pointer_x,
    double pointer_z,
    double* out_start_x,
    double* out_start_z,
    double* out_end_x,
    double* out_end_z)
{
    /* tan(22.5 degrees): past it a run is nearer the diagonal than the axis. */
    double const k_octant = 0.41421356237;
    double sx;
    double sz;
    double dx;
    double dz;
    double ax;
    double az;

    assert(out_start_x);
    assert(out_start_z);
    assert(out_end_x);
    assert(out_end_z);
    sx = floor(start_x) + 0.5;
    sz = floor(start_z) + 0.5;
    dx = pointer_x - sx;
    dz = pointer_z - sz;
    ax = fabs(dx);
    az = fabs(dz);
    if( az <= ax * k_octant )
    {
        dx = floor(dx + 0.5);
        dz = 0.0;
    }
    else if( ax <= az * k_octant )
    {
        dx = 0.0;
        dz = floor(dz + 0.5);
    }
    else
    {
        double const run = floor((ax + az) / 2.0 + 0.5);
        dx = dx < 0.0 ? -run : run;
        dz = dz < 0.0 ? -run : run;
    }
    *out_start_x = sx;
    *out_start_z = sz;
    *out_end_x = sx + dx;
    *out_end_z = sz + dz;
}

void
Editor_RegionCapsule(
    struct Editor_RegionGrid* grid,
    double ax,
    double az,
    double bx,
    double bz,
    double radius)
{
    double const dx = bx - ax;
    double const dz = bz - az;
    double const length2 = dx * dx + dz * dz;
    double const r2 = radius * radius;
    int gc0;
    int gr0;
    int gc1;
    int gr1;
    int c0;
    int c1;
    int r0;
    int r1;

    assert(grid);
    assert(grid->tiles);
    assert(radius >= 0.0);
    region_sample_bounds(grid, &gc0, &gr0, &gc1, &gr1);
    c0 = (int)floor(((ax < bx ? ax : bx) - radius) * EDITOR_REGION_MASK);
    c1 = (int)ceil(((ax > bx ? ax : bx) + radius) * EDITOR_REGION_MASK);
    r0 = (int)floor(((az < bz ? az : bz) - radius) * EDITOR_REGION_MASK);
    r1 = (int)ceil(((az > bz ? az : bz) + radius) * EDITOR_REGION_MASK);
    c0 = c0 < gc0 ? gc0 : c0;
    r0 = r0 < gr0 ? gr0 : r0;
    c1 = c1 > gc1 ? gc1 : c1;
    r1 = r1 > gr1 ? gr1 : r1;
    for( int r = r0; r <= r1; r++ )
        for( int c = c0; c <= c1; c++ )
        {
            double const x = (c + 0.5) / EDITOR_REGION_MASK;
            double const z = (r + 0.5) / EDITOR_REGION_MASK;
            double t = length2 > 0.0 ? ((x - ax) * dx + (z - az) * dz) / length2 : 0.0;
            double qx;
            double qz;
            if( t < 0.0 )
                t = 0.0;
            if( t > 1.0 )
                t = 1.0;
            qx = ax + dx * t;
            qz = az + dz * t;
            if( (x - qx) * (x - qx) + (z - qz) * (z - qz) <= r2 )
                region_sample_set(grid, c, r);
        }
}

void
Editor_RegionPolygon(
    struct Editor_RegionGrid* grid,
    const double* xs,
    const double* zs,
    int count)
{
    double* cross;
    double min_z = 1e18;
    double max_z = -1e18;
    int gc0;
    int gr0;
    int gc1;
    int gr1;
    int r0;
    int r1;

    assert(grid);
    assert(grid->tiles);
    assert(xs);
    assert(zs);
    assert(count >= 3);
    region_sample_bounds(grid, &gc0, &gr0, &gc1, &gr1);
    for( int i = 0; i < count; i++ )
    {
        min_z = zs[i] < min_z ? zs[i] : min_z;
        max_z = zs[i] > max_z ? zs[i] : max_z;
    }
    r0 = (int)floor(min_z * EDITOR_REGION_MASK);
    r1 = (int)ceil(max_z * EDITOR_REGION_MASK);
    r0 = r0 < gr0 ? gr0 : r0;
    r1 = r1 > gr1 ? gr1 : r1;
    cross = ToriMem_Alloc(ToriMem_Editor(), sizeof(*cross) * (size_t)count);
    assert(cross);
    for( int r = r0; r <= r1; r++ )
    {
        double const z = (r + 0.5) / EDITOR_REGION_MASK;
        int n = 0;
        for( int i = 0, j = count - 1; i < count; j = i++ )
        {
            if( (zs[i] > z) == (zs[j] > z) )
                continue;
            cross[n++] = xs[i] + (z - zs[i]) * (xs[j] - xs[i]) / (zs[j] - zs[i]);
        }
        /* Insertion sort: a row crosses a loop a handful of times. */
        for( int i = 1; i < n; i++ )
        {
            double const v = cross[i];
            int k = i - 1;
            while( k >= 0 && cross[k] > v )
            {
                cross[k + 1] = cross[k];
                k--;
            }
            cross[k + 1] = v;
        }
        for( int i = 0; i + 1 < n; i += 2 )
        {
            /* Samples whose centre is in [cross[i], cross[i + 1]]: one ON
             * the loop is in, as the client's shapes count a sample on
             * their own edge (a corner-to-corner edge is the half
             * diagonal's 36 samples, not 28). */
            int c0 = (int)ceil(cross[i] * EDITOR_REGION_MASK - 0.5);
            int c1 = (int)floor(cross[i + 1] * EDITOR_REGION_MASK - 0.5);
            c0 = c0 < gc0 ? gc0 : c0;
            c1 = c1 > gc1 ? gc1 : c1;
            for( int c = c0; c <= c1; c++ )
                region_sample_set(grid, c, r);
        }
    }
    ToriMem_Free(ToriMem_Editor(), cross);
}

void
Editor_RegionLoopSmooth(
    double* xs,
    double* zs,
    int* count,
    int max,
    int passes)
{
    double* tx;
    double* tz;

    assert(xs);
    assert(zs);
    assert(count);
    if( *count < 3 || passes <= 0 )
        return;
    tx = ToriMem_Alloc(ToriMem_Editor(), sizeof(*tx) * (size_t)max);
    tz = ToriMem_Alloc(ToriMem_Editor(), sizeof(*tz) * (size_t)max);
    assert(tx);
    assert(tz);
    for( int pass = 0; pass < passes && *count * 2 <= max; pass++ )
    {
        int const n = *count;
        for( int i = 0; i < n; i++ )
        {
            int const j = (i + 1) % n;
            tx[2 * i] = 0.75 * xs[i] + 0.25 * xs[j];
            tz[2 * i] = 0.75 * zs[i] + 0.25 * zs[j];
            tx[2 * i + 1] = 0.25 * xs[i] + 0.75 * xs[j];
            tz[2 * i + 1] = 0.25 * zs[i] + 0.75 * zs[j];
        }
        memcpy(xs, tx, sizeof(*xs) * (size_t)(2 * n));
        memcpy(zs, tz, sizeof(*zs) * (size_t)(2 * n));
        *count = 2 * n;
    }
    ToriMem_Free(ToriMem_Editor(), tx);
    ToriMem_Free(ToriMem_Editor(), tz);
}

void
Editor_RegionReadBack(
    struct Editor_RegionGrid* grid,
    Editor_RegionTileFn existing,
    void* user)
{
    assert(grid);
    assert(grid->tiles);
    assert(existing);
    for( int tz = 0; tz < grid->h; tz++ )
        for( int tx = 0; tx < grid->w; tx++ )
        {
            int shape;
            int rotation;
            if( existing(user, grid->x0 + tx, grid->z0 + tz, &shape, &rotation) != EDITOR_REGION_HAS_SAME )
                continue;
            grid->tiles[(size_t)tz * grid->w + tx] |= Editor_RegionShapeMask(shape, rotation);
        }
}

void
Editor_RegionSmooth(
    struct Editor_RegionGrid* grid,
    int radius,
    int hardness)
{
    int sw;
    int sh;
    int area;
    int32_t* sum;
    uint64_t* out;

    assert(grid);
    assert(grid->tiles);
    assert(radius >= 0);
    assert(hardness > 0);
    assert(hardness <= 100);
    if( radius == 0 )
        return;
    sw = grid->w * EDITOR_REGION_MASK;
    sh = grid->h * EDITOR_REGION_MASK;
    area = (2 * radius + 1) * (2 * radius + 1);
    /* A summed-area table, one row and column of zeros before the grid. */
    sum = ToriMem_Calloc(ToriMem_Editor(), (size_t)(sw + 1) * (size_t)(sh + 1), sizeof(*sum));
    out = ToriMem_Calloc(ToriMem_Editor(), (size_t)grid->w * (size_t)grid->h, sizeof(*out));
    assert(sum);
    assert(out);
    for( int r = 0; r < sh; r++ )
    {
        int32_t row = 0;
        for( int c = 0; c < sw; c++ )
        {
            uint64_t const m = grid->tiles[(size_t)(r / EDITOR_REGION_MASK) * grid->w + c / EDITOR_REGION_MASK];
            row += (int32_t)(m >> ((r % EDITOR_REGION_MASK) * EDITOR_REGION_MASK + c % EDITOR_REGION_MASK) & 1);
            sum[(size_t)(r + 1) * (sw + 1) + c + 1] = sum[(size_t)r * (sw + 1) + c + 1] + row;
        }
    }
    for( int r = 0; r < sh; r++ )
        for( int c = 0; c < sw; c++ )
        {
            int const a0 = c - radius < 0 ? 0 : c - radius;
            int const b0 = r - radius < 0 ? 0 : r - radius;
            int const a1 = c + radius + 1 > sw ? sw : c + radius + 1;
            int const b1 = r + radius + 1 > sh ? sh : r + radius + 1;
            int32_t const in = sum[(size_t)b1 * (sw + 1) + a1] - sum[(size_t)b0 * (sw + 1) + a1]
                               - sum[(size_t)b1 * (sw + 1) + a0] + sum[(size_t)b0 * (sw + 1) + a0];
            /* Outside the grid reads bare: the box keeps its whole area. */
            if( (int64_t)in * 100 >= (int64_t)hardness * area )
                out[(size_t)(r / EDITOR_REGION_MASK) * grid->w + c / EDITOR_REGION_MASK] |=
                    (uint64_t)1 << ((r % EDITOR_REGION_MASK) * EDITOR_REGION_MASK + c % EDITOR_REGION_MASK);
        }
    memcpy(grid->tiles, out, sizeof(*out) * (size_t)grid->w * (size_t)grid->h);
    ToriMem_Free(ToriMem_Editor(), sum);
    ToriMem_Free(ToriMem_Editor(), out);
}

/* ---- the plan ------------------------------------------------------------------ */

/* A coverage's samples along each side, 8 bits west to east / south to north. */
static unsigned
region_side_south(uint64_t m)
{
    return (unsigned)(m & 0xff);
}

static unsigned
region_side_north(uint64_t m)
{
    return (unsigned)(m >> 56 & 0xff);
}

static unsigned
region_side_column(
    uint64_t m,
    int column)
{
    unsigned out = 0;
    for( int r = 0; r < EDITOR_REGION_MASK; r++ )
        out |= (unsigned)(m >> (r * EDITOR_REGION_MASK + column) & 1) << r;
    return out;
}

/* How far a tile's sides are from its neighbours' facing sides: a step where
 * an edge leaves one tile and does not carry on into the next. Only between
 * tiles the region touches, or against one that holds the overlay: against
 * bare ground a side is the region's own edge. */
static int
region_seams(
    uint64_t m,
    const uint64_t* target,
    const uint64_t* chosen,
    int w,
    int h,
    int tx,
    int tz)
{
    int seams = 0;
    size_t at;
    if( tx > 0 && (target[at = (size_t)tz * w + tx - 1] || chosen[at]) )
        seams += Editor_RegionPopcount(region_side_column(m, 0) ^ region_side_column(chosen[at], 7));
    if( tx + 1 < w && (target[at = (size_t)tz * w + tx + 1] || chosen[at]) )
        seams += Editor_RegionPopcount(region_side_column(m, 7) ^ region_side_column(chosen[at], 0));
    if( tz > 0 && (target[at = (size_t)(tz - 1) * w + tx] || chosen[at]) )
        seams += Editor_RegionPopcount(region_side_south(m) ^ region_side_north(chosen[at]));
    if( tz + 1 < h && (target[at = (size_t)(tz + 1) * w + tx] || chosen[at]) )
        seams += Editor_RegionPopcount(region_side_north(m) ^ region_side_south(chosen[at]));
    return seams;
}

/* A seam sample weighs 2.5 coverage samples at hardness 50 (the costs are in
 * hundredths of a sample's two weights, so 2.5 x 50): enough that a tile
 * whose edge would step out of line with both neighbours takes the shape that
 * carries their edge on, not enough to move an edge off the region. Chosen by
 * eye on editor_path_test's dirt curve: at 0 and 1.25 whole tiles stick out
 * of a sloping edge every few tiles, at 2.5 the slope runs on, at 4 edges
 * start to spike out where no path is. */
#define REGION_SEAM_WEIGHT 125
#define REGION_SETTLE_PASSES 4

int
Editor_RegionPlan(
    const struct Editor_RegionParams* params,
    const struct Editor_RegionGrid* grid,
    Editor_RegionTileFn existing,
    void* user,
    struct Editor_RegionTile* out,
    int max)
{
    size_t n;
    uint64_t* target;
    uint64_t* chosen;
    signed char* shape_of;
    signed char* rotation_of;
    unsigned char* touched;
    unsigned char* has;
    unsigned char* cap;
    int shape_count;
    const int* shapes;
    int written = 0;

    assert(params);
    assert(params->op >= 0);
    assert(params->op < EDITOR_REGION_OP_COUNT);
    assert(params->hardness >= EDITOR_REGION_HARDNESS_MIN);
    assert(params->hardness <= EDITOR_REGION_HARDNESS_MAX);
    assert(grid);
    assert(grid->tiles);
    assert(out);
    shapes = region_set_shapes(params->set, &shape_count);
    region_masks_init();
    n = (size_t)grid->w * (size_t)grid->h;
    target = ToriMem_Alloc(ToriMem_Editor(), n * sizeof(*target));
    chosen = ToriMem_Alloc(ToriMem_Editor(), n * sizeof(*chosen));
    shape_of = ToriMem_Alloc(ToriMem_Editor(), n);
    rotation_of = ToriMem_Alloc(ToriMem_Editor(), n);
    touched = ToriMem_Alloc(ToriMem_Editor(), n);
    has = ToriMem_Alloc(ToriMem_Editor(), n);
    cap = ToriMem_Alloc(ToriMem_Editor(), n);
    assert(target);
    assert(chosen);
    assert(shape_of);
    assert(rotation_of);
    assert(touched);
    assert(has);
    assert(cap);
    assert(params->misfit_cap >= 0);

    for( int tz = 0; tz < grid->h; tz++ )
        for( int tx = 0; tx < grid->w; tx++ )
        {
            size_t const at = (size_t)tz * grid->w + tx;
            uint64_t const g = grid->tiles[at];
            int had_shape = 0;
            int had_rotation = 0;
            int const had = existing ? existing(user, grid->x0 + tx, grid->z0 + tz, &had_shape, &had_rotation)
                                     : EDITOR_REGION_HAS_NONE;
            uint64_t const was = had == EDITOR_REGION_HAS_SAME ? Editor_RegionShapeMask(had_shape, had_rotation) : 0;
            /* Written by Editor_RegionFit only when it finds a fit. */
            int shape = 0;
            int rotation = 0;

            has[at] = (unsigned char)had;
            /* Untouched: a fixed neighbour, as it is. */
            chosen[at] = was;
            target[at] = 0;
            touched[at] = 0;
            shape_of[at] = -1;
            rotation_of[at] = 0;
            if( had == EDITOR_REGION_HAS_OTHER )
            {
                /* Another overlay gives its tile up only to a region that
                 * covers it whole; anywhere else the two would have to share
                 * the tile, which the client cannot draw. */
                if( params->op == EDITOR_REGION_ADD && g == ~(uint64_t)0 )
                {
                    touched[at] = 2;
                    target[at] = g;
                    chosen[at] = g;
                    shape_of[at] = 0;
                }
                else
                    chosen[at] = 0;
                continue;
            }
            if( params->op == EDITOR_REGION_ADD )
            {
                touched[at] = g != 0;
                target[at] = g ? g | was : 0;
                /* A stroke that adds no more to a tile already holding the
                 * overlay than a fit may miss leaves it as it is: going over
                 * painted ground again changes nothing, where the union of
                 * the old shape's spill and the stroke would creep outward.
                 * The path (no cap) always joins. */
                if( was && params->misfit_cap > 0 && Editor_RegionPopcount(g & ~was) <= params->misfit_cap )
                {
                    touched[at] = 0;
                    target[at] = 0;
                }
            }
            else if( params->op == EDITOR_REGION_SUBTRACT )
            {
                touched[at] = g != 0 && was != 0;
                target[at] = was & ~g;
                if( Editor_RegionPopcount(target[at]) < EDITOR_REGION_SLIVER )
                    target[at] = 0;
            }
            else
            {
                touched[at] = g != 0 || was != 0;
                target[at] = g;
            }
            if( !touched[at] )
            {
                target[at] = 0;
                continue;
            }
            chosen[at] = 0;
            if( target[at] && Editor_RegionFit(params->set, params->hardness, target[at], &shape, &rotation) )
            {
                chosen[at] = g_masks[shape][rotation];
                shape_of[at] = (signed char)shape;
                rotation_of[at] = (signed char)rotation;
            }
            /* The settle may go as far from the coverage as the cap, or as
             * far as the tile's own best fit already is. */
            {
                int const alone = Editor_RegionPopcount(target[at] ^ chosen[at]);
                cap[at] = (unsigned char)(params->misfit_cap == 0           ? 64
                                          : alone > params->misfit_cap ? alone
                                                                        : params->misfit_cap);
            }
        }

    for( int pass = 0; pass < REGION_SETTLE_PASSES; pass++ )
    {
        int changes = 0;
        for( int tz = 0; tz < grid->h; tz++ )
            for( int tx = 0; tx < grid->w; tx++ )
            {
                size_t const at = (size_t)tz * grid->w + tx;
                int best;
                int best_shape = -1;
                int best_rotation = 0;
                uint64_t best_mask = 0;

                if( touched[at] != 1 || !target[at] )
                    continue;
                /* Bare first, as the fit's own baseline; past the cap, any
                 * shape within it beats it. */
                best = Editor_RegionPopcount(target[at]) <= cap[at]
                           ? region_cost(target[at], 0, params->hardness)
                                 + REGION_SEAM_WEIGHT * region_seams(0, target, chosen, grid->w, grid->h, tx, tz)
                           : INT_MAX;
                for( int i = 0; i < shape_count; i++ )
                    for( int r = 0; r < (shapes[i] == 0 ? 1 : 4); r++ )
                    {
                        uint64_t const m = g_masks[shapes[i]][r];
                        int cost;
                        if( Editor_RegionPopcount(target[at] ^ m) > cap[at] )
                            continue;
                        cost = region_cost(target[at], m, params->hardness)
                               + REGION_SEAM_WEIGHT * region_seams(m, target, chosen, grid->w, grid->h, tx, tz);
                        if( cost < best )
                        {
                            best = cost;
                            best_shape = shapes[i];
                            best_rotation = r;
                            best_mask = m;
                        }
                    }
                if( best_mask != chosen[at] )
                {
                    chosen[at] = best_mask;
                    shape_of[at] = (signed char)best_shape;
                    rotation_of[at] = (signed char)best_rotation;
                    changes++;
                }
            }
        if( changes == 0 )
            break;
    }

    for( int tz = 0; tz < grid->h; tz++ )
        for( int tx = 0; tx < grid->w; tx++ )
        {
            size_t const at = (size_t)tz * grid->w + tx;
            if( !touched[at] || written >= max )
                continue;
            if( shape_of[at] < 0 )
            {
                /* Bare: the overlay comes off a tile that held it -- never
                 * under ADD, which only joins (a path or a stroke grazing
                 * its own overlay leaves it). */
                if( params->op == EDITOR_REGION_ADD || has[at] != EDITOR_REGION_HAS_SAME )
                    continue;
            }
            out[written].world_x = grid->x0 + tx;
            out[written].world_z = grid->z0 + tz;
            out[written].shape = shape_of[at];
            out[written].rotation = shape_of[at] < 0 ? 0 : rotation_of[at];
            out[written].coverage = Editor_RegionPopcount(target[at]);
            out[written].misfit = Editor_RegionPopcount(target[at] ^ chosen[at]);
            written++;
        }
    ToriMem_Free(ToriMem_Editor(), target);
    ToriMem_Free(ToriMem_Editor(), chosen);
    ToriMem_Free(ToriMem_Editor(), shape_of);
    ToriMem_Free(ToriMem_Editor(), rotation_of);
    ToriMem_Free(ToriMem_Editor(), touched);
    ToriMem_Free(ToriMem_Editor(), has);
    ToriMem_Free(ToriMem_Editor(), cap);
    return written;
}

/* ---- underlays: the rim as a dither -------------------------------------------- */

static double
region_hash_unit(
    uint32_t seed,
    int x,
    int z)
{
    uint32_t v = seed ^ (uint32_t)x * 0x9e3779b1U ^ (uint32_t)z * 0x85ebca77U;
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return (double)v / 4294967296.0;
}

int
Editor_RegionDither(
    const struct Editor_RegionGrid* grid,
    int feather,
    uint32_t seed,
    int* out_x,
    int* out_z,
    int max)
{
    int written = 0;

    assert(grid);
    assert(grid->tiles);
    assert(feather >= 0);
    assert(out_x);
    assert(out_z);
    for( int tz = 0; tz < grid->h; tz++ )
        for( int tx = 0; tx < grid->w; tx++ )
        {
            double share;
            if( feather == 0 )
                share = Editor_RegionPopcount(grid->tiles[(size_t)tz * grid->w + tx]) / 64.0;
            else
            {
                double sum = 0.0;
                int seen = 0;
                for( int dz = -feather; dz <= feather; dz++ )
                    for( int dx = -feather; dx <= feather; dx++ )
                    {
                        int const x = tx + dx;
                        int const z = tz + dz;
                        if( dx * dx + dz * dz > feather * feather )
                            continue;
                        seen++;
                        if( x < 0 || z < 0 || x >= grid->w || z >= grid->h )
                            continue;
                        sum += Editor_RegionPopcount(grid->tiles[(size_t)z * grid->w + x]) / 64.0;
                    }
                share = sum / seen;
                /* A tile the region covers whole stays painted whatever its
                 * neighbours: feathering widens the rim outward and in, it
                 * does not thin the middle's own edge tiles to nothing. */
                if( Editor_RegionPopcount(grid->tiles[(size_t)tz * grid->w + tx]) == 64 && share < 0.5 )
                    share = 0.5;
            }
            if( share <= 0.0 || written >= max )
                continue;
            if( share < 1.0 - 1e-9 && region_hash_unit(seed, grid->x0 + tx, grid->z0 + tz) >= share )
                continue;
            out_x[written] = grid->x0 + tx;
            out_z[written] = grid->z0 + tz;
            written++;
        }
    return written;
}

/* ---- the run a tile belongs to ------------------------------------------------- */

#define REGION_FLOOD_SIDE EDITOR_REGION_SPAN_MAX

int
Editor_RegionFlood(
    Editor_RegionTileFn existing,
    void* user,
    int world_x,
    int world_z,
    int* out_x,
    int* out_z,
    int max,
    int* out_clipped)
{
    static const int k_step[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    int const ox = world_x - REGION_FLOOD_SIDE / 2;
    int const oz = world_z - REGION_FLOOD_SIDE / 2;
    unsigned char* seen;
    int shape;
    int rotation;
    int head = 0;
    int count = 0;

    assert(existing);
    assert(out_x);
    assert(out_z);
    assert(max > 0);
    assert(out_clipped);
    *out_clipped = 0;
    if( existing(user, world_x, world_z, &shape, &rotation) != EDITOR_REGION_HAS_SAME )
        return 0;
    seen = ToriMem_Calloc(ToriMem_Editor(), (size_t)REGION_FLOOD_SIDE * REGION_FLOOD_SIDE, 1);
    assert(seen);
    seen[(size_t)(world_z - oz) * REGION_FLOOD_SIDE + (world_x - ox)] = 1;
    out_x[count] = world_x;
    out_z[count] = world_z;
    count++;
    /* Breadth first; the output is the queue. */
    while( head < count )
    {
        int const x = out_x[head];
        int const z = out_z[head];
        head++;
        for( int k = 0; k < 4; k++ )
        {
            int const nx = x + k_step[k][0];
            int const nz = z + k_step[k][1];
            size_t at;
            if( nx - ox < 0 || nz - oz < 0 || nx - ox >= REGION_FLOOD_SIDE || nz - oz >= REGION_FLOOD_SIDE )
            {
                if( existing(user, nx, nz, &shape, &rotation) == EDITOR_REGION_HAS_SAME )
                    *out_clipped = 1;
                continue;
            }
            at = (size_t)(nz - oz) * REGION_FLOOD_SIDE + (nx - ox);
            if( seen[at] )
                continue;
            seen[at] = 1;
            if( existing(user, nx, nz, &shape, &rotation) != EDITOR_REGION_HAS_SAME )
                continue;
            if( count == max )
            {
                *out_clipped = 1;
                continue;
            }
            out_x[count] = nx;
            out_z[count] = nz;
            count++;
        }
    }
    ToriMem_Free(ToriMem_Editor(), seen);
    return count;
}
