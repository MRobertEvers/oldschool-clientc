#include "../../ioqueue/libtorirs_ioqueue.h"
#include "../../platforms/platform_x/cachelib.h"
#include "../../platforms/platform_x/cachelib_client.h"
#include "../../platforms/platform_x/cachelib_platform.h"
#include "../../platforms/platform_x_io_reactor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_FIXTURE "io_reactor_test_config.txt"
#define SCRIPT_FIXTURE "io_reactor_test_script.lua"

static const char k_config_body[] = "revconfig=test\n";
static const char k_script_body[] = "return 42\n";

PT_STATE_BEGIN(io_reactor_test_task)
int test_cache;
int cache_table_id;
int cache_archive_id;
int cache_flags;
PT_STATE_END(io_reactor_test_task)

PT_THREAD(io_reactor_test_task)
{
    PT_BEGIN();

    if( state->test_cache )
    {
        struct CacheLib_IORequest request;
        cachelib_dat1_model_fetch(0, &request);

        LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
        PT_YIELD();
    }

    LibToriRS_IOQueuePushConfigFile(ctx->io, CONFIG_FIXTURE);
    PT_YIELD();

    LibToriRS_IOQueuePushScript(ctx->io, SCRIPT_FIXTURE);
    PT_YIELD();

    PT_END();
}

static int
write_fixture(
    const char* path,
    const char* body)
{
    FILE* fp = fopen(path, "wb");
    if( !fp )
        return -1;
    if( fputs(body, fp) == EOF )
    {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int
assert_bytes(
    const char* label,
    void* data,
    int data_size,
    const char* expected)
{
    int expected_size = (int)strlen(expected);
    if( !data || data_size != expected_size )
    {
        fprintf(
            stderr, "%s: size mismatch (got %d, expected %d)\n", label, data_size, expected_size);
        return -1;
    }
    if( memcmp(data, expected, (size_t)expected_size) != 0 )
    {
        fprintf(stderr, "%s: content mismatch\n", label);
        return -1;
    }
    return 0;
}

static void
usage(const char* prog)
{
    fprintf(
        stderr,
        "Usage: %s [cache_dir [mode table_id archive_id]]\n"
        "  cache_dir omitted: config/script only\n"
        "  env TORI_CACHE_DIR also accepted\n",
        prog);
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = NULL;
    int cache_mode = CACHE_MODE_DAT1;
    int cache_table_id = 1;
    int cache_archive_id = 1;
    int cache_flags = 0;
    int test_cache = 0;

    if( argc >= 2 )
        cache_dir = argv[1];
    else
        cache_dir = getenv("TORI_CACHE_DIR");

    if( cache_dir && cache_dir[0] != '\0' )
    {
        test_cache = 1;
        if( argc >= 3 )
            cache_mode = atoi(argv[2]);
        if( argc >= 4 )
            cache_table_id = atoi(argv[3]);
        if( argc >= 5 )
            cache_archive_id = atoi(argv[4]);
    }
    else if( argc > 1 )
    {
        usage(argv[0]);
        return 1;
    }

    if( write_fixture(CONFIG_FIXTURE, k_config_body) != 0 )
    {
        fprintf(stderr, "failed to write %s\n", CONFIG_FIXTURE);
        return 1;
    }
    if( write_fixture(SCRIPT_FIXTURE, k_script_body) != 0 )
    {
        fprintf(stderr, "failed to write %s\n", SCRIPT_FIXTURE);
        return 1;
    }

    struct CacheLib* cache = NULL;
    if( test_cache )
    {
        cache = cachelib_new(cache_mode);
        if( !cache )
        {
            fprintf(stderr, "cachelib_new failed\n");
            return 1;
        }
        if( cachelib_platform_init(cache, cache_dir) != 1 )
        {
            fprintf(stderr, "cachelib_platform_init failed for: %s\n", cache_dir);
            cachelib_free(cache);
            return 1;
        }
    }

    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(cache);
    if( !reactor )
    {
        fprintf(stderr, "LibToriPlatformX_IOReactorNew failed\n");
        if( cache )
            cachelib_free(cache);
        return 1;
    }

    struct LibToriRS_IOQueue* io_queue = LibToriRS_IOQueueNew();
    if( !io_queue )
    {
        fprintf(stderr, "LibToriRS_IOQueueNew failed\n");
        LibToriPlatformX_IOReactorFree(reactor);
        if( cache )
            cachelib_free(cache);
        return 1;
    }

    struct LibToriRS_IOContext ctx = {
        .io = io_queue,
    };

    PT_SPAWN(io_reactor_test_task, task_state);
    PT_STATE_INIT(&task_state);
    task_state.test_cache = test_cache;
    task_state.cache_table_id = cache_table_id;
    task_state.cache_archive_id = cache_archive_id;
    task_state.cache_flags = cache_flags;

    PT_Status status = PT_YIELDED;
    while( status == PT_YIELDED )
    {
        status = io_reactor_test_task(&task_state, &ctx);
        LibToriPlatformX_IOReactorProcess(reactor, io_queue);
    }

    if( status != PT_FINISHED )
    {
        fprintf(stderr, "io_reactor_test_task did not finish\n");
        LibToriRS_IOQueueFree(io_queue);
        LibToriPlatformX_IOReactorFree(reactor);
        if( cache )
            cachelib_free(cache);
        return 1;
    }

    int failed = 0;
    int expected_items = test_cache ? 3 : 2;

    if( io_queue->count != expected_items )
    {
        fprintf(
            stderr,
            "queue count mismatch (got %d, expected %d)\n",
            io_queue->count,
            expected_items);
        failed = 1;
    }

    for( int i = 0; i < io_queue->count; i++ )
    {
        struct LibToriRS_IOQueueItem item;
        if( !LibToriRS_IOQueuePopRead(io_queue, &item) )
        {
            fprintf(stderr, "PopRead failed at item %d\n", i);
            failed = 1;
            break;
        }

        if( item.status != TORIRSIO_STAT_DONE )
        {
            fprintf(
                stderr,
                "item %d not done (kind=%d error_code=%d)\n",
                i,
                item.kind,
                item.error_code);
            failed = 1;
            continue;
        }

        switch( item.kind )
        {
        case TORIRSIO_KIND_CACHE:
            if( !item.data )
            {
                fprintf(stderr, "cache item load failed\n");
                failed = 1;
            }
            break;
        case TORIRSIO_KIND_CONFIG_FILE:
            if( assert_bytes("config", item.data, item.data_size, k_config_body) != 0 )
                failed = 1;
            break;
        case TORIRSIO_KIND_SCRIPT:
            if( assert_bytes("script", item.data, item.data_size, k_script_body) != 0 )
                failed = 1;
            break;
        default:
            fprintf(stderr, "unexpected item kind %d\n", item.kind);
            failed = 1;
            break;
        }
    }

    LibToriRS_IOQueueFree(io_queue);
    LibToriPlatformX_IOReactorFree(reactor);
    if( cache )
        cachelib_free(cache);

    if( failed )
    {
        fprintf(stderr, "FAIL\n");
        return 1;
    }

    printf("PASS (%d queue items%s)\n", expected_items, test_cache ? ", cache included" : "");
    return 0;
}
