#include "task_query.h"

#include "query_executor_dat.h"

#include <stdlib.h>
#include <string.h>

struct TaskQuery
{
    struct GGame* game;
    struct QueryEngine* query_engine;
    struct QEQuery* q;
};

struct TaskQuery*
task_query_new(
    struct GGame* game,
    struct QEQuery* q)
{
    struct TaskQuery* task = malloc(sizeof(struct TaskQuery));
    memset(task, 0, sizeof(struct TaskQuery));
    task->game = game;
    task->q = q;

    task->query_engine = query_engine_new();
    return task;
}

void
task_query_free(struct TaskQuery* task)
{
    query_engine_qfree(task->q);
    query_engine_free(task->query_engine);
    free(task);
}

enum GameTaskStatus
task_query_step(struct TaskQuery* task)
{
    if( !query_engine_qisdone(task->q) )
    {
        query_executor_dat_step(
            task->query_engine, task->q, task->game->io, task->game->buildcachedat);
        return GAMETASK_STATUS_PENDING;
    }
    else
    {
        return GAMETASK_STATUS_COMPLETED;
    }
}