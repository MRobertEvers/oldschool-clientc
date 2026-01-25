#ifndef TASK_LOAD_DAT_H
#define TASK_LOAD_DAT_H

#include "game.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"

struct TaskLoadDat;

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
    STEP_INIT_SCENE_DAT_DONE,
    STEP_INIT_SCENE_DAT_STEP_COUNT,
};

struct TaskLoadDat*
task_load_dat_new(
    struct GGame* game,
    int* sequence_ids,
    int sequence_count,
    int* model_ids,
    int model_count);

void
task_load_dat_free(struct TaskLoadDat* task);

enum GameTaskStatus
task_load_dat_step(struct TaskLoadDat* task);

#endif