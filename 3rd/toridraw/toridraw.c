#include "toridraw.h"
#include <assert.h>

#include "toridraw_types.h"
#include "toridraw_shared_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)
_Static_assert(
    offsetof(struct ToriDraw_ProjectionPreparedCamera, cos_yaw) == 0,
    "prepared projection cos-yaw offset");
_Static_assert(
    offsetof(struct ToriDraw_ProjectionPreparedCamera, sin_yaw) == 16,
    "prepared projection sin-yaw offset");
_Static_assert(
    offsetof(struct ToriDraw_ProjectionPreparedCamera, cos_pitch) == 32,
    "prepared projection cos-pitch offset");
_Static_assert(
    offsetof(struct ToriDraw_ProjectionPreparedCamera, sin_pitch) == 48,
    "prepared projection sin-pitch offset");
_Static_assert(
    offsetof(struct ToriDraw_ProjectionPreparedCamera, cot15) == 64,
    "prepared projection cotangent offset");
_Static_assert(
    sizeof(struct ToriDraw_ProjectionPreparedCamera) == 80,
    "prepared projection camera size");
_Static_assert(
    _Alignof(struct ToriDraw_ProjectionPreparedCamera) >= 16,
    "prepared projection camera alignment");
_Static_assert(
    offsetof(struct ToriDraw_Scene, screen_vertices_y) ==
        offsetof(struct ToriDraw_Scene, screen_vertices_x) + sizeof(int*),
    "projection screen-y pointer layout");
_Static_assert(
    offsetof(struct ToriDraw_Scene, screen_vertices_z) ==
        offsetof(struct ToriDraw_Scene, screen_vertices_x) + 2 * sizeof(int*),
    "projection screen-z pointer layout");
_Static_assert(
    offsetof(struct ToriDraw_Scene, orthographic_vertices_x) ==
        offsetof(struct ToriDraw_Scene, screen_vertices_x) + 3 * sizeof(int*),
    "projection orthographic-x pointer layout");
_Static_assert(
    offsetof(struct ToriDraw_Scene, orthographic_vertices_y) ==
        offsetof(struct ToriDraw_Scene, screen_vertices_x) + 4 * sizeof(int*),
    "projection orthographic-y pointer layout");
_Static_assert(
    offsetof(struct ToriDraw_Scene, orthographic_vertices_z) ==
        offsetof(struct ToriDraw_Scene, screen_vertices_x) + 5 * sizeof(int*),
    "projection orthographic-z pointer layout");
_Static_assert(
    offsetof(struct ToriDraw_Scene, projection_prepared_camera) ==
        offsetof(struct ToriDraw_Scene, screen_vertices_x) + 6 * sizeof(int*),
    "prepared projection relative layout");
_Static_assert(
    offsetof(struct ToriDraw_Scene, projection_bound) ==
        offsetof(struct ToriDraw_Scene, screen_vertices_x) + 6 * sizeof(int*) +
            sizeof(struct ToriDraw_ProjectionPreparedCamera),
    "projection bound block relative layout");
_Static_assert(
    sizeof(((struct ToriDraw_Scene*)0)->projection_bound) == 64,
    "projection bound block size");
_Static_assert(
    offsetof(struct ToriDraw_Position, x) == 0,
    "projection position-x layout");
_Static_assert(
    offsetof(struct ToriDraw_Position, y) ==
        offsetof(struct ToriDraw_Position, x) + sizeof(int),
    "projection position-y layout");
_Static_assert(
    offsetof(struct ToriDraw_Position, z) ==
        offsetof(struct ToriDraw_Position, x) + 2 * sizeof(int),
    "projection position-z layout");
#endif

#define TORIDRAW_LOW_MAX_VERTICES    2048
#define TORIDRAW_LOW_MAX_FACES       4096
#define TORIDRAW_LOW_PRIORITY_STRIDE 4096
#define TORIDRAW_LOW_FLEX_PRIO11     4096
#define TORIDRAW_LOW_FLEX_PRIO12     4096

#define TORIDRAW_MED_MAX_VERTICES    4096
#define TORIDRAW_MED_MAX_FACES       8192
#define TORIDRAW_MED_PRIORITY_STRIDE 8192
#define TORIDRAW_MED_FLEX_PRIO11     8192
#define TORIDRAW_MED_FLEX_PRIO12     8192

/* HIGH_8K is the existing QBD-safe vertex/face allocation.  Do not reduce it
 * without replacing the explicit capacity guard in ToriDraw_Project: the
 * merged sleeping QBD has 6,223 vertices and 9,012 faces.  Its 4,791-unit
 * animated bounding sphere separately requires TORIDRAW_SCENE_DEPTH_16K. */
#define TORIDRAW_HIGH_MAX_VERTICES    8192
#define TORIDRAW_HIGH_MAX_FACES       16384
#define TORIDRAW_HIGH_PRIORITY_STRIDE 16384
#define TORIDRAW_HIGH_FLEX_PRIO11     16384
#define TORIDRAW_HIGH_FLEX_PRIO12     16384

/* VERYHIGH_16K exists for merged NPCs that outgrow HIGH_8K: the
 * face-synthesised QBD body (70260 at K=4) plus its collar ring (69766)
 * projects 9,097 vertices.  Same 2:1 face allowance as the other tiers. */
#define TORIDRAW_VERYHIGH_MAX_VERTICES    16384
#define TORIDRAW_VERYHIGH_MAX_FACES       32768
#define TORIDRAW_VERYHIGH_PRIORITY_STRIDE 32768
#define TORIDRAW_VERYHIGH_FLEX_PRIO11     32768
#define TORIDRAW_VERYHIGH_FLEX_PRIO12     32768

#define TORIDRAW_DEPTH_LEVELS_REFERENCE 1500
#define TORIDRAW_DEPTH_LEVELS_16K       16384
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
    caps->depth_levels = (flags & TORIDRAW_SCENE_DEPTH_16K)
                             ? TORIDRAW_DEPTH_LEVELS_16K
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
scene_alloc_vertices(struct ToriDraw_Scene* scene, const struct ToriDraw_SceneCaps* caps)
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
scene_alloc_face_order(struct ToriDraw_Scene* scene, const struct ToriDraw_SceneCaps* caps)
{
    if( scene->tmp_face_order )
        return true;

    scene->tmp_face_order = malloc((size_t)caps->max_faces * sizeof(int));
    assert(scene->tmp_face_order);
    return true;
}

/* The dense depth_levels x depth_stride bucket table, for a full scene. */
static bool
scene_alloc_bucket_sort(struct ToriDraw_Scene* scene, const struct ToriDraw_SceneCaps* caps)
{
    if( scene->tmp_depth_faces )
        return true;

    /* calloc, not malloc: the render path never clears this table whole.
     * Each sort re-zeroes only the buckets it dirtied after its consumer
     * has walked them (ToriDraw_ComputeProjectedFaceOrder), so the
     * all-zero state is established here, once. */
    scene->tmp_depth_face_count = calloc((size_t)caps->depth_levels, sizeof(faceint_t));
    scene->tmp_depth_faces = malloc(
        (size_t)caps->depth_levels * (size_t)caps->depth_stride * sizeof(faceint_t));
    scene->tmp_priority_face_count = malloc(12 * sizeof(faceint_t));
    scene->tmp_priority_depth_sum = malloc(12 * sizeof(int));
    scene->tmp_priority_faces =
        malloc(12 * (size_t)caps->priority_stride * sizeof(faceint_t));
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
scene_alloc_csr_sort(struct ToriDraw_Scene* scene, const struct ToriDraw_SceneCaps* caps)
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

/* The flat sort's key arrays. Rounded up to a power of two so the bitonic
 * network can pad without a second buffer. */
static bool
scene_alloc_flat_keys(struct ToriDraw_Scene* scene, const struct ToriDraw_SceneCaps* caps)
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
    return true;
}

/* The y-ordered stash the batched raster walk reads. Eight ints per face; see
 * the field for the layout and for why the depth sort fills it. Capacity is
 * max_faces, but the region actually touched is num_faces of whichever model
 * is being drawn, which is a few hundred bytes for the median one. */
static bool
scene_alloc_presort_xy(struct ToriDraw_Scene* scene, const struct ToriDraw_SceneCaps* caps)
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
     * and with it the flat sort's keys and the batched walk's stash. */
    if( caps->small_mode )
    {
        scene_alloc_csr_sort(scene, caps);
        scene_alloc_flat_keys(scene, caps);
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

    printf("toridraw scene size (scratch=%s, flags=0x%x%s%s%s):\n",
           profile_name,
           (unsigned)flags,
           caps.small_mode ? ", SMALL" : ", FULL",
           caps.lazy_textures ? ", LAZY_TEXTURES" : "",
           (flags & TORIDRAW_SCENE_DEPTH_16K) ? ", DEPTH_16K" : "");
    printf("  struct:     %6zu bytes\n", struct_bytes);
    printf("  vertices:   %6zu bytes (%d verts x 6 arrays)\n", vertex_bytes, caps.max_vertices);
    printf("  face order: %6zu bytes (%d faces)\n", order_bytes, caps.max_faces);
    printf("  sort:       %6zu bytes (%s)\n",
           sort_bytes,
           caps.small_mode ? "CSR small variant" : "full bucket arrays");
    printf("  textures:   %6zu bytes%s\n",
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

    raw = calloc(
        1, sizeof(struct ToriDraw_Scene) + TORIDRAW_SCENE_ALIGN + sizeof(void*));
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
#include "triangles/toridraw_triangle_clip.u.c"
#include "triangles/toridraw_triangle_face_alpha.u.c"
#include "graphics/raster/scanline/scanline.u.c"
#include "triangles/toridraw_triangle_flat.u.c"
#include "triangles/toridraw_triangle_gouraud.u.c"
#ifndef TORIDRAW_PIXEL16
#include "triangles/toridraw_triangle_texture_opaque.u.c"
#include "triangles/toridraw_triangle_texture_transparent.u.c"
#include "triangles/toridraw_triangle_texture_affine.u.c"
/* The depth-tested family draws through the 32-bit texture and blend paths, so
 * it shares the PIXEL16 exclusion with them. Under a 16-bit target
 * TORIDRAW_MODEL_FLAG_ZBUFFER is inert and models draw by face order alone. */
#include "triangles/toridraw_triangle_zbuf.u.c"
#endif
#include "toridraw_render.u.c"
#ifndef TORIDRAW_PIXEL16
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
#include "graphics/raster/texture/texplane.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.facealpha.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.facealpha.modulate.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.modulate.branching.lerp8_v3.u.c"
/* The depth-tested twins of all 48, one for one. Same four families, same
 * twelve compositing points; the walk and the uv fit are the plain kernel's
 * and only the per-pixel depth test is added. ToriDraw_RenderHDZBuffered is
 * the only caller. */
#include "graphics/raster/texture/texplane.persp.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texalpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.texopaque.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texplane.persp.textrans.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texalpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.texopaque.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texpmn.persp.textrans.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texalpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.texopaque.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcylinder.persp.textrans.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texalpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.texopaque.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texcube.persp.textrans.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texalpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.texopaque.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.facealpha.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.facealpha.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.modulate.zbuf.branching.lerp8_v3.u.c"
#include "graphics/raster/texture/texsphere.persp.textrans.zbuf.branching.lerp8_v3.u.c"
// clang-format on
#endif /* !TORIDRAW_PIXEL16 */

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

static const struct ToriDraw_RasterKernelSD*
toridraw_stock_builtin_kernel(bool smooth)
{
    if( ToriDraw_RasterGetScanline() )
        return smooth ? ToriDraw_RasterKernelSDGetSmoothScanline()
                      : ToriDraw_RasterKernelSDGetScanline();
    return smooth ? ToriDraw_RasterKernelSDGetSmoothBranching()
                  : ToriDraw_RasterKernelSDGetBranching();
}

static bool
toridraw_stock_model_needs_zbuffer(
    struct ToriDraw_ModelHandle hnd,
    const struct ToriDraw_Scene* scene,
    const struct ToriDraw_ViewPort* view_port)
{
#ifdef TORIDRAW_PIXEL16
    (void)hnd;
    (void)scene;
    (void)view_port;
    return false;
#else
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
#endif
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
#include "kernels/facesort.flat.u.c"
#include "kernels/sd.gpu.u.c"
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
scene_caps_from_scene(const struct ToriDraw_Scene* scene, struct ToriDraw_SceneCaps* caps)
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
        resident |= TORIDRAW_SCENE_SCRATCH_FLAT_KEYS;
    if( scene->sm_face_x4 )
        resident |= TORIDRAW_SCENE_SCRATCH_PRESORT_XY;
    return resident;
}

bool
ToriDraw_SceneHasScratch(const struct ToriDraw_Scene* scene, uint32_t needs)
{
    assert(scene);
    return (ToriDraw_SceneScratchResident(scene) & needs) == needs;
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

    /* The flat sort's keys, asked for by the kernel rather than inferred from
     * its identity -- and only where the CSR sorter runs, because a full scene
     * takes the dense bucket walk whichever sort is named. */
    sort = (kernel && kernel->face_sort) ? kernel->face_sort
                                         : ToriDraw_FaceCullSortKernelGetDefault();
    if( small && (sort->needs & TORIDRAW_FACESORT_NEEDS_FLAT_KEYS) )
        needs |= TORIDRAW_SCENE_SCRATCH_FLAT_KEYS;

    /* The presort stash. Both halves have to want it: a sort that can leave it
     * behind, and a raster that will read it. The raster side is still the
     * stock branching kernel by identity -- the batched walk tests for that
     * vtable -- and becomes a declared whole-model door in its own phase. */
    if( small && (sort->provides & TORIDRAW_FACESORT_PROVIDES_PRESORTED_XY) &&
        (!kernel || kernel == ToriDraw_RasterKernelSDGetBranching()) )
        needs |= TORIDRAW_SCENE_SCRATCH_PRESORT_XY;

    return needs;
}

bool
ToriDraw_SceneEnsureScratch(struct ToriDraw_Scene* scene, uint32_t needs)
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
    if( needs & TORIDRAW_SCENE_SCRATCH_FLAT_KEYS )
        scene_alloc_flat_keys(scene, &caps);
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
    return ToriDraw_SceneEnsureScratch(
        scene, ToriDraw_SceneKernelScratchNeeds(scene, kernel));
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
    if( kernel->projection )
    {
        assert(kernel->projection->project);
        return kernel->projection->project(
            kernel->projection->user_data, scene, hnd, position, view_port, camera);
    }
    return ToriDraw_RenderModel1Project(hnd, scene, position, view_port, camera);
}

static inline int
sd_kernel_sort(
    const struct ToriDraw_RasterKernelSD* kernel,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    bool presort)
{
    if( kernel->face_sort )
    {
        assert(kernel->face_sort->sort);
        return kernel->face_sort->sort(kernel->face_sort->user_data, scene, hnd, presort);
    }
    return presort ? ToriDraw_RenderModel2SortFacesPresorted(hnd, scene)
                   : ToriDraw_RenderModel2SortFaces(hnd, scene);
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

    /* The one caller that goes on to raster in software through the stock
     * branching kernels, which is the batched walk's only door. Everything
     * else uses the plain sort next door. */
    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
        sd_kernel_sort(kernel, hnd, scene, true);

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

#ifdef TORIDRAW_PIXEL16
    assert(false && "SD Z-buffer raster kernels need the 32-bit raster");
    (void)hnd;
    (void)scene;
    (void)position;
    (void)view_port;
    (void)camera;
    (void)pixel_buffer;
    return TORIDRAW_CULL_ERROR;
#else
    int cull;

    cull = sd_kernel_project(kernel, hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    if( kernel->flags & TORIDRAW_RASTER_KERNEL_FLAG_NEEDS_FACE_SORTING )
        sd_kernel_sort(kernel, hnd, scene, false);

    return ToriDraw_RasterZ(scene, hnd, view_port, camera, pixel_buffer, kernel)
               ? TORIDRAW_CULL_VISIBLE
               : TORIDRAW_CULL_ERROR;
#endif
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
    assert(
        ((uintptr_t)&scene->projection_prepared_camera &
         (TORIDRAW_SCENE_ALIGN - 1)) == 0);
    /* The float block is read with an ALIGNED load, so this one is not a
     * tidiness check -- an unaligned movaps faults. */
    assert(
        ((uintptr_t)&scene->projection_prepared_camera_f &
         (TORIDRAW_SCENE_ALIGN - 1)) == 0);

    /* Publish the source only after all five vectors are complete. */
    scene->projection_prepared_camera_source = NULL;
    prepared = &scene->projection_prepared_camera;
    prepared_f = &scene->projection_prepared_camera_f;
    values[0] = ToriDraw_ReadCosTable(camera->yaw);
    values[1] = ToriDraw_ReadSinTable(camera->yaw);
    values[2] = ToriDraw_ReadCosTable(camera->pitch);
    values[3] = ToriDraw_ReadSinTable(camera->pitch);
    values[4] =
        toridraw_proj_cot16(camera->proj_mode, camera->proj_scale, camera->fov_rpi2048) >> 1;

    for( int lane = 0; lane < 4; lane++ )
    {
        prepared->cos_yaw[lane] = values[0];
        prepared->sin_yaw[lane] = values[1];
        prepared->cos_pitch[lane] = values[2];
        prepared->sin_pitch[lane] = values[3];
        prepared->cot15[lane] = values[4];

        /* Exactly what toridraw_proj_prepared_core used to build per call.
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
        kernel = toridraw_stock_zbuffered_kernel(false, true);

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

int
ToriDraw_RenderModel1ProjectWithKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel);
    return sd_kernel_project(kernel, hnd, scene, position, view_port, camera);
}

int
ToriDraw_RenderModel2SortFacesWithKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel);
    return sd_kernel_sort(kernel, hnd, scene, false);
}

int
ToriDraw_RenderModel2SortFacesPresortedWithKernel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_RasterKernelSD* kernel)
{
    assert(kernel);
    return sd_kernel_sort(kernel, hnd, scene, true);
}

int
ToriDraw_RenderModel3RasterWithKernel(
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
    {
        bool smooth = kernel == ToriDraw_RasterKernelSDGetSmoothBranching() ||
                      kernel == ToriDraw_RasterKernelSDGetSmoothScanline();
        kernel = toridraw_stock_zbuffered_kernel(smooth, true);
    }
    return ToriDraw_RenderModel3RasterWithRasterKernel(
        scene, view_port, camera, pixel_buffer, kernel);
}

/*
 * Sort this model's faces back to front.
 *
 * This is the plain entry and it does NOT leave the pre-sort store behind. Use
 * it whenever the faces are going anywhere except the batched software raster
 * walk -- which is every D3D9 and GL renderer, the HD path, the sprite baker
 * and the tests. They read the order out of tmp_face_order and nothing else,
 * and filling sm_face_x4/y4 for them is seven stores and a six-way compare per
 * drawn face into a buffer none of them loads.
 */
int
ToriDraw_RenderModel2SortFaces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene)
{
    if( scene->flags & TORIDRAW_SCENE_SMALL )
        ToriDraw_ComputeProjectedFaceOrderSmall(scene, hnd, false);
    else
        ToriDraw_ComputeProjectedFaceOrder(scene, hnd, false);
    return scene->tmp_face_order_count;
}

/*
 * The same sort, leaving the y ordering behind for the batched raster walk.
 *
 * The sort already holds all three y values -- it needed them for the winding
 * test -- so ordering the triangle here costs a permuted copy and saves every
 * kernel downstream a six-way compare ladder, which is up to six unpredictable
 * branches on a part that pays twenty pipeline stages for a mispredict.
 *
 * Only worth calling if the batched walk will actually run on the result. It
 * may decline: a full-mode scene has no sm_face_x4/y4 to fill, and
 * TORIDRAW_RASTER_BATCH=0 asks for the old pipeline. Either way the sort
 * records what it did in scene->sm_face_xy_valid and the walk reads that, so
 * asking for the store and not getting it is safe rather than silent.
 */
int
ToriDraw_RenderModel2SortFacesPresorted(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene)
{
    if( scene->flags & TORIDRAW_SCENE_SMALL )
        ToriDraw_ComputeProjectedFaceOrderSmall(scene, hnd, true);
    else
        ToriDraw_ComputeProjectedFaceOrder(scene, hnd, true);
    return scene->tmp_face_order_count;
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
        kernel = toridraw_stock_zbuffered_kernel(smooth, true);

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
#ifdef TORIDRAW_PIXEL16
        assert(false && "SD Z-buffer raster kernels need the 32-bit raster");
        return TORIDRAW_CULL_ERROR;
#else
        return ToriDraw_RasterZ(
                   scene, scene->active_hnd, view_port, camera, pixel_buffer, kernel)
                   ? TORIDRAW_CULL_VISIBLE
                   : TORIDRAW_CULL_ERROR;
#endif
    }

    return ToriDraw_RasterPainter(
               scene, scene->active_hnd, view_port, camera, pixel_buffer, kernel)
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

    return sd_render_with_kernel_z(
        hnd, scene, position, view_port, camera, pixel_buffer, kernel);
}
