#ifndef GAME_MODEL_H
#define GAME_MODEL_H

#include "osrs/tables/model.h"

enum TransformFlag
{
    F_TRANSFORM_MIRROR = 1 << 0,
    F_TRANSFORM_HILSKWEW = 1 << 1,
    F_TRANSFORM_RECOLOR = 1 << 2,
    F_TRANSFORM_RETEXTURE = 1 << 3,
    F_TRANSFORM_SCALE = 1 << 4,
    F_TRANSFORM_ROTATE = 1 << 5,
};

void model_transform_mirror(struct CacheModel* model);

void model_transform_hillskew(
    struct CacheModel* model, int sw_height, int se_height, int ne_height, int nw_height);

void model_transform_recolor(struct CacheModel* model, int color_src, int color_dst);

void model_transform_retexture(struct CacheModel* model, int texture_src, int texture_dst);

void model_transform_scale(struct CacheModel* model, int x, int y, int z);

// void model_transform_rotate(struct CacheModel* model, int yaw, int pitch, int roll);

void model_transform_translate(struct CacheModel* model, int x, int y, int z);

#endif