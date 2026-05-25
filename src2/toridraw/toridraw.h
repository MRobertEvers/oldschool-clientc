#ifndef TORIDRAW_H
#define TORIDRAW_H

#include "toridraw_hsl16.h"
#include "toridraw_math.h"
#include "toridraw_types.h"

#include <stdbool.h>

void
toridraw_init(void);

struct ToriDraw_Context*
toridraw_context_new(void);
void
toridraw_context_free(struct ToriDraw_Context* context);

void
toridraw_render_model(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    int* pixel_buffer);

int
toridraw_render_model1_project(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera);

int
toridraw_render_model2_sort_faces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context);

int
toridraw_render_model3_raster(
    struct ToriDraw_Context* context,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    int* pixel_buffer,
    bool smooth);

#endif