#ifndef GTASK_INIT_SCENE_H
#define GTASK_INIT_SCENE_H

#include "game.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"
#include "osrs/rscache/tables/model.h"

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
enum TaskInitSceneStep
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
    STEP_INIT_SCENE_10_LOAD_SEQUENCES,
    STEP_INIT_SCENE_11_LOAD_FRAMES,
    STEP_INIT_SCENE_12_LOAD_FRAMEMAPS,
    // Shade map is made from the cached terrain.
    STEP_INIT_SCENE_13_BUILD_WORLD3D,
    // Terrain requires scenery to be loaded for shademap.
    STEP_INIT_SCENE_14_BUILD_TERRAIN3D,
    STEP_INIT_SCENE_DONE,
    STEP_INIT_SCENE_STEP_COUNT,
};

struct TaskInitScene;
struct TaskInitScene*
task_init_scene_new(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z);
void
task_init_scene_free(struct TaskInitScene* task);

enum GameTaskStatus
task_init_scene_step(struct TaskInitScene* task);

#endif