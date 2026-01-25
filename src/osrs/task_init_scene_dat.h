#ifndef GTASK_INIT_SCENE_DAT_H
#define GTASK_INIT_SCENE_DAT_H

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
enum TaskInitSceneDatStep
{
    STEP_INIT_SCENE_DAT_INITIAL = 0,
    STEP_INIT_SCENE_DAT_0_LOAD_CONFIGS,
    STEP_INIT_SCENE_DAT_1_LOAD_TERRAIN,
    STEP_INIT_SCENE_DAT_2_LOAD_FLOORTYPE,
    STEP_INIT_SCENE_DAT_3_LOAD_SCENERY,
    STEP_INIT_SCENE_DAT_4_LOAD_SCENERY_CONFIG,
    STEP_INIT_SCENE_DAT_5_LOAD_MODELS,
    STEP_INIT_SCENE_DAT_6_LOAD_TEXTURES,
    STEP_INIT_SCENE_DAT_7_LOAD_SEQUENCES,
    STEP_INIT_SCENE_DAT_8_LOAD_ANIMFRAMES_INDEX,
    STEP_INIT_SCENE_DAT_9_LOAD_ANIMBASEFRAMES,
    STEP_INIT_SCENE_DAT_10_LOAD_SOUNDS,
    STEP_INIT_SCENE_DAT_11_LOAD_MEDIA,
    STEP_INIT_SCENE_DAT_12_LOAD_TITLE,
    STEP_INIT_SCENE_DAT_13_LOAD_IDKITS,
    STEP_INIT_SCENE_DAT_14_LOAD_OBJECTS,
    STEP_INIT_SCENE_DAT_DONE,
    STEP_INIT_SCENE_DAT_STEP_COUNT,
};

struct TaskInitSceneDat;
struct TaskInitSceneDat*
task_init_scene_dat_new(
    struct GGame* game,
    int map_sw_x,
    int map_sw_z,
    int map_ne_x,
    int map_ne_z);

void
task_init_scene_dat_free(struct TaskInitSceneDat* task);

enum GameTaskStatus
task_init_scene_dat_step(struct TaskInitSceneDat* task);

#endif