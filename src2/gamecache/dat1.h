#ifndef GAMECACHE_DAT1_H
#define GAMECACHE_DAT1_H

#include "../libtorirs.h"
#include "3rd/minipt.h"
#include "buildcache/dat1_buildcache.h"
#include "dat1io.h"
#include "gamecache_l.h"
#include "src2/gamecache/toridraw_cachemodel.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1ModelLoad
{
    struct pt thread;
    struct GameCacheL* gamecache_l;
    int model_id;
};

struct Task_Dat1ModelLoad*
Task_Dat1ModelLoad_New(
    struct GameCacheL* gamecache_l,
    int model_id)
{
    struct Task_Dat1ModelLoad* task = malloc(sizeof(struct Task_Dat1ModelLoad));
    assert(task);
    memset(task, 0, sizeof(struct Task_Dat1ModelLoad));
    PT_INIT(&task->thread);
    task->gamecache_l = gamecache_l;
    task->model_id = model_id;
    return task;
}

void
Task_Dat1ModelLoad_Free(struct Task_Dat1ModelLoad* task)
{
    if( !task )
        return;
    free(task);
}

int
Task_Dat1ModelLoad_Run(
    struct Task_Dat1ModelLoad* task,
    struct LibToriRS_IOContext* ctx)
{
    struct CacheModel* model;
    struct CacheModel* copy;
    PT_BEGIN(&task->thread);

    dat1io_model_fetch(ctx, task->model_id);

    PT_YIELD(&task->thread);

    model = dat1io_model_decode(ctx, task->model_id);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        PT_EXIT(&task->thread);
    }

    dat1_buildcache_model_add(dat1(task->gamecache_l), task->model_id, model);

    copy = model_new_copy(model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = toridraw_model_new_from_cache_model(copy),
    };

    gamecache_model_add(gamecache(task->gamecache_l), task->model_id, hnd);
    model_free(copy);

    PT_END(&task->thread);
}

#endif