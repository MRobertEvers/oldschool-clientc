#include "editor/editor_path.h"

#include "mem/torirs_mem.h"

#include <assert.h>
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

#define PATH_SHAPES 12
#define PATH_PI 3.14159265358979323846

static uint64_t g_masks[PATH_SHAPES][4];
static int g_masks_ready;

/* The client turns an edge-middle or quarter vertex type with the tile; the
 * corners stay and the faces' corner indices turn instead. */
static int
path_rotate_type(
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
path_in_triangle(
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
path_build_mask(
    int shape,
    int rotation)
{
    int const row = shape + 1;
    double pos[6][2];
    uint64_t mask = 0;

    for( int v = 0; v < k_vertex_counts[row]; v++ )
    {
        int const type = path_rotate_type(k_vertex_types[row][v], rotation);
        pos[v][0] = k_type_pos[type][0];
        pos[v][1] = k_type_pos[type][1];
    }
    for( int r = 0; r < EDITOR_PATH_MASK; r++ )
        for( int c = 0; c < EDITOR_PATH_MASK; c++ )
        {
            double const x = (c + 0.5) * 128.0 / EDITOR_PATH_MASK;
            double const z = (r + 0.5) * 128.0 / EDITOR_PATH_MASK;
            for( int f = 0; f < k_face_counts[row]; f++ )
            {
                const int* face = &k_faces[row][f * 4];
                int index[3];
                if( face[0] != 1 )
                    continue;
                for( int k = 0; k < 3; k++ )
                    index[k] = face[1 + k] < 4 ? (face[1 + k] - rotation) & 3 : face[1 + k];
                if( path_in_triangle(pos[index[0]], pos[index[1]], pos[index[2]], x, z) )
                {
                    mask |= (uint64_t)1 << (r * EDITOR_PATH_MASK + c);
                    break;
                }
            }
        }
    return mask;
}

static void
path_masks_init(void)
{
    if( g_masks_ready )
        return;
    for( int s = 0; s < PATH_SHAPES; s++ )
        for( int r = 0; r < 4; r++ )
            g_masks[s][r] = path_build_mask(s, r);
    g_masks_ready = 1;
}

uint64_t
Editor_PathShapeMask(
    int shape,
    int rotation)
{
    assert(shape >= 0);
    assert(shape < PATH_SHAPES);
    assert(rotation >= 0);
    assert(rotation < 4);
    path_masks_init();
    return g_masks[shape][rotation];
}

static int
path_popcount(uint64_t v)
{
    int n = 0;
    while( v )
    {
        v &= v - 1;
        n++;
    }
    return n;
}

/* The shapes a style may write, in tie order: the full tile, the half
 * diagonal, the half square first. Pavement keeps to the straight ones and
 * the corners a 45-degree bend needs; dirt has every shape the content's
 * tracks use (not 11, the trapezium). */
static const int k_dirt_shapes[] = { 0, 1, 6, 4, 5, 2, 3, 8, 7, 10, 9 };
static const int k_paved_shapes[] = { 0, 1, 6, 8, 7 };

int
Editor_PathFit(
    enum Editor_PathStyle style,
    uint64_t mask,
    int* out_shape,
    int* out_rotation)
{
    const int* shapes = style == EDITOR_PATH_PAVED ? k_paved_shapes : k_dirt_shapes;
    int const shape_count = style == EDITOR_PATH_PAVED ? (int)(sizeof(k_paved_shapes) / sizeof(k_paved_shapes[0]))
                                                       : (int)(sizeof(k_dirt_shapes) / sizeof(k_dirt_shapes[0]));
    int best = path_popcount(mask);
    int found = 0;

    assert(style >= 0);
    assert(style < EDITOR_PATH_STYLE_COUNT);
    assert(out_shape);
    assert(out_rotation);
    path_masks_init();
    for( int i = 0; i < shape_count; i++ )
        for( int r = 0; r < (shapes[i] == 0 ? 1 : 4); r++ )
        {
            int const cost = path_popcount(mask ^ g_masks[shapes[i]][r]);
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

/* ---- fitting the tiles together --------------------------------------------- */

/* A coverage's samples along each side, 8 bits west to east / south to north. */
static unsigned
path_side_south(uint64_t m)
{
    return (unsigned)(m & 0xff);
}

static unsigned
path_side_north(uint64_t m)
{
    return (unsigned)(m >> 56 & 0xff);
}

static unsigned
path_side_column(
    uint64_t m,
    int column)
{
    unsigned out = 0;
    for( int r = 0; r < EDITOR_PATH_MASK; r++ )
        out |= (unsigned)(m >> (r * EDITOR_PATH_MASK + column) & 1) << r;
    return out;
}

/* How far a tile's sides are from its neighbours' facing sides: a step where
 * a path's edge leaves one tile and does not carry on into the next. Only
 * between tiles the path touches, or against one that already holds the
 * overlay: against bare ground a side is the path's own edge. */
static int
path_seams(
    uint64_t m,
    const uint64_t* target,
    const uint64_t* chosen,
    const struct PathGrid* grid,
    int tx,
    int tz)
{
    int seams = 0;
    size_t at;
    if( tx > 0 && (target[at = (size_t)tz * grid->w + tx - 1] || chosen[at]) )
        seams += path_popcount(path_side_column(m, 0) ^ path_side_column(chosen[at], 7));
    if( tx + 1 < grid->w && (target[at = (size_t)tz * grid->w + tx + 1] || chosen[at]) )
        seams += path_popcount(path_side_column(m, 7) ^ path_side_column(chosen[at], 0));
    if( tz > 0 && (target[at = (size_t)(tz - 1) * grid->w + tx] || chosen[at]) )
        seams += path_popcount(path_side_south(m) ^ path_side_north(chosen[at]));
    if( tz + 1 < grid->h && (target[at = (size_t)(tz + 1) * grid->w + tx] || chosen[at]) )
        seams += path_popcount(path_side_north(m) ^ path_side_south(chosen[at]));
    return seams;
}

/* The seams weigh this much against a sample of coverage: enough that a tile
 * whose edge would step out of line with both neighbours takes the shape that
 * carries their edge on, not enough to move an edge off the path. Chosen by
 * eye on editor_path_test's dirt curve: at 0 and 1.25 whole tiles stick out
 * of a sloping edge every few tiles, at 2.5 the slope runs on, at 4 edges
 * start to spike out where no path is. */
#define PATH_SEAM_WEIGHT 2.5
#define PATH_SETTLE_PASSES 4

/* Each touched tile's shape: first the best cover on its own, then a few
 * passes in which each tile may change to whatever best covers it AND meets
 * its neighbours (a tile the stroke did not touch is a fixed neighbour: bare,
 * or the overlay it holds). Writes the touched tiles that end up covered. */
static int
path_settle(
    enum Editor_PathStyle style,
    const struct PathGrid* grid,
    Editor_PathExistingFn existing,
    void* user,
    struct Editor_PathTile* out,
    int max)
{
    const int* shapes = style == EDITOR_PATH_PAVED ? k_paved_shapes : k_dirt_shapes;
    int const shape_count = style == EDITOR_PATH_PAVED ? (int)(sizeof(k_paved_shapes) / sizeof(k_paved_shapes[0]))
                                                       : (int)(sizeof(k_dirt_shapes) / sizeof(k_dirt_shapes[0]));
    size_t const n = (size_t)grid->w * (size_t)grid->h;
    uint64_t* target = ToriMem_Alloc(ToriMem_Editor(), n * sizeof(*target));
    uint64_t* chosen = ToriMem_Alloc(ToriMem_Editor(), n * sizeof(*chosen));
    signed char* shape_of = ToriMem_Alloc(ToriMem_Editor(), n);
    signed char* rotation_of = ToriMem_Alloc(ToriMem_Editor(), n);
    int written = 0;

    assert(target);
    assert(chosen);
    assert(shape_of);
    assert(rotation_of);
    for( int tz = 0; tz < grid->h; tz++ )
        for( int tx = 0; tx < grid->w; tx++ )
        {
            size_t const at = (size_t)tz * grid->w + tx;
            int had_shape;
            int had_rotation;
            int const had = existing && existing(user, grid->x0 + tx, grid->z0 + tz, &had_shape, &had_rotation);
            uint64_t const was = had ? Editor_PathShapeMask(had_shape, had_rotation) : 0;
            int shape;
            int rotation;

            target[at] = grid->tiles[at] ? grid->tiles[at] | was : 0;
            /* Untouched: a fixed neighbour, as it is. */
            chosen[at] = was;
            shape_of[at] = -1;
            rotation_of[at] = 0;
            if( target[at] && Editor_PathFit(style, target[at], &shape, &rotation) )
            {
                chosen[at] = Editor_PathShapeMask(shape, rotation);
                shape_of[at] = (signed char)shape;
                rotation_of[at] = (signed char)rotation;
            }
            else if( target[at] )
                chosen[at] = 0;
        }

    for( int pass = 0; pass < PATH_SETTLE_PASSES; pass++ )
    {
        int changes = 0;
        for( int tz = 0; tz < grid->h; tz++ )
            for( int tx = 0; tx < grid->w; tx++ )
            {
                size_t const at = (size_t)tz * grid->w + tx;
                double best;
                int best_shape = -1;
                int best_rotation = 0;
                uint64_t best_mask = 0;

                if( !target[at] )
                    continue;
                best = path_popcount(target[at]) + PATH_SEAM_WEIGHT * path_seams(0, target, chosen, grid, tx, tz);
                for( int i = 0; i < shape_count; i++ )
                    for( int r = 0; r < (shapes[i] == 0 ? 1 : 4); r++ )
                    {
                        uint64_t const m = g_masks[shapes[i]][r];
                        double const cost =
                            path_popcount(target[at] ^ m) + PATH_SEAM_WEIGHT * path_seams(m, target, chosen, grid, tx, tz);
                        if( cost < best - 1e-9 )
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
            if( !target[at] || shape_of[at] < 0 || written >= max )
                continue;
            out[written].world_x = grid->x0 + tx;
            out[written].world_z = grid->z0 + tz;
            out[written].shape = shape_of[at];
            out[written].rotation = rotation_of[at];
            out[written].coverage = path_popcount(target[at]);
            written++;
        }
    ToriMem_Free(ToriMem_Editor(), target);
    ToriMem_Free(ToriMem_Editor(), chosen);
    ToriMem_Free(ToriMem_Editor(), shape_of);
    ToriMem_Free(ToriMem_Editor(), rotation_of);
    return written;
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
    struct PathLine line = { 0 };
    struct PathGrid grid;
    double min_x = 1e18;
    double min_z = 1e18;
    double max_x = -1e18;
    double max_z = -1e18;
    double reach;
    int written = 0;

    assert(params);
    assert(params->style >= 0);
    assert(params->style < EDITOR_PATH_STYLE_COUNT);
    assert(params->width >= 0.5);
    assert(params->width <= EDITOR_PATH_WIDTH_MAX);
    assert(out);
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
    else
        path_paved_line(params, &line);

    /* The box every sample that can be in lies within. */
    reach = params->width * (params->style == EDITOR_PATH_DIRT ? 0.5 * (1.0 + 0.4 * params->roughness) + 0.25 : 0.75) + 1.0;
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

    if( params->style == EDITOR_PATH_DIRT )
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

    written = path_settle(params->style, &grid, existing, user, out, max);
    ToriMem_Free(ToriMem_Editor(), grid.tiles);
    ToriMem_Free(ToriMem_Editor(), line.points);
    return written;
}
