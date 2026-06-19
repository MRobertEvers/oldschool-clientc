#include "toriauxlibc.h"

#include "3rd/minipt.h"
#include "dat1.h"
#include "dat1_world.h"
#include "buildcache/dat1_buildcache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriAuxLibC
{
    struct ToriAuxLibCore* core;
    enum ToriAuxLibCMode mode;
    struct Dat1BuildCache* dat1_buildcache;
};

struct ToriAuxLibC*
ToriAuxLibC_New(
    enum ToriAuxLibCMode mode,
    struct ToriAuxLibCore* core)
{
    struct ToriAuxLibC* c = calloc(1, sizeof(struct ToriAuxLibC));
    if( !c )
        return NULL;

    c->mode = mode;
    c->core = core;

    switch( mode )
    {
    case TORIAUXLIBC_MODE_DAT1:
        c->dat1_buildcache = dat1_buildcache_new();
        if( !c->dat1_buildcache )
        {
            free(c);
            return NULL;
        }
        break;
    default:
        free(c);
        return NULL;
    }

    return c;
}

void
ToriAuxLibC_Free(struct ToriAuxLibC* c)
{
    if( !c )
        return;
    if( c->dat1_buildcache )
        dat1_buildRSCacheDat2Disk_Free(c->dat1_buildcache);
    free(c);
}

struct Dat1BuildCache*
ToriAuxLibC_Dat1BuildCache(struct ToriAuxLibC* c)
{
    assert(c->mode == TORIAUXLIBC_MODE_DAT1);
    return c->dat1_buildcache;
}

struct ToriAuxLibCore*
ToriAuxLibC_Core(struct ToriAuxLibC* c)
{
    return c ? c->core : NULL;
}

enum ToriAuxLibCMode
ToriAuxLibC_Mode(struct ToriAuxLibC* c)
{
    return c->mode;
}

struct Task_ToriAuxLibC_ModelLoad
{
    struct ToriAuxLibC* c;
    int model_id;
    struct Task_Dat1ModelLoad* task_dat1;
};

struct Task_ToriAuxLibC_ModelLoad*
Task_ToriAuxLibC_ModelLoad_New(
    struct ToriAuxLibC* c,
    int model_id)
{
    struct Task_ToriAuxLibC_ModelLoad* task = calloc(1, sizeof(struct Task_ToriAuxLibC_ModelLoad));
    if( !task )
        return NULL;
    task->c = c;
    task->model_id = model_id;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1ModelLoad_New(c, model_id);
  else
    {
        free(task);
        return NULL;
    }
    return task;
}

int
Task_ToriAuxLibC_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_ModelLoad* task = (struct Task_ToriAuxLibC_ModelLoad*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1ModelLoad_Run(task->task_dat1, ctx);
    return PT_ENDED;
}

struct Task_ToriAuxLibC_WorldRebuildNormal
{
    struct ToriAuxLibC* c;
    struct Task_Dat1WorldRebuildNormal* task_dat1;
};

struct Task_ToriAuxLibC_WorldRebuildNormal*
Task_ToriAuxLibC_WorldRebuildNormal_New(
    struct ToriAuxLibC* c,
    int zonex,
    int zonez)
{
    struct Task_ToriAuxLibC_WorldRebuildNormal* task =
        calloc(1, sizeof(struct Task_ToriAuxLibC_WorldRebuildNormal));
    if( !task )
        return NULL;
    task->c = c;
    if( c->mode == TORIAUXLIBC_MODE_DAT1 )
        task->task_dat1 = Task_Dat1WorldRebuildNormal_New(c, zonex, zonez);
    else
    {
        free(task);
        return NULL;
    }
    return task;
}

void
Task_ToriAuxLibC_WorldRebuildNormal_Free(struct Task_ToriAuxLibC_WorldRebuildNormal* task)
{
    if( !task )
        return;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 && task->task_dat1 )
        Task_Dat1WorldRebuildNormal_Free(task->task_dat1);
    free(task);
}

int
Task_ToriAuxLibC_WorldRebuildNormal_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_ToriAuxLibC_WorldRebuildNormal* task =
        (struct Task_ToriAuxLibC_WorldRebuildNormal*)task_state;
    if( task->c->mode == TORIAUXLIBC_MODE_DAT1 )
        return Task_Dat1WorldRebuildNormal_Run(task->task_dat1, ctx);
    return PT_ENDED;
}
