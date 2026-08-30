/*
 * Score vertex-projection kernels against the shape of a real scene.
 *
 *   make -C src test-proj-bench
 *
 * WHY THE CALL AND NOT THE LOOP
 *
 * A projection census over lumbridge-ground (graphics/proj_census.h, built
 * with -DTORIDRAW_PROJECTION_CENSUS=1) counted 2653519 model projections and
 * 59772954 vertices in one run, and the distribution is the whole story:
 *
 *   - Only the model-yaw kernels ever fire. Every 6DOF and pitch+yaw entry
 *     stayed at zero, so the twenty kernels in projection16_simd.sse2.u.c
 *     reduce to one family.
 *   - 1563125 of those 2653519 models -- 59% -- have exactly four vertices.
 *     That is a single 4-wide block: the loop runs once and the per-call
 *     prologue is not amortised over anything.
 *   - Only 1.67% of vertices land in the scalar tail, so the tail is not
 *     worth tuning, and 98.7% of models take the noclip path.
 *
 * A bench that hoists one big vertex array and times the inner loop would
 * therefore measure the half of the cost that the scene does not actually
 * spend. This one replays models, one call each, with the counts drawn from
 * the census histogram, so a kernel that wins the loop but pays for it in
 * setup scores as what it is.
 *
 * The vertex arena is walked rather than re-read, for the same reason the
 * span bench walks a framebuffer: a kernel timed against 4 KB resident in L1
 * is measuring instruction count, not the loads and stores it really issues.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "graphics/projection16_fast.sse2.h"
#include "graphics/projection16_simd.u.c"

/* ------------------------------------------------------------ census shape */

/*
 * Vertex counts and model shares, transcribed from proj_census.txt. These are
 * the buckets carrying at least ~0.05% of models; the remainder of the
 * histogram is a long tail that the sampler folds into the nearest listed
 * count rather than pretending to resolve.
 */
struct CountShare
{
    int vertices;
    double models;
};

static struct CountShare const g_census[] = {
    { 4, 1563125 }, { 5, 37576 },   { 6, 54267 },   { 8, 5436 },
    { 10, 28844 },  { 12, 207587 }, { 14, 5446 },   { 15, 2215 },
    { 16, 27472 },  { 17, 7887 },   { 19, 16641 },  { 20, 2384 },
    { 21, 4812 },   { 22, 1972 },   { 24, 54737 },  { 25, 6938 },
    { 26, 11124 },  { 28, 10561 },  { 29, 10985 },  { 30, 11749 },
    { 31, 9338 },   { 32, 12448 },  { 35, 67755 },  { 36, 159674 },
    { 44, 4167 },   { 50, 25721 },  { 58, 3075 },   { 96, 134225 },
    { 190, 88825 }, { 380, 18587 },
};

#define CENSUS_LEN ( (int)( sizeof(g_census) / sizeof(g_census[0]) ) )

/* Share of models that take the textured path (the rest are untextured). */
#define TEX_MODEL_SHARE 0.28
/* Share of models that need the near-plane clip path. */
#define CLIP_MODEL_SHARE 0.0132

/* Half of a generous viewport; vertices outside this never reach a pixel. */
#define VIEWPORT_HALF 1024

#define MODEL_COUNT 4096
#define ARENA_VERTS ( 1 << 17 ) /* 128k vertices == 768 KB of int16 xyz */
#define MAX_MODEL_VTX 512

struct Model
{
    int arena_offset;
    int num_vertices;
    int model_yaw;
    int scene_x;
    int scene_y;
    int scene_z;
    int textured;
    int clipped;
};

static vertexint_t g_vx[ARENA_VERTS];
static vertexint_t g_vy[ARENA_VERTS];
static vertexint_t g_vz[ARENA_VERTS];
static struct Model g_models[MODEL_COUNT];

static int g_ox[MAX_MODEL_VTX];
static int g_oy[MAX_MODEL_VTX];
static int g_oz[MAX_MODEL_VTX];
static int g_sx[MAX_MODEL_VTX];
static int g_sy[MAX_MODEL_VTX];
static int g_sz[MAX_MODEL_VTX];

static int g_bx[MAX_MODEL_VTX];
static int g_by[MAX_MODEL_VTX];
static int g_bz[MAX_MODEL_VTX];
static int g_tx[MAX_MODEL_VTX];
static int g_ty[MAX_MODEL_VTX];
static int g_tz[MAX_MODEL_VTX];

/* Deterministic; a bench that reshuffles between runs is not comparable. */
static unsigned int g_rng = 0x9e3779b9u;

static unsigned int
rng_next(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rng_range(int lo, int hi)
{
    assert(hi >= lo);
    return lo + (int)( rng_next() % (unsigned int)( hi - lo + 1 ) );
}

static double
rng_unit(void)
{
    return (double)( rng_next() >> 8 ) / (double)( 1u << 24 );
}

/* ------------------------------------------------------------------ camera */

#define CAM_PITCH 128
#define CAM_YAW 600
#define CAM_COT16 ( 1 << 16 )
#define NEAR_Z 50

/*
 * Scatter the model in front of the camera rather than around the origin. A
 * scene laid out around the origin and viewed from a yaw leaves most of its
 * vertices at negative depth, where the reference's arithmetic shift floors
 * and a float conversion truncates toward zero -- the two disagree by a unit
 * for a reason no pixel ever sees, and the comparison drowns in it. Depth and
 * lateral offset are chosen in camera space and rotated back out by the
 * inverse camera yaw.
 */
static void
place_in_front(int depth, int lateral, struct Model* m)
{
    int const ccy = ToriDraw_ReadCosTable(CAM_YAW);
    int const scy = ToriDraw_ReadSinTable(CAM_YAW);

    assert(m);

    m->scene_x = ( lateral * ccy - depth * scy ) >> 16;
    m->scene_z = ( depth * ccy + lateral * scy ) >> 16;
}

static void
build_workload(void)
{
    double total = 0.0;
    double cum[CENSUS_LEN];
    int arena = 0;

    for( int i = 0; i < CENSUS_LEN; i++ )
        total += g_census[i].models;
    assert(total > 0.0);

    cum[0] = g_census[0].models / total;
    for( int i = 1; i < CENSUS_LEN; i++ )
        cum[i] = cum[i - 1] + g_census[i].models / total;

    for( int i = 0; i < ARENA_VERTS; i++ )
    {
        /* Model-local coordinates; real models sit well inside int16. */
        g_vx[i] = (vertexint_t)rng_range(-2048, 2048);
        g_vy[i] = (vertexint_t)rng_range(-1024, 256);
        g_vz[i] = (vertexint_t)rng_range(-2048, 2048);
    }

    for( int m = 0; m < MODEL_COUNT; m++ )
    {
        double const r = rng_unit();
        int n = g_census[CENSUS_LEN - 1].vertices;

        for( int i = 0; i < CENSUS_LEN; i++ )
        {
            if( r <= cum[i] )
            {
                n = g_census[i].vertices;
                break;
            }
        }

        if( arena + n > ARENA_VERTS )
            arena = 0;

        g_models[m].arena_offset = arena;
        g_models[m].num_vertices = n;
        /*
         * Most scenery is axis-aligned, so a real scene hits model_yaw == 0
         * far more often than uniform would -- and that picks the noyaw
         * kernel, which is the cheaper of the pair. Weight it.
         */
        g_models[m].model_yaw = ( rng_next() & 3 ) ? 0 : rng_range(1, 2047);
        place_in_front(
            rng_range(200, 9000), rng_range(-4000, 4000), &g_models[m]);
        g_models[m].scene_y = rng_range(-2000, 2000);
        g_models[m].textured = rng_unit() < TEX_MODEL_SHARE;
        g_models[m].clipped = 0;

        arena += n;
    }
}

/* ----------------------------------------------------------------- driving */

struct Outputs
{
    int* ox;
    int* oy;
    int* oz;
    int* sx;
    int* sy;
    int* sz;
};

static struct Outputs const g_out_a = { g_ox, g_oy, g_oz, g_sx, g_sy, g_sz };
static struct Outputs const g_out_b = { g_bx, g_by, g_bz, g_tx, g_ty, g_tz };

static void
project_one(struct Model const* m, struct Outputs const* o, int fast)
{
    vertexint_t* vx = &g_vx[m->arena_offset];
    vertexint_t* vy = &g_vy[m->arena_offset];
    vertexint_t* vz = &g_vz[m->arena_offset];
    int const n = m->num_vertices;
    int const yaw = m->model_yaw;

    assert(m);
    assert(o);
    assert(n <= MAX_MODEL_VTX);

    /* clang-format off */
    if( m->clipped )
    {
        if( m->textured )
        {
            if( fast )
                toridraw_projection_fast_clip(
                    o->ox, o->oy, o->oz, o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
            else
                project_vertices_array_fused_clip(
                    o->ox, o->oy, o->oz, o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
        }
        else
        {
            if( fast )
                toridraw_projection_fast_notex_clip(
                    o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
            else
                project_vertices_array_fused_notex_clip(
                    o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
        }
    }
    else
    {
        if( m->textured )
        {
            if( fast )
                toridraw_projection_fast_noclip(
                    o->ox, o->oy, o->oz, o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
            else
                project_vertices_array_fused_noclip(
                    o->ox, o->oy, o->oz, o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
        }
        else
        {
            if( fast )
                toridraw_projection_fast_notex_noclip(
                    o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
            else
                project_vertices_array_fused_notex_noclip(
                    o->sx, o->sy, o->sz, vx, vy, vz, n,
                    yaw, 0, m->scene_x, m->scene_y, m->scene_z,
                    NEAR_Z, CAM_COT16, CAM_PITCH, CAM_YAW);
        }
    }
    /* clang-format on */
}

/*
 * Route each model the way the real dispatcher does.
 *
 * The noclip kernels exist because the caller has already established that no
 * vertex of the model is behind the near plane; they divide by z with no
 * guard because nothing can make z small. A bench that hands them a model
 * straddling z == 0 is not testing them, it is testing what a reciprocal of
 * zero does -- and there the two kernels disagree by two billion for the
 * uninteresting reason that one truncated z to 0 and the other to 1.
 *
 * So: project once, look at the depth that came out, and mark anything that
 * reaches the near plane as clipped.
 */
static double
classify_by_depth(void)
{
    double clipped = 0.0;

    for( int m = 0; m < MODEL_COUNT; m++ )
    {
        int const n = g_models[m].num_vertices;

        g_models[m].clipped = 0;
        project_one(&g_models[m], &g_out_a, 0);

        for( int i = 0; i < n; i++ )
        {
            /* model_mid_z is 0 here, so screen z is the camera-space depth. */
            if( g_sz[i] < NEAR_Z )
            {
                g_models[m].clipped = 1;
                break;
            }
        }
        clipped += g_models[m].clipped ? 1.0 : 0.0;
    }

    return clipped / (double)MODEL_COUNT;
}

/* ------------------------------------------------------------- correctness */

struct Diff
{
    double checked;
    double differing;
    int worst;
};

static void
diff_arrays(struct Diff* d, int const* a, int const* b, int n)
{
    assert(d);
    assert(a);
    assert(b);

    for( int i = 0; i < n; i++ )
    {
        int const delta = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];

        d->checked += 1.0;
        if( delta != 0 )
        {
            d->differing += 1.0;
            if( delta > d->worst )
                d->worst = delta;
        }
    }
}

static void
print_diff(char const* label, struct Diff const* d)
{
    assert(label);
    assert(d);

    printf(
        "  %-17s: %9.0f values, %8.0f differ (%5.2f%%), worst |d| = %d\n",
        label,
        d->checked,
        d->differing,
        d->checked > 0.0 ? 100.0 * d->differing / d->checked : 0.0,
        d->worst);
}

static int
compare_kernels(void)
{
    struct Diff ortho = { 0.0, 0.0, 0 };
    struct Diff screen_xy = { 0.0, 0.0, 0 };
    struct Diff screen_z = { 0.0, 0.0, 0 };
    struct Diff visible = { 0.0, 0.0, 0 };
    int worst_m = -1;
    int worst_i = -1;
    int failed = 0;

    for( int m = 0; m < MODEL_COUNT; m++ )
    {
        int const n = g_models[m].num_vertices;

        memset(g_ox, 0, sizeof(g_ox));
        memset(g_oy, 0, sizeof(g_oy));
        memset(g_oz, 0, sizeof(g_oz));
        memset(g_sx, 0, sizeof(g_sx));
        memset(g_sy, 0, sizeof(g_sy));
        memset(g_sz, 0, sizeof(g_sz));
        memset(g_bx, 0, sizeof(g_bx));
        memset(g_by, 0, sizeof(g_by));
        memset(g_bz, 0, sizeof(g_bz));
        memset(g_tx, 0, sizeof(g_tx));
        memset(g_ty, 0, sizeof(g_ty));
        memset(g_tz, 0, sizeof(g_tz));

        project_one(&g_models[m], &g_out_a, 0);
        project_one(&g_models[m], &g_out_b, 1);

        if( g_models[m].textured )
        {
            diff_arrays(&ortho, g_ox, g_bx, n);
            diff_arrays(&ortho, g_oy, g_by, n);
            diff_arrays(&ortho, g_oz, g_bz, n);
        }
        diff_arrays(&screen_xy, g_sx, g_tx, n);
        diff_arrays(&screen_xy, g_sy, g_ty, n);
        diff_arrays(&screen_z, g_sz, g_tz, n);

        /*
         * A vertex sitting a couple of hundred units in front of the eye
         * multiplies any depth wobble by x/z, so a one-unit disagreement in
         * depth shows up as tens of units of screen x -- at coordinates far
         * outside the viewport, where the raster clips it away anyway. Gate
         * on the vertices that could actually land on a screen.
         */
        for( int i = 0; i < n; i++ )
        {
            int const dx = g_sx[i] > g_tx[i] ? g_sx[i] - g_tx[i] : g_tx[i] - g_sx[i];
            int const dy = g_sy[i] > g_ty[i] ? g_sy[i] - g_ty[i] : g_ty[i] - g_sy[i];
            int const d = dx > dy ? dx : dy;

            if( g_sx[i] < -VIEWPORT_HALF || g_sx[i] > VIEWPORT_HALF )
                continue;
            if( g_sy[i] < -VIEWPORT_HALF || g_sy[i] > VIEWPORT_HALF )
                continue;

            visible.checked += 1.0;
            if( d == 0 )
                continue;
            visible.differing += 1.0;
            if( d <= visible.worst )
                continue;

            visible.worst = d;
            worst_m = m;
            worst_i = i;
        }
    }

    printf("compare (fast vs projection16_simd.sse2):\n");
    print_diff("orthographic xyz", &ortho);
    print_diff("screen xy", &screen_xy);
    print_diff("screen z", &screen_z);
    print_diff("on-screen xy", &visible);

    if( worst_m >= 0 )
    {
        project_one(&g_models[worst_m], &g_out_a, 0);
        project_one(&g_models[worst_m], &g_out_b, 1);
        printf(
            "  worst on-screen vertex: model %d vertex %d of %d, yaw %d, "
            "depth %d -> old (%d, %d) new (%d, %d)\n",
            worst_m,
            worst_i,
            g_models[worst_m].num_vertices,
            g_models[worst_m].model_yaw,
            g_sz[worst_i],
            g_sx[worst_i],
            g_sy[worst_i],
            g_tx[worst_i],
            g_ty[worst_i]);
    }

    if( visible.worst > 4 )
    {
        printf("  FAIL: on-screen coordinates drift by more than 4 units\n");
        failed = 1;
    }
    return failed;
}

/* ------------------------------------------------------------------ timing */

static double
time_variant(int fast, int passes)
{
    clock_t const t0 = clock();

    for( int p = 0; p < passes; p++ )
        for( int m = 0; m < MODEL_COUNT; m++ )
            project_one(&g_models[m], &g_out_a, fast);

    return 1000.0 * (double)( clock() - t0 ) / (double)CLOCKS_PER_SEC;
}

int
main(int argc, char** argv)
{
    int passes = ( argc > 1 ) ? atoi(argv[1]) : 500;
    double vertices = 0.0;
    double best_old = 0.0;
    double best_new = 0.0;
    double clip_share;
    int failed;

    if( passes <= 0 )
        passes = 500;

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    build_workload();
    clip_share = classify_by_depth();

    for( int m = 0; m < MODEL_COUNT; m++ )
        vertices += (double)g_models[m].num_vertices;

    printf(
        "proj-bench: %d models/pass, %.0f vertices/pass, %d passes\n",
        MODEL_COUNT,
        vertices,
        passes);

    failed = compare_kernels();

    /* Warm the arena and the branch predictors before the clock starts. */
    time_variant(0, 1);
    time_variant(1, 1);

    printf("\n            old (ms)   new (ms)    speedup\n");
    for( int rep = 0; rep < 5; rep++ )
    {
        /* Interleaved, so a thermal drift hits both variants equally. */
        double const a = time_variant(0, passes);
        double const b = time_variant(1, passes);

        printf("  rep %d:   %8.2f   %8.2f    %6.3fx\n", rep, a, b, a / b);

        if( best_old == 0.0 || a < best_old )
            best_old = a;
        if( best_new == 0.0 || b < best_new )
            best_new = b;
    }

    printf(
        "  best :   %8.2f   %8.2f    %6.3fx\n",
        best_old,
        best_new,
        best_old / best_new);
    printf(
        "  per-vertex: %.2f -> %.2f ns    per-model: %.2f -> %.2f ns\n",
        1e6 * best_old / ( vertices * passes ),
        1e6 * best_new / ( vertices * passes ),
        1e6 * best_old / ( (double)MODEL_COUNT * passes ),
        1e6 * best_new / ( (double)MODEL_COUNT * passes ));

    return failed;
}
