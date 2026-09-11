/*
 * The adaptor's four disagreements, each pinned by the thing that goes wrong
 * when it is not handled.
 *
 * The fixture is round-tripped through RSCache_ModelEncode rather than read
 * from a cache, so this test needs no cache on disk and no revision profile --
 * and it exercises the blob entry point, which is the one a small client
 * actually calls.
 */
#include "toridraw_rscache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while( 0 )

#define FIX_VERTS 4
#define FIX_FACES 4

static const int FIX_VX[FIX_VERTS] = { 0, -100, 100, 0 };
static const int FIX_VY[FIX_VERTS] = { -140, 40, 40, 40 };
static const int FIX_VZ[FIX_VERTS] = { 0, -60, -60, 100 };
static const int FIX_FA[FIX_FACES] = { 0, 0, 0, 1 };
static const int FIX_FB[FIX_FACES] = { 1, 2, 3, 3 };
static const int FIX_FC[FIX_FACES] = { 2, 3, 1, 2 };

static int*
dup_ints(
    const int* v,
    int n)
{
    int* p = malloc((size_t)n * sizeof(int));

    memcpy(p, v, (size_t)n * sizeof(int));
    return p;
}

/* A decoded cache model, as RSCache would hand one over. `format_version`
 * selects the scale-down arm. */
static void
fixture_init(
    struct RSCache_Model* src,
    int format_version)
{
    int i;

    memset(src, 0, sizeof(*src));
    src->vertex_count = FIX_VERTS;
    src->face_count = FIX_FACES;
    src->vertices_x = dup_ints(FIX_VX, FIX_VERTS);
    src->vertices_y = dup_ints(FIX_VY, FIX_VERTS);
    src->vertices_z = dup_ints(FIX_VZ, FIX_VERTS);
    src->face_indices_a = dup_ints(FIX_FA, FIX_FACES);
    src->face_indices_b = dup_ints(FIX_FB, FIX_FACES);
    src->face_indices_c = dup_ints(FIX_FC, FIX_FACES);
    src->face_colors = malloc(FIX_FACES * sizeof(uint16_t));
    src->face_priorities = malloc(FIX_FACES);
    for( i = 0; i < FIX_FACES; i++ )
    {
        src->face_colors[i] = (uint16_t)(4000 + i * 900);
        /* Deliberately not all equal, and spanning both nibbles of a packed
         * byte: an even/odd mixup in the repack is invisible on a uniform
         * array. */
        src->face_priorities[i] = (uint8_t)(i * 3);
    }
    src->format_version = format_version;
}

static void
fixture_cleanup(struct RSCache_Model* src)
{
    free(src->vertices_x);
    free(src->vertices_y);
    free(src->vertices_z);
    free(src->face_indices_a);
    free(src->face_indices_b);
    free(src->face_indices_c);
    free(src->face_colors);
    free(src->face_priorities);
    memset(src, 0, sizeof(*src));
}

/* -------------------------------------------------------------------- tests */

static void
test_copy_leaves_the_source_intact(void)
{
    struct RSCache_Model src;
    struct ToriDraw_Model* dst;
    int i;

    fixture_init(&src, 1);
    dst = ToriDraw_RSCacheModelNew(&src);

    CHECK(dst->vertex_count == FIX_VERTS);
    CHECK(dst->face_count == FIX_FACES);
    for( i = 0; i < FIX_VERTS; i++ )
        CHECK(dst->vertices_x[i] == (vertexint_t)FIX_VX[i]);

    /* The whole difference from Steal: nothing was taken. */
    CHECK(src.vertices_x != NULL);
    CHECK(src.face_colors != NULL);
    CHECK(src.face_indices_a != NULL);

    ToriDraw_ModelFree(dst);
    fixture_cleanup(&src);
}

static void
test_steal_hollows_the_source(void)
{
    struct RSCache_Model src;
    struct ToriDraw_Model* dst;
    uint16_t* colors_before;

    fixture_init(&src, 1);
    colors_before = src.face_colors;
    dst = ToriDraw_RSCacheModelSteal(&src);

    /* Moved by POINTER, not copied: that is the saving the entry point exists
     * for, and it is the thing a memcpy-based rewrite would silently undo. */
    CHECK((void*)dst->face_colors == (void*)colors_before);
    CHECK(src.face_colors == NULL);

    /* Narrowed, so necessarily copied, and the source freed. */
    CHECK(src.vertices_x == NULL);
    CHECK(src.face_indices_a == NULL);
    CHECK(dst->vertices_x[1] == (vertexint_t)FIX_VX[1]);

    ToriDraw_ModelFree(dst);
    fixture_cleanup(&src);
}

/*
 * Disagreement 3. A version-13 model's vertices are stored at 4x and the
 * reference shifts them down; RSCache does not, because its bar is byte-exact
 * round-trip. Without this every 643-era model draws four times too large.
 */
static void
test_format_13_scales_down(void)
{
    struct RSCache_Model src;
    struct ToriDraw_Model* plain;
    struct ToriDraw_Model* scaled;
    int i;

    fixture_init(&src, 12);
    plain = ToriDraw_RSCacheModelNew(&src);
    fixture_cleanup(&src);

    fixture_init(&src, 13);
    scaled = ToriDraw_RSCacheModelNew(&src);
    fixture_cleanup(&src);

    for( i = 0; i < FIX_VERTS; i++ )
    {
        CHECK(plain->vertices_x[i] == (vertexint_t)FIX_VX[i]);
        /* Arithmetic >>, matching the reference's JS >> on negatives -- which
         * is why the fixture carries negative coordinates. */
        CHECK(scaled->vertices_y[i] == (vertexint_t)(FIX_VY[i] >> 2));
    }
    /* At least one coordinate must actually be negative, or the arithmetic
     * shift above is only being tested on values where it cannot differ from a
     * logical one. */
    CHECK(FIX_VY[0] < 0);

    ToriDraw_ModelFree(plain);
    ToriDraw_ModelFree(scaled);
}

/* Disagreement 2: one byte per priority becomes two nibbles per byte. */
static void
test_priorities_repack_into_nibbles(void)
{
    struct RSCache_Model src;
    struct ToriDraw_Model* dst;
    int i;

    fixture_init(&src, 1);
    dst = ToriDraw_RSCacheModelNew(&src);

    CHECK(dst->face_priorities != NULL);
    for( i = 0; i < FIX_FACES; i++ )
        CHECK(ToriDraw_ModelGetFacePriority(dst->face_priorities, i) == i * 3);

    ToriDraw_ModelFree(dst);
    fixture_cleanup(&src);
}

static void
test_blob_round_trip_and_lighting(void)
{
    struct RSCache_Model src;
    struct RSCache cache;
    struct ToriDraw_Model* model;
    uint8_t* blob;
    uint32_t cap;
    int n;
    int i;
    int lit = 0;

    fixture_init(&src, 1);

    memset(&cache, 0, sizeof(cache));
    cap = RSCache_ModelEncodeBound(&src, NULL);
    blob = malloc(cap);
    n = RSCache_ModelEncode(&cache, &src, NULL, blob, cap);
    CHECK(n > 0);
    fixture_cleanup(&src);

    model = ToriDraw_RSCacheModelFromBlob(blob, n);
    free(blob);
    CHECK(model != NULL);
    if( !model )
        return;

    CHECK(model->vertex_count == FIX_VERTS);
    CHECK(model->face_count == FIX_FACES);
    for( i = 0; i < FIX_VERTS; i++ )
        CHECK(model->vertices_z[i] == (vertexint_t)FIX_VZ[i]);

    /* The conversion computes the bounds. Without them ToriDraw_Project
     * returns TORIDRAW_CULL_ERROR and the model draws nothing at all, with no
     * other symptom. */
    CHECK(model->has_bounds_cylinder);
    CHECK(model->bounds_cylinder.radius > 0);
    CHECK(model->bounds_cylinder.min_z_depth_any_rotation > 0);

    /* Allocated by the conversion, filled by the lighting, and separate from
     * the flat face_colors the cache carries. A model that skips the lighting
     * draws black rather than crashing, which is why they exist this early. */
    CHECK(model->face_colors_a != NULL);
    for( i = 0; i < FIX_FACES; i++ )
        CHECK(model->face_colors_a[i] == 0);

    ToriDraw_RSCacheModelLight(model, NULL);
    for( i = 0; i < FIX_FACES; i++ )
        if( model->face_colors_a[i] || model->face_colors_b[i] || model->face_colors_c[i] )
            lit++;
    CHECK(lit == FIX_FACES);

    /* Released after lighting: ten bytes a vertex, and nothing downstream
     * reads them. */
    CHECK(model->normals == NULL);

    ToriDraw_ModelFree(model);
}

/* The point of all of the above: it draws. */
static void
test_it_draws(void)
{
    static _Alignas(TORIDRAW_ARENA_ALIGN) uint8_t arena[32 * 1024];
    static toripixel_t framebuffer[48 * 48];

    struct RSCache_Model src;
    struct ToriDraw_Model* model;
    struct ToriDraw_ModelHandle hnd;
    struct ToriDraw_MiniLimits limits;
    struct ToriDraw_MiniView* view;
    struct ToriDraw_MiniTarget target;
    struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
    int lit = 0;
    int i;

    fixture_init(&src, 1);
    model = ToriDraw_RSCacheModelSteal(&src);
    fixture_cleanup(&src);
    ToriDraw_RSCacheModelLight(model, NULL);

    hnd = ToriDraw_ModelHandleOwned(model);
    ToriDraw_MiniLimitsForModel(hnd, &limits);
    CHECK(ToriDraw_MiniViewBytes(&limits) <= sizeof(arena));

    view = ToriDraw_MiniViewInit(arena, sizeof(arena), &limits);

    target.pixels = framebuffer;
    target.width = 48;
    target.height = 48;
    target.stride = 48;
    ToriDraw_MiniClear(&target, (toripixel_t)0);
    CHECK(ToriDraw_MiniDrawModel(view, hnd, &target, &pose));

    for( i = 0; i < 48 * 48; i++ )
        if( framebuffer[i] != (toripixel_t)0 )
            lit++;
    CHECK(lit > 100);

    ToriDraw_ModelHandleFree(hnd);
}

int
main(void)
{
    ToriDraw_Init();

    test_copy_leaves_the_source_intact();
    test_steal_hollows_the_source();
    test_format_13_scales_down();
    test_priorities_repack_into_nibbles();
    test_blob_round_trip_and_lighting();
    test_it_draws();

    if( g_fail )
    {
        fprintf(stderr, "toridraw_rscache_test: FAILED\n");
        return 1;
    }
    printf("toridraw_rscache_test: PASS (format %s)\n", TORIPIXEL_FORMAT_NAME);
    return 0;
}
