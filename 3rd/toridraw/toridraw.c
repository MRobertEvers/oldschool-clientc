#include "toridraw.h"

#include "toridraw_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    free(scene->model_zbuffer);

    memset(scene, 0, sizeof(*scene));
}

static bool
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

    if( !scene->screen_vertices_x || !scene->screen_vertices_y || !scene->screen_vertices_z ||
        !scene->orthographic_vertices_x || !scene->orthographic_vertices_y ||
        !scene->orthographic_vertices_z || !scene->tmp_face_order )
        return false;

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

        if( !scene->sm_face_depth || !scene->sm_depth_offset || !scene->sm_depth_cursor ||
            !scene->sm_faces_by_depth || !scene->sm_prio_offset || !scene->sm_prio_faces ||
            !scene->sm_flex_prio11_face_to_depth || !scene->sm_flex_prio12_face_to_depth )
            return false;
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

        if( !scene->tmp_depth_face_count || !scene->tmp_depth_faces ||
            !scene->tmp_priority_face_count || !scene->tmp_priority_depth_sum ||
            !scene->tmp_priority_faces || !scene->tmp_flex_prio11_face_to_depth ||
            !scene->tmp_flex_prio12_face_to_depth )
            return false;
    }

    if( !caps->lazy_textures )
    {
        scene->tex_state = calloc(1, sizeof(struct ToriDraw_TextureState));
        if( !scene->tex_state )
            return false;
    }

    return true;
}

bool
ToriDraw_SceneModelZBufferResize(
    struct ToriDraw_Scene* scene,
    int stride,
    int rows)
{
    torizdepth_t* grown;
    size_t want;

    if( !scene || stride <= 0 || rows <= 0 )
        return false;

    /* Never shrink: the buffer is scratch, and a scene that has already drawn a
     * large viewport would otherwise realloc back and forth between two
     * viewports of different sizes. */
    if( stride < scene->model_zbuffer_stride )
        stride = scene->model_zbuffer_stride;
    if( rows < scene->model_zbuffer_rows )
        rows = scene->model_zbuffer_rows;
    if( scene->model_zbuffer && stride == scene->model_zbuffer_stride &&
        rows == scene->model_zbuffer_rows )
        return true;

    want = (size_t)stride * (size_t)rows;
    grown = (torizdepth_t*)realloc(scene->model_zbuffer, want * sizeof(torizdepth_t));
    if( !grown )
        return false;

    scene->model_zbuffer = grown;
    scene->model_zbuffer_stride = stride;
    scene->model_zbuffer_rows = rows;
    /* Contents are undefined until a model clears the region it draws into, so
     * there is nothing to preserve or initialise here. */
    return true;
}

void
ToriDraw_SceneModelZBufferFree(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return;
    free(scene->model_zbuffer);
    scene->model_zbuffer = NULL;
    scene->model_zbuffer_stride = 0;
    scene->model_zbuffer_rows = 0;
}

bool
ToriDraw_SceneHasModelZBuffer(
    const struct ToriDraw_Scene* scene,
    int stride,
    int rows)
{
    return scene && scene->model_zbuffer && scene->model_zbuffer_stride >= stride &&
           scene->model_zbuffer_rows >= rows;
}

struct ToriDraw_TextureState*
ToriDraw_SceneTexState(struct ToriDraw_Scene* scene)
{
    if( !scene )
        return NULL;

    if( !scene->tex_state )
    {
        scene->tex_state = calloc(1, sizeof(struct ToriDraw_TextureState));
        if( !scene->tex_state )
            return NULL;
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
    if( !scene )
        return NULL;

    scene->flags = flags;
    /* Build on first query rather than reporting an empty list. */
    scene->anim_list_dirty = true;

    if( !ToriDraw_SceneAllocBuffers(scene, &caps) )
    {
        ToriDraw_SceneFreeBuffers(scene);
        free(scene);
        return NULL;
    }

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
#include "triangles/toridraw_triangle_texture_alpha.u.c"
#include "triangles/toridraw_triangle_texture_affine.u.c"
#endif
#include "toridraw_render.u.c"
#include "toridraw_raster.u.c"
// clang-format on

void
ToriDraw_Init(void)
{
    ToriDraw_InitMath();
    ToriDraw_InitHsl16();

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

void
ToriDraw_RenderModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer)
{
    int cull = ToriDraw_RenderModel1Project(hnd, scene, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return;

    ToriDraw_RenderModel2SortFaces(hnd, scene);

    ToriDraw_RenderModel3Raster(scene, view_port, camera, pixel_buffer, false);
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
    ToriDraw_Raster(scene, scene->active_hnd, view_port, camera, pixel_buffer, smooth);
    return TORIDRAW_CULL_VISIBLE;
}
