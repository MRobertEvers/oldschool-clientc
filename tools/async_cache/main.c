#include "../../src2/ioqueue/libtorirs_io.h"
#include "../../src2/platforms/platform_x/cachelib_platform.h"
#include "../../src2/platforms/platform_x_io_reactor.h"
#include "async_cache_tasks.h"
#include "cachedat2.h"
#include "osrs/rscache/rscache_unity.h"

#include <assert.h>
#include <stdio.h>

int
main(void)
{
    struct LibToriRS_IOQueue queue = { 0 };

    const char* cache_dir = "/Users/matthewevers/Documents/git_repos/3draster/cache";
    const int cache_mode = CACHE_MODE_DAT2;

    struct RSCacheDat2DiskLib* cachedisk = NULL;
    cachedisk = cachelib_new(cache_mode);
    if( !cachedisk )
    {
        fprintf(stderr, "cachelib_new failed\n");
        return 1;
    }
    if( cachelib_platform_init(cachedisk, cache_dir) != 1 )
    {
        fprintf(stderr, "cachelib_platform_init failed for: %s\n", cache_dir);
        cachelib_free(cachedisk);
        return 1;
    }

    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(cachedisk);
    assert(reactor && "Must have a reactor");

    struct LibToriRS_TaskRunner runner = { 0 };
    LibToriRS_TaskRunner_Init(&runner, &queue);

    struct CacheDat2 cache = { 0 };
    CacheDat2_Init(&cache);

    const int test_object_id = 1333;
    struct LibToriRS_Task* task = Task_AsyncCacheDat2_ObjectModel_Load_New(test_object_id, &cache);

    LibToriRS_TaskRunner_Add(&runner, task);

    while( LibToriRS_TaskRunner_Run(&runner) )
        LibToriPlatformX_IOReactorProcess(reactor, &queue);

    struct RSCacheDat2A_Model* object_model = CacheDat2_ObjectModel_Get(&cache, test_object_id);
    if( !object_model )
    {
        fprintf(stderr, "async_cache: object model load failed for object_id=%d\n", test_object_id);
        LibToriPlatformX_IOReactorFree(reactor);
        return 1;
    }

    fprintf(
        stderr,
        "async_cache: loaded object model object_id=%d vertices=%d faces=%d\n",
        test_object_id,
        object_model->vertex_count,
        object_model->face_count);

    LibToriPlatformX_IOReactorFree(reactor);

    return 0;
}
