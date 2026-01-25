#ifndef TASK_LOAD_DAT_H
#define TASK_LOAD_DAT_H

#include "game.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"

struct TaskLoadDat;

enum TaskLoadDatStep
{
    STEP_LOAD_DAT_INITIAL = 0,
    STEP_LOAD_DAT_LOAD_MODELS,
    STEP_LOAD_DAT_DONE,
    STEP_LOAD_DAT_STEP_COUNT,
};

struct TaskLoadDat*
task_load_dat_new(
    struct GGame* game,
    int* model_ids,
    int model_count);

void
task_load_dat_free(struct TaskLoadDat* task);

enum GameTaskStatus
task_load_dat_step(struct TaskLoadDat* task);

#endif