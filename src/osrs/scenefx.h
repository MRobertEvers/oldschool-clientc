#ifndef OSRS_SCENEFX_H
#define OSRS_SCENEFX_H

#include "cache.h"

struct SceneFX;

struct SceneFX* scenefx_new_pix3d();
void scenefx_free(struct SceneFX* scene_fx);

void
scenefx_scene_load_map(struct SceneFX* scene_fx, struct Cache* cache, int chunk_x, int chunk_y);

// void scenefx_model_load(struct SceneFX* scene_fx, struct Cache* cache, int model_id);
// void scenefx_model_unload(struct SceneFX* scene_fx, int model_id);

void scenefx_render(struct SceneFX* scene_fx);

#endif // OSRS_SCENEFX_H