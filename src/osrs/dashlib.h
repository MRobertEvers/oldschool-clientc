#ifndef DASHLIB_H
#define DASHLIB_H

#include "graphics/dash.h"
#include "osrs/tables/model.h"

struct DashModel* dashmodel_new_from_cache_model(struct CacheModel* model);
struct DashModelBones* dashmodel_bones_new(int* bone_map, int bone_count);

static struct DashModelLighting* dashmodel_lighting_new_default(
    struct CacheModel* model,
    struct DashModelNormals* normals,
    int model_contrast,
    int model_attenuation);

#endif