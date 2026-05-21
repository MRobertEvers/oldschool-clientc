#include "toridraw.h"

#include "toridraw_types.h"

#include <stdlib.h>
#include <string.h>

// clang-format off
#include "triangles/toridraw_triangle_clip.u.c"
#include "triangles/toridraw_triangle_face_alpha.u.c"
#include "triangles/toridraw_triangle_flat.u.c"
#include "triangles/toridraw_triangle_gouraud.u.c"
#include "triangles/toridraw_triangle_texture_opaque.u.c"
#include "triangles/toridraw_triangle_texture_transparent.u.c"
#include "triangles/toridraw_triangle_texture_affine.u.c"
#include "toridraw_render.u.c"
#include "toridraw_raster.u.c"
// clang-format on

void
toridraw_init(void)
{
    toridraw_init_math();
    toridraw_init_hsl16();
}

struct ToriDraw_Context*
toridraw_context_new(void)
{
    struct ToriDraw_Context* context = malloc(sizeof(struct ToriDraw_Context));
    if( !context )
        return NULL;
    memset(context, 0, sizeof(struct ToriDraw_Context));
    return context;
}

void
toridraw_context_free(struct ToriDraw_Context* context)
{
    if( !context )
        return;
    free(context);
}

void
toridraw_render_model(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    int* pixel_buffer)
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
    toridraw_compute_projected_face_order(context, hnd);
    return context->tmp_face_order_count;
}

int
toridraw_render_model3_raster(
    struct ToriDraw_Context* context,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    int* pixel_buffer,
    bool smooth)
{
    toridraw_raster(context, context->active_hnd, view_port, camera, pixel_buffer, smooth);
    return TORIDRAW_CULL_VISIBLE;
}
