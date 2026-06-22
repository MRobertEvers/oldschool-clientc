#ifndef TORIDRAW_H
#define TORIDRAW_H

#include "toridraw_animation.h"
#include "toridraw_hsl16.h"
#include "toridraw_light_model.h"
#include "toridraw_lighting.h"
#include "toridraw_map.h"
#include "toridraw_math.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "toridraw_types.h"
#include "toridraw_vec.h"
#include "graphics/shared_tables.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TORIDRAW_SCENE_FULL          0u
#define TORIDRAW_SCENE_SMALL         (1u << 0)
#define TORIDRAW_SCENE_LAZY_TEXTURES (1u << 1)

void
ToriDraw_Init(void);

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

#endif
