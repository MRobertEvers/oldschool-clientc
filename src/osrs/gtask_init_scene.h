#ifndef GTASK_INIT_SCENE_H
#define GTASK_INIT_SCENE_H

#include "libg.h"
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
    STEP_INIT_SCENE_1_LOAD_SCENERY,
    STEP_INIT_SCENE_2_LOAD_SCENERY_CONFIG,
    STEP_INIT_SCENE_3_LOAD_SCENERY_MODELS,
    STEP_INIT_SCENE_4_LOAD_TERRAIN,
    STEP_INIT_SCENE_5_LOAD_UNDERLAY,
    STEP_INIT_SCENE_6_LOAD_OVERLAY,
    STEP_INIT_SCENE_7_LOAD_TEXTURES,
    STEP_INIT_SCENE_8_LOAD_SPRITEPACKS,
    STEP_INIT_SCENE_9_BUILD_TEXTURES,
    // Shade map is made from the cached terrain.
    STEP_INIT_SCENE_10_BUILD_WORLD3D,
    // Terrain requires scenery to be loaded for shademap.
    STEP_INIT_SCENE_11_BUILD_TERRAIN3D,
    STEP_INIT_SCENE_DONE,
};

struct GTaskInitScene;
struct GTaskInitScene*
gtask_init_scene_new(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z);
void
gtask_init_scene_free(struct GTaskInitScene* task);

enum GTaskStatus
gtask_init_scene_step(struct GTaskInitScene* task);

struct CacheModel*
gtask_init_scene_value(struct GTaskInitScene* task);

#endif