#ifndef TASK_LOAD_DAT_H
#define TASK_LOAD_DAT_H

#include "game.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"

struct TaskCommandDat;

struct TaskCommandDat*
task_command_dat_new(
    struct GGame* game,
    int command_kind,
    void* command_data,
    int command_size);

void
task_command_dat_free(struct TaskCommandDat* task);

enum GameTaskStatus
task_command_dat_step(struct TaskCommandDat* task);

#endif