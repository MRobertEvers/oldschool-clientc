#ifndef TRSPK_TORIDRAW_H
#define TRSPK_TORIDRAW_H

#include "platformkit/core/trspk_vbo.h"
#include "toridraw/toridraw_types.h"

#include <stdbool.h>

bool
trspk_toridraw_texture_is_animated(
    struct ToriDraw_Context* ctx,
    int tex_id);

void
trspk_toridraw_vbo_set(
    struct TRSPK_VBO* vbo,
    struct ToriDraw_ModelHandle model_handle);

#endif
