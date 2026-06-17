#include "gamecache_l.h"

#include "3rd/minipt.h"
#include "dat1.h"
#include "dat1_world.h"
#include "gamecache.h"
#include "src2/buildcache/dat1_buildcache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct GameCacheL
{
    struct GameCache* gamecache;

    enum GameCacheLMode mode;

    union
    {
        struct Dat1BuildCache* buildcachedat_dat1;
    } u;
};

struct GameCacheL*
GameCacheL_New(enum GameCacheLMode mode)
{
    struct GameCacheL* gamecache_l = malloc(sizeof(struct GameCacheL));
    assert(gamecache_l);

    memset(gamecache_l, 0, sizeof(struct GameCacheL));
    gamecache_l->mode = mode;

    gamecache_l->gamecache = gamecache_new();
    assert(gamecache_l->gamecache);

    switch( mode )
    {
    case GAMECACHE_L_MODE_DAT1:
        gamecache_l->u.buildcachedat_dat1 = dat1_buildcache_new();
        assert(gamecache_l->u.buildcachedat_dat1);
        break;
    default:
        assert(0);
        return NULL;
    }

    return gamecache_l;
}

void
GameCacheL_Free(struct GameCacheL* gamecache_l)
{
    if( !gamecache_l )
        return;
    free(gamecache_l);
}

struct Dat1BuildCache*
GameCacheL_GetDat1BuildCache(struct GameCacheL* gamecache_l)
{
    assert(gamecache_l->mode == GAMECACHE_L_MODE_DAT1);
    return gamecache_l->u.buildcachedat_dat1;
}

struct GameCache*
GameCacheL_GetGameCache(struct GameCacheL* gamecache_l)
{
    return gamecache_l->gamecache;
}

enum GameCacheLMode
GameCacheL_GetMode(struct GameCacheL* gamecache_l)
{
    return gamecache_l->mode;
}

struct Task_GameCacheL_ModelLoad
{
    struct GameCacheL* gamecache_l;
    int model_id;

    union
    {
        struct Task_Dat1ModelLoad* task_dat1;
    } u;
};

struct Task_GameCacheL_ModelLoad*
Task_GameCacheL_ModelLoad_New(
    struct GameCacheL* gamecache_l,
    int model_id)
{
    struct Task_GameCacheL_ModelLoad* task = malloc(sizeof(struct Task_GameCacheL_ModelLoad));
    assert(task);
    memset(task, 0, sizeof(struct Task_GameCacheL_ModelLoad));
    task->gamecache_l = gamecache_l;
    task->model_id = model_id;
    switch( gamecache_l->mode )
    {
    case GAMECACHE_L_MODE_DAT1:
        task->u.task_dat1 = Task_Dat1ModelLoad_New(gamecache_l, model_id);
        break;
    default:
        assert(0);
        return NULL;
    }
    return task;
}

int
Task_GameCacheL_ModelLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_GameCacheL_ModelLoad* task = (struct Task_GameCacheL_ModelLoad*)task_state;

    switch( task->gamecache_l->mode )
    {
    case GAMECACHE_L_MODE_DAT1:
        return Task_Dat1ModelLoad_Run(task->u.task_dat1, ctx);
    default:
        assert(0);
        return PT_ENDED;
    }
}

struct Task_GameCacheL_WorldRebuildNormal
{
    struct GameCacheL* gamecache_l;
    union
    {
        struct Task_Dat1WorldRebuildNormal* task_dat1;
    } u;
};

struct Task_GameCacheL_WorldRebuildNormal*
Task_GameCacheL_WorldRebuildNormal_New(
    struct GameCacheL* gamecache_l,
    int zonex,
    int zonez)
{
    struct Task_GameCacheL_WorldRebuildNormal* task =
        calloc(1, sizeof(struct Task_GameCacheL_WorldRebuildNormal));
    assert(task);
    task->gamecache_l = gamecache_l;
    switch( gamecache_l->mode )
    {
    case GAMECACHE_L_MODE_DAT1:
        task->u.task_dat1 = Task_Dat1WorldRebuildNormal_New(gamecache_l, zonex, zonez);
        break;
    default:
        assert(0);
        free(task);
        return NULL;
    }
    return task;
}

void
Task_GameCacheL_WorldRebuildNormal_Free(struct Task_GameCacheL_WorldRebuildNormal* task)
{
    if( !task )
        return;
    if( task->gamecache_l->mode == GAMECACHE_L_MODE_DAT1 && task->u.task_dat1 )
        Task_Dat1WorldRebuildNormal_Free(task->u.task_dat1);
    free(task);
}

int
Task_GameCacheL_WorldRebuildNormal_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_GameCacheL_WorldRebuildNormal* task =
        (struct Task_GameCacheL_WorldRebuildNormal*)task_state;

    switch( task->gamecache_l->mode )
    {
    case GAMECACHE_L_MODE_DAT1:
        return Task_Dat1WorldRebuildNormal_Run(task->u.task_dat1, ctx);
    default:
        assert(0);
        return PT_ENDED;
    }
}