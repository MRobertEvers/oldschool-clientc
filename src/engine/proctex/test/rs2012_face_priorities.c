/*
 * Face-priority authoring for models imported from a z-buffered client.
 *
 * ToriDraw resolves visibility with a per-face painter's sort, not a depth
 * buffer. A model authored against RS727's z-buffer carries no draw order to
 * recover: its surfaces interleave freely, and a centroid depth sort cannot
 * keep a thin decorative plate lying on a neck in front of the neck, or a claw
 * ring in front of the arm it grips. Face render priorities are the only lever
 * the sorter exposes, and there are exactly twelve of them.
 *
 * WHAT THE SORTER DOES WITH THEM (toridraw_render.u.c sort_face_draw_order),
 * because it decides everything below. Emitted back to front:
 *
 *     flexible faces deeper than avg(prio 1,2)
 *     prio 0, 1, 2                <- fixed band, each internally depth sorted
 *     flexible faces deeper than avg(prio 3,4)
 *     prio 3, 4
 *     flexible faces deeper than avg(prio 6,8)
 *     prio 5, 6, 7, 8, 9
 *     remaining flexible faces
 *
 * Priorities 0..9 are ten HARD bands: every face in band N paints over every
 * face in band N-1 whatever their depths, and inside a band the depth sort
 * still runs. Priorities 10 and 11 are the FLEXIBLE band, purely depth sorted
 * and spliced into the fixed run at three depth averages.
 *
 * Two rules follow, and this tool exists to enforce them:
 *
 *   1. A feature lives in exactly one band. Splitting one surface across two
 *      bands does not refine its order, it forbids its own halves from
 *      interleaving, so the far half paints over the near half. Bands separate
 *      features; depth orders faces within one.
 *
 *   2. A band separation is a claim that holds from EVERY camera angle. Two
 *      features that swap places as the model turns (a left and a right wing)
 *      must share a band, or one of them is wrong at half the angles.
 *
 * So the bands are not guessed from names or bounding boxes -- they are
 * measured, and the thing measured is not "which feature is in front".
 * Asking that merges the whole dragon into one band, and rightly: a spike
 * standing out of a neck is in front of the neck from one side and behind it
 * from the other, so no band can hold it. Almost every pair of features on a
 * closed creature answers "both".
 *
 * The question that pays is narrower. The depth sort already resolves the
 * overwhelming majority of pixels correctly. A band is worth spending only
 * where it FIXES pixels the depth sort gets wrong, and every band also BREAKS
 * the pixels where the loser legitimately wins. So each ordered pair is scored
 * by rendering the model from a sphere of viewpoints and counting, per pixel:
 *
 *   fixable[A][B]   A should win here, B is what the depth sort paints last
 *   breakable[A][B] B should win here and does; putting A over B destroys it
 *
 * net(A over B) = fixable - breakable. What that number must NOT be used for
 * is deciding the pair on its own. A band is not a pairwise promise: putting A
 * one band above B puts it above every other feature in B's band too,
 * including the 2,258-face body it was never compared against. Authoring pair
 * by pair reads as a win on every pair and lands a small spike in front of the
 * whole dragon -- measured here at 4.5% wrong pixels going to 7.4%, with the
 * mean depth error of an error rising from 9 units to 80.
 *
 * So the assignment is solved globally: choose a band per feature to maximise
 *
 *     sum over pairs with band(A) > band(B) of net(A over B)
 *
 * by hill climbing from "everything in band 0", which is exactly today's pure
 * depth sort and therefore a baseline the result cannot score below. A pair
 * that swaps as the model turns scores its own conflict away and is never
 * separated -- rule 2 falls out of the arithmetic instead of being imposed on
 * top of it.
 *
 * The measurement depends on the assignment (it asks what the sort paints
 * today) and the assignment depends on the measurement, so the two alternate
 * until the bands stop moving. Round one measures the pure depth sort.
 *
 * The input .ob3 is never written. Analysis runs over all inputs together --
 * an npc's model1 and model2 are merged before they are drawn, so their bands
 * have to be chosen in one coordinate system -- and each output gets its own
 * slice of the result.
 *
 * Build and run:
 *   make -C src rs2012-face-priorities
 *   src/build/rs2012_face_priorities --in head.ob3 --in body.ob3 --report \
 *       --out head.prio.ob3 --out body.prio.ob3
 */

#include "datatypes/model.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUTS 16
#define MAX_FEATURES 2048
#define HARD_BANDS 10

/* -std=c11 hides M_PI; the view sweep is the only thing that wants it. */
#define PRIO_PI 3.14159265358979323846

/* --------------------------------------------------------------- inputs -- */

struct input
{
    const char* in_path;
    const char* out_path;
    struct RSCache_Model* model;
    struct RSCache_ModelProvenance* provenance;
    int vertex_base;
    int face_base;
};

/* Concatenated geometry across every input, in input order, so a face index
 * here maps back to (input, local face) by the recorded bases. */
struct geometry
{
    int vertex_count;
    int face_count;
    int* vx;
    int* vy;
    int* vz;
    int* fa;
    int* fb;
    int* fc;
    int* face_feature;
};

struct feature
{
    int face_count;
    int band;
};

/** What the sort gets wrong, and how much of it a band could ever reach. */
struct tally
{
    long covered;
    long wrong_within;  /* one feature sorting wrongly against itself */
    long wrong_between; /* two features: the part priorities can address */
};

/* ------------------------------------------------------------------ io --- */

static uint8_t*
read_file(const char* path, long* out_size)
{
    FILE* f = fopen(path, "rb");
    uint8_t* bytes;
    long size;

    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 )
    {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if( size <= 0 || fseek(f, 0, SEEK_SET) != 0 )
    {
        fclose(f);
        return NULL;
    }
    bytes = (uint8_t*)malloc((size_t)size);
    if( !bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size )
    {
        free(bytes);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return bytes;
}

static bool
write_file(const char* path, const uint8_t* bytes, uint32_t size)
{
    FILE* f = fopen(path, "wb");
    bool ok;

    if( !f )
        return false;
    ok = fwrite(bytes, 1, size, f) == size;
    fclose(f);
    return ok;
}

/* ----------------------------------------------------------- union find -- */

static int
uf_find(int* parent, int x)
{
    while( parent[x] != x )
    {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

static void
uf_union(int* parent, int a, int b)
{
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if( a != b )
        parent[b] = a;
}

/* --------------------------------------------------------- segmentation -- */

struct weld_key
{
    int64_t packed;
    int vertex;
};

static int
cmp_weld_key(const void* a, const void* b)
{
    int64_t ka = ((const struct weld_key*)a)->packed;
    int64_t kb = ((const struct weld_key*)b)->packed;
    return ka < kb ? -1 : (ka > kb ? 1 : 0);
}

/**
 * Split the geometry into connected features.
 *
 * Faces sharing a vertex index are one surface -- that is what "one feature"
 * meant to whoever built it. `weld` additionally unions vertices at identical
 * coordinates, because an imported model routinely duplicates a seam's
 * vertices and the halves must not be able to land in different bands.
 */
static int
segment(struct geometry* g, bool weld)
{
    int* parent = (int*)malloc((size_t)g->vertex_count * sizeof(int));
    int* remap = (int*)malloc((size_t)g->vertex_count * sizeof(int));
    int count = 0;

    if( !parent || !remap )
    {
        free(parent);
        free(remap);
        return 0;
    }
    for( int v = 0; v < g->vertex_count; v++ )
        parent[v] = v;

    if( weld )
    {
        struct weld_key* keys =
            (struct weld_key*)malloc((size_t)g->vertex_count * sizeof(struct weld_key));
        if( keys )
        {
            for( int v = 0; v < g->vertex_count; v++ )
            {
                int64_t x = (int64_t)(g->vx[v] + 32768);
                int64_t y = (int64_t)(g->vy[v] + 32768);
                int64_t z = (int64_t)(g->vz[v] + 32768);
                keys[v].packed = (x << 34) | (y << 17) | z;
                keys[v].vertex = v;
            }
            qsort(keys, (size_t)g->vertex_count, sizeof(struct weld_key), cmp_weld_key);
            for( int i = 1; i < g->vertex_count; i++ )
                if( keys[i].packed == keys[i - 1].packed )
                    uf_union(parent, keys[i].vertex, keys[i - 1].vertex);
            free(keys);
        }
    }

    for( int f = 0; f < g->face_count; f++ )
    {
        uf_union(parent, g->fa[f], g->fb[f]);
        uf_union(parent, g->fb[f], g->fc[f]);
    }

    for( int v = 0; v < g->vertex_count; v++ )
        remap[v] = -1;
    for( int f = 0; f < g->face_count; f++ )
    {
        int root = uf_find(parent, g->fa[f]);
        if( remap[root] < 0 )
            remap[root] = count++;
        g->face_feature[f] = remap[root];
    }

    free(parent);
    free(remap);
    return count;
}

/* ---------------------------------------------------------- measurement -- */

/**
 * One sampled camera. Only the rotation matters: any orthonormal set covering
 * the sphere answers "does this pair ever resolve the other way", and the
 * sorter's own projection is not needed to ask that.
 */
struct view
{
    double m[9]; /* row-major model->camera rotation */
};

static void
view_make(struct view* v, double yaw, double pitch)
{
    double cy = cos(yaw), sy = sin(yaw);
    double cp = cos(pitch), sp = sin(pitch);

    /* yaw about Y, then pitch about X. */
    v->m[0] = cy;       v->m[1] = 0.0; v->m[2] = sy;
    v->m[3] = sp * sy;  v->m[4] = cp;  v->m[5] = -sp * cy;
    v->m[6] = -cp * sy; v->m[7] = sp;  v->m[8] = cp * cy;
}

struct raster
{
    int w, h;
    int* near_feature;  /* z-buffer winner: what the pixel should show */
    int* near_z;
    int* paint_feature; /* depth-sort winner: what the pixel does show */
    int* paint_z;
    int* scratch_z;
    int* scratch_stamp;
    int stamp;
    int* touched;
    int touched_count;
    int* sx;
    int* sy;
    int* sz;
    int* face_depth;
    int* face_band;
    int* face_order;
};

/**
 * Rasterize one triangle, calling back per covered pixel with its interpolated
 * depth. Faces whose screen winding is back-facing are dropped exactly as
 * ToriDraw drops them (toridraw_winding_front_facing: winding > 0), because a
 * face the client never draws must not constrain a band.
 */
#define TRI_FOR_EACH_PIXEL(r, f, PIXEL_BODY)                                                       \
    do                                                                                             \
    {                                                                                              \
        int const _ia = g->fa[f], _ib = g->fb[f], _ic = g->fc[f];                                  \
        long const _ax = (r)->sx[_ia], _ay = (r)->sy[_ia], _az = (r)->sz[_ia];                     \
        long const _bx = (r)->sx[_ib], _by = (r)->sy[_ib], _bz = (r)->sz[_ib];                     \
        long const _cx = (r)->sx[_ic], _cy = (r)->sy[_ic], _cz = (r)->sz[_ic];                     \
        long const _winding = (_ax - _bx) * (_cy - _by) - (_ay - _by) * (_cx - _bx);               \
        if( _winding > 0 )                                                                         \
        {                                                                                          \
            long const _area = (_bx - _ax) * (_cy - _ay) - (_by - _ay) * (_cx - _ax);              \
            int _min_x = (int)(_ax < _bx ? (_ax < _cx ? _ax : _cx) : (_bx < _cx ? _bx : _cx));     \
            int _max_x = (int)(_ax > _bx ? (_ax > _cx ? _ax : _cx) : (_bx > _cx ? _bx : _cx));     \
            int _min_y = (int)(_ay < _by ? (_ay < _cy ? _ay : _cy) : (_by < _cy ? _by : _cy));     \
            int _max_y = (int)(_ay > _by ? (_ay > _cy ? _ay : _cy) : (_by > _cy ? _by : _cy));     \
            if( _area != 0 )                                                                       \
            {                                                                                      \
                if( _min_x < 0 ) _min_x = 0;                                                       \
                if( _min_y < 0 ) _min_y = 0;                                                       \
                if( _max_x >= (r)->w ) _max_x = (r)->w - 1;                                        \
                if( _max_y >= (r)->h ) _max_y = (r)->h - 1;                                        \
                for( int _y = _min_y; _y <= _max_y; _y++ )                                         \
                    for( int _x = _min_x; _x <= _max_x; _x++ )                                     \
                    {                                                                              \
                        long _w0 = (_bx - _ax) * (_y - _ay) - (_by - _ay) * (_x - _ax);            \
                        long _w1 = (_cx - _bx) * (_y - _by) - (_cy - _by) * (_x - _bx);            \
                        long _w2 = (_ax - _cx) * (_y - _cy) - (_ay - _cy) * (_x - _cx);            \
                        int at, z;                                                                 \
                        if( _area > 0 )                                                            \
                        {                                                                          \
                            if( _w0 < 0 || _w1 < 0 || _w2 < 0 )                                    \
                                continue;                                                          \
                        }                                                                          \
                        else if( _w0 > 0 || _w1 > 0 || _w2 > 0 )                                   \
                            continue;                                                              \
                        z = (int)(((double)_w1 * _az + (double)_w2 * _bz + (double)_w0 * _cz) /    \
                                  (double)_area);                                                  \
                        at = _y * (r)->w + _x;                                                     \
                        (void)at;                                                                  \
                        (void)z;                                                                   \
                        PIXEL_BODY                                                                 \
                    }                                                                              \
            }                                                                                      \
        }                                                                                          \
    } while( 0 )

static void
project_view(
    const struct geometry* g,
    const struct view* v,
    struct raster* r,
    int distance,
    int scale)
{
    for( int i = 0; i < g->vertex_count; i++ )
    {
        double x = g->vx[i], y = g->vy[i], z = g->vz[i];
        double cx = v->m[0] * x + v->m[1] * y + v->m[2] * z;
        double cy = v->m[3] * x + v->m[4] * y + v->m[5] * z;
        double cz = v->m[6] * x + v->m[7] * y + v->m[8] * z + distance;

        if( cz < 1.0 )
            cz = 1.0;
        r->sx[i] = (int)(cx * scale / cz) + r->w / 2;
        r->sy[i] = (int)(cy * scale / cz) + r->h / 2;
        r->sz[i] = (int)cz;
    }
}

/* The draw order ToriDraw produces for a model whose priorities are all in the
 * hard range: band ascending, and inside a band the depth buckets walked from
 * the far end. Ties keep face order, as the buckets do. */
static const int* g_sort_depth;
static const int* g_sort_band;

static int
cmp_face_order(const void* a, const void* b)
{
    int fa = *(const int*)a, fb = *(const int*)b;
    if( g_sort_band[fa] != g_sort_band[fb] )
        return g_sort_band[fa] - g_sort_band[fb];
    if( g_sort_depth[fa] != g_sort_depth[fb] )
        return g_sort_depth[fb] - g_sort_depth[fa];
    return fa - fb;
}

/**
 * Score every ordered feature pair for one view.
 *
 * Three passes over the same projection:
 *   1. z-buffer      -- which feature the pixel SHOULD show
 *   2. painter       -- which feature the depth sort actually leaves there
 *   3. per feature   -- every feature that reaches the pixel, and how deep
 *
 * Pass 3 replays each feature into a private depth buffer rather than counting
 * per face, so a relation is weighted by the pixels it covers and not by how
 * finely the loser happens to be tessellated.
 *
 * The verdict per (pixel, covering feature F), against the true winner T:
 *   F is in front of T          -- impossible, T is the z-buffer minimum
 *   F behind T and F is painted -- a visible error; T over F would fix it
 *   F behind T and T is painted -- correct today; F over T would break it
 */
static void
measure_view(
    const struct geometry* g,
    struct raster* r,
    int feature_count,
    const int* feature_face_offset,
    const int* feature_faces,
    const int* band,
    long* fixable,
    long* breakable,
    struct tally* tally,
    int slack)
{
    for( int i = 0; i < r->w * r->h; i++ )
    {
        r->near_feature[i] = -1;
        r->paint_feature[i] = -1;
        r->near_z[i] = 1 << 30;
    }

    for( int f = 0; f < g->face_count; f++ )
    {
        int const feat = g->face_feature[f];
        TRI_FOR_EACH_PIXEL(r, f, {
            if( z < r->near_z[at] )
            {
                r->near_z[at] = z;
                r->near_feature[at] = feat;
            }
        });
    }

    for( int f = 0; f < g->face_count; f++ )
    {
        r->face_depth[f] = (r->sz[g->fa[f]] + r->sz[g->fb[f]] + r->sz[g->fc[f]]) / 3;
        r->face_band[f] = band[g->face_feature[f]];
        r->face_order[f] = f;
    }
    g_sort_depth = r->face_depth;
    g_sort_band = r->face_band;
    qsort(r->face_order, (size_t)g->face_count, sizeof(int), cmp_face_order);

    for( int i = 0; i < g->face_count; i++ )
    {
        int const f = r->face_order[i];
        int const feat = g->face_feature[f];
        TRI_FOR_EACH_PIXEL(r, f, {
            r->paint_feature[at] = feat;
            r->paint_z[at] = z;
        });
    }

    /* The ceiling. A pixel whose painted surface is behind the true one is an
     * error, but only the ones where the two belong to DIFFERENT features can
     * ever be reached by a band -- rule 1 forbids splitting a feature across
     * two, so a surface sorting wrongly against itself is out of scope for
     * priorities entirely. Worth printing before anyone spends a day on it. */
    for( int i = 0; i < r->w * r->h; i++ )
    {
        if( r->paint_feature[i] < 0 || r->paint_z[i] <= r->near_z[i] + slack )
            continue;
        if( r->paint_feature[i] == r->near_feature[i] )
            tally->wrong_within++;
        else
            tally->wrong_between++;
    }
    for( int i = 0; i < r->w * r->h; i++ )
        if( r->paint_feature[i] >= 0 )
            tally->covered++;

    for( int feat = 0; feat < feature_count; feat++ )
    {
        int const begin = feature_face_offset[feat];
        int const end = feature_face_offset[feat + 1];

        r->stamp++;
        r->touched_count = 0;
        for( int i = begin; i < end; i++ )
        {
            int const f = feature_faces[i];
            TRI_FOR_EACH_PIXEL(r, f, {
                if( r->scratch_stamp[at] != r->stamp )
                {
                    r->scratch_stamp[at] = r->stamp;
                    r->scratch_z[at] = z;
                    r->touched[r->touched_count++] = at;
                }
                else if( z < r->scratch_z[at] )
                    r->scratch_z[at] = z;
            });
        }

        for( int i = 0; i < r->touched_count; i++ )
        {
            int const at = r->touched[i];
            int const winner = r->near_feature[at];

            if( winner < 0 || winner == feat )
                continue;
            if( r->scratch_z[at] <= r->near_z[at] + slack )
                continue; /* coplanar with the winner: no order can be wrong */

            if( r->paint_feature[at] == feat )
                fixable[(long)winner * feature_count + feat]++;
            else
                breakable[(long)feat * feature_count + winner]++;
        }
    }
}

/* --------------------------------------------------------- band solving -- */

/** Pixels won, minus pixels lost, by the current band assignment. */
static long
objective(int feature_count, const long* net, const int* band)
{
    long total = 0;
    for( int a = 0; a < feature_count; a++ )
        for( int b = 0; b < feature_count; b++ )
            if( band[a] > band[b] )
                total += net[(long)a * feature_count + b];
    return total;
}

/**
 * Hill climb the band assignment.
 *
 * Every feature is offered every band in turn and keeps the one with the best
 * objective, repeating until a whole sweep changes nothing. Starting from all
 * zero -- the pure depth sort -- means the objective only ever rises, so the
 * result is never worse than shipping no priorities at all, which is the one
 * guarantee worth having when the alternative is a spike through the head.
 *
 * The move is evaluated in O(features) by touching only the pairs the moved
 * feature is part of; the rest of the sum is unchanged by definition.
 */
static long
solve_bands(int feature_count, const long* net, int* band, int max_sweeps)
{
    long total = objective(feature_count, net, band);

    for( int sweep = 0; sweep < max_sweeps; sweep++ )
    {
        bool moved = false;

        for( int x = 0; x < feature_count; x++ )
        {
            int const from = band[x];
            int best_band = from;
            long best_delta = 0;

            for( int to = 0; to < HARD_BANDS; to++ )
            {
                long delta = 0;
                if( to == from )
                    continue;
                for( int b = 0; b < feature_count; b++ )
                {
                    if( b == x )
                        continue;
                    /* x above b */
                    delta += net[(long)x * feature_count + b] *
                             ((to > band[b]) - (from > band[b]));
                    /* b above x */
                    delta += net[(long)b * feature_count + x] *
                             ((band[b] > to) - (band[b] > from));
                }
                if( delta > best_delta )
                {
                    best_delta = delta;
                    best_band = to;
                }
            }

            if( best_band != from )
            {
                band[x] = best_band;
                total += best_delta;
                moved = true;
            }
        }
        if( !moved )
            break;
    }
    return total;
}

/**
 * Slide the used bands down onto 0,1,2,... .
 *
 * The climb leaves gaps, and a gap is not free: the flexible-priority splice
 * points are averages over bands 1/2, 3/4 and 6/8, so which numbers are
 * occupied changes the order even when their relative sequence does not.
 * Occupying a contiguous run from 0 keeps the assignment meaning exactly what
 * it was solved to mean.
 */
static void
compact_bands(int feature_count, int* band)
{
    int map[HARD_BANDS];
    int next = 0;

    for( int b = 0; b < HARD_BANDS; b++ )
        map[b] = -1;
    for( int i = 0; i < feature_count; i++ )
        map[band[i]] = 1;
    for( int b = 0; b < HARD_BANDS; b++ )
        if( map[b] == 1 )
            map[b] = next++;
    for( int i = 0; i < feature_count; i++ )
        band[i] = map[band[i]];
}

/* ------------------------------------------------------------------ main -- */

static void
usage(const char* argv0)
{
    fprintf(
        stderr,
        "Usage: %s --in FILE.ob3 [--in FILE.ob3 ...] [--out FILE.ob3 ...]\n"
        "       [--report] [--views N] [--pitches N] [--res N] [--rounds N]\n"
        "       [--min-pixels N] [--no-weld]\n"
        "\n"
        "Analysis runs over every --in together; each --out receives the slice\n"
        "belonging to the --in at the same position. Inputs are never written.\n",
        argv0);
}

int
main(int argc, char** argv)
{
    struct input inputs[MAX_INPUTS];
    int input_count = 0;
    int out_count = 0;
    bool do_report = false;
    bool weld = true;
    int yaw_steps = 12;
    int pitch_steps = 5;
    int res = 224;
    int rounds = 4;
    double elev_min_deg = -20.0;
    double elev_max_deg = 67.0;
    long min_pixels = 40;
    bool bulk_flex = false;

    struct geometry g = { 0 };
    struct feature* features = NULL;
    struct raster r = { 0 };
    long* fixable = NULL;
    long* breakable = NULL;
    long* net = NULL;
    int* band = NULL;
    int* previous = NULL;
    struct tally tally = { 0, 0, 0 };
    struct tally baseline = { 0, 0, 0 };
    int candidate_count = 0;
    int settled_rounds = 0;
    long gain = 0;
    int feature_count = 0;
    int* feature_face_offset = NULL;
    int* feature_faces = NULL;
    int distance = 0, scale = 0, extent = 0;
    int rc = 1;

    memset(inputs, 0, sizeof(inputs));

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--in") == 0 && i + 1 < argc )
        {
            if( input_count >= MAX_INPUTS )
            {
                fprintf(stderr, "rs2012_face_priorities: too many --in\n");
                return 2;
            }
            inputs[input_count++].in_path = argv[++i];
        }
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
        {
            if( out_count >= MAX_INPUTS )
            {
                fprintf(stderr, "rs2012_face_priorities: too many --out\n");
                return 2;
            }
            inputs[out_count++].out_path = argv[++i];
        }
        else if( strcmp(argv[i], "--report") == 0 )
            do_report = true;
        else if( strcmp(argv[i], "--no-weld") == 0 )
            weld = false;
        else if( strcmp(argv[i], "--bulk-flex") == 0 )
            bulk_flex = true;
        else if( strcmp(argv[i], "--views") == 0 && i + 1 < argc )
            yaw_steps = atoi(argv[++i]);
        else if( strcmp(argv[i], "--pitches") == 0 && i + 1 < argc )
            pitch_steps = atoi(argv[++i]);
        else if( strcmp(argv[i], "--res") == 0 && i + 1 < argc )
            res = atoi(argv[++i]);
        else if( strcmp(argv[i], "--rounds") == 0 && i + 1 < argc )
            rounds = atoi(argv[++i]);
        else if( strcmp(argv[i], "--elev") == 0 && i + 1 < argc )
        {
            if( sscanf(argv[++i], "%lf,%lf", &elev_min_deg, &elev_max_deg) != 2 )
            {
                usage(argv[0]);
                return 2;
            }
        }
        else if( strcmp(argv[i], "--min-pixels") == 0 && i + 1 < argc )
            min_pixels = atol(argv[++i]);
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    if( input_count == 0 || (out_count && out_count != input_count) || yaw_steps < 1 ||
        pitch_steps < 1 || res < 32 || res > 1024 )
    {
        usage(argv[0]);
        return 2;
    }

    /* ---- load and concatenate ---- */
    for( int i = 0; i < input_count; i++ )
    {
        long size = 0;
        uint8_t* bytes = read_file(inputs[i].in_path, &size);
        if( !bytes )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot read %s\n", inputs[i].in_path);
            goto done;
        }
        inputs[i].model =
            RSCache_ModelNewDecodeProvenance(bytes, (int)size, &inputs[i].provenance);
        free(bytes);
        if( !inputs[i].model || !inputs[i].provenance )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot decode %s\n", inputs[i].in_path);
            goto done;
        }
        inputs[i].vertex_base = g.vertex_count;
        inputs[i].face_base = g.face_count;
        g.vertex_count += inputs[i].model->vertex_count;
        g.face_count += inputs[i].model->face_count;
    }

    g.vx = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    g.vy = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    g.vz = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    g.fa = (int*)malloc((size_t)g.face_count * sizeof(int));
    g.fb = (int*)malloc((size_t)g.face_count * sizeof(int));
    g.fc = (int*)malloc((size_t)g.face_count * sizeof(int));
    g.face_feature = (int*)malloc((size_t)g.face_count * sizeof(int));
    if( !g.vx || !g.vy || !g.vz || !g.fa || !g.fb || !g.fc || !g.face_feature )
        goto done;

    for( int i = 0; i < input_count; i++ )
    {
        const struct RSCache_Model* m = inputs[i].model;
        /* Version-13+ ob3s store vertices at 4x; the engine's adaptor shifts
         * them down on the way in, and this tool has to agree with it or two
         * inputs at different versions land at different scales. */
        int const shift = m->format_version >= 13 ? 2 : 0;
        for( int v = 0; v < m->vertex_count; v++ )
        {
            g.vx[inputs[i].vertex_base + v] = m->vertices_x[v] >> shift;
            g.vy[inputs[i].vertex_base + v] = m->vertices_y[v] >> shift;
            g.vz[inputs[i].vertex_base + v] = m->vertices_z[v] >> shift;
        }
        for( int f = 0; f < m->face_count; f++ )
        {
            g.fa[inputs[i].face_base + f] = m->face_indices_a[f] + inputs[i].vertex_base;
            g.fb[inputs[i].face_base + f] = m->face_indices_b[f] + inputs[i].vertex_base;
            g.fc[inputs[i].face_base + f] = m->face_indices_c[f] + inputs[i].vertex_base;
        }
    }

    feature_count = segment(&g, weld);
    if( feature_count <= 0 || feature_count > MAX_FEATURES )
    {
        fprintf(
            stderr,
            "rs2012_face_priorities: %d features, outside the %d the tool holds\n",
            feature_count,
            MAX_FEATURES);
        goto done;
    }

    features = (struct feature*)calloc((size_t)feature_count, sizeof(struct feature));
    feature_face_offset = (int*)calloc((size_t)feature_count + 1, sizeof(int));
    feature_faces = (int*)malloc((size_t)g.face_count * sizeof(int));
    fixable = (long*)calloc((size_t)feature_count * (size_t)feature_count, sizeof(long));
    breakable = (long*)calloc((size_t)feature_count * (size_t)feature_count, sizeof(long));
    if( !features || !feature_face_offset || !feature_faces || !fixable || !breakable )
        goto done;

    for( int f = 0; f < g.face_count; f++ )
        features[g.face_feature[f]].face_count++;
    for( int i = 0; i < feature_count; i++ )
        feature_face_offset[i + 1] = feature_face_offset[i] + features[i].face_count;
    {
        int* cursor = (int*)malloc((size_t)feature_count * sizeof(int));
        if( !cursor )
            goto done;
        memcpy(cursor, feature_face_offset, (size_t)feature_count * sizeof(int));
        for( int f = 0; f < g.face_count; f++ )
            feature_faces[cursor[g.face_feature[f]]++] = f;
        free(cursor);
    }

    /* ---- framing ---- */
    {
        int min_v[3] = { 1 << 30, 1 << 30, 1 << 30 };
        int max_v[3] = { -(1 << 30), -(1 << 30), -(1 << 30) };
        int center[3];
        long radius2 = 0;

        for( int v = 0; v < g.vertex_count; v++ )
        {
            int c[3] = { g.vx[v], g.vy[v], g.vz[v] };
            for( int k = 0; k < 3; k++ )
            {
                if( c[k] < min_v[k] ) min_v[k] = c[k];
                if( c[k] > max_v[k] ) max_v[k] = c[k];
            }
        }
        for( int k = 0; k < 3; k++ )
            center[k] = (min_v[k] + max_v[k]) / 2;
        for( int v = 0; v < g.vertex_count; v++ )
        {
            long dx = g.vx[v] -= center[0];
            long dy = g.vy[v] -= center[1];
            long dz = g.vz[v] -= center[2];
            long d2 = dx * dx + dy * dy + dz * dz;
            if( d2 > radius2 )
                radius2 = d2;
        }
        extent = (int)sqrt((double)radius2) + 1;
        scale = 512;
        /* Far enough that the whole model fits whatever way it turns. */
        distance = (int)((double)extent * scale / (0.45 * res)) + extent;
    }

    /* ---- measure ---- */
    r.w = r.h = res;
    r.near_feature = (int*)malloc((size_t)res * res * sizeof(int));
    r.near_z = (int*)malloc((size_t)res * res * sizeof(int));
    r.paint_feature = (int*)malloc((size_t)res * res * sizeof(int));
    r.paint_z = (int*)malloc((size_t)res * res * sizeof(int));
    r.scratch_z = (int*)malloc((size_t)res * res * sizeof(int));
    r.scratch_stamp = (int*)calloc((size_t)res * res, sizeof(int));
    r.touched = (int*)malloc((size_t)res * res * sizeof(int));
    r.sx = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    r.sy = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    r.sz = (int*)malloc((size_t)g.vertex_count * sizeof(int));
    r.face_depth = (int*)malloc((size_t)g.face_count * sizeof(int));
    r.face_band = (int*)malloc((size_t)g.face_count * sizeof(int));
    r.face_order = (int*)malloc((size_t)g.face_count * sizeof(int));
    if( !r.near_feature || !r.near_z || !r.paint_feature || !r.paint_z || !r.scratch_z ||
        !r.scratch_stamp ||
        !r.touched || !r.sx || !r.sy || !r.sz || !r.face_depth || !r.face_band || !r.face_order )
        goto done;

    band = (int*)malloc((size_t)feature_count * sizeof(int));
    net = (long*)malloc((size_t)feature_count * (size_t)feature_count * sizeof(long));
    previous = (int*)malloc((size_t)feature_count * sizeof(int));
    if( !band || !net || !previous )
        goto done;

    /* Start in the middle, not at zero.
     *
     * The climb only ever moves one feature at a time, so from band 0 the only
     * available move is "put this one over everything". The relation that
     * actually wants expressing is usually the opposite -- a single feature
     * needs to go BEHIND the rest, and reaching that from zero would take a
     * coordinated move of every other feature at once, which single-feature
     * hill climbing cannot find. From the middle both directions are one move
     * away. compact_bands() slides the answer back down afterwards, so the
     * starting band is scaffolding and not part of the result. */
    for( int i = 0; i < feature_count; i++ )
        band[i] = HARD_BANDS / 2;

    /* Measure, solve, and measure again against what was solved: the first
     * round's "what does the sort paint here" answer stops being true the
     * moment a band moves. Two or three rounds is where it settles. */
    for( int round = 0; round < rounds; round++ )
    {
        memset(fixable, 0, (size_t)feature_count * (size_t)feature_count * sizeof(long));
        memset(breakable, 0, (size_t)feature_count * (size_t)feature_count * sizeof(long));
        memset(&tally, 0, sizeof(tally));

        for( int p = 0; p < pitch_steps; p++ )
        {
            /* Only the elevations the client can actually produce.
             *
             * This is not a detail. Sweeping the whole sphere asks every pair
             * to agree from underneath the model as well, and almost no pair
             * on a closed creature does -- which is how a legitimate ordering
             * gets scored away as a conflict. app.c clamps world camera pitch
             * to 128..383 of 2048, i.e. 22.5 to 67.3 degrees looking DOWN, and
             * nothing in the client unclamps it. The range is extended above
             * the horizon only by as much as perspective gives on a model
             * taller than the camera -- the Queen's head is twenty units up,
             * and players do see its underside. */
            double pitch =
                elev_min_deg * PRIO_PI / 180.0 +
                (elev_max_deg - elev_min_deg) * PRIO_PI / 180.0 *
                    (pitch_steps == 1 ? 0.5 : (double)p / (pitch_steps - 1));
            for( int y = 0; y < yaw_steps; y++ )
            {
                struct view v;
                view_make(&v, 2.0 * PRIO_PI * y / yaw_steps, pitch);
                project_view(&g, &v, &r, distance, scale);
                measure_view(
                    &g,
                    &r,
                    feature_count,
                    feature_face_offset,
                    feature_faces,
                    band,
                    fixable,
                    breakable,
                    &tally,
                    2);
            }
        }

        /* A pair too small to have been sampled properly is noise, not a
         * finding; below the floor it is not allowed to move a band. */
        candidate_count = 0;
        for( int a = 0; a < feature_count; a++ )
            for( int b = 0; b < feature_count; b++ )
            {
                long const fix = fixable[(long)a * feature_count + b];
                long const brk = breakable[(long)a * feature_count + b];
                long value = 0;

                if( a != b && (fix >= min_pixels || brk >= min_pixels) )
                {
                    value = fix - brk;
                    if( value > 0 )
                        candidate_count++;
                }
                net[(long)a * feature_count + b] = value;
            }

        if( round == 0 )
            baseline = tally;

        memcpy(previous, band, (size_t)feature_count * sizeof(int));
        gain = solve_bands(feature_count, net, band, 32);
        compact_bands(feature_count, band);

        if( memcmp(previous, band, (size_t)feature_count * sizeof(int)) == 0 )
            break;
        settled_rounds = round + 1;
    }

    for( int i = 0; i < feature_count; i++ )
        features[i].band = band[i];

    /* ---- report ---- */
    if( do_report )
    {
        int band_faces[HARD_BANDS] = { 0 };
        int band_features[HARD_BANDS] = { 0 };

        printf(
            "geometry: %d vertices, %d faces across %d input model(s)\n",
            g.vertex_count,
            g.face_count,
            input_count);
        printf(
            "features: %d connected\n"
            "baseline: %ld/%ld wrong pixels under the pure depth sort (%.2f%%)\n"
            "          %ld within one feature (no band can reach these), "
            "%ld between features\n"
            "final:    %ld/%ld wrong (%.2f%%), %ld within, %ld between\n"
            "pairs:    %d a band could pay for\n"
            "solve:    settled after %d round(s), model predicts %+ld pixels\n",
            feature_count,
            baseline.wrong_within + baseline.wrong_between,
            baseline.covered,
            baseline.covered ? 100.0 *
                                   (double)(baseline.wrong_within + baseline.wrong_between) /
                                   (double)baseline.covered
                             : 0.0,
            baseline.wrong_within,
            baseline.wrong_between,
            tally.wrong_within + tally.wrong_between,
            tally.covered,
            tally.covered
                ? 100.0 * (double)(tally.wrong_within + tally.wrong_between) /
                      (double)tally.covered
                : 0.0,
            tally.wrong_within,
            tally.wrong_between,
            candidate_count,
            settled_rounds ? settled_rounds : 1,
            gain);

        for( int i = 0; i < feature_count; i++ )
        {
            band_faces[features[i].band] += features[i].face_count;
            band_features[features[i].band]++;
        }
        printf("%6s %10s %10s\n", "band", "features", "faces");
        for( int b = 0; b < HARD_BANDS; b++ )
            if( band_features[b] )
                printf("%6d %10d %10d\n", b, band_features[b], band_faces[b]);

        printf("\nlargest features:\n%6s %7s %6s\n", "id", "faces", "band");
        {
            unsigned char* shown = (unsigned char*)calloc((size_t)feature_count, 1);
            for( int n = 0; shown && n < 20; n++ )
            {
                int best = -1;
                for( int i = 0; i < feature_count; i++ )
                    if( !shown[i] &&
                        (best < 0 || features[i].face_count > features[best].face_count) )
                        best = i;
                if( best < 0 )
                    break;
                shown[best] = 1;
                printf("%6d %7d %6d\n", best, features[best].face_count, features[best].band);
            }
            free(shown);
        }

        /* Where the between-feature error actually sits, and what a band would
         * cost there. A pair with a large fixable and a comparable breakable is
         * a pair that swaps as the model turns: the error is real, and no band
         * is the answer to it. */
        printf(
            "\nworst feature pairs (last round):\n%6s %6s %8s %8s %8s %5s %5s\n",
            "over",
            "under",
            "fixable",
            "breaks",
            "net",
            "bndA",
            "bndB");
        {
            long* scratch = (long*)malloc(
                (size_t)feature_count * (size_t)feature_count * sizeof(long));
            if( scratch )
            {
                memcpy(
                    scratch,
                    fixable,
                    (size_t)feature_count * (size_t)feature_count * sizeof(long));
                for( int n = 0; n < 15; n++ )
                {
                    int ba = -1, bb = -1;
                    long best = 0;
                    for( int a = 0; a < feature_count; a++ )
                        for( int b = 0; b < feature_count; b++ )
                            if( scratch[(long)a * feature_count + b] > best )
                            {
                                best = scratch[(long)a * feature_count + b];
                                ba = a;
                                bb = b;
                            }
                    if( ba < 0 )
                        break;
                    printf(
                        "%6d %6d %8ld %8ld %8ld %5d %5d\n",
                        ba,
                        bb,
                        best,
                        breakable[(long)ba * feature_count + bb],
                        best - breakable[(long)ba * feature_count + bb],
                        band[ba],
                        band[bb]);
                    scratch[(long)ba * feature_count + bb] = 0;
                }
                free(scratch);
            }
        }

        printf("\nseparations the bands actually buy:\n%6s %6s %10s\n", "over", "under", "net px");
        {
            long threshold = 0;
            for( int n = 0; n < 15; n++ )
            {
                int ba = -1, bb = -1;
                long best = threshold;
                for( int a = 0; a < feature_count; a++ )
                    for( int b = 0; b < feature_count; b++ )
                        if( band[a] > band[b] && net[(long)a * feature_count + b] > best )
                        {
                            best = net[(long)a * feature_count + b];
                            ba = a;
                            bb = b;
                        }
                if( ba < 0 )
                    break;
                printf("%6d %6d %10ld\n", ba, bb, best);
                net[(long)ba * feature_count + bb] = 0;
            }
        }
    }

    /* ---- write ---- */
    for( int i = 0; i < out_count; i++ )
    {
        struct RSCache_Model* m = inputs[i].model;
        struct RSCache_ModelProvenance* p = inputs[i].provenance;
        uint8_t* encoded = NULL;
        uint32_t bound, written;

        free(m->face_priorities);
        m->face_priorities = (uint8_t*)malloc((size_t)m->face_count);
        if( !m->face_priorities )
            goto done;
        for( int f = 0; f < m->face_count; f++ )
        {
            int b = features[g.face_feature[inputs[i].face_base + f]].band;
            /* --bulk-flex: the untouched bulk goes to priority 10 rather than
             * band 0. That is not cosmetic. With the bulk flexible, a feature
             * left in a hard band is no longer pinned in front of everything:
             * the sorter splices the flexible run around it at the averaged
             * depth of the occupied hard bands, so the feature sorts AS A UNIT
             * at its own depth -- over the far half of the surface it sits on
             * and under the near half. That is the behaviour a claw ring round
             * a neck actually wants, and no arrangement of hard bands can
             * express it. */
            if( bulk_flex && b == 0 )
                b = 10;
            m->face_priorities[f] = (uint8_t)b;
        }
        m->model_priority = 255;

        /* The encoder prefers the provenance's recorded header over anything
         * derived from the model, so the header's priority byte has to say
         * "per face" (255) or the array it now carries is not written. */
        if( p->header_flag_count > 1 )
            p->header_flags[1] = 255;

        bound = RSCache_ModelEncodeBound(m, p);
        encoded = bound ? (uint8_t*)malloc(bound) : NULL;
        written = encoded ? RSCache_ModelEncodeFormat(m, p, p->format, encoded, bound) : 0;
        if( !written )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot encode %s\n", inputs[i].out_path);
            free(encoded);
            goto done;
        }

        /* Prove the file that lands decodes back to the priorities intended. */
        {
            struct RSCache_ModelProvenance* cp = NULL;
            struct RSCache_Model* check =
                RSCache_ModelNewDecodeProvenance(encoded, (int)written, &cp);
            bool ok = check && cp && check->face_count == m->face_count &&
                      check->face_priorities &&
                      memcmp(check->face_priorities, m->face_priorities,
                             (size_t)m->face_count) == 0;
            RSCache_ModelFree(check);
            RSCache_ModelProvenanceFree(cp);
            if( !ok )
            {
                fprintf(
                    stderr,
                    "rs2012_face_priorities: %s failed its decode check\n",
                    inputs[i].out_path);
                free(encoded);
                goto done;
            }
        }

        if( !write_file(inputs[i].out_path, encoded, written) )
        {
            fprintf(stderr, "rs2012_face_priorities: cannot write %s\n", inputs[i].out_path);
            free(encoded);
            goto done;
        }
        free(encoded);
        printf(
            "rs2012_face_priorities: %s -> %s (%u bytes)\n",
            inputs[i].in_path,
            inputs[i].out_path,
            written);
    }

    rc = 0;

done:
    free(g.vx);
    free(g.vy);
    free(g.vz);
    free(g.fa);
    free(g.fb);
    free(g.fc);
    free(g.face_feature);
    free(features);
    free(feature_face_offset);
    free(feature_faces);
    free(fixable);
    free(breakable);
    free(net);
    free(band);
    free(previous);
    free(r.near_feature);
    free(r.near_z);
    free(r.paint_feature);
    free(r.paint_z);
    free(r.scratch_z);
    free(r.scratch_stamp);
    free(r.touched);
    free(r.sx);
    free(r.sy);
    free(r.sz);
    free(r.face_depth);
    free(r.face_band);
    free(r.face_order);
    for( int i = 0; i < input_count; i++ )
    {
        RSCache_ModelFree(inputs[i].model);
        RSCache_ModelProvenanceFree(inputs[i].provenance);
    }
    return rc;
}
