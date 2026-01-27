#ifndef TASK_QUERY_H
#define TASK_QUERY_H

#include "game.h"
#include "gametask_status.h"
#include "gio.h"
#include "query_engine.h"

struct TaskQuery;

struct TaskQuery*
task_query_new(
    struct GGame* game,
    struct QEQuery* q);

void
task_query_free(struct TaskQuery* task);

enum GameTaskStatus
task_query_step(struct TaskQuery* task);

#endif