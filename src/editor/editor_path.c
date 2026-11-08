#include "editor/editor_path.h"

#include "editor/editor_region.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the client's overlay shapes: editor/editor_region.c ------------------------ */

_Static_assert(EDITOR_PATH_MASK == EDITOR_REGION_MASK, "a path's coverage is a region's");
_Static_assert(EDITOR_PATH_SPAN_MAX == EDITOR_REGION_SPAN_MAX, "a path plans as one region");

#define PATH_PI 3.14159265358979323846

uint64_t
Editor_PathShapeMask(
    int shape,
    int rotation)
{
    return Editor_RegionShapeMask(shape, rotation);
}

/* Dirt has every shape the content's tracks use, pavement the straight ones
 * and the corners a 45-degree bend needs: the region's NATURAL and PAVED. */
static enum Editor_RegionSet
path_set(enum Editor_PathStyle style)
{
    /* A river's banks take the dirt track's shapes. */
    return style == EDITOR_PATH_PAVED ? EDITOR_REGION_PAVED : EDITOR_REGION_NATURAL;
}

int
Editor_PathFit(
    enum Editor_PathStyle style,
    uint64_t mask,
    int* out_shape,
    int* out_rotation)
{
    assert(style >= 0);
    assert(style < EDITOR_PATH_STYLE_COUNT);
    return Editor_RegionFit(path_set(style), EDITOR_REGION_HARDNESS_DEFAULT, mask, out_shape, out_rotation);
}

/* ---- the region ----------------------------------------------------------------- */

struct PathPoint
{
    double x;
    double z;
    /** Dirt: the half widths to the left and the right of the direction. */
    double left;
    double right;
};

struct PathLine
{
    struct PathPoint* points;
    int count;
    int capacity;
};

static void
path_line_push(
    struct PathLine* line,
    double x,
    double z)
{
    if( line->count == line->capacity )
    {
        line->capacity = line->capacity ? line->capacity * 2 : 64;
        line->points = ToriMem_Realloc(ToriMem_Editor(), line->points, sizeof(*line->points) * (size_t)line->capacity);
        assert(line->points);
    }
    line->points[line->count].x = x;
    line->points[line->count].z = z;
    line->points[line->count].left = 0.0;
    line->points[line->count].right = 0.0;
    line->count++;
}

static uint32_t
path_hash(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}

/* Smooth value noise in [-1, 1] along `s`: lattice values every unit,
 * smoothstep between them. */
static double
path_noise(
    uint32_t seed,
    double s)
{
    double const cell = floor(s);
    double const t = s - cell;
    double const u = t * t * (3.0 - 2.0 * t);
    int32_t const i = (int32_t)cell;
    double const a = (double)(path_hash(seed ^ (uint32_t)i * 0x9e3779b1U) & 0xffff) / 32767.5 - 1.0;
    double const b = (double)(path_hash(seed ^ (uint32_t)(i + 1) * 0x9e3779b1U) & 0xffff) / 32767.5 - 1.0;
    return a + (b - a) * u;
}

/* Two octaves: a slow swell and a quicker ripple. */
static double
path_wander(
    uint32_t seed,
    double s,
    double period)
{
    return 0.7 * path_noise(seed, s / period) + 0.3 * path_noise(seed * 31u + 7u, s * 2.3 / period);
}

/* Chaikin's corner cutting: a tile-by-tile drag's staircase becomes a curve
 * through it; the ends stay where the drag began and ended. */
static void
path_chaikin(struct PathLine* line)
{
    struct PathLine out = { 0 };

    if( line->count < 3 )
        return;
    path_line_push(&out, line->points[0].x, line->points[0].z);
    for( int i = 0; i + 1 < line->count; i++ )
    {
        struct PathPoint const a = line->points[i];
        struct PathPoint const b = line->points[i + 1];
        if( i > 0 )
            path_line_push(&out, 0.75 * a.x + 0.25 * b.x, 0.75 * a.z + 0.25 * b.z);
        if( i + 2 < line->count )
            path_line_push(&out, 0.25 * a.x + 0.75 * b.x, 0.25 * a.z + 0.75 * b.z);
    }
    path_line_push(&out, line->points[line->count - 1].x, line->points[line->count - 1].z);
    ToriMem_Free(ToriMem_Editor(), line->points);
    *line = out;
}

/* Points every `step` tiles along the line, the last one kept. */
static void
path_resample(
    struct PathLine* line,
    double step)
{
    struct PathLine out = { 0 };
    double carry = 0.0;

    assert(line->count > 0);
    path_line_push(&out, line->points[0].x, line->points[0].z);
    for( int i = 0; i + 1 < line->count; i++ )
    {
        double const dx = line->points[i + 1].x - line->points[i].x;
        double const dz = line->points[i + 1].z - line->points[i].z;
        double const length = sqrt(dx * dx + dz * dz);
        double at = step - carry;
        while( at <= length )
        {
            path_line_push(&out, line->points[i].x + dx * at / length, line->points[i].z + dz * at / length);
            at += step;
        }
        carry = length - (at - step);
    }
    if( out.points[out.count - 1].x != line->points[line->count - 1].x
        || out.points[out.count - 1].z != line->points[line->count - 1].z )
        path_line_push(&out, line->points[line->count - 1].x, line->points[line->count - 1].z);
    ToriMem_Free(ToriMem_Editor(), line->points);
    *line = out;
}

/* Dirt: a smoothed centreline that drifts sideways a little, each side's half
 * width breathing on its own. */
static void
path_dirt_line(
    const struct Editor_PathParams* params,
    struct PathLine* line)
{
    double const half = params->width / 2.0;
    double const rough = params->roughness;
    double s = 0.0;

    for( int pass = 0; pass < 3; pass++ )
        path_chaikin(line);
    path_resample(line, 0.25);
    for( int i = 0; i < line->count; i++ )
    {
        struct PathPoint* p = &line->points[i];
        int const a = i > 0 ? i - 1 : i;
        int const b = i + 1 < line->count ? i + 1 : i;
        double dx = line->points[b].x - line->points[a].x;
        double dz = line->points[b].z - line->points[a].z;
        double const length = sqrt(dx * dx + dz * dz);
        double drift;

        if( i > 0 )
        {
            double const ex = p->x - line->points[i - 1].x;
            double const ez = p->z - line->points[i - 1].z;
            s += sqrt(ex * ex + ez * ez);
        }
        if( length > 0.0 )
        {
            dx /= length;
            dz /= length;
        }
        /* The drift moves the centre along the normal (left of travel). */
        drift = 0.22 * rough * params->width * path_wander(params->seed + 101u, s, 6.0);
        p->left = half * (1.0 + 0.4 * rough * path_wander(params->seed + 202u, s, 3.5));
        p->right = half * (1.0 + 0.4 * rough * path_wander(params->seed + 303u, s, 3.5));
        if( p->left < 0.3 )
            p->left = 0.3;
        if( p->right < 0.3 )
            p->right = 0.3;
        p->x += -dz * drift;
        p->z += dx * drift;
    }
}

/* River: the dirt line's way, slower and wider. The bed meanders across a
 * width or more over a dozen tiles, both banks swell together with a ripple
 * of their own on each, and the curve is cut smoother (the content's
 * streams: no corner the drag made survives). */
static void
path_river_line(
    const struct Editor_PathParams* params,
    struct PathLine* line)
{
    double const half = params->width / 2.0;
    double const rough = params->roughness;
    double s = 0.0;

    for( int pass = 0; pass < 4; pass++ )
        path_chaikin(line);
    path_resample(line, 0.25);
    for( int i = 0; i < line->count; i++ )
    {
        struct PathPoint* p = &line->points[i];
        int const a = i > 0 ? i - 1 : i;
        int const b = i + 1 < line->count ? i + 1 : i;
        double dx = line->points[b].x - line->points[a].x;
        double dz = line->points[b].z - line->points[a].z;
        double const length = sqrt(dx * dx + dz * dz);
        double drift;
        double swell;

        if( i > 0 )
        {
            double const ex = p->x - line->points[i - 1].x;
            double const ez = p->z - line->points[i - 1].z;
            s += sqrt(ex * ex + ez * ez);
        }
        if( length > 0.0 )
        {
            dx /= length;
            dz /= length;
        }
        drift = 0.45 * rough * params->width * path_wander(params->seed + 404u, s, 12.0);
        swell = 0.3 * rough * path_wander(params->seed + 505u, s, 9.0);
        p->left = half * (1.0 + swell + 0.15 * rough * path_wander(params->seed + 606u, s, 4.0));
        p->right = half * (1.0 + swell + 0.15 * rough * path_wander(params->seed + 707u, s, 4.0));
        if( p->left < 0.4 )
            p->left = 0.4;
        if( p->right < 0.4 )
            p->right = 0.4;
        p->x += -dz * drift;
        p->z += dx * drift;
    }
}

/* Douglas-Peucker on the drag's tile centres: the corners the hand meant. */
static void
path_simplify(
    const struct PathLine* in,
    int first,
    int last,
    double tolerance,
    unsigned char* keep)
{
    double worst = -1.0;
    int at = -1;
    double const ax = in->points[first].x;
    double const az = in->points[first].z;
    double const dx = in->points[last].x - ax;
    double const dz = in->points[last].z - az;
    double const length = sqrt(dx * dx + dz * dz);

    for( int i = first + 1; i < last; i++ )
    {
        double const px = in->points[i].x - ax;
        double const pz = in->points[i].z - az;
        double const d = length > 0.0 ? fabs(px * dz - pz * dx) / length : sqrt(px * px + pz * pz);
        if( d > worst )
        {
            worst = d;
            at = i;
        }
    }
    if( at >= 0 && worst > tolerance )
    {
        keep[at] = 1;
        path_simplify(in, first, at, tolerance, keep);
        path_simplify(in, at, last, tolerance, keep);
    }
}

/* Pavement: the drag's corners, each leg turned to the nearest 45 degrees and
 * stepped in whole tiles from an edge-aligned start, so straight edges run on
 * tile edges (or tile middles: the half squares) and diagonal ones corner to
 * corner. */
static void
path_paved_line(
    const struct Editor_PathParams* params,
    struct PathLine* line)
{
    struct PathLine out = { 0 };
    unsigned char* keep;
    double const half = params->width / 2.0;
    double x;
    double z;

    keep = ToriMem_Calloc(ToriMem_Editor(), (size_t)line->count, 1);
    assert(keep);
    keep[0] = 1;
    keep[line->count - 1] = 1;
    if( line->count > 2 )
        path_simplify(line, 0, line->count - 1, 1.0, keep);

    /* The start stays on its tile's middle along the first leg; across it,
     * the edges go on whole tiles (a whole width fills whole rows, a half one
     * ends in a row of half squares). A diagonal first leg keeps the middle:
     * x - z is whole there, which puts the edges corner to corner. */
    x = line->points[0].x;
    z = line->points[0].z;
    for( int i = 1; i < line->count; i++ )
    {
        double const dx = line->points[i].x - x;
        double const dz = line->points[i].z - z;
        double angle;
        if( !keep[i] || (dx == 0.0 && dz == 0.0) )
            continue;
        angle = floor(atan2(dz, dx) / (PATH_PI / 4.0) + 0.5) * (PATH_PI / 4.0);
        if( fabs(round(sin(angle))) < 0.5 )
            z = floor(z - half + 0.5) + half;
        else if( fabs(round(cos(angle))) < 0.5 )
            x = floor(x - half + 0.5) + half;
        break;
    }
    path_line_push(&out, x, z);
    for( int i = 1; i < line->count; i++ )
    {
        double dx;
        double dz;
        double angle;
        double ux;
        double uz;
        double steps;

        if( !keep[i] )
            continue;
        dx = line->points[i].x - x;
        dz = line->points[i].z - z;
        if( dx == 0.0 && dz == 0.0 )
            continue;
        angle = floor(atan2(dz, dx) / (PATH_PI / 4.0) + 0.5) * (PATH_PI / 4.0);
        ux = round(cos(angle));
        uz = round(sin(angle));
        /* Whole steps along the snapped direction ((1, 1) is one diagonal
         * step), at least one. */
        steps = floor((dx * ux + dz * uz) / (ux * ux + uz * uz) + 0.5);
        if( steps < 1.0 )
            continue;
        x += ux * steps;
        z += uz * steps;
        /* A leg that carries on the last one's direction extends it. */
        if( out.count >= 2 )
        {
            struct PathPoint const* a = &out.points[out.count - 2];
            struct PathPoint* b = &out.points[out.count - 1];
            double const px = b->x - a->x;
            double const pz = b->z - a->z;
            if( fabs(px * uz - pz * ux) < 1e-9 && px * ux + pz * uz > 0.0 )
            {
                b->x = x;
                b->z = z;
                continue;
            }
        }
        path_line_push(&out, x, z);
    }
    ToriMem_Free(ToriMem_Editor(), keep);
    ToriMem_Free(ToriMem_Editor(), line->points);
    *line = out;
}

/* ---- rasterising -------------------------------------------------------------- */

struct PathGrid
{
    int x0;
    int z0;
    int w;
    int h;
    uint64_t* tiles;
};

/* Every sample whose centre lies within the box gets BODY asked of it (SX,
 * SZ: the sample in world tiles); the ones it answers 1 for are set. World
 * tiles are never negative, so neither is a sample index. */
#define PATH_FOR_SAMPLES(grid, min_x, min_z, max_x, max_z, SX, SZ, BODY)                                       do                                                                                                           {                                                                                                                int const c0_ = (int)floor((min_x) * EDITOR_PATH_MASK);                                                      int const c1_ = (int)ceil((max_x) * EDITOR_PATH_MASK);                                                       int const r0_ = (int)floor((min_z) * EDITOR_PATH_MASK);                                                      int const r1_ = (int)ceil((max_z) * EDITOR_PATH_MASK);                                                       for( int r_ = r0_ < 0 ? 0 : r0_; r_ <= r1_; r_++ )                                                               for( int c_ = c0_ < 0 ? 0 : c0_; c_ <= c1_; c_++ )                                                           {                                                                                                                int const tx_ = c_ / EDITOR_PATH_MASK - (grid)->x0;                                                          int const tz_ = r_ / EDITOR_PATH_MASK - (grid)->z0;                                                          double const SX = ((double)c_ + 0.5) / EDITOR_PATH_MASK;                                                     double const SZ = ((double)r_ + 0.5) / EDITOR_PATH_MASK;                                                     if( tx_ < 0 || tz_ < 0 || tx_ >= (grid)->w || tz_ >= (grid)->h )                                                 continue;                                                                                                if( BODY )                                                                                                       (grid)->tiles[tz_ * (grid)->w + tx_] |=                                                                          (uint64_t)1 << ((r_ % EDITOR_PATH_MASK) * EDITOR_PATH_MASK + c_ % EDITOR_PATH_MASK);             }                                                                                                    } while( 0 )

/* Dirt: a sample is in when it is within the side's half width of a segment
 * (the ends of a segment are round, so the bends close up). */
static int
path_dirt_inside(
    const struct PathPoint* a,
    const struct PathPoint* b,
    double x,
    double z)
{
    double const dx = b->x - a->x;
    double const dz = b->z - a->z;
    double const length2 = dx * dx + dz * dz;
    double t = length2 > 0.0 ? ((x - a->x) * dx + (z - a->z) * dz) / length2 : 0.0;
    double qx;
    double qz;
    double d2;
    double side;
    double reach;

    if( t < 0.0 )
        t = 0.0;
    if( t > 1.0 )
        t = 1.0;
    qx = a->x + dx * t;
    qz = a->z + dz * t;
    d2 = (x - qx) * (x - qx) + (z - qz) * (z - qz);
    side = dx * (z - a->z) - dz * (x - a->x);
    reach = side >= 0.0 ? a->left + (b->left - a->left) * t : a->right + (b->right - a->right) * t;
    return d2 <= reach * reach;
}

/* Pavement: a sample is in a leg's rectangle; a leg is lengthened at a bend
 * by the mitre the turn needs, and by half a tile at the stroke's two ends. */
static int
path_paved_inside(
    double ax,
    double az,
    double ux,
    double uz,
    double length,
    double half,
    double ext0,
    double ext1,
    double x,
    double z)
{
    double const along = (x - ax) * ux + (z - az) * uz;
    double const across = -(x - ax) * uz + (z - az) * ux;
    return along >= -ext0 && along <= length + ext1 && fabs(across) <= half;
}

static double
path_mitre(
    const struct PathPoint* a,
    const struct PathPoint* b,
    const struct PathPoint* c,
    double half)
{
    double const ux = b->x - a->x;
    double const uz = b->z - a->z;
    double const vx = c->x - b->x;
    double const vz = c->z - b->z;
    double const lu = sqrt(ux * ux + uz * uz);
    double const lv = sqrt(vx * vx + vz * vz);
    double cosine;
    double turn;

    if( lu <= 0.0 || lv <= 0.0 )
        return 0.0;
    cosine = (ux * vx + uz * vz) / (lu * lv);
    if( cosine > 1.0 )
        cosine = 1.0;
    if( cosine < -1.0 )
        cosine = -1.0;
    turn = acos(cosine);
    /* Past a right angle the mitre runs away; a square end is enough. */
    if( turn > PATH_PI / 2.0 + 1e-6 )
        return half;
    return half * tan(turn / 2.0);
}

/* A diagonal leg's half width rounded so its edges run corner to corner: the
 * leg's middle is on a tile-corner diagonal (x - z whole: the start's edges
 * are on whole tiles and a diagonal step is (1, 1)), and those diagonals are
 * 1/sqrt(2) apart across the leg. */
static double
path_paved_half(
    double half,
    int diagonal)
{
    double steps;
    if( !diagonal )
        return half;
    steps = floor(half * sqrt(2.0) + 0.5);
    if( steps < 1.0 )
        steps = 1.0;
    return steps / sqrt(2.0);
}

/* ---- fitting the tiles together: editor/editor_region.c ----------------------- */

/* The grid planned as a region: ADD at hardness 50, the tiles that already
 * hold the overlay joined. A path never takes an overlay off. */
static int
path_settle(
    enum Editor_PathStyle style,
    const struct PathGrid* grid,
    Editor_PathExistingFn existing,
    void* user,
    struct Editor_PathTile* out,
    int max)
{
    struct Editor_RegionParams params;
    struct Editor_RegionGrid region;
    struct Editor_RegionTile* plan;
    int planned;
    int written = 0;

    params.set = path_set(style);
    params.op = EDITOR_REGION_ADD;
    params.hardness = EDITOR_REGION_HARDNESS_DEFAULT;
    params.misfit_cap = 0;
    region.x0 = grid->x0;
    region.z0 = grid->z0;
    region.w = grid->w;
    region.h = grid->h;
    region.tiles = grid->tiles;
    plan = ToriMem_Alloc(ToriMem_Editor(), sizeof(*plan) * (size_t)(max > 0 ? max : 1));
    assert(plan);
    planned = Editor_RegionPlan(&params, &region, existing, user, plan, max);
    for( int i = 0; i < planned; i++ )
    {
        assert(plan[i].shape >= 0);
        out[written].world_x = plan[i].world_x;
        out[written].world_z = plan[i].world_z;
        out[written].shape = plan[i].shape;
        out[written].rotation = plan[i].rotation;
        out[written].coverage = plan[i].coverage;
        written++;
    }
    ToriMem_Free(ToriMem_Editor(), plan);
    return written;
}

/* The path's coverage: the line through the drag, shaped by the style, and
 * every sample within it set. 1 with grid->tiles allocated (the caller frees
 * them); 0 for no points or a path wider than EDITOR_PATH_SPAN_MAX. */
static int
path_build_grid(
    const struct Editor_PathParams* params,
    const int* xs,
    const int* zs,
    int count,
    struct PathGrid* grid_out)
{
    struct PathLine line = { 0 };
    struct PathGrid grid;
    double min_x = 1e18;
    double min_z = 1e18;
    double max_x = -1e18;
    double max_z = -1e18;
    double reach;

    assert(params);
    assert(params->style >= 0);
    assert(params->style < EDITOR_PATH_STYLE_COUNT);
    assert(params->width >= 0.5);
    assert(params->width <= EDITOR_PATH_WIDTH_MAX);
    assert(grid_out);
    memset(grid_out, 0, sizeof(*grid_out));
    if( count <= 0 )
        return 0;
    assert(xs);
    assert(zs);
    assert(count <= EDITOR_PATH_POINTS_MAX);

    for( int i = 0; i < count; i++ )
        if( line.count == 0 || line.points[line.count - 1].x != xs[i] + 0.5
            || line.points[line.count - 1].z != zs[i] + 0.5 )
            path_line_push(&line, xs[i] + 0.5, zs[i] + 0.5);
    if( params->style == EDITOR_PATH_DIRT )
        path_dirt_line(params, &line);
    else if( params->style == EDITOR_PATH_RIVER )
        path_river_line(params, &line);
    else
        path_paved_line(params, &line);

    /* The box every sample that can be in lies within. */
    reach = params->width
                * (params->style == EDITOR_PATH_DIRT    ? 0.5 * (1.0 + 0.4 * params->roughness) + 0.25
                   : params->style == EDITOR_PATH_RIVER ? 0.5 * (1.0 + 0.45 * params->roughness) + 0.25
                                                        : 0.75)
            + 1.0;
    for( int i = 0; i < line.count; i++ )
    {
        min_x = line.points[i].x < min_x ? line.points[i].x : min_x;
        min_z = line.points[i].z < min_z ? line.points[i].z : min_z;
        max_x = line.points[i].x > max_x ? line.points[i].x : max_x;
        max_z = line.points[i].z > max_z ? line.points[i].z : max_z;
    }
    grid.x0 = (int)floor(min_x - reach);
    grid.z0 = (int)floor(min_z - reach);
    grid.x0 = grid.x0 < 0 ? 0 : grid.x0;
    grid.z0 = grid.z0 < 0 ? 0 : grid.z0;
    grid.w = (int)ceil(max_x + reach) - grid.x0 + 1;
    grid.h = (int)ceil(max_z + reach) - grid.z0 + 1;
    if( grid.w > EDITOR_PATH_SPAN_MAX || grid.h > EDITOR_PATH_SPAN_MAX )
    {
        ToriMem_Free(ToriMem_Editor(), line.points);
        return 0;
    }
    grid.tiles = ToriMem_Calloc(ToriMem_Editor(), (size_t)grid.w * (size_t)grid.h, sizeof(*grid.tiles));
    assert(grid.tiles);

    if( params->style != EDITOR_PATH_PAVED )
    {
        if( line.count == 1 )
        {
            struct PathPoint const* p = &line.points[0];
            double const r = p->left > p->right ? p->left : p->right;
            PATH_FOR_SAMPLES(&grid, p->x - r, p->z - r, p->x + r, p->z + r, sx, sz, path_dirt_inside(p, p, sx, sz));
        }
        for( int i = 0; i + 1 < line.count; i++ )
        {
            struct PathPoint const* a = &line.points[i];
            struct PathPoint const* b = &line.points[i + 1];
            double r = a->left;
            r = a->right > r ? a->right : r;
            r = b->left > r ? b->left : r;
            r = b->right > r ? b->right : r;
            PATH_FOR_SAMPLES(&grid, (a->x < b->x ? a->x : b->x) - r, (a->z < b->z ? a->z : b->z) - r,
                (a->x > b->x ? a->x : b->x) + r, (a->z > b->z ? a->z : b->z) + r, sx, sz,
                path_dirt_inside(a, b, sx, sz));
        }
    }
    else
    {
        double const half = params->width / 2.0;
        if( line.count == 1 )
        {
            struct PathPoint const* p = &line.points[0];
            PATH_FOR_SAMPLES(&grid, p->x - half, p->z - half, p->x + half, p->z + half, sx, sz,
                fabs(sx - p->x) <= half && fabs(sz - p->z) <= half);
        }
        for( int i = 0; i + 1 < line.count; i++ )
        {
            struct PathPoint const* a = &line.points[i];
            struct PathPoint const* b = &line.points[i + 1];
            double const dx = b->x - a->x;
            double const dz = b->z - a->z;
            double const length = sqrt(dx * dx + dz * dz);
            int const diagonal = dx != 0.0 && dz != 0.0;
            double const leg_half = path_paved_half(half, diagonal);
            /* A bend's mitre; the stroke's own ends reach to the end tiles'
             * far edges, so the tiles clicked are paved. */
            double const ext0 = i > 0 ? path_mitre(&line.points[i - 1], a, b, leg_half) : 0.5;
            double const ext1 = i + 2 < line.count ? path_mitre(a, b, &line.points[i + 2], leg_half) : 0.5;
            double const ux = length > 0.0 ? dx / length : 1.0;
            double const uz = length > 0.0 ? dz / length : 0.0;
            double const r = leg_half + (ext0 > ext1 ? ext0 : ext1) + 0.01;
            PATH_FOR_SAMPLES(&grid, (a->x < b->x ? a->x : b->x) - r, (a->z < b->z ? a->z : b->z) - r,
                (a->x > b->x ? a->x : b->x) + r, (a->z > b->z ? a->z : b->z) + r, sx, sz,
                path_paved_inside(a->x, a->z, ux, uz, length, leg_half, ext0, ext1, sx, sz));
        }
    }

    ToriMem_Free(ToriMem_Editor(), line.points);
    *grid_out = grid;
    return 1;
}

int
Editor_PathPlan(
    const struct Editor_PathParams* params,
    const int* xs,
    const int* zs,
    int count,
    Editor_PathExistingFn existing,
    void* user,
    struct Editor_PathTile* out,
    int max)
{
    struct PathGrid grid;
    int written;

    assert(out);
    if( !path_build_grid(params, xs, zs, count, &grid) )
        return 0;
    written = path_settle(params->style, &grid, existing, user, out, max);
    ToriMem_Free(ToriMem_Editor(), grid.tiles);
    return written;
}

/* Whether sample (row, column) of tile (tx, tz) of the grid is set; outside
 * the grid, no. */
static int
path_sample(
    const struct PathGrid* grid,
    int tx,
    int tz,
    int row,
    int column)
{
    if( tx < 0 || tz < 0 || tx >= grid->w || tz >= grid->h )
        return 0;
    return (int)(grid->tiles[(size_t)tz * grid->w + tx] >> (row * EDITOR_PATH_MASK + column) & 1);
}

int
Editor_PathCorners(
    const struct Editor_PathParams* params,
    const int* xs,
    const int* zs,
    int count,
    int* out_x,
    int* out_z,
    int max)
{
    struct PathGrid grid;
    int written = 0;

    assert(out_x);
    assert(out_z);
    if( !path_build_grid(params, xs, zs, count, &grid) )
        return 0;
    /* Corner (cx, cz) of the grid touches the NE sample of tile (cx-1, cz-1),
     * the NW of (cx, cz-1), the SE of (cx-1, cz) and the SW of (cx, cz). */
    for( int cz = 0; cz <= grid.h && written < max; cz++ )
        for( int cx = 0; cx <= grid.w && written < max; cx++ )
        {
            int const top = EDITOR_PATH_MASK - 1;
            int const in = path_sample(&grid, cx - 1, cz - 1, top, top) + path_sample(&grid, cx, cz - 1, top, 0)
                           + path_sample(&grid, cx - 1, cz, 0, top) + path_sample(&grid, cx, cz, 0, 0);
            if( in < 2 )
                continue;
            out_x[written] = grid.x0 + cx;
            out_z[written] = grid.z0 + cz;
            written++;
        }
    ToriMem_Free(ToriMem_Editor(), grid.tiles);
    return written;
}
