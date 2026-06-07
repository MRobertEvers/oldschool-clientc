#include "gamecache_l.h"

#include "3rd/minipt.h"
#include "dat1.h"
#include "gamecache.h"
#include "src2/buildcache/dat1_buildcache.h"
#include "src2/gamecache/toridraw_cachemodel.h"

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
        task->u.task_dat1 = Task_Dat1ModelLoad_New(gamecache_l->u.buildcachedat_dat1, model_id);
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

    int res = PT_ENDED;

    switch( task->gamecache_l->mode )
    {
    case GAMECACHE_L_MODE_DAT1:
    {
        res = Task_Dat1ModelLoad_Run(task->u.task_dat1, ctx);
        if( res != PT_ENDED )
            return res;
        struct CacheModel* model =
            dat1_buildcache_model_get(dat1(task->gamecache_l), task->model_id);
        if( !model )
            return PT_ENDED;

        struct CacheModel* copy = model_new_copy(model);
        if( !copy )
            return PT_ENDED;

        struct ToriDraw_Model* td = toridraw_model_new_from_cache_model(copy);
        model_free(copy);
        if( !td )
            return PT_ENDED;

        struct ToriDraw_ModelHandle hnd = {
            .kind = TORIDRAWMK_MODEL,
            .u.model.model = td,
        };
        gamecache_model_add(gamecache(task->gamecache_l), task->model_id, hnd);

        return PT_ENDED;
    }
    default:
        assert(0);
        return PT_ENDED;
    }
    return PT_ENDED;
}