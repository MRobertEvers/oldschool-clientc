#include "toridraw_arena.h"

#include "toridraw_model.h"
#include "toridraw_scene.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/*
 * A bump allocator over the caller's block.
 *
 * It runs TWICE over the same layout: once with `base` NULL to total the bytes
 * (ToriDraw_SceneArenaBytes) and once for real (ToriDraw_SceneArenaInit). One
 * layout function serves both, so the number a caller sizes a static buffer
 * with and the number the init consumes cannot drift apart -- which they will
 * the moment they are two lists of the same fields maintained by hand.
 */
struct td_arena
{
    uint8_t* base; /* NULL when measuring */
    size_t used;
    size_t cap;
};

static void*
td_arena_take(
    struct td_arena* a,
    size_t bytes,
    size_t align)
{
    size_t start = (a->used + align - 1) & ~(align - 1);

    a->used = start + bytes;
    if( !a->base )
        return NULL;

    /* The caller sized with ToriDraw_SceneArenaBytes; running past the end
     * means the two disagreed, and every byte written past here lands in a
     * live neighbour. */
    assert(a->used <= a->cap);
    return a->base + start;
}

#define TD_ARENA_TAKE(a, type, count) ((type*)td_arena_take((a), sizeof(type) * (size_t)(count), _Alignof(type)))

/**
 * The one description of an arena scene's memory.
 *
 * `scene` is NULL on the measuring pass, and every store is guarded by that --
 * not by a second copy of the layout. The groups below are exactly those
 * ToriDraw_SceneAllocBuffers takes for a SMALL scene, minus the bitonic+radix
 * keys and the presort stash: see toridraw_arena.h for why those are out.
 */
static void
td_arena_layout(
    struct td_arena* a,
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneLimits* limits,
    int depth_levels)
{
    int const mv = limits->max_vertices;
    int const mf = limits->max_faces;

    /* The scene struct itself, which _Alignas(16)es its prepared-camera block
     * -- hence TORIDRAW_ARENA_ALIGN on the whole arena. */
    (void)td_arena_take(a, sizeof(struct ToriDraw_Scene), _Alignof(struct ToriDraw_Scene));

#define TD_FIELD(field, type, count)                                                               \
    do                                                                                             \
    {                                                                                              \
        type* p_ = TD_ARENA_TAKE(a, type, (count));                                                \
        if( scene )                                                                                \
            scene->field = p_;                                                                     \
    } while( 0 )

    TD_FIELD(screen_vertices_x, int, mv);
    TD_FIELD(screen_vertices_y, int, mv);
    TD_FIELD(screen_vertices_z, int, mv);
    TD_FIELD(orthographic_vertices_x, int, mv);
    TD_FIELD(orthographic_vertices_y, int, mv);
    TD_FIELD(orthographic_vertices_z, int, mv);

    TD_FIELD(tmp_face_order, int, mf);

    /* The CSR sorter. sm_depth_offset carries the end sentinel, so it is one
     * longer than the levels it indexes (sm_bucket_sort_finish). */
    TD_FIELD(sm_face_depth, faceint_t, mf);
    TD_FIELD(sm_depth_offset, int, depth_levels + 1);
    TD_FIELD(sm_depth_cursor, int, depth_levels);
    TD_FIELD(sm_faces_by_depth, faceint_t, mf);
    TD_FIELD(sm_prio_offset, int, 13);
    TD_FIELD(sm_prio_faces, faceint_t, (size_t)13 * (size_t)mf);
    TD_FIELD(sm_flex_prio11_face_to_depth, int, mf);
    TD_FIELD(sm_flex_prio12_face_to_depth, int, mf);

    /*
     * The bitonic+radix sort's composite keys, rounded to a power of two so
     * the network can pad without a second buffer -- the same rounding
     * scene_alloc_bitonic_radix_keys applies. Unconditional at 8 bytes per
     * face: which of the two face-sort kernels a table names is not a fact
     * this API's caller should have to know, and the alternative is an abort
     * in ToriDraw_KernelTake over a buffer that costs 4 KB at 512 faces.
     */
    {
        size_t keys = 8;

        while( keys < (size_t)mf )
            keys <<= 1;
        keys += 4;
        TD_FIELD(sm_sort_keys, uint32_t, keys);
        TD_FIELD(sm_sort_tmp, uint32_t, keys);
    }

    /* Four ints per face per plane, plus four records of slack for the
     * vector sort's unconditional block store. */
    if( limits->batched_raster )
    {
        TD_FIELD(sm_face_x4, int, ((size_t)mf + 4) * 4);
        TD_FIELD(sm_face_y4, int, ((size_t)mf + 4) * 4);
    }

    if( limits->textures )
        TD_FIELD(tex_state, struct ToriDraw_TextureState, 1);

#undef TD_FIELD
}

static int
td_arena_depth_levels(const struct ToriDraw_SceneLimits* limits)
{
    return limits->depth_levels > 0 ? limits->depth_levels : 1500;
}

static void
td_arena_assert_limits(const struct ToriDraw_SceneLimits* limits)
{
    assert(limits);
    assert(limits->max_vertices > 0);
    assert(limits->max_faces > 0);
    assert(limits->depth_levels >= 0);
    /* A face index is stored in a faceint_t, and a vertex index is read out of
     * one; limits past that range produce silent truncation in the sort rather
     * than an overrun anything can see. */
    assert(limits->max_vertices <= 32767);
    assert(limits->max_faces <= 32767);
}

size_t
ToriDraw_SceneArenaBytes(const struct ToriDraw_SceneLimits* limits)
{
    struct td_arena a = { NULL, 0, 0 };

    td_arena_assert_limits(limits);
    td_arena_layout(&a, NULL, limits, td_arena_depth_levels(limits));
    return a.used;
}

struct ToriDraw_Scene*
ToriDraw_SceneArenaInit(
    void* memory,
    size_t bytes,
    const struct ToriDraw_SceneLimits* limits)
{
    struct td_arena a;
    struct ToriDraw_Scene* scene;
    int depth_levels;
    size_t needed;

    assert(memory);
    td_arena_assert_limits(limits);
    assert(((uintptr_t)memory & (TORIDRAW_ARENA_ALIGN - 1)) == 0);

    needed = ToriDraw_SceneArenaBytes(limits);
    /* Under NDEBUG this assert is gone and a short buffer would be scribbled
     * over instead. That is why the header tells callers to hold their buffer
     * to ToriDraw_SceneArenaBytes with a _Static_assert: a compile-time check
     * is the only one that survives the build a target actually ships. */
    assert(bytes >= needed);

    depth_levels = td_arena_depth_levels(limits);

    /* Zero first, then lay out. The scene is calloc'd on the heap path and a
     * dozen fields depend on starting at zero -- sm_face_xy_valid, the
     * prepared-camera source pointer, every count. The sort tables that must
     * start zeroed (sm_depth_offset, whose window each sort restores rather
     * than re-clearing) are covered by the same memset.
     *
     * Only the span the layout will USE, not the whole buffer: a caller may
     * have handed over the front of something larger and be keeping their own
     * data behind it. */
    memset(memory, 0, needed);

    a.base = (uint8_t*)memory;
    a.used = 0;
    a.cap = bytes;

    scene = (struct ToriDraw_Scene*)memory;
    td_arena_layout(&a, scene, limits, depth_levels);

    scene->flags = TORIDRAW_SCENE_SMALL | TORIDRAW_SCENE_ARENA;
    if( !limits->textures )
        scene->flags |= TORIDRAW_SCENE_LAZY_TEXTURES;
    scene->max_vertices = limits->max_vertices;
    scene->max_faces = limits->max_faces;
    scene->depth_levels = depth_levels;
    /* SMALL mode carries no dense bucket table, so both strides are zero --
     * the same values resolve_caps gives a small scene. */
    scene->depth_stride = 0;
    scene->priority_stride = 0;
    scene->flex_prio_capacity = limits->max_faces;

    /* Empty, so anything that walks the element pool finds nothing rather than
     * following a null next-pointer. The asset hash maps stay NULL: an arena
     * scene has no registry, and a caller reaching one has the wrong scene. */
    ToriDraw_IntrusiveListInit(&scene->elements);

    return scene;
}

void
ToriDraw_SceneLimitsForModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_SceneLimits* out_limits)
{
    assert(out_limits);

    memset(out_limits, 0, sizeof(*out_limits));
    ToriDraw_SceneLimitsInclude(out_limits, hnd);
}

void
ToriDraw_SceneLimitsInclude(
    struct ToriDraw_SceneLimits* limits,
    struct ToriDraw_ModelHandle hnd)
{
    const struct ToriDraw_BoundsCylinder* bounds;
    int vertices;
    int faces;
    int depth;

    assert(limits);

    vertices = ToriDraw_ModelGetVertexCount(hnd);
    faces = ToriDraw_ModelGetFaceCount(hnd);
    if( vertices > limits->max_vertices )
        limits->max_vertices = vertices;
    if( faces > limits->max_faces )
        limits->max_faces = faces;

    /*
     * The sort buckets a face at `avg(camera z) + min_z_depth_any_rotation`,
     * and the average ranges over +/- that same radius whatever the yaw and
     * pitch -- which is what "any rotation" means. So the table has to reach
     * twice it, plus one for the end sentinel's level and one for the rounding
     * in the fixed-point third.
     *
     * Falling back to the reference 1500 for a model with no bounds cylinder
     * is not a guess: that is the number the reference client ships, and a
     * model that has not been measured is one this library did not build.
     */
    bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    depth = bounds ? 2 * bounds->min_z_depth_any_rotation + 2 : 1500;
    if( depth > limits->depth_levels )
        limits->depth_levels = depth;

    if( ToriDraw_ModelHasTextures(hnd) )
        limits->textures = true;
}
