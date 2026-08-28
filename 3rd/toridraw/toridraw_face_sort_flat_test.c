/*
 * The flat face sort against the bucket sort it replaces, order for order.
 *
 * toridraw_face_sort_flat.u.c takes a different road to the same place: a
 * four-wide SIMD winding cull compacted into (depth, face) keys, a bitonic
 * network for a small model and a two-pass radix for a big one, and the
 * draw order read off the sorted keys. The bucket sort scatters faces into
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
 *   - the pre-sort stash the batched raster walk reads: the y-sorted screen
 *     coordinates and the clip flag, for every drawn face;
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
 * (ToriDraw_FaceCullSortKernelGetBucket / GetFlat), so the test also holds
 * the kernel dispatch to the sorts it names.
 *
 * Build and run:  make -C src test-face-sort-flat
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
make_model(int vertex_count, int face_count, int extent, int with_priorities, int with_alpha)
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
        for( int f = 0; f < face_count; f++ )
        {
            /* All twelve priority classes, the flexible 10 and 11 included,
             * with a bias toward the common few so runs are long enough for
             * the partition's within-run order to matter. */
            int prio = (rnd() & 3) ? rnd_range(0, 3) : rnd_range(0, 11);
            m->face_priorities[f >> 1] |= (uint8_t)(prio << ((f & 1) * 4));
        }
    }
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
    const struct SortResult* flat)
{
    int n = bucket->count < flat->count ? bucket->count : flat->count;

    g_fixtures++;
    g_faces_drawn += bucket->count;
    CHECK(bucket->count == flat->count, "%s: drawn count bucket=%d flat=%d", label,
          bucket->count, flat->count);
    CHECK(bucket->stash_valid == flat->stash_valid, "%s: stash_valid bucket=%d flat=%d",
          label, bucket->stash_valid, flat->stash_valid);
    for( int i = 0; i < n; i++ )
    {
        g_faces_compared++;
        if( bucket->order[i] != flat->order[i] )
        {
            CHECK(0, "%s: order[%d] bucket=face %d flat=face %d", label, i,
                  bucket->order[i], flat->order[i]);
            return;
        }
        if( bucket->stash_valid && flat->stash_valid )
        {
            const int* bx = &bucket->x4[i * 4];
            const int* fx = &flat->x4[i * 4];
            const int* by = &bucket->y4[i * 4];
            const int* fy = &flat->y4[i * 4];
            if( memcmp(bx, fx, 4 * sizeof(int)) || memcmp(by, fy, 4 * sizeof(int)) )
            {
                CHECK(0,
                      "%s: stash for face %d differs: bucket x(%d,%d,%d,%d) y(%d,%d,%d,%d) "
                      "flat x(%d,%d,%d,%d) y(%d,%d,%d,%d)",
                      label, bucket->order[i], bx[0], bx[1], bx[2], bx[3], by[0], by[1],
                      by[2], by[3], fx[0], fx[1], fx[2], fx[3], fy[0], fy[1], fy[2], fy[3]);
                return;
            }
        }
    }
}

static struct SortResult g_bucket_result;
static struct SortResult g_flat_result;

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
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
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
    const struct ToriDraw_FaceCullSortKernel* flat = ToriDraw_FaceCullSortKernelGetFlat();
    int cull;

    cull = ToriDraw_RenderModel1Project(hnd, scene, &position, &viewport, &camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return;

    /* The stash is written only when the batched walk is armed; arm it for
     * both so the y-order permutation is compared too. */
    ToriDraw_RasterBatchSetArmed(1);

    bucket->sort(bucket->user_data, scene, hnd, presort != 0);
    capture(scene, &g_bucket_result);

    /* Dirty the outputs between the two so the flat sort cannot pass by
     * leaving the bucket sort's answer in place. */
    memset(scene->tmp_face_order, 0x7F, (size_t)model->face_count * sizeof(int));
    if( scene->sm_face_x4 )
    {
        memset(scene->sm_face_x4, 0x55, (size_t)model->face_count * 4 * sizeof(int));
        memset(scene->sm_face_y4, 0x55, (size_t)model->face_count * 4 * sizeof(int));
    }
    scene->sm_face_xy_valid = 0;
    scene->tmp_face_order_count = -1;

    flat->sort(flat->user_data, scene, hnd, presort != 0);
    capture(scene, &g_flat_result);

    compare(label, &g_bucket_result, &g_flat_result);
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

    /* The small tier is the world painter's; it is the one with the flat
     * sort's buffers. LOW_2K holds 2048 faces, so every count above fits. */
    scene = ToriDraw_SceneNew(TORIDRAW_SCENE_SMALL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);

    for( size_t fi = 0; fi < sizeof(face_counts) / sizeof(face_counts[0]); fi++ )
    {
        int face_count = face_counts[fi];
        int vertex_count = face_count < 8 ? 8 : (face_count > 2000 ? 2000 : face_count);

        for( int variant = 0; variant < 12; variant++ )
        {
            int with_priorities = (variant & 1);
            int with_alpha = (variant & 2) != 0;
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

done:
    ToriDraw_SceneFree(scene);
    printf("face sort flat vs bucket: %d fixtures, %ld faces compared, %ld drawn -- %s\n",
           g_fixtures, g_faces_compared, g_faces_drawn, g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
