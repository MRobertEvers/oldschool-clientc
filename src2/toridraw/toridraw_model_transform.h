#ifndef TORIDRAW_MODEL_TRANSFORM_H
#define TORIDRAW_MODEL_TRANSFORM_H

#include "toridraw_types.h"

struct ToriDraw_Model*
ToriDraw_ModelCopy(struct ToriDraw_Model* src);

struct ToriDraw_Model*
ToriDraw_ModelMerge(
    struct ToriDraw_Model** models,
    int model_count);

void
ToriDraw_ModelRecolor(
    struct ToriDraw_Model* model,
    int color_src,
    int color_dst);

void
ToriDraw_ModelRetexture(
    struct ToriDraw_Model* model,
    int texture_src,
    int texture_dst);

void
ToriDraw_ModelMirror(struct ToriDraw_Model* model);

void
ToriDraw_ModelOrient(
    struct ToriDraw_Model* model,
    int orientation);

void
ToriDraw_ModelScale(
    struct ToriDraw_Model* model,
    int x,
    int z,
    int height);

void
ToriDraw_ModelTranslate(
    struct ToriDraw_Model* model,
    int x,
    int y,
    int z);

void
ToriDraw_ModelSetBoundsCylinder(struct ToriDraw_Model* model);

#endif
