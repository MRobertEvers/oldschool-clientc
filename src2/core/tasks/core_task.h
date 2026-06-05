#ifndef CORE_TASKS_CORE_TASK_H
#define CORE_TASKS_CORE_TASK_H

#include "../../games/game_handle.h"
#include "../../ioqueue/libtorirs_io.h"

#include <stdbool.h>

#define REVCONFIG_MAX_FILES 4

enum CoreTaskKind
{
    CORE_TASK_KIND_DAT1_MODEL_LOAD = 0,
    CORE_TASK_KIND_REVCONFIG_LOAD,
};

struct CoreTask
{
    enum CoreTaskKind kind;
    void* state;
};

struct CoreTask*
core_task_new_dat1_model_load(
    struct GameHandle game,
    int model_id);

struct CoreTask*
core_task_new_revconfig_load(
    struct GameHandle game,
    char const* filename_1,
    char const* filename_2,
    char const* filename_3,
    char const* filename_4);

PT_Status
core_task_step(
    struct CoreTask* task,
    struct LibToriRS_IOContext* ctx);

void
core_task_free(struct CoreTask* task);

#endif
