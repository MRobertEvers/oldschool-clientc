#include "task_load_dat.h"

#include <stdlib.h>
#include <string.h>

struct TaskLoadDat
{
    enum TaskLoadDatStep step;
    struct GGame* game;
    struct GIOQueue* io;
    struct TaskStep task_steps[STEP_LOAD_DAT_STEP_COUNT];

    int* sequence_ids;
    int sequence_count;
    int* model_ids;
    int model_count;

    struct FileListDat* config_jagfile;
    struct FileListDat* versionlist_jagfile;
};

struct TaskLoadDat*
task_load_dat_new(
    struct GGame* game,
    int* sequence_ids,
    int sequence_count,
    int* model_ids,
    int model_count)
{
    struct TaskLoadDat* task = malloc(sizeof(struct TaskLoadDat));
    memset(task, 0, sizeof(struct TaskLoadDat));
    task->game = game;

    task->sequence_ids = sequence_ids;
    task->sequence_count = sequence_count;
    task->model_ids = model_ids;
    task->model_count = model_count;

    task->config_jagfile = game->config_jagfile;
    task->versionlist_jagfile = game->versionlist_jagfile;

    return task;
}

void
task_load_dat_free(struct TaskLoadDat* task)
{
    free(task);
}

enum GameTaskStatus
task_load_dat_step(struct TaskLoadDat* task)
{
    return GAMETASK_STATUS_COMPLETED;
}
