#include "toridraw.h"

#include "toridraw_shared_model.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
_Static_assert(
    offsetof(
        struct ToriDraw_ProjectionPreparedCamera,
        cos_yaw) == 0,
    "prepared projection cos-yaw offset");
_Static_assert(
    offsetof(
        struct ToriDraw_ProjectionPreparedCamera,
        sin_yaw) == 16,
    "prepared projection sin-yaw offset");
_Static_assert(
    offsetof(
        struct ToriDraw_ProjectionPreparedCamera,
        cos_pitch) == 32,
    "prepared projection cos-pitch offset");
_Static_assert(
    offsetof(
        struct ToriDraw_ProjectionPreparedCamera,
        sin_pitch) == 48,
    "prepared projection sin-pitch offset");
_Static_assert(
    offsetof(
        struct ToriDraw_ProjectionPreparedCamera,
        cot15) == 64,
    "prepared projection cotangent offset");
_Static_assert(
    sizeof(struct ToriDraw_ProjectionPreparedCamera) == 80,
    "prepared projection camera size");
_Static_assert(
    _Alignof(struct ToriDraw_ProjectionPreparedCamera) >= 16,
    "prepared projection camera alignment");
_Static_assert(
    offsetof(
        struct ToriDraw_Scene,
        screen_vertices_y) ==
        offsetof(
            struct ToriDraw_Scene,
            screen_vertices_x) +
            sizeof(int*),
    "projection screen-y pointer layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Scene,
        screen_vertices_z) ==
        offsetof(
            struct ToriDraw_Scene,
            screen_vertices_x) +
            2 * sizeof(int*),
    "projection screen-z pointer layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Scene,
        orthographic_vertices_x) ==
        offsetof(
            struct ToriDraw_Scene,
            screen_vertices_x) +
            3 * sizeof(int*),
    "projection orthographic-x pointer layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Scene,
        orthographic_vertices_y) ==
        offsetof(
            struct ToriDraw_Scene,
            screen_vertices_x) +
            4 * sizeof(int*),
    "projection orthographic-y pointer layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Scene,
        orthographic_vertices_z) ==
        offsetof(
            struct ToriDraw_Scene,
            screen_vertices_x) +
            5 * sizeof(int*),
    "projection orthographic-z pointer layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Scene,
        projection_prepared_camera) ==
        offsetof(
            struct ToriDraw_Scene,
            screen_vertices_x) +
            6 * sizeof(int*),
    "prepared projection relative layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Scene,
        projection_bound) ==
        offsetof(
            struct ToriDraw_Scene,
            screen_vertices_x) +
            6 * sizeof(int*) + sizeof(struct ToriDraw_ProjectionPreparedCamera),
    "projection bound block relative layout");
_Static_assert(
    sizeof(((struct ToriDraw_Scene*)0)->projection_bound) == 64,
    "projection bound block size");
_Static_assert(
    offsetof(
        struct ToriDraw_Position,
        x) == 0,
    "projection position-x layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Position,
        y) ==
        offsetof(
            struct ToriDraw_Position,
            x) +
            sizeof(int),
    "projection position-y layout");
_Static_assert(
    offsetof(
        struct ToriDraw_Position,
        z) ==
        offsetof(
            struct ToriDraw_Position,
            x) +
            2 * sizeof(int),
    "projection position-z layout");
#endif

#define TORIDRAW_LOW_MAX_VERTICES 2048
#define TORIDRAW_LOW_MAX_FACES 4096
#define TORIDRAW_LOW_PRIORITY_STRIDE 4096
#define TORIDRAW_LOW_FLEX_PRIO11 4096
#define TORIDRAW_LOW_FLEX_PRIO12 4096

#define TORIDRAW_MED_MAX_VERTICES 4096
#define TORIDRAW_MED_MAX_FACES 8192
#define TORIDRAW_MED_PRIORITY_STRIDE 8192
#define TORIDRAW_MED_FLEX_PRIO11 8192
#define TORIDRAW_MED_FLEX_PRIO12 8192

/* HIGH_8K is the existing QBD-safe vertex/face allocation.  Do not reduce it
 * without replacing the explicit capacity guard in ToriDraw_Project: the
 * merged sleeping QBD has 6,223 vertices and 9,012 faces.  Its 4,791-unit
 * animated bounding sphere separately requires TORIDRAW_SCENE_DEPTH_16K. */
#define TORIDRAW_HIGH_MAX_VERTICES 8192
#define TORIDRAW_HIGH_MAX_FACES 16384
#define TORIDRAW_HIGH_PRIORITY_STRIDE 16384
#define TORIDRAW_HIGH_FLEX_PRIO11 16384
#define TORIDRAW_HIGH_FLEX_PRIO12 16384

/* VERYHIGH_16K exists for merged NPCs that outgrow HIGH_8K: the
 * face-synthesised QBD body (70260 at K=4) plus its collar ring (69766)
 * projects 9,097 vertices.  Same 2:1 face allowance as the other tiers. */
#define TORIDRAW_VERYHIGH_MAX_VERTICES 16384
#define TORIDRAW_VERYHIGH_MAX_FACES 32768
#define TORIDRAW_VERYHIGH_PRIORITY_STRIDE 32768
#define TORIDRAW_VERYHIGH_FLEX_PRIO11 32768
#define TORIDRAW_VERYHIGH_FLEX_PRIO12 32768

#define TORIDRAW_DEPTH_LEVELS_REFERENCE 1500
#define TORIDRAW_DEPTH_LEVELS_16K 16384
#define TORIDRAW_FULL_DEPTH_STRIDE 512

struct ToriDraw_ScratchProfile
{
    int max_vertices;
    int max_faces;
    int priority_stride;
    int flex_prio11;
    int flex_prio12;
    const char* name;
};

/* The tier labels name the projection-vertex capacity.  LOW follows the
 * 2K-scale temporary buffers used by Client-TS; MED is the 4K reference-client
 * tier; HIGH keeps the current 8K QBD allocation.  Faces have a 2:1 allowance
 * because a model can have substantially more triangles than vertices. */
static const struct ToriDraw_ScratchProfile
g_scratch_profiles[TORIDRAW_SCRATCH_BUFFER_SIZE_COUNT] = {
    [TORIDRAW_SCRATCH_BUFFER_LOW_2K] = {
        TORIDRAW_LOW_MAX_VERTICES,
        TORIDRAW_LOW_MAX_FACES,
        TORIDRAW_LOW_PRIORITY_STRIDE,
        TORIDRAW_LOW_FLEX_PRIO11,
        TORIDRAW_LOW_FLEX_PRIO12,
        "LOW_2K",
    },
    [TORIDRAW_SCRATCH_BUFFER_MED_4K] = {
        TORIDRAW_MED_MAX_VERTICES,
        TORIDRAW_MED_MAX_FACES,
        TORIDRAW_MED_PRIORITY_STRIDE,
        TORIDRAW_MED_FLEX_PRIO11,
        TORIDRAW_MED_FLEX_PRIO12,
        "MED_4K",
    },
    [TORIDRAW_SCRATCH_BUFFER_HIGH_8K] = {
        TORIDRAW_HIGH_MAX_VERTICES,
        TORIDRAW_HIGH_MAX_FACES,
        TORIDRAW_HIGH_PRIORITY_STRIDE,
        TORIDRAW_HIGH_FLEX_PRIO11,
        TORIDRAW_HIGH_FLEX_PRIO12,
        "HIGH_8K",
    },
    [TORIDRAW_SCRATCH_BUFFER_VERYHIGH_16K] = {
        TORIDRAW_VERYHIGH_MAX_VERTICES,
        TORIDRAW_VERYHIGH_MAX_FACES,
        TORIDRAW_VERYHIGH_PRIORITY_STRIDE,
        TORIDRAW_VERYHIGH_FLEX_PRIO11,
        TORIDRAW_VERYHIGH_FLEX_PRIO12,
        "VERYHIGH_16K",
    },
};

struct ToriDraw_SceneCaps
{
    int max_vertices;
    int max_faces;
    int depth_levels;
    int depth_stride;
    int priority_stride;
    int flex_prio11;
    int flex_prio12;
    bool small_mode;
    bool lazy_textures;
};

static bool
resolve_caps(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size,
    struct ToriDraw_SceneCaps* caps)
{
    const struct ToriDraw_ScratchProfile* profile;

    if( (unsigned int)scratch_buffer_size >= TORIDRAW_SCRATCH_BUFFER_SIZE_COUNT )
        return false;

    profile = &g_scratch_profiles[scratch_buffer_size];
    caps->small_mode = (flags & TORIDRAW_SCENE_SMALL) != 0;
    caps->lazy_textures = (flags & TORIDRAW_SCENE_LAZY_TEXTURES) != 0;
    caps->depth_levels = (flags & TORIDRAW_SCENE_DEPTH_16K) ? TORIDRAW_DEPTH_LEVELS_16K
                                                            : TORIDRAW_DEPTH_LEVELS_REFERENCE;

    /* Capacity always comes from the tier; SMALL only selects the CSR
     * sorter, whose buffers scale with max_faces instead of carrying the
     * dense depth_levels x depth_stride bucket table. */
    caps->max_vertices = profile->max_vertices;
    caps->max_faces = profile->max_faces;
    caps->flex_prio11 = profile->flex_prio11;
    caps->flex_prio12 = profile->flex_prio12;
    if( caps->small_mode )
    {
        caps->depth_stride = 0;
        caps->priority_stride = 0;
    }
    else
    {
        caps->depth_stride = TORIDRAW_FULL_DEPTH_STRIDE;
        caps->priority_stride = profile->priority_stride;
    }

    return true;
}

static size_t
vertex_buffer_bytes(const struct ToriDraw_SceneCaps* caps)
{
    return (size_t)caps->max_vertices * sizeof(int) * 6;
}

static size_t
full_sort_buffer_bytes(const struct ToriDraw_SceneCaps* caps)
{
    size_t bytes = 0;
    bytes += (size_t)caps->depth_levels * sizeof(faceint_t);
    bytes += (size_t)caps->depth_levels * (size_t)caps->depth_stride * sizeof(faceint_t);
    bytes += 12 * sizeof(faceint_t);
    bytes += 12 * sizeof(int);
    bytes += 12 * (size_t)caps->priority_stride * sizeof(faceint_t);
    bytes += (size_t)caps->flex_prio11 * sizeof(int);
    bytes += (size_t)caps->flex_prio12 * sizeof(int);
    return bytes;
}

static size_t
small_sort_buffer_bytes(const struct ToriDraw_SceneCaps* caps)
{
    size_t bytes = 0;
    bytes += 13 * (size_t)caps->max_faces * sizeof(faceint_t);
    bytes += (size_t)(caps->depth_levels + 1) * sizeof(int);
    bytes += (size_t)caps->depth_levels * sizeof(int);
    bytes += (size_t)caps->max_faces * sizeof(faceint_t);
    bytes += 13 * sizeof(int);
    bytes += (size_t)caps->max_faces * sizeof(faceint_t);
    bytes += (size_t)caps->flex_prio11 * sizeof(int);
    bytes += (size_t)caps->flex_prio12 * sizeof(int);
    return bytes;
}

static size_t
ToriDraw_SceneBufferBytes(const struct ToriDraw_SceneCaps* caps)
{
    size_t bytes = sizeof(struct ToriDraw_Scene);
    bytes += vertex_buffer_bytes(caps);
    bytes += (size_t)caps->max_faces * sizeof(int);
    if( !caps->lazy_textures )
        bytes += sizeof(struct ToriDraw_TextureState);
    if( caps->small_mode )
        bytes += small_sort_buffer_bytes(caps);
    else
        bytes += full_sort_buffer_bytes(caps);
    return bytes;
}

static void
ToriDraw_SceneFreeBuffers(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return;

    free(scene->screen_vertices_x);
    free(scene->screen_vertices_y);
    free(scene->screen_vertices_z);
    free(scene->orthographic_vertices_x);
    free(scene->orthographic_vertices_y);
    free(scene->orthographic_vertices_z);
    free(scene->tmp_depth_face_count);
    free(scene->tmp_depth_faces);
    free(scene->tmp_priority_face_count);
    free(scene->tmp_priority_depth_sum);
    free(scene->tmp_priority_faces);
    free(scene->tmp_flex_prio11_face_to_depth);
    free(scene->tmp_flex_prio12_face_to_depth);
    free(scene->sm_face_depth);
    free(scene->sm_face_x4);
    free(scene->sm_face_y4);
    free(scene->sm_sort_keys);
    free(scene->sm_sort_tmp);
    free(scene->sm_vertex_xyz);
    free(scene->sm_depth_offset);
    free(scene->sm_depth_cursor);
    free(scene->sm_faces_by_depth);
    free(scene->sm_prio_offset);
    free(scene->sm_prio_faces);
    free(scene->sm_flex_prio11_face_to_depth);
    free(scene->sm_flex_prio12_face_to_depth);
    free(scene->tmp_face_order);
    free(scene->tex_state);
    free(scene->anim_list);
    free(scene->zbuffer);
    free(scene->event_queue.events);

    memset(scene, 0, sizeof(*scene));
}

/*
 * Scratch allocation, split by WHAT NEEDS IT rather than by scene tier.
 *
 * Each helper below owns one TORIDRAW_SCENE_SCRATCH_* group and is idempotent:
 * it returns true immediately when the group is already resident. That is what
 * lets ToriDraw_SceneEnsureScratch run after ToriDraw_SceneNew, for a kernel
 * the scene was not created for, without re-allocating what is already there.
 *
 * ToriDraw_SceneNew still decides the same groups it always did -- the tier and
 * the SMALL flag pick exactly one of the two sort families -- so a scene that
 * nobody calls the ensure API on allocates byte for byte what it used to.
 */

static bool
scene_alloc_vertices(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneCaps* caps)
{
    if( scene->screen_vertices_x )
        return true;

    scene->screen_vertices_x = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->screen_vertices_y = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->screen_vertices_z = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->orthographic_vertices_x = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->orthographic_vertices_y = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->orthographic_vertices_z = malloc((size_t)caps->max_vertices * sizeof(int));

    assert(scene->screen_vertices_x);
    assert(scene->screen_vertices_y);
    assert(scene->screen_vertices_z);
    assert(scene->orthographic_vertices_x);
    assert(scene->orthographic_vertices_y);
    assert(scene->orthographic_vertices_z);
    return true;
}

static bool
scene_alloc_face_order(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneCaps* caps)
{
    if( scene->tmp_face_order )
        return true;

    scene->tmp_face_order = malloc((size_t)caps->max_faces * sizeof(int));
    assert(scene->tmp_face_order);
    return true;
}

/* The dense depth_levels x depth_stride bucket table, for a full scene. */
static bool
scene_alloc_bucket_sort(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneCaps* caps)
{
    if( scene->tmp_depth_faces )
        return true;

    /* calloc, not malloc: the render path never clears this table whole.
     * Each sort re-zeroes only the buckets it dirtied after its consumer
     * has walked them (ToriDraw_ComputeProjectedFaceOrder), so the
     * all-zero state is established here, once. */
    scene->tmp_depth_face_count = calloc((size_t)caps->depth_levels, sizeof(faceint_t));
    scene->tmp_depth_faces =
        malloc((size_t)caps->depth_levels * (size_t)caps->depth_stride * sizeof(faceint_t));
    scene->tmp_priority_face_count = malloc(12 * sizeof(faceint_t));
    scene->tmp_priority_depth_sum = malloc(12 * sizeof(int));
    scene->tmp_priority_faces = malloc(12 * (size_t)caps->priority_stride * sizeof(faceint_t));
    scene->tmp_flex_prio11_face_to_depth = malloc((size_t)caps->flex_prio11 * sizeof(int));
    scene->tmp_flex_prio12_face_to_depth = malloc((size_t)caps->flex_prio12 * sizeof(int));

    assert(scene->tmp_depth_face_count);
    assert(scene->tmp_depth_faces);
    assert(scene->tmp_priority_face_count);
    assert(scene->tmp_priority_depth_sum);
    assert(scene->tmp_priority_faces);
    assert(scene->tmp_flex_prio11_face_to_depth);
    assert(scene->tmp_flex_prio12_face_to_depth);
    return true;
}

/* The CSR sorter's arrays, which scale with max_faces instead of carrying the
 * dense table above. Both stock face sorts walk these on a small scene. */
static bool
scene_alloc_csr_sort(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneCaps* caps)
{
    if( scene->sm_faces_by_depth )
        return true;

    scene->sm_face_depth = malloc((size_t)caps->max_faces * sizeof(faceint_t));
    /* calloc, not malloc: the counting sort never clears this table whole.
     * Each sort re-zeroes only the [min, max + 1] window it dirtied after
     * its consumer has walked it, so the all-zero state is established
     * here, once (the full-mode tmp_depth_face_count invariant). */
    scene->sm_depth_offset = calloc((size_t)(caps->depth_levels + 1), sizeof(int));
    scene->sm_depth_cursor = malloc((size_t)caps->depth_levels * sizeof(int));
    scene->sm_faces_by_depth = malloc((size_t)caps->max_faces * sizeof(faceint_t));
    scene->sm_prio_offset = malloc(13 * sizeof(int));
    scene->sm_prio_faces = malloc(13 * (size_t)caps->max_faces * sizeof(faceint_t));
    scene->sm_flex_prio11_face_to_depth = malloc((size_t)caps->flex_prio11 * sizeof(int));
    scene->sm_flex_prio12_face_to_depth = malloc((size_t)caps->flex_prio12 * sizeof(int));

    assert(scene->sm_face_depth);
    assert(scene->sm_depth_offset);
    assert(scene->sm_depth_cursor);
    assert(scene->sm_faces_by_depth);
    assert(scene->sm_prio_offset);
    assert(scene->sm_prio_faces);
    assert(scene->sm_flex_prio11_face_to_depth);
    assert(scene->sm_flex_prio12_face_to_depth);
    return true;
}

/* The bitonic+radix sort's key arrays. Rounded up to a power of two so the
 * bitonic network can pad without a second buffer. */
static bool
scene_alloc_bitonic_radix_keys(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneCaps* caps)
{
    size_t keys = 8;

    if( scene->sm_sort_keys )
        return true;

    while( keys < (size_t)caps->max_faces )
        keys <<= 1;
    keys += 4;
    scene->sm_sort_keys = malloc(keys * sizeof(uint32_t));
    scene->sm_sort_tmp = malloc(keys * sizeof(uint32_t));

    assert(scene->sm_sort_keys);
    assert(scene->sm_sort_tmp);
    scene->sm_vertex_xyz = malloc(((size_t)caps->max_vertices + 4) * 4 * sizeof(int));
    assert(scene->sm_vertex_xyz);
    return true;
}

/* The y-ordered stash the batched raster walk reads. Eight ints per face; see
 * the field for the layout and for why the depth sort fills it. Capacity is
 * max_faces, but the region actually touched is num_faces of whichever model
 * is being drawn, which is a few hundred bytes for the median one. */
static bool
scene_alloc_presort_xy(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneCaps* caps)
{
    if( scene->sm_face_x4 )
        return true;

    scene->sm_face_x4 = malloc(((size_t)caps->max_faces + 4) * 4 * sizeof(int));
    scene->sm_face_y4 = malloc(((size_t)caps->max_faces + 4) * 4 * sizeof(int));

    assert(scene->sm_face_x4);
    assert(scene->sm_face_y4);
    return true;
}

static void
ToriDraw_SceneAllocBuffers(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_SceneCaps* caps)
{
    scene->max_vertices = caps->max_vertices;
    scene->max_faces = caps->max_faces;
    scene->depth_levels = caps->depth_levels;
    scene->depth_stride = caps->depth_stride;
    scene->priority_stride = caps->priority_stride;
    scene->flex_prio_capacity =
        caps->flex_prio11 < caps->flex_prio12 ? caps->flex_prio11 : caps->flex_prio12;

    scene_alloc_vertices(scene, caps);
    scene_alloc_face_order(scene, caps);

    /* Capacity always comes from the tier; SMALL only selects the CSR sorter,
     * and with it the bitonic+radix sort's keys and the batched walk's
     * stash. */
    if( caps->small_mode )
    {
        scene_alloc_csr_sort(scene, caps);
        scene_alloc_bitonic_radix_keys(scene, caps);
        scene_alloc_presort_xy(scene, caps);
    }
    else
    {
        scene_alloc_bucket_sort(scene, caps);
    }

    if( !caps->lazy_textures )
    {
        scene->tex_state = calloc(1, sizeof(struct ToriDraw_TextureState));
        assert(scene->tex_state);
    }
}

bool
ToriDraw_SceneZBufferResize(
    struct ToriDraw_Scene* scene,
    int stride,
    int rows)
{
    torizdepth_t* grown;
    size_t want;

    if( stride <= 0 || rows <= 0 )
        return false;
    assert(scene);

    /* Never shrink: the buffer is scratch, and a scene alternating between two
     * viewport sizes would otherwise realloc on every switch. */
    if( stride < scene->zbuffer_stride )
        stride = scene->zbuffer_stride;
    if( rows < scene->zbuffer_rows )
        rows = scene->zbuffer_rows;
    if( scene->zbuffer && stride == scene->zbuffer_stride && rows == scene->zbuffer_rows )
        return true;

    want = (size_t)stride * (size_t)rows;
    grown = (torizdepth_t*)realloc(scene->zbuffer, want * sizeof(torizdepth_t));
    assert(grown);

    scene->zbuffer = grown;
    scene->zbuffer_stride = stride;
    scene->zbuffer_rows = rows;
    /* Contents are undefined until a model resets the region it draws into, so
     * there is nothing to preserve or initialise here. */
    return true;
}

void
ToriDraw_SceneZBufferFree(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return;
    free(scene->zbuffer);
    scene->zbuffer = NULL;
    scene->zbuffer_stride = 0;
    scene->zbuffer_rows = 0;
}

bool
ToriDraw_SceneHasZBuffer(
    const struct ToriDraw_Scene* scene,
    int stride,
    int rows)
{
    return scene && scene->zbuffer && scene->zbuffer_stride >= stride &&
           scene->zbuffer_rows >= rows;
}

struct ToriDraw_TextureState*
ToriDraw_SceneTexState(struct ToriDraw_Scene* scene)
{
    assert(scene);

    if( !scene->tex_state )
    {
        scene->tex_state = calloc(1, sizeof(struct ToriDraw_TextureState));
        assert(scene->tex_state);
    }

    return scene->tex_state;
}

size_t
ToriDraw_SceneSize(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size)
{
    struct ToriDraw_SceneCaps caps;
    if( !resolve_caps(flags, scratch_buffer_size, &caps) )
        return 0;
    return ToriDraw_SceneBufferBytes(&caps);
}

void
ToriDraw_ScenePrintSize(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size)
{
    struct ToriDraw_SceneCaps caps;
    const char* profile_name = "invalid";

    if( !resolve_caps(flags, scratch_buffer_size, &caps) )
    {
        printf("toridraw scene size: invalid scratch-buffer size %d\n", scratch_buffer_size);
        return;
    }
    if( (unsigned int)scratch_buffer_size < TORIDRAW_SCRATCH_BUFFER_SIZE_COUNT )
        profile_name = g_scratch_profiles[scratch_buffer_size].name;

    size_t struct_bytes = sizeof(struct ToriDraw_Scene);
    size_t vertex_bytes = vertex_buffer_bytes(&caps);
    size_t order_bytes = (size_t)caps.max_faces * sizeof(int);
    size_t sort_bytes =
        caps.small_mode ? small_sort_buffer_bytes(&caps) : full_sort_buffer_bytes(&caps);
    size_t tex_bytes = caps.lazy_textures ? 0 : sizeof(struct ToriDraw_TextureState);
    size_t total = struct_bytes + vertex_bytes + order_bytes + sort_bytes + tex_bytes;

    printf(
        "toridraw scene size (scratch=%s, flags=0x%x%s%s%s):\n",
        profile_name,
        (unsigned)flags,
        caps.small_mode ? ", SMALL" : ", FULL",
        caps.lazy_textures ? ", LAZY_TEXTURES" : "",
        (flags & TORIDRAW_SCENE_DEPTH_16K) ? ", DEPTH_16K" : "");
    printf("  struct:     %6zu bytes\n", struct_bytes);
    printf("  vertices:   %6zu bytes (%d verts x 6 arrays)\n", vertex_bytes, caps.max_vertices);
    printf("  face order: %6zu bytes (%d faces)\n", order_bytes, caps.max_faces);
    printf(
        "  sort:       %6zu bytes (%s)\n",
        sort_bytes,
        caps.small_mode ? "CSR small variant" : "full bucket arrays");
    printf(
        "  textures:   %6zu bytes%s\n",
        tex_bytes,
        caps.lazy_textures ? " (lazy, not included in alloc)" : "");
    printf("  total:      %6zu bytes (%.1f KiB)\n", total, (double)total / 1024.0);
}

/*
 * struct ToriDraw_Scene declares _Alignas(16) on the prepared-camera block --
 * the SSE2 and Apple AArch64 projection kernels are built around that layout --
 * but 32-bit malloc promises only eight, so the declaration was a promise the
 * allocator never made. The compiler believed it and wrote the block with
 * `movaps`; an unaligned aligned-store raises a general-protection fault, which
 * Windows reports as an access violation at 0xffffffff.
 *
 * It stayed hidden while the scene was 5.5 MB, because a block that size comes
 * back page-aligned by accident. Moving the event queue off the struct left an
 * ordinary 25 KB block, and whether it landed on 8 or 16 then followed the heap
 * layout -- which is why the fault tracked the size of the environment block and
 * vanished under a debugger.
 *
 * Over-allocate and keep the allocator's own pointer in the word below the
 * block, so the free path hands back exactly what it was given.
 */
#define TORIDRAW_SCENE_ALIGN _Alignof(struct ToriDraw_Scene)

static struct ToriDraw_Scene*
td_scene_alloc_aligned(void)
{
    void* raw;
    uintptr_t aligned;

    raw = calloc(1, sizeof(struct ToriDraw_Scene) + TORIDRAW_SCENE_ALIGN + sizeof(void*));
    assert(raw);
    aligned = ((uintptr_t)raw + sizeof(void*) + TORIDRAW_SCENE_ALIGN - 1) &
              ~(uintptr_t)(TORIDRAW_SCENE_ALIGN - 1);
    ((void**)aligned)[-1] = raw;
    return (struct ToriDraw_Scene*)aligned;
}

/* A deallocator, so NULL is an idiom here exactly as it is for free. */
static void
td_scene_free_aligned(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return;
    free(((void**)scene)[-1]);
}

struct ToriDraw_Scene*
ToriDraw_SceneNew(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size)
{
    struct ToriDraw_SceneCaps caps;
    if( !resolve_caps(flags, scratch_buffer_size, &caps) )
        return NULL;

    struct ToriDraw_Scene* scene = td_scene_alloc_aligned();

    scene->flags = flags;
    /* Build on first query rather than reporting an empty list. */
    scene->anim_list_dirty = true;

    ToriDraw_SceneAllocBuffers(scene, &caps);

    if( !ToriDraw_SceneGraphInit(scene) )
    {
        ToriDraw_SceneFreeBuffers(scene);
        td_scene_free_aligned(scene);
        return NULL;
    }

    return scene;
}

void
ToriDraw_SceneFree(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return;
    ToriDraw_SceneGraphShutdown(scene);
    /* After the shutdown, not before: disposing the elements is what returns
     * the models they borrowed, and the store asserts it is empty. */
    ToriDraw_SharedModelStoreFree(scene->shared_models);
    ToriDraw_SharedFacesStoreFree(scene->shared_faces);
    ToriDraw_SceneFreeBuffers(scene);
    td_scene_free_aligned(scene);
}

/* Raster family selector; see graphics/raster/scanline/scanline_select.h. */
int g_toridraw_raster_scanline = 0;

// clang-format off
#include "impl/raster/dispatch/tri.clip.u.c"
#include "impl/raster/dispatch/tri.face_alpha.u.c"
#include "impl/raster/scanline/scanline.dispatch.u.c"
#include "impl/raster/dispatch/tri.flat.u.c"
#include "impl/raster/dispatch/tri.gouraud.u.c"
#include "impl/raster/dispatch/tri.texture_opaque.u.c"
#include "impl/raster/dispatch/tri.texture_transparent.u.c"
#include "impl/raster/dispatch/tri.texture_affine.u.c"
#include "impl/raster/dispatch/tri.zbuf.u.c"
#include "toridraw_render.u.c"
/* The HD kernel set: five projection families x twelve compositing variants,
 * each with a depth-tested twin. One file per variant; they share two
 * templates. Included here rather than from the triangle wrappers because
 * nothing in the stock path calls them — only ToriDraw_RenderHD does.
 *
 * `texplane` is the SD eye-ray plane walk over the sampler matrix. RenderHD no
 * longer routes to it — render type 0 goes to `texpmn`, the HD reference's
 * per-vertex frame projection (see texmap_common.h) — but it stays: it is the
 * bridge test-texture-matrix uses to prove every sampler span kernel against
 * its plain SD twin, which no other family can be compared to. */
// clang-format off
#include "impl/raster/tex/raster.texplane.perspective.texalpha.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texalpha.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texalpha.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texalpha.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.nofacealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.facealpha.nomodulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.facealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.nofacealpha.modulate.painter.branching.lerp8_v3.scalar.u.c"
/* The depth-tested twins of all 48, one for one. Same four families, same
 * twelve compositing points; the walk and the uv fit are the plain kernel's
 * and only the per-pixel depth test is added. ToriDraw_RenderHDZBuffered is
 * the only caller. */
#include "impl/raster/tex/raster.texplane.perspective.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texalpha.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texalpha.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texalpha.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.texopaque.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texplane.perspective.textrans.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texalpha.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.texopaque.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texpmn.perspective.textrans.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texalpha.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.texopaque.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcylinder.perspective.textrans.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texalpha.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.texopaque.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texcube.perspective.textrans.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texalpha.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.texopaque.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.facealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.nofacealpha.modulate.zbuf.branching.lerp8_v3.scalar.u.c"
#include "impl/raster/tex/raster.texsphere.perspective.textrans.nofacealpha.nomodulate.zbuf.branching.lerp8_v3.scalar.u.c"
// clang-format on

#include "toridraw_raster.u.c"
#include "toridraw_render_hd.u.c"
// clang-format on

void
ToriDraw_Init(void)
{
    ToriDraw_InitMath();
    ToriDraw_InitHsl16();
    /* The HD path's cylinder and sphere projections read this. */
    ToriDraw_InitAtanTable();

    const char* scanline_env = getenv("TORIDRAW_RASTER_SCANLINE");
    if( scanline_env )
        g_toridraw_raster_scanline = (scanline_env[0] != '0' && scanline_env[0] != '\0');
}

int g_toridraw_raster_batch_override = -1;

void
ToriDraw_RasterBatchSetArmed(int enabled)
{
    g_toridraw_raster_batch_override = enabled;
}

void
ToriDraw_RasterSetScanline(bool enabled)
{
    g_toridraw_raster_scanline = enabled ? 1 : 0;
}

bool
ToriDraw_RasterGetScanline(void)
{
    return g_toridraw_raster_scanline != 0;
}

/*
 * The stock painter walk: with its whole-model door, or without it.
 *
 * TORIDRAW_RASTER_BATCH used to be read inside the SORT, once per model, as
 * `presort && toridraw_raster_batch_armed()`. That is a per-frame constant
 * asked per model, and worse, it was a second place that decided whether the
 * stash gets filled -- the first being whether the raster has a door to read
 * it at all. Two answers to one question, in two stages, agreeing only by
 * construction.
 *
 * Now it is one answer, made where every other kernel choice is made: the
 * knob picks the KERNEL. Armed, the painter is the branching kernel with its
 * run door; disarmed, it is the same four callbacks with no door
 * (branching_perface). sd_wants_presort then derives the stash from the door
 * alone, which is what it always meant, and the sort has nothing left to ask.
 *
 * Smooth has no door either way -- the smooth branching kernel names
 * ToriDraw_RasterWalkPerFace -- so the knob does not reach it.
 */
static const struct ToriDraw_RasterKernelSD*
toridraw_stock_painter_kernel(bool smooth)
{
    if( smooth )
        return ToriDraw_RasterKernelSDGetSmoothBranching();
    return toridraw_raster_batch_armed() ? ToriDraw_RasterKernelSDGetBranching()
                                         : ToriDraw_RasterKernelSDGetBranchingPerFace();
}

static const struct ToriDraw_RasterKernelSD*
toridraw_stock_builtin_kernel(bool smooth)
{
    if( ToriDraw_RasterGetScanline() )
        return smooth ? ToriDraw_RasterKernelSDGetSmoothScanline()
                      : ToriDraw_RasterKernelSDGetScanline();
    return toridraw_stock_painter_kernel(smooth);
}

static bool
toridraw_stock_model_needs_zbuffer(
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_ViewPort* view_port)
{
    const struct ToriDraw_Model* model;
    int clip_top;
    int clip_bottom;
    int rows;
    int stride;

    if( !ToriDraw_ModelKindIsFull(hnd.kind) || !hnd.u.model.model )
        return false;

    model = model_as_full(hnd);
    if( !(model->flags & TORIDRAW_MODEL_FLAG_ZBUFFER) )
        return false;

    clip_top = view_port->clip_top > 0 ? view_port->clip_top : 0;
    clip_bottom = view_port->clip_bottom > 0 ? view_port->clip_bottom : view_port->height;
    if( clip_bottom < clip_top )
        clip_bottom = clip_top;
    rows = clip_top + (clip_bottom - clip_top);
    stride = view_port->stride ? view_port->stride : view_port->width;

    return ToriDraw_SceneHasZBuffer(scene, stride, rows) ||
           (scene->flags & TORIDRAW_SCENE_MODEL_ZBUFFER) != 0;
}

/*
 * The kernel to raster with once the model has asked for a depth buffer.
 *
 * A kernel names its own depth-tested twin, so nothing here identifies the
 * caller's kernel to carry an attribute across the swap: smooth shading and
 * the face-sort flag survive it because the twin was chosen BY the kernel that
 * has them, at the point where they are still known, rather than reconstructed
 * by whoever happened to notice the model's flag.
 *
 * A kernel that names no twin gets the stock depth painter, which is all this
 * could ever do for a kernel the library did not build -- but it is a stated
 * fallback for an unstated slot now, not the silent flattening of every kernel
 * that failed an address comparison.
 */
static const struct ToriDraw_RasterKernelSD*
sd_kernel_zbuffered_variant(const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel);
    if( !kernel->zbuffered_variant )
        return toridraw_stock_zbuffered_kernel(false, true);
    assert((kernel->zbuffered_variant->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER) &&
           "a depth-tested twin must ask for the depth buffer");
    assert(!kernel->zbuffered_variant->zbuffered_variant &&
           "a depth-tested twin is its own twin and names none");
    return kernel->zbuffered_variant;
}

/* ---- The projection and face cull+sort stages as kernels ------------- */

/*
 * One file per prebaked kernel. Here rather than beside the SD raster kernels
 * because these two stages are defined in terms of ToriDraw_Project and the
 * face-order entry points, which this file owns.
 */
// clang-format off
#include "kernels/projection.prepared.u.c"
#include "kernels/projection.portable.u.c"
#include "kernels/facesort.bucket.u.c"
#include "kernels/facesort.bitonic_radix.u.c"
#include "kernels/sd.gpu.u.c"

/* The prebaked tables, one file each. After the subkernels they name: each
 * refers to a kernel object defined above by address, so the tables are
 * immutable statics rather than rebuilt on every call. */
#include "kernels/table.software_painter.u.c"
#include "kernels/table.software_scanline.u.c"
#include "kernels/table.software_zbuffered.u.c"
#include "kernels/table.gpu.u.c"
#include "kernels/table.sprite_baker.u.c"
#include "kernels/table.hd_painter.u.c"
#include "kernels/table.hd_zbuffered.u.c"
// clang-format on

const struct ToriDraw_RasterKernelSD*
ToriDraw_RasterKernelSDGetStock(bool smooth)
{
    return toridraw_stock_builtin_kernel(smooth);
}

/* ---- Kernel scratch ------------------------------------------------- */

/*
 * Recover the caps a scene was built with.
 *
 * Every field the allocators need is already on the scene, so the ensure API
 * does not have to be told the tier a second time. flex_prio_capacity is the
 * min of the two flexible-priority capacities, which are equal in every
 * scratch profile, so it stands in for both.
 */
static void
scene_caps_from_scene(
    const struct ToriDraw_Scene* scene,
    struct ToriDraw_SceneCaps* caps)
{
    assert(scene);
    assert(caps);

    caps->max_vertices = scene->max_vertices;
    caps->max_faces = scene->max_faces;
    caps->depth_levels = scene->depth_levels;
    caps->depth_stride = scene->depth_stride;
    caps->priority_stride = scene->priority_stride;
    caps->flex_prio11 = scene->flex_prio_capacity;
    caps->flex_prio12 = scene->flex_prio_capacity;
    caps->small_mode = (scene->flags & TORIDRAW_SCENE_SMALL) != 0;
    caps->lazy_textures = (scene->flags & TORIDRAW_SCENE_LAZY_TEXTURES) != 0;
}

uint32_t
ToriDraw_SceneScratchResident(const struct ToriDraw_Scene* scene)
{
    uint32_t resident = 0;

    assert(scene);

    if( scene->screen_vertices_x )
        resident |= TORIDRAW_SCENE_SCRATCH_VERTICES;
    if( scene->tmp_face_order )
        resident |= TORIDRAW_SCENE_SCRATCH_FACE_ORDER;
    if( scene->tmp_depth_faces )
        resident |= TORIDRAW_SCENE_SCRATCH_BUCKET_SORT;
    if( scene->sm_faces_by_depth )
        resident |= TORIDRAW_SCENE_SCRATCH_CSR_SORT;
    if( scene->sm_sort_keys )
        resident |= TORIDRAW_SCENE_SCRATCH_BITONIC_RADIX_KEYS;
    if( scene->sm_face_x4 )
        resident |= TORIDRAW_SCENE_SCRATCH_PRESORT_XY;
    return resident;
}

bool
ToriDraw_SceneHasScratch(
    const struct ToriDraw_Scene* scene,
    uint32_t needs)
{
    assert(scene);
    return (ToriDraw_SceneScratchResident(scene) & needs) == needs;
}

static const struct ToriDraw_FaceCullSortKernel*
sd_kernel_face_sort(const struct ToriDraw_RasterKernelSD* kernel)
{
    /* A raster kernel names a RASTER. Stages 1 and 2 used to hang off it, and
     * the entries that reach here are precisely the ones whose caller named no
     * table -- ToriDraw_RenderModel and friends -- so "the usual sort" is the
     * honest answer and this is the one place that says it. */
    (void)kernel;
    return ToriDraw_FaceCullSortKernelGetDefault();
}

/*
 * Does this raster draw whole models, rather than leaning on the stock
 * per-face walk?
 *
 * Asked by identity against ToriDraw_RasterWalkPerFace rather than by a flag:
 * naming the stock walk IS the declaration that this kernel has no traversal
 * of its own, so there is nothing extra to keep in sync. It is also what makes
 * a lane with no presorted-run assembly answer correctly -- there
 * toridraw_raster_walk_batched IS ToriDraw_RasterWalkPerFace (raster.batch.h),
 * so the branching kernel reports no door and nothing fills a stash that would
 * never be read.
 */
static bool
sd_raster_is_whole_model(const struct ToriDraw_RasterKernelSD* raster)
{
    /* A NULL walk is not a defaulted one here: it is the GPU kernel, which has
     * no stage 3 at all and so has no door either. Every kernel that rasters
     * names its walk, and ToriDraw_RasterKernelSDAssertValid says so. */
    return raster->draw_model && raster->draw_model != ToriDraw_RasterWalkPerFace;
}

/*
 * Does this raster want the y-ordered stash, and can this sort make one?
 *
 * THE rule, and the only copy of it. Everything that has to agree about the
 * presort -- the scratch the scene allocates, stage 2 when it runs, the
 * validator that reports DEGRADED -- asks here, so no two of them can answer
 * differently. It is a property of the kernel and never of the call site: only
 * a raster with a whole-model door loads sm_face_x4/y4, and a caller that asks
 * for the store anyway pays seven stores and a six-way compare per drawn face
 * to fill a buffer nothing reads.
 */
static bool
sd_wants_presort(
    const struct ToriDraw_RasterKernelSD* raster,
    const struct ToriDraw_FaceCullSortKernel* sort)
{
    return sd_raster_is_whole_model(raster) &&
           (sort->provides & TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY) != 0;
}

/* The same question of a kernel a renderer holds, which carries its own
 * (deprecated) face-sort slot. */
static bool
sd_kernel_wants_presort(const struct ToriDraw_RasterKernelSD* kernel)
{
    return sd_wants_presort(kernel, sd_kernel_face_sort(kernel));
}

uint32_t
ToriDraw_SceneKernelScratchNeeds(
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    bool const small = (scene->flags & TORIDRAW_SCENE_SMALL) != 0;
    uint32_t needs = TORIDRAW_SCENE_SCRATCH_VERTICES;
    const struct ToriDraw_FaceCullSortKernel* sort;

    assert(scene);

    /* A NULL kernel is the stock painter: it sorts. */
    if( kernel && !(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING) )
        return needs;

    needs |= TORIDRAW_SCENE_SCRATCH_FACE_ORDER;
    needs |= small ? TORIDRAW_SCENE_SCRATCH_CSR_SORT : TORIDRAW_SCENE_SCRATCH_BUCKET_SORT;

    /* A NULL kernel is the stock painter; name it, and everything below reads
     * one object rather than repeating the defaulting. */
    if( !kernel )
        kernel = ToriDraw_RasterKernelSDGetBranching();

    /* The bitonic+radix sort's keys, asked for by the kernel rather than
     * inferred from its identity -- and only where the CSR sorter runs,
     * because a full scene takes the dense bucket walk whichever sort is
     * named. */
    sort = sd_kernel_face_sort(kernel);
    if( small && (sort->needs & TORIDRAW_FACESORT_NEEDS_BITONIC_RADIX_KEYS) )
        needs |= TORIDRAW_SCENE_SCRATCH_BITONIC_RADIX_KEYS;

    /* The presort stash, decided by the rule stage 2 will apply to this same
     * kernel. Asking here and asking there cannot disagree, because it is one
     * function. */
    if( small && sd_wants_presort(kernel, sort) )
        needs |= TORIDRAW_SCENE_SCRATCH_PRESORT_XY;

    return needs;
}

bool
ToriDraw_SceneEnsureScratch(
    struct ToriDraw_Scene* scene,
    uint32_t needs)
{
    struct ToriDraw_SceneCaps caps;

    assert(scene);

    if( ToriDraw_SceneHasScratch(scene, needs) )
        return true;

    scene_caps_from_scene(scene, &caps);

    if( needs & TORIDRAW_SCENE_SCRATCH_VERTICES )
        scene_alloc_vertices(scene, &caps);
    if( needs & TORIDRAW_SCENE_SCRATCH_FACE_ORDER )
        scene_alloc_face_order(scene, &caps);
    if( needs & TORIDRAW_SCENE_SCRATCH_BUCKET_SORT )
        scene_alloc_bucket_sort(scene, &caps);
    if( needs & TORIDRAW_SCENE_SCRATCH_CSR_SORT )
        scene_alloc_csr_sort(scene, &caps);
    if( needs & TORIDRAW_SCENE_SCRATCH_BITONIC_RADIX_KEYS )
        scene_alloc_bitonic_radix_keys(scene, &caps);
    if( needs & TORIDRAW_SCENE_SCRATCH_PRESORT_XY )
        scene_alloc_presort_xy(scene, &caps);

    return ToriDraw_SceneHasScratch(scene, needs);
}

bool
ToriDraw_SceneEnsureKernelScratch(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(scene);
    return ToriDraw_SceneEnsureScratch(scene, ToriDraw_SceneKernelScratchNeeds(scene, kernel));
}

/* ---- The kernel table ------------------------------------------------ */

/* The stages a table names. Both are required: a table is the object that
 * answers "which projection, which sort, which raster", and one that answers
 * NULL to either is not a table with a default, it is a table with a stage
 * missing. */
static void
kernel_table_resolve(
    const struct ToriDraw_Kernel* kernel,
    const struct ToriDraw_ProjectionKernel** projection,
    const struct ToriDraw_FaceCullSortKernel** sort)
{
    assert(kernel->projection);
    assert(kernel->face_sort);
    *projection = kernel->projection;
    *sort = kernel->face_sort;
}

/* Does this table's raster draw whole models? A GPU table has no raster at
 * all, so it never does. */
static bool
kernel_table_raster_is_whole_model(const struct ToriDraw_Kernel* kernel)
{
    return kernel->raster && sd_raster_is_whole_model(kernel->raster);
}

/* Does this table's raster want the y-ordered stash, and can its sort make one? */
static bool
kernel_table_wants_presort(
    const struct ToriDraw_Kernel* kernel,
    const struct ToriDraw_FaceCullSortKernel* sort)
{
    return kernel->raster && sd_wants_presort(kernel->raster, sort);
}

uint32_t
ToriDraw_KernelScratchNeeds(
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_Kernel* kernel)
{
    bool const small = (scene->flags & TORIDRAW_SCENE_SMALL) != 0;
    uint32_t needs = TORIDRAW_SCENE_SCRATCH_VERTICES;
    const struct ToriDraw_ProjectionKernel* projection;
    const struct ToriDraw_FaceCullSortKernel* sort;

    assert(scene);
    assert(kernel);

    kernel_table_resolve(kernel, &projection, &sort);
    (void)projection; /* Projection reads and writes the vertex arrays only. */

    /* A raster that resolves depth per pixel takes no face order, so stage 2
     * does not run and none of its scratch is touched. Which raster answers
     * that is the table's other question: an HD table fills raster_hd and
     * leaves raster NULL, and a table with neither is the GPU one, which does
     * sort -- its faces go to a vertex upload in that order. */
    if( kernel->raster &&
        !(kernel->raster->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING) )
        return needs;
    if( kernel->raster_hd &&
        !(kernel->raster_hd->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING) )
        return needs;

    needs |= TORIDRAW_SCENE_SCRATCH_FACE_ORDER;
    needs |= small ? TORIDRAW_SCENE_SCRATCH_CSR_SORT : TORIDRAW_SCENE_SCRATCH_BUCKET_SORT;

    if( small && (sort->needs & TORIDRAW_FACESORT_NEEDS_BITONIC_RADIX_KEYS) )
        needs |= TORIDRAW_SCENE_SCRATCH_BITONIC_RADIX_KEYS;

    /* Both halves have to want it: a sort that can stash, and a raster with a
     * whole-model door to read it. */
    if( small && kernel_table_wants_presort(kernel, sort) )
        needs |= TORIDRAW_SCENE_SCRATCH_PRESORT_XY;

    return needs;
}

bool
ToriDraw_KernelEnsureScratch(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_Kernel* kernel)
{
    assert(scene);
    assert(kernel);
    return ToriDraw_SceneEnsureScratch(scene, ToriDraw_KernelScratchNeeds(scene, kernel));
}

/*
 * Take a table for a scene: validate it, say so when it will not be what was
 * asked for, and provision what it needs.
 *
 * The validate/ensure pair existed before this and had no production caller at
 * all -- every renderer took a getter's result and rendered with it, so the
 * DEGRADED diagnostic the validator exists to produce was never printed and
 * the scratch API was exercised only by tests. That is the shape of an API
 * nobody can be blamed for skipping: it is two extra calls, both of which look
 * optional at the call site because the frame draws without them.
 *
 * One function instead, and the renderer's line reads as what it means:
 *
 *     renderer->kernel = ToriDraw_KernelTake(scene, ToriDraw_KernelGetSoftwarePainter());
 *
 * The getter is still where the library resolves the environment knobs into a
 * choice; this is where that choice meets the scene it will actually draw
 * into. Returns the table it was handed, so it wraps the getter rather than
 * replacing it.
 *
 * INCOMPATIBLE aborts. It means the next frame draws wrong pixels or trips an
 * assert deeper in, and a renderer cannot do anything useful with the news at
 * this point -- the reason is printed first, because an assert that fires on
 * the fit enum alone tells you nothing about which of a dozen conditions it
 * was.
 *
 * DEGRADED does NOT abort: the frame is correct, some stage is just slower
 * than the table's name implies. That is exactly the case a caller cannot
 * discover any other way short of a profile, so it is reported once, here.
 *
 * Does NOT provision the z-buffer. That is sized from the viewport rather than
 * the scene tier and outlives no renderer's resize -- call
 * ToriDraw_SceneZBufferResize where the viewport is known.
 */
/*
 * The software table a renderer takes when it has no opinion beyond "the
 * usual one": branching or scanline, as TORIDRAW_RASTER_SCANLINE /
 * ToriDraw_RasterSetScanline decided.
 *
 * The table twin of ToriDraw_RasterKernelSDGetStock, and the same rule -- the
 * choice is resolved AT THE CALL, so a renderer takes it once at init, after
 * ToriDraw_Init. There is no smooth twin here because smooth is a per-draw
 * request in the SD kernels rather than a property of a pass, and no renderer
 * holds a smooth table.
 */
const struct ToriDraw_Kernel*
ToriDraw_KernelGetStock(void)
{
    return ToriDraw_RasterGetScanline() ? ToriDraw_KernelGetSoftwareScanline()
                                        : ToriDraw_KernelGetSoftwarePainter();
}

/*
 * A name for a slot that has one, and a visible marker for a slot that does
 * not. Every kernel the library hands out is named; a caller assembling its
 * own table is not obliged to name it, and a report that silently printed
 * nothing there would read as if the slot were empty rather than anonymous.
 */
static const char*
kernel_name_or_unnamed(const char* name)
{
    return name ? name : "(unnamed)";
}

/* TORIDRAW_KERNEL_LOG=0 silences the report ToriDraw_KernelTake prints. Read
 * once; it selects nothing, so it is the one knob in the inventory whose
 * answer no stage can observe. */
static int
toridraw_kernel_log_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
    {
        const char* v = getenv("TORIDRAW_KERNEL_LOG");
        armed = (v && v[0] == '0') ? 0 : 1;
    }
    return armed;
}

/*
 * WHY THE REPORT IS NOT SIMPLY PRINTED AT EVERY TAKE.
 *
 * ToriRS_Soft3D_Init -- the renderer's whole kernel setup, and the only
 * production caller of ToriDraw_KernelTake -- runs once per FRAME, not once
 * per process: it resets the renderer against this frame's pixel buffer, and
 * UITreeCmd_Render goes further and builds a throwaway renderer per picture.
 * So "log at the take" is sixty reports a second of a configuration that has
 * not moved.
 *
 * The fix is not a once-per-process flag either. Several renderers run in one
 * client -- the world, the chrome, an offscreen picture -- and they need not
 * hold the same table; a flag would report whichever took first and hide the
 * rest for the life of the program.
 *
 * So: report a CONFIGURATION the first time it is seen. Identity is the five
 * pointers a report is made of and nothing else, which is what makes an
 * override that swaps a stage without swapping the table --
 * ToriDraw_FaceSortSetBitonicRadix, the scanline setter -- come out as the
 * news it is, while
 * the same table taken again every frame stays silent.
 *
 * The table is small and fixed. Six prebaked tables exist and a caller may
 * assemble more, so overflow is not impossible, only unlikely; when it
 * happens the report says it is standing down rather than falling back to
 * printing every frame, which is the failure this whole function exists to
 * prevent.
 */
#define TORIDRAW_KERNEL_LOG_SLOTS 8

struct toridraw_kernel_log_entry
{
    const struct ToriDraw_Kernel* table;
    const struct ToriDraw_ProjectionKernel* projection;
    const struct ToriDraw_FaceCullSortKernel* sort;
    const struct ToriDraw_RasterKernelSD* raster;
    const struct ToriDraw_RasterKernelHD* raster_hd;
};

static bool
toridraw_kernel_log_is_new(const struct ToriDraw_Kernel* table)
{
    static struct toridraw_kernel_log_entry seen[TORIDRAW_KERNEL_LOG_SLOTS];
    static int seen_count;
    static bool stood_down;
    struct toridraw_kernel_log_entry entry;
    int i;

    entry.table = table;
    entry.projection = table->projection;
    entry.sort = table->face_sort;
    entry.raster = table->raster;
    entry.raster_hd = table->raster_hd;

    for( i = 0; i < seen_count; i++ )
    {
        if( seen[i].table == entry.table && seen[i].projection == entry.projection &&
            seen[i].sort == entry.sort && seen[i].raster == entry.raster &&
            seen[i].raster_hd == entry.raster_hd )
            return false;
    }

    if( seen_count == TORIDRAW_KERNEL_LOG_SLOTS )
    {
        if( !stood_down )
        {
            stood_down = true;
            fprintf(
                stderr,
                "toridraw: %d kernel configurations reported; no more will be\n",
                TORIDRAW_KERNEL_LOG_SLOTS);
        }
        return false;
    }

    seen[seen_count++] = entry;
    return true;
}

void
ToriDraw_KernelLogConfiguration(const struct ToriDraw_Kernel* table)
{
    const struct ToriDraw_ProjectionKernel* projection;
    const struct ToriDraw_FaceCullSortKernel* sort;

    assert(table);
    kernel_table_resolve(table, &projection, &sort);

    fprintf(stderr, "toridraw: table      : %s\n", kernel_name_or_unnamed(table->name));
    fprintf(stderr, "toridraw: projection : %s\n", kernel_name_or_unnamed(projection->name));
    fprintf(stderr, "toridraw: face_sort  : %s\n", kernel_name_or_unnamed(sort->name));
    /* A table names exactly ONE raster, and which slot holds it is what says
     * which pipeline this is -- so the label is the report's answer to that,
     * not a fixed string with a name after it. The walk is read off draw_model
     * rather than taken from the name: `branching` is one kernel object with
     * the door and another without, and the whole point of printing this is to
     * say which one a build actually got. */
    if( table->raster )
        fprintf(
            stderr,
            "toridraw: raster SD  : %s (%s)\n",
            kernel_name_or_unnamed(table->raster->name),
            kernel_table_raster_is_whole_model(table) ? "whole-model door" : "per-face walk");
    else if( table->raster_hd )
        fprintf(
            stderr,
            "toridraw: raster HD  : %s\n",
            kernel_name_or_unnamed(table->raster_hd->name));
    else
        fprintf(stderr, "toridraw: raster     : none -- stages 1 and 2 only\n");
}

const struct ToriDraw_Kernel*
ToriDraw_KernelTake(struct ToriDraw_Scene* scene, const struct ToriDraw_Kernel* table)
{
    const char* why = NULL;
    enum ToriDraw_KernelFit fit;

    assert(scene);
    assert(table);

    /* Before the fit check, so a DEGRADED or INCOMPATIBLE line lands under the
     * report of the table it is about rather than naming one on its own. The
     * new-configuration test comes second: a silenced log must not consume the
     * slot that would report this configuration if the log were turned on
     * later in the same process. */
    if( toridraw_kernel_log_armed() && toridraw_kernel_log_is_new(table) )
        ToriDraw_KernelLogConfiguration(table);

    fit = ToriDraw_KernelValidate(table, scene, &why);
    if( fit == TORIDRAW_KERNEL_FIT_INCOMPATIBLE )
    {
        fprintf(
            stderr,
            "toridraw: kernel table `%s` is INCOMPATIBLE with this scene: %s\n",
            kernel_name_or_unnamed(table->name),
            why);
        assert(!"ToriDraw_KernelTake: incompatible kernel table for this scene");
    }
    else if( fit == TORIDRAW_KERNEL_FIT_DEGRADED )
    {
        fprintf(
            stderr,
            "toridraw: kernel table `%s` is DEGRADED on this scene: %s\n",
            kernel_name_or_unnamed(table->name),
            why);
    }

    {
        bool const provisioned = ToriDraw_KernelEnsureScratch(scene, table);

        /* An allocation failure here is not a case to handle: the frame would
         * draw down a path the table did not name, which is the exact thing
         * the fit check above just certified against. */
        assert(provisioned);
        (void)provisioned;
    }

    return table;
}

enum ToriDraw_KernelFit
ToriDraw_KernelValidate(
    const struct ToriDraw_Kernel* kernel,
    const struct ToriDraw_Scene* scene,
    const char** why)
{
    static const char* ok = "ok";
    bool const small = (scene->flags & TORIDRAW_SCENE_SMALL) != 0;
    const struct ToriDraw_ProjectionKernel* projection;
    const struct ToriDraw_FaceCullSortKernel* sort;
    enum ToriDraw_KernelFit fit = TORIDRAW_KERNEL_FIT_OK;

    assert(kernel);
    assert(scene);
    assert(why);
    *why = ok;

    kernel_table_resolve(kernel, &projection, &sort);

    /* ---- INCOMPATIBLE: would draw wrong, or assert. ---- */

    if( !projection->project )
    {
        *why = "projection kernel has no project function";
        return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
    }
    if( !sort->sort )
    {
        *why = "face sort kernel has no sort function";
        return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
    }
    if( kernel->raster )
    {
        if( !kernel->raster->vtable )
        {
            /* The GPU kernel object, named as a table's raster. Legal as a
             * table (stages 1 and 2), so this is only wrong if someone put it
             * where a software raster belongs -- which is what the NULL raster
             * slot is for. Say so rather than silently accepting it. */
            *why = "raster kernel has no vtable; use a NULL raster slot for a GPU table";
            return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
        }
        for( int i = 0; i < TORIDRAW_RASTER_FACE_SD_CLASS_COUNT; i++ )
        {
            if( !kernel->raster->vtable->draw[i] )
            {
                *why = "raster vtable has a NULL face slot";
                return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
            }
        }
        if( (kernel->raster->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING) &&
            !(sort->provides & TORIDRAW_FACESORT_PROVIDES_FACE_ORDER) )
        {
            *why = "raster needs a face order the sort does not provide";
            return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
        }
    }

    if( kernel->raster && kernel->raster_hd )
    {
        /* A table names ONE raster. Two would leave every entry point to guess
         * which pipeline the caller meant. */
        *why = "table names both an SD and an HD raster";
        return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
    }
    if( kernel->raster_hd )
    {
        if( !kernel->raster_hd->vtable )
        {
            *why = "HD raster kernel has no vtable";
            return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
        }
        for( int i = 0; i < TORIDRAW_RASTER_FACE_HD_CLASS_COUNT; i++ )
        {
            if( !kernel->raster_hd->vtable->draw[i] )
            {
                *why = "HD raster vtable has a NULL face slot";
                return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
            }
        }
        if( (kernel->raster_hd->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING) &&
            !(sort->provides & TORIDRAW_FACESORT_PROVIDES_FACE_ORDER) )
        {
            *why = "HD raster needs a face order the sort does not provide";
            return TORIDRAW_KERNEL_FIT_INCOMPATIBLE;
        }
    }

    /* ---- DEGRADED: correct pixels, slower path. ---- */

    if( !small && (sort->needs & TORIDRAW_FACESORT_NEEDS_SMALL_SCENE) )
    {
        *why = "face sort falls back to the bucket sort on a full scene";
        fit = TORIDRAW_KERNEL_FIT_DEGRADED;
    }
    else if( kernel_table_raster_is_whole_model(kernel) && !small )
    {
        /* The door exists and the sort could stash, but the scene has no
         * sm_face_x4/y4 to stash into, so the batched walk never runs. */
        *why = "whole-model raster falls back per face: a full scene has no presort stash";
        fit = TORIDRAW_KERNEL_FIT_DEGRADED;
    }
    else if(
        kernel_table_raster_is_whole_model(kernel) &&
        !(sort->provides & TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY) )
    {
        *why = "whole-model raster falls back per face: the sort cannot presort";
        fit = TORIDRAW_KERNEL_FIT_DEGRADED;
    }

    return fit;
}

static inline int
sd_kernel_project(
    const struct ToriDraw_RasterKernelSD* kernel,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    const struct ToriDraw_ProjectionKernel* const projection =
        ToriDraw_ProjectionKernelGetDefault();

    /* As above: these entries are the ones whose caller named no table. */
    (void)kernel;
    assert(projection->project);
    scene->active_hnd = hnd;
    return projection->project(projection->user_data, scene, hnd, position, view_port, camera);
}

/* The stock sort, with the presort the caller already decided. The only place
 * that choice is still spelled out by hand; every entry above it derives it. */
static int
sd_sort_faces_stock(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    bool presort)
{
    if( scene->flags & TORIDRAW_SCENE_SMALL )
        ToriDraw_ComputeProjectedFaceOrderSmall(scene, hnd, presort);
    else
        ToriDraw_ComputeProjectedFaceOrder(scene, hnd, presort);
    return scene->tmp_face_order_count;
}

/*
 * Stage 2 through a held kernel, with the presort decided HERE.
 *
 * The caller does not get a say, and there is no presorted twin to pick
 * between: the stash has exactly one consumer -- the batched walk behind a
 * whole-model draw_model -- so the kernel that names the raster is the only
 * thing that knows. See sd_wants_presort.
 */
static inline int
sd_kernel_sort(
    const struct ToriDraw_RasterKernelSD* kernel,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene)
{
    const struct ToriDraw_FaceCullSortKernel* const sort = sd_kernel_face_sort(kernel);
    bool const presort = sd_wants_presort(kernel, sort);

    assert(sort->sort);
    return sort->sort(sort->user_data, scene, hnd, presort);
}

static int
sd_render_with_kernel_painter(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    int cull;

    ToriDraw_RasterKernelSDAssertValid(kernel);
    assert(!(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER));

    cull = sd_kernel_project(kernel, hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
        sd_kernel_sort(kernel, hnd, scene);

    return ToriDraw_RasterPainter(scene, hnd, view_port, camera, pixel_buffer, kernel)
               ? TORIDRAW_CULL_VISIBLE
               : TORIDRAW_CULL_ERROR;
}

static int
sd_render_with_kernel_z(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    ToriDraw_RasterKernelSDAssertValid(kernel);
    assert(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER);

    int cull;

    cull = sd_kernel_project(kernel, hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
        sd_kernel_sort(kernel, hnd, scene);

    return ToriDraw_RasterZ(scene, hnd, view_port, camera, pixel_buffer, kernel)
               ? TORIDRAW_CULL_VISIBLE
               : TORIDRAW_CULL_ERROR;
}

void
ToriDraw_ScenePrepareProjectionCamera(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_Camera* camera)
{
    struct ToriDraw_ProjectionPreparedCamera* prepared;
    struct ToriDraw_ProjectionPreparedCameraFloat* prepared_f;
    int values[5];

    assert(scene);
    assert(camera);
    /* The compiler writes these five vectors with aligned stores, on the
     * strength of the _Alignas(16) in the struct; td_scene_alloc_aligned is
     * what makes that true of the block the scene actually lives in. */
    assert(((uintptr_t)&scene->projection_prepared_camera & (TORIDRAW_SCENE_ALIGN - 1)) == 0);
    /* The float block is read with an ALIGNED load, so this one is not a
     * tidiness check -- an unaligned movaps faults. */
    assert(((uintptr_t)&scene->projection_prepared_camera_f & (TORIDRAW_SCENE_ALIGN - 1)) == 0);

    /* Publish the source only after all five vectors are complete. */
    scene->projection_prepared_camera_source = NULL;
    prepared = &scene->projection_prepared_camera;
    prepared_f = &scene->projection_prepared_camera_f;
    values[0] = ToriDraw_ReadCosTable(camera->yaw);
    values[1] = ToriDraw_ReadSinTable(camera->yaw);
    values[2] = ToriDraw_ReadCosTable(camera->pitch);
    values[3] = ToriDraw_ReadSinTable(camera->pitch);
    values[4] = toridraw_projection_cot16(
                    camera->projection_mode, camera->projection_scale, camera->fov_rpi2048) >>
                1;

    for( int lane = 0; lane < 4; lane++ )
    {
        prepared->cos_yaw[lane] = values[0];
        prepared->sin_yaw[lane] = values[1];
        prepared->cos_pitch[lane] = values[2];
        prepared->sin_pitch[lane] = values[3];
        prepared->cot15[lane] = values[4];

        /* Exactly what toridraw_projection_prepared_core used to build per call.
         * Both scales are powers of two, so the multiply is an exponent
         * adjustment and the only rounding is the int-to-float conversion --
         * the same one cvtdq2ps performed. Bit-for-bit the same operands. */
        prepared_f->cos_pitch[lane] = (float)values[2] * (1.0f / 65536.0f);
        prepared_f->sin_pitch[lane] = (float)values[3] * (1.0f / 65536.0f);
        prepared_f->cot15[lane] = (float)values[4] * (1.0f / 64.0f);
    }
    scene->projection_prepared_camera_source = camera;
}

void
ToriDraw_SceneClearProjectionCamera(struct ToriDraw_Scene* scene)
{
    assert(scene);
    scene->projection_prepared_camera_source = NULL;
}

void
ToriDraw_RenderModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer)
{
    const struct ToriDraw_RasterKernelSD* kernel = toridraw_stock_builtin_kernel(false);

    if( toridraw_stock_model_needs_zbuffer(hnd, scene, view_port) )
        kernel = sd_kernel_zbuffered_variant(kernel);

    (void)ToriDraw_RenderModelWithRasterKernel(
        hnd, scene, position, view_port, camera, pixel_buffer, kernel);
}

int
ToriDraw_RenderModelWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(scene);
    assert(kernel);

    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER )
        return sd_render_with_kernel_z(
            hnd, scene, position, view_port, camera, pixel_buffer, kernel);
    return sd_render_with_kernel_painter(
        hnd, scene, position, view_port, camera, pixel_buffer, kernel);
}

/* ---- The three stages, driven by a table ----------------------------- */

static inline const struct ToriDraw_ProjectionKernel*
table_projection(const struct ToriDraw_Kernel* table)
{
    assert(table->projection);
    return table->projection;
}

static inline const struct ToriDraw_FaceCullSortKernel*
table_face_sort(const struct ToriDraw_Kernel* table)
{
    assert(table->face_sort);
    return table->face_sort;
}

int
ToriDraw_RenderModel1ProjectWithTable(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    const struct ToriDraw_Kernel* table)
{
    const struct ToriDraw_ProjectionKernel* projection;

    assert(scene);
    assert(table);
    projection = table_projection(table);
    assert(projection->project);

    scene->active_hnd = hnd;
    return projection->project(projection->user_data, scene, hnd, position, view_port, camera);
}

/*
 * Stage 2, with the presort decision made HERE rather than by the caller.
 *
 * The choice used to be spelled into the entry's NAME, which put it on whoever
 * called it, and getting it wrong is the regression this exists to prevent: a
 * GPU renderer that calls a presorting entry pays seven stores and a six-way
 * compare per drawn face to fill a buffer nothing downstream loads. A table
 * already knows, because it names the raster: the stash is worth filling
 * exactly when that raster has a whole-model door to read it and the sort can
 * produce it: sd_wants_presort is the only copy of that rule.
 */
int
ToriDraw_RenderModel2SortFacesWithTable(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_Kernel* table)
{
    const struct ToriDraw_FaceCullSortKernel* sort;
    bool presort;

    assert(scene);
    assert(table);
    sort = table_face_sort(table);
    assert(sort->sort);

    presort = kernel_table_wants_presort(table, sort);
    return sort->sort(sort->user_data, scene, hnd, presort);
}

static int
sd_raster_with_zswap(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel);

int
ToriDraw_RenderModel3RasterWithTable(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_Kernel* table)
{
    assert(scene);
    assert(table);
    assert(table->raster && "a GPU table has no raster stage");
    return sd_raster_with_zswap(scene, view_port, camera, pixel_buffer, table->raster);
}

/*
 * All three stages through one table.
 *
 * Not a wrapper over ToriDraw_RenderModelWithRasterKernel: that one reads the
 * projection and face-sort slots off the RASTER kernel, which is where they
 * used to live. A table names them itself, so running the stages here is what
 * keeps a table's sort choice from being silently replaced by the default.
 */
int
ToriDraw_RenderModelWithTable(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_Kernel* table)
{
    int cull;

    assert(scene);
    assert(table);
    assert(table->raster && "a GPU table has no raster stage");

    cull = ToriDraw_RenderModel1ProjectWithTable(hnd, scene, position, view_port, camera, table);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    if( table->raster->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
        ToriDraw_RenderModel2SortFacesWithTable(hnd, scene, table);

    return ToriDraw_RenderModel3RasterWithTable(scene, view_port, camera, pixel_buffer, table);
}

int
ToriDraw_RenderModel1Project(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    scene->active_hnd = hnd;
    return ToriDraw_Project(scene, hnd, position, view_port, camera);
}

/*
 * Stage 3 with the depth-tested swap in front of it.
 *
 * Internal, and no longer a public entry: a caller holding a raster kernel and
 * nothing else was how stages 1 and 2 ended up hanging off the stage-3 object.
 * The table entries share this body, which is all that entry ever was.
 */
static int
sd_raster_with_zswap(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel);
    assert(kernel->vtable && "a GPU kernel has no raster stage");
    if( !(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER) &&
        toridraw_stock_model_needs_zbuffer(scene->active_hnd, scene, view_port) )
        kernel = sd_kernel_zbuffered_variant(kernel);
    return ToriDraw_RenderModel3RasterWithRasterKernel(
        scene, view_port, camera, pixel_buffer, kernel);
}

/*
 * Sort this model's faces back to front, for a caller that names no kernel.
 *
 * It does NOT leave the pre-sort store behind, and there is no entry that
 * does: the stash is the batched walk's, so the only caller who can honestly
 * ask for it is one holding the kernel that would read it, and that caller
 * takes ...WithTable and never states the choice. What is
 * left here reads the order out of tmp_face_order and nothing else -- the HD
 * path, the sprite baker, the tests -- and filling sm_face_x4/y4 for them is
 * seven stores and a six-way compare per drawn face into a buffer none of them
 * loads.
 */
int
ToriDraw_RenderModel2SortFaces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene)
{
    return sd_sort_faces_stock(hnd, scene, false);
}

int
ToriDraw_RenderModel3Raster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    const struct ToriDraw_RasterKernelSD* kernel = toridraw_stock_builtin_kernel(smooth);

    if( toridraw_stock_model_needs_zbuffer(scene->active_hnd, scene, view_port) )
        kernel = sd_kernel_zbuffered_variant(kernel);

    return ToriDraw_RenderModel3RasterWithRasterKernel(
        scene, view_port, camera, pixel_buffer, kernel);
}

int
ToriDraw_RenderModel3RasterWithRasterKernel(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(scene);
    assert(kernel);
    ToriDraw_RasterKernelSDAssertValid(kernel);

    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER )
    {
        return ToriDraw_RasterZ(scene, scene->active_hnd, view_port, camera, pixel_buffer, kernel)
                   ? TORIDRAW_CULL_VISIBLE
                   : TORIDRAW_CULL_ERROR;
    }

    return ToriDraw_RasterPainter(scene, scene->active_hnd, view_port, camera, pixel_buffer, kernel)
               ? TORIDRAW_CULL_VISIBLE
               : TORIDRAW_CULL_ERROR;
}

int
ToriDraw_RenderZBuffered(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    return ToriDraw_RenderZBufferedWithRasterKernel(
        hnd,
        scene,
        position,
        view_port,
        camera,
        pixel_buffer,
        smooth ? ToriDraw_RasterKernelSDGetSmoothZBuffered()
               : ToriDraw_RasterKernelSDGetZBuffered());
}

int
ToriDraw_RenderZBufferedWithRasterKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(scene);
    assert(kernel);
    assert(kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_ZBUFFER);

    return sd_render_with_kernel_z(hnd, scene, position, view_port, camera, pixel_buffer, kernel);
}
