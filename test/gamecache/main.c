#include "3rd/minipt.h"
#include "core/dat1/dat1io.h"
#include "gamecache/gamecache.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "osrs/rscache/tables/model.h"
#include "platforms/platform_x/cachelib_client.h"
#include "platforms/platform_x/cachelib_platform.h"
#include "platforms/platform_x_io_reactor.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

struct Task_LoadInterface_State
{
    struct pt thread;
    int model_id;
};

struct Task_LoadInterface_State*
Task_LoadInterface_New(int model_id)
{
    struct Task_LoadInterface_State* state = malloc(sizeof(struct Task_LoadInterface_State));
    assert(state);
    memset(state, 0, sizeof(struct Task_LoadInterface_State));
    PT_INIT(&state->thread);
    state->model_id = model_id;
    return state;
}

int
Task_LoadInterface(
    struct Task_LoadInterface_State* state,
    struct LibToriRS_IOContext* ctx)
{
    struct CacheModel* model;
    PT_BEGIN(&state->thread);

    dat1io_model_fetch(ctx, state->model_id);

    PT_YIELD(&state->thread);

    model = dat1io_model_decode(ctx, state->model_id);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        PT_EXIT(&state->thread);
    }

    PT_END(&state->thread);
}

int
main(void)
{
    int exit_code = 0;
    struct GameCache* gamecache = gamecache_new();
    if( !gamecache )
    {
        fprintf(stderr, "gamecache_new failed\n");
        exit_code = 1;
        goto end;
    }

    struct CacheLib* cache = cachelib_new(CACHE_MODE_DAT1);
    cachelib_platform_init(cache, "/Users/matthewevers/Documents/git_repos/3draster/cache254");

    struct LibToriPlatformX_IOReactor* io_reactor = LibToriPlatformX_IOReactorNew(cache);
    if( !io_reactor )
    {
        fprintf(stderr, "LibToriPlatformX_IOReactorNew failed\n");
        exit_code = 1;
        goto end;
    }

    struct LibToriRS_IOQueue* io_queue = LibToriRS_IOQueueNew();
    if( !io_queue )
    {
        fprintf(stderr, "LibToriRS_IOQueueNew failed\n");
        exit_code = 1;
        goto end;
    }

    struct LibToriRS_IOContext ctx = {
        .io = io_queue,
    };

    struct Task_LoadInterface_State* state = Task_LoadInterface_New(1571);

    int res = Task_LoadInterface(state, &ctx);
    while( res == PT_YIELDED )
    {
        LibToriPlatformX_IOReactorProcess(io_reactor, io_queue);
        res = Task_LoadInterface(state, &ctx);
    }

    printf("task load interface completed\n");

end:
    LibToriRS_IOQueueFree(io_queue);
    LibToriPlatformX_IOReactorFree(io_reactor);
    gamecache_free(gamecache);

    return exit_code;
}
