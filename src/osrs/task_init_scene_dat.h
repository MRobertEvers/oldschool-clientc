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
    STEP_INIT_SCENE_DAT_1_LOAD_TERRAIN,
    STEP_INIT_SCENE_DAT_2_LOAD_FLOORTYPE,
    STEP_INIT_SCENE_DAT_3_LOAD_TEXTURES,
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