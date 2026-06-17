#include "toridraw.h"

#include "toridraw_gccontext.h"
#include "toridraw_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TORIDRAW_FULL_MAX_VERTICES   4096
#define TORIDRAW_FULL_MAX_FACES      4096
#define TORIDRAW_FULL_DEPTH_LEVELS   1500
#define TORIDRAW_FULL_DEPTH_STRIDE   512
#define TORIDRAW_FULL_PRIORITY_STRIDE 2000
#define TORIDRAW_FULL_FLEX_PRIO11    1024
#define TORIDRAW_FULL_FLEX_PRIO12    512

#define TORIDRAW_SMALL_MAX_VERTICES  1024
#define TORIDRAW_SMALL_MAX_FACES     2048
#define TORIDRAW_SMALL_DEPTH_LEVELS  1500
#define TORIDRAW_SMALL_FLEX_PRIO11   1024
#define TORIDRAW_SMALL_FLEX_PRIO12   512

struct ToriDraw_ContextCaps
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

static void
resolve_caps(
    uint32_t flags,
    struct ToriDraw_ContextCaps* caps)
{
    caps->small_mode = (flags & TORIDRAW_CTX_SMALL) != 0;
    caps->lazy_textures = (flags & TORIDRAW_CTX_LAZY_TEXTURES) != 0;

    if( caps->small_mode )
    {
        caps->max_vertices = TORIDRAW_SMALL_MAX_VERTICES;
        caps->max_faces = TORIDRAW_SMALL_MAX_FACES;
        caps->depth_levels = TORIDRAW_SMALL_DEPTH_LEVELS;
        caps->depth_stride = 0;
        caps->priority_stride = 0;
        caps->flex_prio11 = TORIDRAW_SMALL_FLEX_PRIO11;
        caps->flex_prio12 = TORIDRAW_SMALL_FLEX_PRIO12;
    }
    else
    {
        caps->max_vertices = TORIDRAW_FULL_MAX_VERTICES;
        caps->max_faces = TORIDRAW_FULL_MAX_FACES;
        caps->depth_levels = TORIDRAW_FULL_DEPTH_LEVELS;
        caps->depth_stride = TORIDRAW_FULL_DEPTH_STRIDE;
        caps->priority_stride = TORIDRAW_FULL_PRIORITY_STRIDE;
        caps->flex_prio11 = TORIDRAW_FULL_FLEX_PRIO11;
        caps->flex_prio12 = TORIDRAW_FULL_FLEX_PRIO12;
    }
}

static size_t
vertex_buffer_bytes(const struct ToriDraw_ContextCaps* caps)
{
    return (size_t)caps->max_vertices * sizeof(int) * 6;
}

static size_t
full_sort_buffer_bytes(const struct ToriDraw_ContextCaps* caps)
{
    size_t bytes = 0;
    bytes += (size_t)caps->depth_levels * sizeof(faceint_t);
    bytes += (size_t)caps->depth_levels * (size_t)caps->depth_stride * sizeof(faceint_t);
    bytes += 12 * sizeof(faceint_t);
    bytes += 12 * sizeof(faceint_t);
    bytes += 12 * (size_t)caps->priority_stride * sizeof(faceint_t);
    bytes += (size_t)caps->flex_prio11 * sizeof(int);
    bytes += (size_t)caps->flex_prio12 * sizeof(int);
    return bytes;
}

static size_t
small_sort_buffer_bytes(const struct ToriDraw_ContextCaps* caps)
{
    size_t bytes = 0;
    bytes += (size_t)caps->max_faces * sizeof(faceint_t);
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
toridraw_context_buffer_bytes(const struct ToriDraw_ContextCaps* caps)
{
    size_t bytes = sizeof(struct ToriDraw_Context);
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
toridraw_context_free_buffers(struct ToriDraw_Context* context)
{
    if( !context )
        return;

    free(context->screen_vertices_x);
    free(context->screen_vertices_y);
    free(context->screen_vertices_z);
    free(context->orthographic_vertices_x);
    free(context->orthographic_vertices_y);
    free(context->orthographic_vertices_z);
    free(context->tmp_depth_face_count);
    free(context->tmp_depth_faces);
    free(context->tmp_priority_face_count);
    free(context->tmp_priority_depth_sum);
    free(context->tmp_priority_faces);
    free(context->tmp_flex_prio11_face_to_depth);
    free(context->tmp_flex_prio12_face_to_depth);
    free(context->sm_face_depth);
    free(context->sm_depth_offset);
    free(context->sm_depth_cursor);
    free(context->sm_faces_by_depth);
    free(context->sm_prio_offset);
    free(context->sm_prio_faces);
    free(context->sm_flex_prio11_face_to_depth);
    free(context->sm_flex_prio12_face_to_depth);
    free(context->tmp_face_order);
    free(context->tex_state);

    memset(context, 0, sizeof(*context));
}

static bool
toridraw_context_alloc_buffers(
    struct ToriDraw_Context* context,
    const struct ToriDraw_ContextCaps* caps)
{
    context->max_vertices = caps->max_vertices;
    context->max_faces = caps->max_faces;
    context->depth_levels = caps->depth_levels;
    context->depth_stride = caps->depth_stride;
    context->priority_stride = caps->priority_stride;

    context->screen_vertices_x = malloc((size_t)caps->max_vertices * sizeof(int));
    context->screen_vertices_y = malloc((size_t)caps->max_vertices * sizeof(int));
    context->screen_vertices_z = malloc((size_t)caps->max_vertices * sizeof(int));
    context->orthographic_vertices_x = malloc((size_t)caps->max_vertices * sizeof(int));
    context->orthographic_vertices_y = malloc((size_t)caps->max_vertices * sizeof(int));
    context->orthographic_vertices_z = malloc((size_t)caps->max_vertices * sizeof(int));
    context->tmp_face_order = malloc((size_t)caps->max_faces * sizeof(int));

    if( !context->screen_vertices_x || !context->screen_vertices_y || !context->screen_vertices_z ||
        !context->orthographic_vertices_x || !context->orthographic_vertices_y ||
        !context->orthographic_vertices_z || !context->tmp_face_order )
        return false;

    if( caps->small_mode )
    {
        context->sm_face_depth = malloc((size_t)caps->max_faces * sizeof(faceint_t));
        context->sm_depth_offset = malloc((size_t)(caps->depth_levels + 1) * sizeof(int));
        context->sm_depth_cursor = malloc((size_t)caps->depth_levels * sizeof(int));
        context->sm_faces_by_depth = malloc((size_t)caps->max_faces * sizeof(faceint_t));
        context->sm_prio_offset = malloc(13 * sizeof(int));
        context->sm_prio_faces = malloc((size_t)caps->max_faces * sizeof(faceint_t));
        context->sm_flex_prio11_face_to_depth =
            malloc((size_t)caps->flex_prio11 * sizeof(int));
        context->sm_flex_prio12_face_to_depth =
            malloc((size_t)caps->flex_prio12 * sizeof(int));

        if( !context->sm_face_depth || !context->sm_depth_offset || !context->sm_depth_cursor ||
            !context->sm_faces_by_depth || !context->sm_prio_offset || !context->sm_prio_faces ||
            !context->sm_flex_prio11_face_to_depth || !context->sm_flex_prio12_face_to_depth )
            return false;
    }
    else
    {
        context->tmp_depth_face_count = malloc((size_t)caps->depth_levels * sizeof(faceint_t));
        context->tmp_depth_faces = malloc(
            (size_t)caps->depth_levels * (size_t)caps->depth_stride * sizeof(faceint_t));
        context->tmp_priority_face_count = malloc(12 * sizeof(faceint_t));
        context->tmp_priority_depth_sum = malloc(12 * sizeof(faceint_t));
        context->tmp_priority_faces =
            malloc(12 * (size_t)caps->priority_stride * sizeof(faceint_t));
        context->tmp_flex_prio11_face_to_depth =
            malloc((size_t)caps->flex_prio11 * sizeof(int));
        context->tmp_flex_prio12_face_to_depth =
            malloc((size_t)caps->flex_prio12 * sizeof(int));

        if( !context->tmp_depth_face_count || !context->tmp_depth_faces ||
            !context->tmp_priority_face_count || !context->tmp_priority_depth_sum ||
            !context->tmp_priority_faces || !context->tmp_flex_prio11_face_to_depth ||
            !context->tmp_flex_prio12_face_to_depth )
            return false;
    }

    if( !caps->lazy_textures )
    {
        context->tex_state = calloc(1, sizeof(struct ToriDraw_TextureState));
        if( !context->tex_state )
            return false;
    }

    return true;
}

struct ToriDraw_TextureState*
toridraw_context_tex_state(struct ToriDraw_Context* context)
{
    if( !context )
        return NULL;

    if( !context->tex_state )
    {
        context->tex_state = calloc(1, sizeof(struct ToriDraw_TextureState));
        if( !context->tex_state )
            return NULL;
    }

    return context->tex_state;
}

size_t
toridraw_context_size(uint32_t flags)
{
    struct ToriDraw_ContextCaps caps;
    resolve_caps(flags, &caps);
    return toridraw_context_buffer_bytes(&caps);
}

void
toridraw_context_print_size(uint32_t flags)
{
    struct ToriDraw_ContextCaps caps;
    resolve_caps(flags, &caps);

    size_t struct_bytes = sizeof(struct ToriDraw_Context);
    size_t vertex_bytes = vertex_buffer_bytes(&caps);
    size_t order_bytes = (size_t)caps.max_faces * sizeof(int);
    size_t sort_bytes =
        caps.small_mode ? small_sort_buffer_bytes(&caps) : full_sort_buffer_bytes(&caps);
    size_t tex_bytes = caps.lazy_textures ? 0 : sizeof(struct ToriDraw_TextureState);
    size_t total = struct_bytes + vertex_bytes + order_bytes + sort_bytes + tex_bytes;

    printf("toridraw context size (flags=0x%x%s%s):\n",
           (unsigned)flags,
           caps.small_mode ? ", SMALL" : ", FULL",
           caps.lazy_textures ? ", LAZY_TEXTURES" : "");
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

struct ToriDraw_Context*
toridraw_context_new(uint32_t flags)
{
    struct ToriDraw_ContextCaps caps;
    resolve_caps(flags, &caps);

    struct ToriDraw_Context* context = calloc(1, sizeof(struct ToriDraw_Context));
    if( !context )
        return NULL;

    context->flags = flags;

    if( !toridraw_context_alloc_buffers(context, &caps) )
    {
        toridraw_context_free_buffers(context);
        free(context);
        return NULL;
    }

    if( !toridraw_gc_init(context) )
    {
        toridraw_context_free_buffers(context);
        free(context);
        return NULL;
    }

    return context;
}

void
toridraw_context_free(struct ToriDraw_Context* context)
{
    if( !context )
        return;
    toridraw_gc_shutdown(context);
    toridraw_context_free_buffers(context);
    free(context);
}

// clang-format off
#include "triangles/toridraw_triangle_clip.u.c"
#include "triangles/toridraw_triangle_face_alpha.u.c"
#include "triangles/toridraw_triangle_flat.u.c"
#include "triangles/toridraw_triangle_gouraud.u.c"
#ifndef TORIDRAW_PIXEL16
#include "triangles/toridraw_triangle_texture_opaque.u.c"
#include "triangles/toridraw_triangle_texture_transparent.u.c"
#include "triangles/toridraw_triangle_texture_affine.u.c"
#endif
#include "toridraw_render.u.c"
#include "toridraw_raster.u.c"
// clang-format on

void
toridraw_init(void)
{
    toridraw_init_math();
    toridraw_init_hsl16();
}

void
toridraw_render_model(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer)
{
    int cull = toridraw_render_model1_project(hnd, context, position, view_port, camera);
    if( cull != TORIDRAW_CULL_VISIBLE )
        return;

    toridraw_render_model2_sort_faces(hnd, context);

    toridraw_render_model3_raster(context, view_port, camera, pixel_buffer, false);
}

int
toridraw_render_model1_project(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera)
{
    context->active_hnd = hnd;
    return toridraw_project(context, hnd, position, view_port, camera);
}

int
toridraw_render_model2_sort_faces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context)
{
    if( context->flags & TORIDRAW_CTX_SMALL )
        toridraw_compute_projected_face_order_small(context, hnd);
    else
        toridraw_compute_projected_face_order(context, hnd);
    return context->tmp_face_order_count;
}

int
toridraw_render_model3_raster(
    struct ToriDraw_Context* context,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth)
{
    toridraw_raster(context, context->active_hnd, view_port, camera, pixel_buffer, smooth);
    return TORIDRAW_CULL_VISIBLE;
}
