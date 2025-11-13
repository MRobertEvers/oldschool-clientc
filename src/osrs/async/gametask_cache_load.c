#include "gametask_cache_load.h"

#include "osrs/cache.h"
#include "osrs/gameio.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum CacheLoadStep
{
    E_CACHE_LOAD_STEP_INITIAL = 0,
    E_CACHE_LOAD_STEP_TABLES,
    E_CACHE_LOAD_STEP_DONE,
};

struct GameTaskCacheLoad
{
    enum CacheLoadStep step;
    struct GameIO* io;
    struct Cache* cache;

    int table_id;
    struct GameIORequest* request;
};

struct GameTaskCacheLoad*
gametask_cache_load_new(struct GameIO* io, struct Cache* cache)
{
    struct GameTaskCacheLoad* task = malloc(sizeof(struct GameTaskCacheLoad));
    memset(task, 0, sizeof(struct GameTaskCacheLoad));
    task->step = E_CACHE_LOAD_STEP_INITIAL;
    task->io = io;
    task->cache = cache;
    return task;
}

void
gametask_cache_load_free(struct GameTaskCacheLoad* task)
{
    free(task);
}

enum GameIOStatus
gametask_cache_load_send(struct GameTaskCacheLoad* task)
{
    enum GameIOStatus status = E_GAMEIO_STATUS_ERROR;
    struct ReferenceTable* table = NULL;
    struct CacheArchive* table_archive = NULL;
    struct GameIO* io = task->io;
    struct Cache* cache = task->cache;

    switch( task->step )
    {
    case E_CACHE_LOAD_STEP_INITIAL:
        goto initial;
    case E_CACHE_LOAD_STEP_TABLES:
        goto tables;
    default:
        goto error;
    }

initial:;
    task->step = E_CACHE_LOAD_STEP_TABLES;

tables:;
    for( ; task->table_id < CACHE_TABLE_COUNT; ++task->table_id )
    {
        if( !cache_is_valid_table_id(task->table_id) )
            continue;

        status = gameio_request_new_archive_load(io, 255, task->table_id, &task->request);
        if( !gameio_resolved(status) )
            return status;

        table_archive = gameio_request_free_archive_receive(&task->request);

        table = reference_table_new_decode(table_archive->data, table_archive->data_size);
        cache->tables[task->table_id] = table;
        table = NULL;

        cache_archive_free(table_archive);
        table_archive = NULL;
    }

    task->step = E_CACHE_LOAD_STEP_DONE;
    return E_GAMEIO_STATUS_OK;

error:;
    printf("Task false start\n");
    return E_GAMEIO_STATUS_ERROR;
}

struct Cache*
gametask_cache_value(struct GameTaskCacheLoad* task)
{
    assert(task->step == E_CACHE_LOAD_STEP_DONE);
    assert(task->cache != NULL);
    return task->cache;
}