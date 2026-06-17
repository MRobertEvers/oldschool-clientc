#ifndef TORIDRAW_H
#define TORIDRAW_H

#include "toridraw_hsl16.h"
#include "toridraw_math.h"
#include "toridraw_types.h"
#include "graphics/shared_tables.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIDRAW_CTX_FULL          0u
#define TORIDRAW_CTX_SMALL         (1u << 0)
#define TORIDRAW_CTX_LAZY_TEXTURES (1u << 1)

void
toridraw_init(void);

struct ToriDraw_Context*
toridraw_context_new(uint32_t flags);
void
toridraw_context_free(struct ToriDraw_Context* context);

size_t
toridraw_context_size(uint32_t flags);
void
toridraw_context_print_size(uint32_t flags);

struct ToriDraw_TextureState*
toridraw_context_tex_state(struct ToriDraw_Context* context);

void
toridraw_render_model(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Context* context,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer);

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
    toripixel_t* pixel_buffer,
    bool smooth);

#endif
