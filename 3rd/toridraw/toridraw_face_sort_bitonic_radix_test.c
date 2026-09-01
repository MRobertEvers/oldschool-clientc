/*
 * The bitonic+radix face sort against the bucket sort it replaces, order for
 * order.
 *
 * facesort.bitonic_radix.small.dispatch.u.c takes a different road to the same
 * place: a four-wide SIMD winding cull compacted into (depth, face) keys, a
 * bitonic network for a small model and a two-pass radix for a big one, and
 * the draw order read off the sorted keys. The bucket sort scatters faces into
 * per-depth lists and walks them far to near. Both must produce the SAME
 * draw order -- not "a correct back-to-front order" but the bucket walk's
 * exact order, face for face, because the painter's result depends on the
 * order of faces that share a depth, and the pixels of a frame drawn by one
 * must be the pixels drawn by the other.
 *
 * What is held equal, for every fixture:
 *
 *   - which faces are drawn (the winding cull, the near-clip sentinel, the
 *     depth-range gate);
 *   - their order, with and without face priorities (the priority partition
 *     reads (face, depth) pairs off the keys and must land where the bucket
 *     partition landed them);
 *   - the pre-sort stash the batched raster walk reads: the clip flag for
 *     every drawn face, and the y-sorted screen coordinates for every face
 *     the flag says is not clipped (compare() says why a CLIPPED face's
 *     coordinates are not part of the contract);
 *   - across both sort arms -- the bitonic network (<= 256 keys) and the
 *     radix (above it) -- and across the four-face block's tail (face counts
 *     that are not a multiple of four).
 *
 * The fixtures are random models through a real projection, so the screen
 * coordinates carry every case the projection produces: back faces,
 * degenerate faces, faces straddling the near plane (a model placed close
 * enough that vertices cross it), and faces beyond the depth table.
 *
 * Both sorts run through the face-sort kernel objects
 * (ToriDraw_FaceCullSortKernelGetBucket / GetBitonicRadix), so the test also
 * holds
 * the kernel dispatch to the sorts it names.
 *
 * Build and run:  make -C src test-face-sort-bitonic-radix
 */

#include "toridraw.h"
#include "toridraw_model.h"
#include "toridraw_raster_kernel.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEW_W 256
#define VIEW_H 200

static int g_failures;
static int g_fixtures;
static long g_faces_compared;
static long g_faces_drawn;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static uint32_t g_rng = 0x9E3779B9u;

static uint32_t
rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static int
rnd_range(int lo, int hi)
{
    return lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
}

/* A random closed-ish blob: vertices in a box, faces random triples of
 * distinct vertices, a random winding, so roughly half face away. */
static struct ToriDraw_Model*
make_model(
    int vertex_count,
    int face_count,
    int extent,
    int with_priorities, /* 0 none, 1 all twelve classes, 2 one value throughout */
    int with_alpha)
{
    struct ToriDraw_Model* m = ToriDraw_ModelNew(vertex_count, face_count, 0);
    assert(m);

    /* ToriDraw_ModelNew allocates the shell only. */
    m->vertices_x = malloc((size_t)vertex_count * sizeof(vertexint_t));
    m->vertices_y = malloc((size_t)vertex_count * sizeof(vertexint_t));
    m->vertices_z = malloc((size_t)vertex_count * sizeof(vertexint_t));
    m->face_indices_a = malloc((size_t)face_count * sizeof(faceint_t));
    m->face_indices_b = malloc((size_t)face_count * sizeof(faceint_t));
    m->face_indices_c = malloc((size_t)face_count * sizeof(faceint_t));
    m->face_colors_a = malloc((size_t)face_count * sizeof(hsl16_t));
    m->face_colors_b = malloc((size_t)face_count * sizeof(hsl16_t));
    m->face_colors_c = malloc((size_t)face_count * sizeof(hsl16_t));
    assert(m->vertices_x);
    assert(m->vertices_y);
    assert(m->vertices_z);
    assert(m->face_indices_a);
    assert(m->face_indices_b);
    assert(m->face_indices_c);
    assert(m->face_colors_a);
    assert(m->face_colors_b);
    assert(m->face_colors_c);
    if( with_alpha )
    {
        m->face_alphas = malloc((size_t)face_count * sizeof(alphaint_t));
        assert(m->face_alphas);
    }

    for( int i = 0; i < vertex_count; i++ )
    {
        m->vertices_x[i] = rnd_range(-extent, extent);
        m->vertices_y[i] = rnd_range(-extent, extent);
        m->vertices_z[i] = rnd_range(-extent, extent);
    }
    for( int f = 0; f < face_count; f++ )
    {
        int a = rnd_range(0, vertex_count - 1);
        int b = rnd_range(0, vertex_count - 1);
        int c = rnd_range(0, vertex_count - 1);
        /* A few degenerate faces are wanted -- they test the cull's zero
         * winding -- so distinct vertices are not forced. */
        m->face_indices_a[f] = (faceint_t)a;
        m->face_indices_b[f] = (faceint_t)b;
        m->face_indices_c[f] = (faceint_t)c;
        m->face_colors_a[f] = (hsl16_t)(rnd() & 0xFFFF);
        m->face_colors_b[f] = (hsl16_t)(rnd() & 0xFFFF);
        m->face_colors_c[f] = (hsl16_t)(rnd() & 0xFFFF);
        if( with_alpha )
            m->face_alphas[f] = (alphaint_t)(rnd() & 0xFF);
    }
    if( with_priorities )
    {
        size_t nbytes = (size_t)((face_count + 1) / 2);
        m->face_priorities = calloc(nbytes, 1);
        assert(m->face_priorities);
        /* with_priorities == 2: one value on every face, so the uniform
         * fast path (toridraw_face_priorities_uniform) is what gets compared
         * against the bucket lane's full partition; the value cycles through
         * the fixed bands and both flexible ones across models. */
        int const uniform = rnd_range(0, 11);
        for( int f = 0; f < face_count; f++ )
        {
            /* All twelve priority classes, the flexible 10 and 11 included,
             * with a bias toward the common few so runs are long enough for
             * the partition's within-run order to matter. */
            int prio = (rnd() & 3) ? rnd_range(0, 3) : rnd_range(0, 11);
            if( with_priorities == 2 )
                prio = uniform;
            m->face_priorities[f >> 1] |= (uint8_t)(prio << ((f & 1) * 4));
        }
    }
    ToriDraw_ModelSetBoundsCylinder(m);
    return m;
}

/*
 * A world terrain tile, built exactly as world_decode_tile builds one of the
 * three 4-vertex, 2-triangle shapes: four corners in SW, SE, NE, NW order,
 * faces (1,2,3) and (0,1,3), each corner index turned by the rotation. Those
 * are 94% of the tiles a loaded map contains, and tile_sort_kernel is what
 * puts them on the compile-time kernel in toridraw_face_sort_bitonic_radix.u.c.
 *
 * Heights are random per corner, which is the point: it is the corner heights
 * that decide the winding of each half of the quad, so a fixture set over
 * random heights covers a back-facing tile, a degenerate flat one, and the two
 * halves disagreeing -- the cases the kernel's two lanes have to get right
 * independently.
 */
static struct ToriDraw_Model*
make_tile_model(int rotation, int height_extent)
{
    static const int base_a[2] = { 1, 0 };
    static const int base_b[2] = { 2, 1 };
    static const int base_c[2] = { 3, 3 };
    static const int corner_x[4] = { -64, 64, 64, -64 }; /* SW, SE, NE, NW */
    static const int corner_z[4] = { -64, -64, 64, 64 };
    struct ToriDraw_Model* m = ToriDraw_ModelNew(4, 2, 0);
    assert(m);

    m->vertices_x = malloc(4 * sizeof(vertexint_t));
    m->vertices_y = malloc(4 * sizeof(vertexint_t));
    m->vertices_z = malloc(4 * sizeof(vertexint_t));
    m->face_indices_a = malloc(2 * sizeof(faceint_t));
    m->face_indices_b = malloc(2 * sizeof(faceint_t));
    m->face_indices_c = malloc(2 * sizeof(faceint_t));
    m->face_colors_a = malloc(2 * sizeof(hsl16_t));
    m->face_colors_b = malloc(2 * sizeof(hsl16_t));
    m->face_colors_c = malloc(2 * sizeof(hsl16_t));
    assert(m->vertices_x);
    assert(m->vertices_y);
    assert(m->vertices_z);
    assert(m->face_indices_a);
    assert(m->face_indices_b);
    assert(m->face_indices_c);
    assert(m->face_colors_a);
    assert(m->face_colors_b);
    assert(m->face_colors_c);

    for( int i = 0; i < 4; i++ )
    {
        m->vertices_x[i] = (vertexint_t)corner_x[i];
        m->vertices_z[i] = (vertexint_t)corner_z[i];
        m->vertices_y[i] = (vertexint_t)rnd_range(-height_extent, height_extent);
    }
    for( int f = 0; f < 2; f++ )
    {
        m->face_indices_a[f] = (faceint_t)((base_a[f] - rotation) & 3);
        m->face_indices_b[f] = (faceint_t)((base_b[f] - rotation) & 3);
        m->face_indices_c[f] = (faceint_t)((base_c[f] - rotation) & 3);
        m->face_colors_a[f] = (hsl16_t)(rnd() & 0xFFFF);
        m->face_colors_b[f] = (hsl16_t)(rnd() & 0xFFFF);
        m->face_colors_c[f] = (hsl16_t)(rnd() & 0xFFFF);
    }
    m->tile_sort_kernel = (uint8_t)(1 + rotation);
    ToriDraw_ModelSetBoundsCylinder(m);
    return m;
}

struct SortResult
{
    int count;
    int stash_valid;
    int order[8192];
    int x4[8192 * 4];
    int y4[8192 * 4];
};

static void
capture(struct ToriDraw_Scene* scene, struct SortResult* out)
{
    out->count = scene->tmp_face_order_count;
    out->stash_valid = scene->sm_face_xy_valid;
    memcpy(out->order, scene->tmp_face_order, (size_t)out->count * sizeof(int));
    if( out->stash_valid )
    {
        for( int i = 0; i < out->count; i++ )
        {
            int f = out->order[i];
            memcpy(&out->x4[i * 4], &scene->sm_face_x4[f * 4], 4 * sizeof(int));
            memcpy(&out->y4[i * 4], &scene->sm_face_y4[f * 4], 4 * sizeof(int));
        }
    }
}

static void
compare(
    const char* label,
    const struct SortResult* bucket,
    const struct SortResult* keys)
{
    int n = bucket->count < keys->count ? bucket->count : keys->count;

    g_fixtures++;
    g_faces_drawn += bucket->count;
    CHECK(bucket->count == keys->count, "%s: drawn count bucket=%d keys=%d", label,
          bucket->count, keys->count);
    CHECK(bucket->stash_valid == keys->stash_valid, "%s: stash_valid bucket=%d keys=%d",
          label, bucket->stash_valid, keys->stash_valid);
    for( int i = 0; i < n; i++ )
    {
        g_faces_compared++;
        if( bucket->order[i] != keys->order[i] )
        {
            CHECK(0, "%s: order[%d] bucket=face %d keys=face %d", label, i,
                  bucket->order[i], keys->order[i]);
            return;
        }
        if( bucket->stash_valid && keys->stash_valid )
        {
            const int* bx = &bucket->x4[i * 4];
            const int* fx = &keys->x4[i * 4];
            const int* by = &bucket->y4[i * 4];
            const int* fy = &keys->y4[i * 4];
            /*
             * x4[3] is the near-clip flag and is the whole contract for a
             * clipped face: it is what makes the batched walk hand the face to
             * the clip builder instead of staging a row, so the other seven
             * values are never read for one (three sites in
             * toridraw_raster.u.c, all spelled `face_x4[face * 4 + 3]`).
             *
             * The two sorts genuinely differ there and always have. The bucket
             * walk and the key sort's scalar tail skip a clip candidate's
             * winding, so they never load its coordinates and write only the
             * flag; the four-wide block and the tile kernel compute all four
             * lanes unconditionally and store what they got. Holding them equal
             * would be pinning a value neither one promises -- so the flag is
             * compared for every drawn face, and the rest only when it is
             * clear.
             */
            int const clipped = bx[3] != 0;
            size_t const n_cmp = clipped ? 0 : 3 * sizeof(int);
            if( bx[3] != fx[3] || memcmp(bx, fx, n_cmp) ||
                memcmp(by, fy, clipped ? 0 : 4 * sizeof(int)) )
            {
                CHECK(0,
                      "%s: stash for face %d differs: bucket x(%d,%d,%d,%d) y(%d,%d,%d,%d) "
                      "keys x(%d,%d,%d,%d) y(%d,%d,%d,%d)",
                      label, bucket->order[i], bx[0], bx[1], bx[2], bx[3], by[0], by[1],
                      by[2], by[3], fx[0], fx[1], fx[2], fx[3], fy[0], fy[1], fy[2], fy[3]);
                return;
            }
        }
    }
}

static struct SortResult g_bucket_result;
static struct SortResult g_keys_result;

static void
run_fixture(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Model* model,
    int distance,
    int yaw,
    int presort,
    const char* label)
{
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(model);
    struct ToriDraw_ViewPort viewport = {
        .width = VIEW_W,
        .height = VIEW_H,
        .stride = VIEW_W,
        .x_center = VIEW_W / 2,
        .y_center = VIEW_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = VIEW_W,
        .clip_bottom = VIEW_H,
    };
    struct ToriDraw_Camera camera = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
        .pitch = 128,
        .yaw = (yaw * 7) & 2047,
    };
    struct ToriDraw_Position position = {
        .x = rnd_range(-100, 100),
        .y = rnd_range(-60, 60),
        .z = distance,
        .yaw = yaw & 2047,
    };
    const struct ToriDraw_FaceCullSortKernel* bucket = ToriDraw_FaceCullSortKernelGetBucket();
    const struct ToriDraw_FaceCullSortKernel* keys =
        ToriDraw_FaceCullSortKernelGetBitonicRadix();
    int cull;

    cull = ToriDraw_RenderModel1Project(hnd, scene, &position, &viewport, &camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return;

    /* The stash is written only when the batched walk is armed; arm it for
     * both so the y-order permutation is compared too. */
    ToriDraw_RasterBatchSetArmed(1);

    bucket->sort(bucket->user_data, scene, hnd, presort != 0);
    capture(scene, &g_bucket_result);

    /* Dirty the outputs between the two so the key sort cannot pass by
     * leaving the bucket sort's answer in place. */
    memset(scene->tmp_face_order, 0x7F, (size_t)model->face_count * sizeof(int));
    if( scene->sm_face_x4 )
    {
        memset(scene->sm_face_x4, 0x55, (size_t)model->face_count * 4 * sizeof(int));
        memset(scene->sm_face_y4, 0x55, (size_t)model->face_count * 4 * sizeof(int));
    }
    scene->sm_face_xy_valid = 0;
    scene->tmp_face_order_count = -1;

    keys->sort(keys->user_data, scene, hnd, presort != 0);
    capture(scene, &g_keys_result);

    compare(label, &g_bucket_result, &g_keys_result);
}

/*
 * Timing mode: TORIDRAW_FACE_SORT_BENCH=1. After the parity pass, time both
 * sorts on projected models of a few sizes, presort stash on. Same process,
 * same scratch, alternating ABAB per repetition so a frequency ramp lands on
 * both. Reports microseconds per sort and nanoseconds per input face.
 */
#include <time.h>

static double
now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

/* A skipped arm (TORIDRAW_FACE_SORT_BENCH_ARM) keeps its 1e30 placeholder; print
 * it as a blank rather than a 31-digit number. */
static double
bench_shown(double best)
{
    return best < 1e29 ? best : 0.0;
}

static double
bench_ratio(double bucket, double keys)
{
    return (bucket < 1e29 && keys < 1e29 && bucket > 0.0) ? keys / bucket : 0.0;
}

static void
bench_sorts(struct ToriDraw_Scene* scene)
{
    /* TORIDRAW_FACE_SORT_BENCH_PRESORT=0 times the sort WITHOUT the y-sorted
     * XY stash the software raster asks for: what a GPU lane (GLES2, D3D9)
     * actually runs, since its kernel table has no raster to presort for. */
    bool const bench_presort =
        !getenv("TORIDRAW_FACE_SORT_BENCH_PRESORT") || atoi(getenv("TORIDRAW_FACE_SORT_BENCH_PRESORT")) != 0;
    /* TORIDRAW_FACE_SORT_BENCH_ARM=bucket|keys runs ONE arm only, so a
     * hardware-counter run (`simpleperf stat`) attributes its cycles,
     * instructions and misses to that kernel alone rather than to the two
     * interleaved. Both arms by default, as the table's ratio column needs. */
    const char* const bench_arm_only = getenv("TORIDRAW_FACE_SORT_BENCH_ARM");
    int const arm_first = bench_arm_only && strcmp(bench_arm_only, "keys") == 0 ? 1 : 0;
    int const arm_count = bench_arm_only ? 1 : 2;
    /* Many models per size, same vertices, different faces: the sort sees a
     * different index stream each call, so the branch predictor cannot learn
     * one model's winding/y-order sequence across repetitions the way it
     * would with a single model looped hot -- which is the trap a single
     * repeated model falls into, and no real frame repeats a model. */
    enum { MODELS = 64 };
    static const int sizes[] = { 64, 200, 256, 1000, 2000 };
    const struct ToriDraw_FaceCullSortKernel* k[2] = {
        ToriDraw_FaceCullSortKernelGetBucket(), ToriDraw_FaceCullSortKernelGetBitonicRadix()
    };
    struct ToriDraw_ViewPort viewport = {
        .width = VIEW_W, .height = VIEW_H, .stride = VIEW_W,
        .x_center = VIEW_W / 2, .y_center = VIEW_H / 2,
        .clip_right = VIEW_W, .clip_bottom = VIEW_H,
    };
    struct ToriDraw_Camera camera = {
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
        .near_plane_z = 50,
        .pitch = 0,
    };
    struct ToriDraw_Position position = { .z = 900 };

    ToriDraw_RasterBatchSetArmed(1);
    printf("\n%-8s %-8s %12s %12s %12s %12s   %s\n", "faces", "drawn", "bucket us",
           "keys us", "bucket ns/f", "keys ns/f", "keys/bucket");
    for( size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++ )
    {
        int const fc = sizes[si];
        struct ToriDraw_Model* models[MODELS];
        struct ToriDraw_ModelHandle hnds[MODELS];
        int const reps = 4000000 / fc;
        double best[2] = { 1e30, 1e30 };
        long drawn = 0;
        int m;
        int cull;

        for( m = 0; m < MODELS; m++ )
        {
            models[m] = make_model(fc, fc, 120, 0, 0);
            if( m > 0 )
            {
                memcpy(models[m]->vertices_x, models[0]->vertices_x, (size_t)fc * sizeof(vertexint_t));
                memcpy(models[m]->vertices_y, models[0]->vertices_y, (size_t)fc * sizeof(vertexint_t));
                memcpy(models[m]->vertices_z, models[0]->vertices_z, (size_t)fc * sizeof(vertexint_t));
                ToriDraw_ModelSetBoundsCylinder(models[m]);
            }
            hnds[m] = ToriDraw_ModelHandleOwned(models[m]);
        }
        cull = ToriDraw_RenderModel1Project(hnds[0], scene, &position, &viewport, &camera);
        if( cull != TORIDRAW_CULL_VISIBLE )
        {
            printf("%-8d culled (%d)\n", fc, cull);
            for( m = 0; m < MODELS; m++ )
                ToriDraw_ModelFree(models[m]);
            continue;
        }
        /* Five batches, best-of, arms alternating inside each batch. */
        for( int batch = 0; batch < 5; batch++ )
        {
            for( int arm = 0; arm < arm_count; arm++ )
            {
                double t0;
                int a = arm_count == 1 ? arm_first : ((batch & 1) ? 1 - arm : arm);
                for( m = 0; m < MODELS; m++ )
                    k[a]->sort(k[a]->user_data, scene, hnds[m], bench_presort); /* warm */
                drawn = 0;
                t0 = now_us();
                for( int r = 0; r < reps; r++ )
                    drawn += k[a]->sort(k[a]->user_data, scene, hnds[r % MODELS], bench_presort);
                t0 = (now_us() - t0) / reps;
                if( t0 < best[a] )
                    best[a] = t0;
            }
        }
        printf("%-8d %-8ld %12.3f %12.3f %12.2f %12.2f   %.2fx\n", fc, drawn / reps,
               bench_shown(best[0]), bench_shown(best[1]), bench_shown(best[0]) * 1000.0 / fc,
               bench_shown(best[1]) * 1000.0 / fc, bench_ratio(best[0], best[1]));
        for( m = 0; m < MODELS; m++ )
            ToriDraw_ModelFree(models[m]);
    }

    /*
     * The terrain tile, timed the same way. Two faces is far too few to
     * amortise anything, which is exactly why it is worth a row: a scene draws
     * hundreds of these per frame and the sort's per-MODEL cost is what they
     * pay, not its per-face cost.
     *
     * TORIDRAW_TILE_SORT=0 in the environment puts the tile back on the
     * general path, so running this binary twice gives the kernel's own
     * before/after in the `keys` column, with the bucket column as the
     * control that must not move between the two runs.
     */
    {
        enum { TILE_MODELS = 256 };
        struct ToriDraw_Model* models[TILE_MODELS];
        struct ToriDraw_ModelHandle hnds[TILE_MODELS];
        int const reps = 200000;
        double best[2] = { 1e30, 1e30 };
        long drawn = 0;
        int m;

        /* Rotations in the census proportion: 94% at 0, the rest spread. */
        for( m = 0; m < TILE_MODELS; m++ )
        {
            int rotation = (m % 16 == 5) ? 1 + (m % 3) : 0;
            models[m] = make_tile_model(rotation, 120);
            hnds[m] = ToriDraw_ModelHandleOwned(models[m]);
        }
        if( ToriDraw_RenderModel1Project(hnds[0], scene, &position, &viewport, &camera) !=
            TORIDRAW_CULL_VISIBLE )
        {
            printf("tile     culled\n");
        }
        else
        {
            for( int batch = 0; batch < 5; batch++ )
            {
                for( int arm = 0; arm < arm_count; arm++ )
                {
                    double t0;
                    int a = arm_count == 1 ? arm_first : ((batch & 1) ? 1 - arm : arm);
                    for( m = 0; m < TILE_MODELS; m++ )
                        k[a]->sort(k[a]->user_data, scene, hnds[m], bench_presort); /* warm */
                    drawn = 0;
                    t0 = now_us();
                    for( int r = 0; r < reps; r++ )
                        drawn += k[a]->sort(k[a]->user_data, scene, hnds[r % TILE_MODELS], bench_presort);
                    t0 = (now_us() - t0) / reps;
                    if( t0 < best[a] )
                        best[a] = t0;
                }
            }
            printf("%-8s %-8ld %12.3f %12.3f %12.2f %12.2f   %.2fx   (tile kernel %s)\n", "tile2",
                   drawn / reps, bench_shown(best[0]), bench_shown(best[1]),
                   bench_shown(best[0]) * 1000.0 / 2, bench_shown(best[1]) * 1000.0 / 2,
                   bench_ratio(best[0], best[1]),
                   getenv("TORIDRAW_TILE_SORT") ? getenv("TORIDRAW_TILE_SORT")
                                                : "1 (scalar, default)");
        }
        for( m = 0; m < TILE_MODELS; m++ )
            ToriDraw_ModelFree(models[m]);
    }
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    static const int face_counts[] = {
        1,   2,   3,   4,   5,   7,   8,   9,   15,  16,  17,  63,   64,   65,
        100, 127, 128, 129, 200, 255, 256, 257, 258, 300, 511, 512, 1000, 1500,
    };
    char label[128];

    ToriDraw_Init();

    /* The small tier is the world painter's; it is the one with the
     * bitonic+radix sort's buffers. LOW_2K holds 2048 faces, so every count
     * above fits. */
    /* DEPTH_16K: what the Android client runs (src/app.c), and what sizes
     * the radix's digits -- the bench has to sort the same key range. */
    scene = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_DEPTH_16K, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);

    for( size_t fi = 0; fi < sizeof(face_counts) / sizeof(face_counts[0]); fi++ )
    {
        int face_count = face_counts[fi];
        int vertex_count = face_count < 8 ? 8 : (face_count > 2000 ? 2000 : face_count);

        for( int variant = 0; variant < 32; variant++ )
        {
            /* Bit 4 turns the priority fixtures uniform: 0 none, 1 varied,
             * 2 one value (the fast path). Unpriced models are run once. */
            int with_priorities = (variant & 1) ? ((variant & 16) ? 2 : 1) : 0;
            if( !(variant & 1) && (variant & 16) )
                continue;
            int with_alpha = (variant & 2) != 0;
            /* 16 variants and not 12, so `presort` and the near distance
             * overlap: at 12 the two bits were disjoint and the stash was
             * never once compared on a model carrying a clipped face. */
            int presort = (variant & 4) != 0;
            /* Near enough that some vertices cross the near plane on the
             * odd variants, far enough that none do on the even ones. */
            int extent = 120;
            int distance = (variant & 8) ? 120 : 900;
            struct ToriDraw_Model* model =
                make_model(vertex_count, face_count, extent, with_priorities, with_alpha);

            for( int rep = 0; rep < 4; rep++ )
            {
                snprintf(label, sizeof(label),
                         "faces=%d prio=%d alpha=%d presort=%d dist=%d rep=%d", face_count,
                         with_priorities, with_alpha, presort, distance, rep);
                run_fixture(scene, model, distance, rep * 373, presort, label);
                if( g_failures > 20 )
                    goto done;
            }
            ToriDraw_ModelFree(model);
        }
    }

    /* The terrain tile, which the key sort answers with a kernel of its own
     * rather than with the four-face block and the scalar tail. Held to the
     * same bucket-walk order as everything above, over all four rotations and
     * over near enough that a corner crosses the near plane. */
    for( int rotation = 0; rotation < 4 && g_failures <= 20; rotation++ )
    {
        for( int variant = 0; variant < 8; variant++ )
        {
            int presort = (variant & 1) != 0;
            int distance = (variant & 2) ? 120 : 900;
            int height_extent = (variant & 4) ? 0 : 120; /* 0 = a flat tile */
            struct ToriDraw_Model* model = make_tile_model(rotation, height_extent);

            for( int rep = 0; rep < 16; rep++ )
            {
                snprintf(label, sizeof(label),
                         "tile rot=%d presort=%d dist=%d height=%d rep=%d", rotation,
                         presort, distance, height_extent, rep);
                run_fixture(scene, model, distance, rep * 373, presort, label);
                if( g_failures > 20 )
                    break;
            }
            ToriDraw_ModelFree(model);
        }
    }

done:
    if( !g_failures && getenv("TORIDRAW_FACE_SORT_BENCH") )
        bench_sorts(scene);
    ToriDraw_SceneFree(scene);
    printf("k16 census: %d models took the K16 block, %d declined (A32 lane only)\n",
           g_toridraw_sort_k16_models, g_toridraw_sort_k16_declined);
    printf("face sort bitonic+radix vs bucket: %d fixtures, %ld faces compared, "
           "%ld drawn -- %s\n",
           g_fixtures, g_faces_compared, g_faces_drawn, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
