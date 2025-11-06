#ifndef DASH3D_H
#define DASH3D_H

#include "scene.h"

#include <stdbool.h>

struct AABB
{
    int min_screen_x;
    int min_screen_y;
    int max_screen_x;
    int max_screen_y;
};

struct Dash3D;

struct Dash3D* dash3d_new(int screen_width, int screen_height, int near_plane_z);
void dash3d_free(struct Dash3D* dash3d);

void dash3d_set_screen_size(struct Dash3D* dash3d, int screen_width, int screen_height);
void dash3d_set_camera(
    struct Dash3D* dash3d,
    int camera_x,
    int camera_y,
    int camera_z,
    int camera_pitch,
    int camera_yaw,
    int fov);

void dash3d_model_raster(struct Dash3D* dash3d, struct SceneModel* scene_model, int* pixel_buffer);
void dash3d_model_raster_face(
    struct Dash3D* dash3d, struct SceneModel* scene_model, int face, int* pixel_buffer);

struct Dash3DModelPainterIter;
void dash3d_model_painters_iter_new(struct Dash3D* dash3d, struct SceneModel* scene_model);
void dash3d_model_painters_iter_free(struct Dash3DModelPainterIter* iter);

/**
 * Call dash3d_set_camera prior to calling this function.
 */
void dash3d_model_painters_iter_begin(struct Dash3DModelPainterIter* iter);
bool dash3d_model_painters_iter_next(struct Dash3DModelPainterIter* iter);
int dash3d_model_painters_iter_value_face(struct Dash3DModelPainterIter* iter);
struct AABB* dash3d_model_painters_iter_aabb(struct Dash3DModelPainterIter* iter);

void dash3d_tile_raster(struct Dash3D* dash3d, struct SceneTile* scene_tile, int* pixel_buffer);
void dash3d_tile_raster_face(
    struct Dash3D* dash3d, struct SceneTile* scene_tile, int face, int* pixel_buffer);

struct Dash3DScenePainterIter;
void dash3d_scene_painters_iter_new(struct Dash3D* dash3d, struct Scene* scene);
void dash3d_scene_painters_iter_free(struct Dash3DScenePainterIter* iter);

/**
 * Call dash3d_set_camera prior to calling this function.
 */
void dash3d_scene_painters_iter_begin(struct Dash3DScenePainterIter* iter);
bool dash3d_scene_painters_iter_next(struct Dash3DScenePainterIter* iter);

#endif