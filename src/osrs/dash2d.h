#ifndef DASH2D_H
#define DASH2D_H

#include "scene.h"

void dash2d_free(struct Dash2D* dash2d);

void dash2d_model_raster(struct Dash2D* dash2d, struct SceneModel* scene_model);
void dash2d_model_raster_face(struct Dash2D* dash2d, struct SceneModel* scene_model, int face);

#endif