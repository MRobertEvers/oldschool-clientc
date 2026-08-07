#ifndef TORIDRAW_H
#define TORIDRAW_H

#include "toridraw_animation.h"
#include "toridraw_hsl16.h"
#include "toridraw_light_model.h"
#include "toridraw_lighting.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_model_sprite.h"
#include "toridraw_model_transform.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"
#include "toridraw_vec.h"
#include "graphics/projection.h"
#include "graphics/shared_tables.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIDRAW_SCENE_FULL          0u
#define TORIDRAW_SCENE_SMALL         (1u << 0)
#define TORIDRAW_SCENE_LAZY_TEXTURES (1u << 1)

void
ToriDraw_Init(void);

/**
 * Select the `scanline` raster family instead of the default `branching` /
 * `sort` kernels. Off by default; ToriDraw_Init() also honours
 * TORIDRAW_RASTER_SCANLINE=1 in the environment.
 */
void
ToriDraw_RasterSetScanline(bool enabled);

bool
ToriDraw_RasterGetScanline(void);

void
ToriDraw_RenderModel(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer);

int
ToriDraw_RenderModel1Project(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene,
    struct ToriDraw_Position* position,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera);

int
ToriDraw_RenderModel2SortFaces(
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_Scene* scene);

int
ToriDraw_RenderModel3Raster(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ViewPort* view_port,
    struct ToriDraw_Camera* camera,
    toripixel_t* pixel_buffer,
    bool smooth);

/** Bounding-box hit test against the last projected model (reference
 *  Model.useAABBMouseCheck). Cheaper and far more forgiving than the per-face
 *  test — the reference uses it for npcs, players and ground objs. */
bool
ToriDraw_ProjectedModelContainsAabb(
    struct ToriDraw_Scene* scene,
    int screen_x,
    int screen_y);

bool
ToriDraw_ProjectedModelContainsPoint(
    struct ToriDraw_Scene* scene,
    struct ToriDraw_ModelHandle hnd,
    struct ToriDraw_ViewPort* view_port,
    int screen_x,
    int screen_y);

#endif
