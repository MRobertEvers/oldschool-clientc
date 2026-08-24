#include "toridraw.h"
#include <assert.h>

#include "toridraw_types.h"

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

#define TORIDRAW_SMALL_MAX_VERTICES  1024
#define TORIDRAW_SMALL_MAX_FACES     2048
#define TORIDRAW_SMALL_FLEX_PRIO11   TORIDRAW_SMALL_MAX_FACES
#define TORIDRAW_SMALL_FLEX_PRIO12   TORIDRAW_SMALL_MAX_FACES

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

    if( caps->small_mode )
    {
        caps->max_vertices = TORIDRAW_SMALL_MAX_VERTICES;
        caps->max_faces = TORIDRAW_SMALL_MAX_FACES;
        caps->depth_stride = 0;
        caps->priority_stride = 0;
        caps->flex_prio11 = TORIDRAW_SMALL_FLEX_PRIO11;
        caps->flex_prio12 = TORIDRAW_SMALL_FLEX_PRIO12;
    }
    else
    {
        caps->max_vertices = profile->max_vertices;
        caps->max_faces = profile->max_faces;
        caps->depth_stride = TORIDRAW_FULL_DEPTH_STRIDE;
        caps->priority_stride = profile->priority_stride;
        caps->flex_prio11 = profile->flex_prio11;
        caps->flex_prio12 = profile->flex_prio12;
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

    memset(scene, 0, sizeof(*scene));
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

    scene->screen_vertices_x = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->screen_vertices_y = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->screen_vertices_z = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->orthographic_vertices_x = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->orthographic_vertices_y = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->orthographic_vertices_z = malloc((size_t)caps->max_vertices * sizeof(int));
    scene->tmp_face_order = malloc((size_t)caps->max_faces * sizeof(int));

    assert(scene->screen_vertices_x);
    assert(scene->screen_vertices_y);
    assert(scene->screen_vertices_z);
    assert(scene->orthographic_vertices_x);
    assert(scene->orthographic_vertices_y);
    assert(scene->orthographic_vertices_z);
    assert(scene->tmp_face_order);

    if( caps->small_mode )
    {
        scene->sm_face_depth = malloc((size_t)caps->max_faces * sizeof(faceint_t));
        scene->sm_depth_offset = malloc((size_t)(caps->depth_levels + 1) * sizeof(int));
        scene->sm_depth_cursor = malloc((size_t)caps->depth_levels * sizeof(int));
        scene->sm_faces_by_depth = malloc((size_t)caps->max_faces * sizeof(faceint_t));
        scene->sm_prio_offset = malloc(13 * sizeof(int));
        scene->sm_prio_faces = malloc(13 * (size_t)caps->max_faces * sizeof(faceint_t));
        scene->sm_flex_prio11_face_to_depth =
            malloc((size_t)caps->flex_prio11 * sizeof(int));
        scene->sm_flex_prio12_face_to_depth =
            malloc((size_t)caps->flex_prio12 * sizeof(int));

        assert(scene->sm_face_depth);
        assert(scene->sm_depth_offset);
        assert(scene->sm_depth_cursor);
        assert(scene->sm_faces_by_depth);
        assert(scene->sm_prio_offset);
        assert(scene->sm_prio_faces);
        assert(scene->sm_flex_prio11_face_to_depth);
        assert(scene->sm_flex_prio12_face_to_depth);
    }
    else
    {
        scene->tmp_depth_face_count = malloc((size_t)caps->depth_levels * sizeof(faceint_t));
        scene->tmp_depth_faces = malloc(
            (size_t)caps->depth_levels * (size_t)caps->depth_stride * sizeof(faceint_t));
        scene->tmp_priority_face_count = malloc(12 * sizeof(faceint_t));
        scene->tmp_priority_depth_sum = malloc(12 * sizeof(int));
        scene->tmp_priority_faces =
            malloc(12 * (size_t)caps->priority_stride * sizeof(faceint_t));
        scene->tmp_flex_prio11_face_to_depth =
            malloc((size_t)caps->flex_prio11 * sizeof(int));
        scene->tmp_flex_prio12_face_to_depth =
            malloc((size_t)caps->flex_prio12 * sizeof(int));

        assert(scene->tmp_depth_face_count);
        assert(scene->tmp_depth_faces);
        assert(scene->tmp_priority_face_count);
        assert(scene->tmp_priority_depth_sum);
        assert(scene->tmp_priority_faces);
        assert(scene->tmp_flex_prio11_face_to_depth);
        assert(scene->tmp_flex_prio12_face_to_depth);
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

struct ToriDraw_Scene*
ToriDraw_SceneNew(
    uint32_t flags,
    enum ToriDraw_ScratchBufferSize scratch_buffer_size)
{
    struct ToriDraw_SceneCaps caps;
    if( !resolve_caps(flags, scratch_buffer_size, &caps) )
        return NULL;

    struct ToriDraw_Scene* scene = calloc(1, sizeof(struct ToriDraw_Scene));
    assert(scene);

    scene->flags = flags;
    /* Build on first query rather than reporting an empty list. */
    scene->anim_list_dirty = true;

    ToriDraw_SceneAllocBuffers(scene, &caps);

    if( !ToriDraw_SceneGraphInit(scene) )
    {
        ToriDraw_SceneFreeBuffers(scene);
        free(scene);
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
    ToriDraw_SceneFreeBuffers(scene);
    free(scene);
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

void
ToriDraw_ScenePrepareProjectionCamera(
    struct ToriDraw_Scene* scene,
    const struct ToriDraw_Camera* camera)
{
    struct ToriDraw_ProjectionPreparedCamera* prepared;
    int values[5];

    assert(scene);
    assert(camera);

    /* Publish the source only after all five vectors are complete. */
    scene->projection_prepared_camera_source = NULL;
    prepared = &scene->projection_prepared_camera;
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
    (void)ToriDraw_RenderModelWithRasterKernel(
        hnd,
        scene,
        position,
        view_port,
        camera,
        pixel_buffer,
        toridraw_stock_builtin_kernel(false));
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
    int cull;

    assert(scene);
    assert(kernel);

    cull = ToriDraw_RenderModel1Project(hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    ToriDraw_RenderModel2SortFaces(hnd, scene);
    return ToriDraw_RenderModel3RasterWithRasterKernel(
        scene, view_port, camera, pixel_buffer, kernel);
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
ToriDraw_RenderModel2SortFaces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene)
{
    if( scene->flags & TORIDRAW_SCENE_SMALL )
        ToriDraw_ComputeProjectedFaceOrderSmall(scene, hnd);
    else
        ToriDraw_ComputeProjectedFaceOrder(scene, hnd);
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
    return ToriDraw_RenderModel3RasterWithRasterKernel(
        scene, view_port, camera, pixel_buffer, toridraw_stock_builtin_kernel(smooth));
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

    return ToriDraw_Raster(
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
        toridraw_stock_builtin_kernel(smooth));
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

#ifdef TORIDRAW_PIXEL16
    /* There is no depth-tested family in a 16-bit build (see the include block
     * above), so this entry point cannot keep its promise there. Saying so beats
     * quietly drawing by face order under a name that says otherwise. */
    assert(false && "ToriDraw_RenderZBuffered needs the 32-bit raster");
    (void)hnd;
    (void)scene;
    (void)position;
    (void)view_port;
    (void)camera;
    (void)pixel_buffer;
    (void)kernel;
    return TORIDRAW_CULL_ERROR;
#else
    int cull = ToriDraw_RenderModel1Project(hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return cull;

    /* Deliberately no ToriDraw_RenderModel2SortFaces: the depth buffer is the
     * visibility answer here, and the face order — priorities included — is the
     * thing this entry point exists to discard. */
    return ToriDraw_RasterZBuffered(
               scene, hnd, view_port, camera, pixel_buffer, kernel)
        ? TORIDRAW_CULL_VISIBLE
        : TORIDRAW_CULL_ERROR;
#endif
}
