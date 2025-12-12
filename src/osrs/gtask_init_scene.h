#ifndef GTASK_INIT_SCENE_H
#define GTASK_INIT_SCENE_H

#include "osrs/gio.h"
#include "osrs/tables/model.h"

/**
 * Data Dependencies
 *
 * Map Locs
 *   -> Log Configs
 *      -> Models
 *         -> Textures
 *           -> Sprite Packs
 *      -> Sequences
 *         -> Animations
 *             -> Framemaps
 *
 * Map Terrain
 *   -> Underlay
 *   -> Overlay
 *      -> Textures
 *        -> Sprite Packs
 *
 * Flattened:
 * Map Locs
 * Loc Configs
 * Models
 * Sequences
 * Animations
 * Framemaps
 * Terrain
 * Underlays
 * Overlay
 * Textures
 * Sprite Packs
 *
 *
 * Don't need:
 * IDK, Object
 */
enum GTaskInitSceneStep
{
    STEP_INIT_SCENE_INITIAL = 0,
    STEP_INIT_SCENE_ONE_LOAD_SCENERY,
    STEP_INIT_SCENE_TWO_LOAD_SCENERY_CONFIG,
    STEP_INIT_SCENE_THREE_LOAD_SCENERY_MODELS,
    STEP_INIT_SCENE_DONE,
};

struct GTaskInitScene;
struct GTaskInitScene* gtask_init_scene_new(struct GIOQueue* io, int chunk_x, int chunk_y);
void gtask_init_scene_free(struct GTaskInitScene* task);

enum GTaskStatus gtask_init_scene_step(struct GTaskInitScene* task);

struct CacheModel* gtask_init_scene_value(struct GTaskInitScene* task);

#endif