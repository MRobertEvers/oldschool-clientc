#ifndef TORIDRAW_MODEL_TRANSFORM_H
#define TORIDRAW_MODEL_TRANSFORM_H

#include "toridraw_types.h"

struct ToriDraw_Model*
toridraw_model_copy(struct ToriDraw_Model* src);

struct ToriDraw_Model*
toridraw_model_merge(
    struct ToriDraw_Model** models,
    int model_count);

void
toridraw_model_recolor(
    struct ToriDraw_Model* model,
    int color_src,
    int color_dst);

void
toridraw_model_retexture(
    struct ToriDraw_Model* model,
    int texture_src,
    int texture_dst);

void
toridraw_model_mirror(struct ToriDraw_Model* model);

void
toridraw_model_orient(
    struct ToriDraw_Model* model,
    int orientation);

void
toridraw_model_scale(
    struct ToriDraw_Model* model,
    int x,
    int z,
    int height);

void
toridraw_model_translate(
    struct ToriDraw_Model* model,
    int x,
    int y,
    int z);

void
toridraw_model_set_bounds_cylinder(struct ToriDraw_Model* model);

#endif
