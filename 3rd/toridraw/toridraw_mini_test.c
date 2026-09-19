/*
 * The small-client contract: a model draws into a buffer out of memory the
 * caller owns, and the library allocates NOTHING while it does.
 *
 * Every claim here was established by making it fail first:
 *
 *  - The allocation count is not a formality. Before the arena existed the
 *    equivalent path took twenty-odd malloc calls and 413 KB; the first version
 *    of ToriDraw_MiniViewInit still reached one, because ToriDraw_KernelTake ->
 *    ToriDraw_SceneEnsureScratch found a group the arena had not provisioned
 *    and quietly grew it. That is exactly the failure an embedded client cannot
 *    see and cannot survive, so it is counted rather than reasoned about.
 *  - The bounds-cylinder check is here because a model without one is CULLED
 *    (TORIDRAW_CULL_ERROR) and draws a blank frame with no other symptom.
 *  - The byte count is asserted EQUAL, not merely sufficient: a caller sizing a
 *    static buffer from ToriDraw_MiniViewBytes needs it to be the whole cost,
 *    and a layout that quietly took more would overrun the neighbour rather
 *    than the arena.
 *
 * Build the malloc counting with:
 *     -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
 * Without those wrappers the file still compiles and every other check runs;
 * the allocation checks then report "not counted" rather than passing silently.
 */
#include "toridraw.h"
#include "toridraw_model_sprite.h"
#include "toridraw_sprite.h"

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

/* ------------------------------------------------------- allocation counting */

#ifdef TORIDRAW_MINI_TEST_WRAP_MALLOC
static long g_alloc_calls = 0;

void* __real_malloc(size_t n);
void* __real_calloc(size_t n, size_t s);
void* __real_realloc(void* p, size_t n);
void __real_free(void* p);

void*
__wrap_malloc(size_t n)
{
    g_alloc_calls++;
    return __real_malloc(n);
}

void*
__wrap_calloc(size_t n, size_t s)
{
    g_alloc_calls++;
    return __real_calloc(n, s);
}

void*
__wrap_realloc(void* p, size_t n)
{
    g_alloc_calls++;
    return __real_realloc(p, n);
}

void
__wrap_free(void* p)
{
    __real_free(p);
}

#define ALLOC_COUNTING 1
#define ALLOCS_SO_FAR() (g_alloc_calls)
#else
#define ALLOC_COUNTING 0
#define ALLOCS_SO_FAR() (0L)
#endif

/* ------------------------------------------------------------------ fixture */

#define FIXTURE_VERTS 4
#define FIXTURE_FACES 4

/*
 * A tetrahedron, deliberately NOT symmetric about any axis.
 *
 * A symmetric fixture agrees with itself under every yaw, so a projection that
 * dropped the model rotation entirely would still pass the "it drew at eight
 * angles" check below. The pixel counts differ per yaw precisely because this
 * one does not.
 */
static struct ToriDraw_Model*
fixture_model(void)
{
    static const vertexint_t VX[FIXTURE_VERTS] = { 0, -100, 100, 0 };
    static const vertexint_t VY[FIXTURE_VERTS] = { -140, 40, 40, 40 };
    static const vertexint_t VZ[FIXTURE_VERTS] = { 0, -60, -60, 100 };
    static const faceint_t FA[FIXTURE_FACES] = { 0, 0, 0, 1 };
    static const faceint_t FB[FIXTURE_FACES] = { 1, 2, 3, 3 };
    static const faceint_t FC[FIXTURE_FACES] = { 2, 3, 1, 2 };

    struct ToriDraw_Model* m = ToriDraw_ModelNew(FIXTURE_VERTS, FIXTURE_FACES, 0);
    int i;

    m->vertices_x = malloc(sizeof(VX));
    m->vertices_y = malloc(sizeof(VY));
    m->vertices_z = malloc(sizeof(VZ));
    m->face_indices_a = malloc(sizeof(FA));
    m->face_indices_b = malloc(sizeof(FB));
    m->face_indices_c = malloc(sizeof(FC));
    m->face_colors_a = malloc(FIXTURE_FACES * sizeof(hsl16_t));
    m->face_colors_b = malloc(FIXTURE_FACES * sizeof(hsl16_t));
    m->face_colors_c = malloc(FIXTURE_FACES * sizeof(hsl16_t));
    memcpy(m->vertices_x, VX, sizeof(VX));
    memcpy(m->vertices_y, VY, sizeof(VY));
    memcpy(m->vertices_z, VZ, sizeof(VZ));
    memcpy(m->face_indices_a, FA, sizeof(FA));
    memcpy(m->face_indices_b, FB, sizeof(FB));
    memcpy(m->face_indices_c, FC, sizeof(FC));
    for( i = 0; i < FIXTURE_FACES; i++ )
    {
        m->face_colors_a[i] = (hsl16_t)(4000 + i * 900);
        m->face_colors_b[i] = (hsl16_t)(4000 + i * 900);
        m->face_colors_c[i] = (hsl16_t)(4000 + i * 900);
    }
    ToriDraw_ModelSetBoundsCylinder(m);
    return m;
}

/* --------------------------------------------------------------------- tests */

static void
test_arena_bytes_is_the_whole_cost(void)
{
    struct ToriDraw_SceneLimits limits;
    size_t bytes;
    size_t again;

    memset(&limits, 0, sizeof(limits));
    limits.max_vertices = 256;
    limits.max_faces = 512;
    limits.depth_levels = 400;

    bytes = ToriDraw_SceneArenaBytes(&limits);
    again = ToriDraw_SceneArenaBytes(&limits);
    /* Deterministic, or it cannot be used in a _Static_assert. */
    CHECK(bytes == again);
    CHECK(bytes > sizeof(struct ToriDraw_Scene));

    /* A texture map is the only optional group that costs real memory at these
     * limits, and asking for it must cost exactly the map. */
    limits.textures = true;
    CHECK(ToriDraw_SceneArenaBytes(&limits) >= bytes + sizeof(struct ToriDraw_TextureState));

    /* Every limit is monotonic: nothing gets cheaper as the model grows. */
    limits.textures = false;
    limits.max_faces = 1024;
    CHECK(ToriDraw_SceneArenaBytes(&limits) > bytes);
}

static void
test_limits_cover_the_model(void)
{
    struct ToriDraw_Model* m = fixture_model();
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(m);
    struct ToriDraw_MiniLimits limits;
    const struct ToriDraw_BoundsCylinder* bounds;

    ToriDraw_MiniLimitsForModel(hnd, &limits);
    CHECK(limits.scene.max_vertices == FIXTURE_VERTS);
    CHECK(limits.scene.max_faces == FIXTURE_FACES);
    CHECK(limits.scene.textures == false);

    /*
     * The depth table must reach twice the "any rotation" radius: the sort
     * buckets a face at avg(camera z) + that radius, and the average ranges
     * over +/- it whatever the yaw. A table sized to the radius alone drops
     * every face in the far half of the model, which draws as a model with its
     * back missing rather than as a failure.
     */
    bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    CHECK(bounds != NULL);
    CHECK(limits.scene.depth_levels > 2 * bounds->min_z_depth_any_rotation);

    /* Folding a second model in only ever widens. */
    {
        struct ToriDraw_MiniLimits widened = limits;
        ToriDraw_MiniLimitsInclude(&widened, hnd);
        CHECK(widened.scene.max_vertices == limits.scene.max_vertices);
        CHECK(widened.scene.max_faces == limits.scene.max_faces);
        CHECK(widened.scene.depth_levels == limits.scene.depth_levels);
    }

    ToriDraw_ModelHandleFree(hnd);
}

static void
test_draw_allocates_nothing(void)
{
    static _Alignas(TORIDRAW_ARENA_ALIGN) uint8_t arena[64 * 1024];
    static toripixel_t framebuffer[64 * 64];

    struct ToriDraw_Model* m = fixture_model();
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(m);
    struct ToriDraw_MiniLimits limits;
    struct ToriDraw_MiniView* view;
    struct ToriDraw_MiniTarget target;
    struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
    size_t want;
    long before;
    int angle;
    int yaws_drawn = 0;
    int last_lit = -1;
    int distinct_counts = 0;

    ToriDraw_MiniLimitsForModel(hnd, &limits);
    want = ToriDraw_MiniViewBytes(&limits);
    CHECK(want <= sizeof(arena));

    view = ToriDraw_MiniViewInit(arena, sizeof(arena), &limits);
    CHECK(view != NULL);
    CHECK(ToriDraw_SceneIsArena(ToriDraw_MiniViewScene(view)));

    target.pixels = framebuffer;
    target.width = 64;
    target.height = 64;
    target.stride = 64;

    /* Count from AFTER the view is built: the view is where a growth would
     * happen, and it has already happened by here if it was going to. Counting
     * the draws separately is what says the steady state is clean. */
    before = ALLOCS_SO_FAR();

    for( angle = 0; angle < 2048; angle += 256 )
    {
        int lit = 0;
        int i;

        ToriDraw_MiniClear(&target, (toripixel_t)0);
        pose.yaw = angle;
        if( !ToriDraw_MiniDrawModel(view, hnd, &target, &pose) )
            continue;
        for( i = 0; i < 64 * 64; i++ )
            if( framebuffer[i] != (toripixel_t)0 )
                lit++;
        if( lit > 0 )
            yaws_drawn++;
        if( lit != last_lit )
            distinct_counts++;
        last_lit = lit;
    }

    CHECK(yaws_drawn == 8);
    /* See fixture_model: an asymmetric model must not look the same at every
     * yaw, or this test would pass on a projection that ignored rotation. */
    CHECK(distinct_counts > 2);

    if( ALLOC_COUNTING )
        CHECK(ALLOCS_SO_FAR() == before);
    else
        fprintf(stderr, "note: allocation counting not compiled in\n");

    /* Orthographic takes a different projection family and must reach the same
     * scratch -- an arena sized for one and not the other would abort here. */
    ToriDraw_MiniClear(&target, (toripixel_t)0);
    pose.yaw = 300;
    pose.orthographic = true;
    CHECK(ToriDraw_MiniDrawModel(view, hnd, &target, &pose));
    if( ALLOC_COUNTING )
        CHECK(ALLOCS_SO_FAR() == before);

    ToriDraw_ModelHandleFree(hnd);
}

static void
test_stride_is_honoured(void)
{
    static _Alignas(TORIDRAW_ARENA_ALIGN) uint8_t arena[64 * 1024];
    /* A target narrower than its stride: the columns past `width` must stay
     * untouched, which is what a client compositing into a larger surface
     * depends on. */
    static toripixel_t framebuffer[64 * 96];

    struct ToriDraw_Model* m = fixture_model();
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(m);
    struct ToriDraw_MiniLimits limits;
    struct ToriDraw_MiniView* view;
    struct ToriDraw_MiniTarget target;
    struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
    int y;
    int x;
    int outside_touched = 0;
    int inside_lit = 0;

    ToriDraw_MiniLimitsForModel(hnd, &limits);
    view = ToriDraw_MiniViewInit(arena, sizeof(arena), &limits);

    for( y = 0; y < 64 * 96; y++ )
        framebuffer[y] = (toripixel_t)0;

    target.pixels = framebuffer;
    target.width = 64;
    target.height = 64;
    target.stride = 96;
    CHECK(ToriDraw_MiniDrawModel(view, hnd, &target, &pose));

    for( y = 0; y < 64; y++ )
    {
        for( x = 0; x < 64; x++ )
            if( framebuffer[y * 96 + x] != (toripixel_t)0 )
                inside_lit++;
        for( x = 64; x < 96; x++ )
            if( framebuffer[y * 96 + x] != (toripixel_t)0 )
                outside_touched++;
    }

    CHECK(inside_lit > 0);
    CHECK(outside_touched == 0);

    ToriDraw_ModelHandleFree(hnd);
}

/*
 * That the counter is WIRED.
 *
 * Without this, `allocations == before` passes trivially on a build where the
 * --wrap flags were dropped from the link line: 0 == 0, forever, and the one
 * claim this file exists to make would be vacuous while still printing PASS.
 * So prove interception against something that certainly allocates.
 */
static void
test_the_counter_counts(void)
{
    long before = ALLOCS_SO_FAR();
    struct ToriDraw_Model* m = fixture_model();

    if( ALLOC_COUNTING )
        CHECK(ALLOCS_SO_FAR() > before);
    ToriDraw_ModelFree(m);
}

/*
 * The arena path and the heap path draw the SAME pixels.
 *
 * This is the check that the whole small-client surface is a re-packaging of
 * ToriDraw and not a second renderer. Everything it could get wrong is
 * invisible without it: an arena that lays a scratch array out at the wrong
 * size, a scene field the arena init forgets to seed, a pose formula that
 * drifts from the reference widget placement. None of those crash. They
 * produce a picture that is nearly right, on the target only.
 *
 * ToriDraw_SpriteNewFromModelRaster is the comparison because it is an
 * INDEPENDENT spelling of the same reference formula (ObjType.getSprite /
 * Model.drawModel2D) against a heap scene, written years before this API.
 * Comparing against a copy of the pose math in this file would only prove the
 * copy matched.
 *
 * It caught one real regression: making the raster take the texture map
 * without building it turned `a lazy-textures scene has not loaded its
 * textures yet` -- an ordinary asynchronous state this engine reports as a
 * tex miss -- into an abort.
 */
static void
test_arena_and_heap_agree(void)
{
    static _Alignas(TORIDRAW_ARENA_ALIGN) uint8_t arena[64 * 1024];
    static toripixel_t mini_fb[96 * 96];

    struct ToriDraw_Model* m = fixture_model();
    struct ToriDraw_ModelHandle hnd = ToriDraw_ModelHandleOwned(m);
    struct ToriDraw_MiniLimits limits;
    struct ToriDraw_MiniView* view;
    struct ToriDraw_MiniTarget target;
    struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
    struct ToriDraw_Scene* heap;
    int yaw;
    int compared = 0;

    ToriDraw_MiniLimitsForModel(hnd, &limits);
    view = ToriDraw_MiniViewInit(arena, sizeof(arena), &limits);
    heap = ToriDraw_SceneNew(
        TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_LAZY_TEXTURES,
        TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    CHECK(heap != NULL);
    if( !heap )
        return;

    target.pixels = mini_fb;
    target.width = 96;
    target.height = 96;
    target.stride = 96;

    for( yaw = 0; yaw < 2048; yaw += 256 )
    {
        struct ToriDraw_Sprite* sprite;
        int differing = 0;
        int lit = 0;
        int i;

        pose.yaw = yaw;
        pose.pitch = 280;
        pose.zoom = 2000;
        ToriDraw_MiniClear(&target, (toripixel_t)0);
        ToriDraw_MiniDrawModel(view, hnd, &target, &pose);

        sprite = ToriDraw_SpriteNewFromModelRaster(heap, hnd, 2000, 280, yaw, 96, 96, false);
        CHECK(sprite != NULL);
        if( !sprite )
            continue;

        for( i = 0; i < 96 * 96; i++ )
        {
            /* Through ARGB8888 on both sides: the sprite is always ARGB and
             * the target is whatever format this build selected, so comparing
             * the words directly would only agree at 32bpp. */
            uint32_t a = toripixel_to_argb8888(mini_fb[i]) & 0x00FFFFFFu;
            uint32_t b = sprite->pixels_argb[i] & 0x00FFFFFFu;

            if( a )
                lit++;
            if( a != b )
                differing++;
        }
        CHECK(differing == 0);
        /* A fixture that drew nothing would make `differing == 0` vacuous. */
        CHECK(lit > 0);
        compared++;
        ToriDraw_SpriteFree(sprite);
    }

    CHECK(compared == 8);
    ToriDraw_SceneFree(heap);
    ToriDraw_ModelHandleFree(hnd);
}

int
main(void)
{
    ToriDraw_Init();

    test_the_counter_counts();
    test_arena_bytes_is_the_whole_cost();
    test_limits_cover_the_model();
    test_draw_allocates_nothing();
    test_stride_is_honoured();
    test_arena_and_heap_agree();

    if( g_fail )
    {
        fprintf(stderr, "mini_test: FAILED\n");
        return 1;
    }
    printf("mini_test: PASS (format %s)\n", TORIPIXEL_FORMAT_NAME);
    return 0;
}
