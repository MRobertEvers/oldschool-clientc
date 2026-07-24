/*
 * DB_* opcode integration test. Drives the real async load pipeline against the
 * osrs230 cache (DBROW config kind 38 + DBTABLEINDEX cache table 21), then
 * exercises the DB opcodes through RS_CS2Host_Exec and checks the results
 * against values decoded straight from the cache:
 *   - table 0 is the quest list; row 0 = "Animal Magnetism"; row 3997 -> table 80.
 */

#include "asyncio.h"
#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_cs2_host.h"
#include "inv/inv_manager.h"
#include "platform/platform_x_io.h"
#include "ui/uitree.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.osrs230"

static int g_fail = 0;

#define CHECK(cond, msg)                                                                            \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
            printf("  ok: %s\n", msg);                                                             \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

static void
run_task(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io,
    struct PlatformX_IO* px,
    struct ToriRS_Task* task)
{
    if( !task )
        return;
    ToriRS_TaskQueue_Add(queue, task);
    while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
        PlatformX_IO_Process(px, io);
}

/* pack a dbcolumn operand the same way exec_db unpacks it */
static int
pack_col(
    int table,
    int column,
    int tuple)
{
    return (table << 12) | (column << 4) | tuple;
}

static int
call_db(
    struct CS2VM2_Thread* t,
    int opcode)
{
    struct CS2VM_HostRequest req;
    memset(&req, 0, sizeof(req));
    req.kind = CS2VM_HOST_REQUEST_DB;
    req.u.db.opcode = opcode;
    return RS_CS2Host_Exec(t, &req);
}

int
main(void)
{
    struct stat st;
    if( stat(CACHE_DIR, &st) != 0 )
    {
        printf("SKIP: cache dir not found: %s\n", CACHE_DIR);
        return 0;
    }

    struct ToriRS_IO* io = ToriRS_IO_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(CACHE_DIR);
    if( !disk )
    {
        printf("SKIP: could not open dat2 disk cache\n");
        return 0;
    }
    {
        char xtea_path[1200];
        snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", CACHE_DIR);
        RSCache_XteaConfigLoadKeys(xtea_path);
    }
    struct PlatformX_IO* px = PlatformX_IO_New();
    PlatformX_IO_InitDat2Disk(px, disk);

    /* Pre-load the resources the opcodes will read (so exec_db finds them
     * resident and does not need to drive a yield/reload cycle). */
    run_task(queue, io, px, CreateTask_DbTableIndexLoad(provider, 0));
    run_task(queue, io, px, CreateTask_DbRowLoad(provider, 0));
    run_task(queue, io, px, CreateTask_DbRowLoad(provider, 3997));

    CHECK(CacheProvider_DbTableIndexHas(provider, 0), "table 0 index loaded");
    CHECK(CacheProvider_DbRowHas(provider, 0), "row 0 loaded");
    CHECK(CacheProvider_DbRowHas(provider, 3997), "row 3997 loaded");

    struct UITree* tree = UITree_New(1024);
    struct InvManager invs;
    InvManager_Init(&invs);
    struct RS_CS2Host host;
    RS_CS2Host_Init(&host, tree, provider, &invs, NULL, NULL);

    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, RS_CS2Host_Exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);

    int iv = 0;
    char* sv = NULL;

    /* DB_GETROWTABLE(row 3997) -> 80 */
    CS2VM2_PushInt(t, 3997);
    call_db(t, CS2_OP_DB_GETROWTABLE);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 80, "DB_GETROWTABLE(3997) == 80");

    /* DB_GETROWTABLE(row 0) -> 0 */
    CS2VM2_PushInt(t, 0);
    call_db(t, CS2_OP_DB_GETROWTABLE);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 0, "DB_GETROWTABLE(0) == 0");

    /* DB_GETFIELD(row 0, col 0 int, index 0) -> 123 (push row, col, index). */
    CS2VM2_PushInt(t, 0);
    CS2VM2_PushInt(t, pack_col(0, 0, 0));
    CS2VM2_PushInt(t, 0);
    call_db(t, CS2_OP_DB_GETFIELD);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 123, "DB_GETFIELD(row0,col0,0) == 123");

    /* DB_GETFIELD(row 0, col 1 string, index 0) -> "Animal Magnetism". */
    CS2VM2_PushInt(t, 0);
    CS2VM2_PushInt(t, pack_col(0, 1, 0));
    CS2VM2_PushInt(t, 0);
    call_db(t, CS2_OP_DB_GETFIELD);
    CS2VM2_PopStr(t, &sv);
    CHECK(sv && strcmp(sv, "Animal Magnetism") == 0, "DB_GETFIELD(row0,col1,0) == \"Animal Magnetism\"");
    free(sv);
    sv = NULL;

    /* DB_GETFIELDCOUNT(row 0, col 23) -> 4 tuples (push row, col). */
    CS2VM2_PushInt(t, 0);
    CS2VM2_PushInt(t, pack_col(0, 23, 0));
    call_db(t, CS2_OP_DB_GETFIELDCOUNT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 4, "DB_GETFIELDCOUNT(row0,col23) == 4");

    /* DB_GETFIELD must push even when nothing resolves — it pops three args and
     * the caller's next opcode reads its result, so skipping the push underflows
     * some later, unrelated opcode (this aborted script 4029 on the live server).
     * An absent column and an out-of-range index both answer the default. */
    {
        int before = t->ints_stack_top;

        CS2VM2_PushInt(t, 0);
        CS2VM2_PushInt(t, pack_col(0, 250, 0)); /* column id the row does not list */
        CS2VM2_PushInt(t, 0);
        call_db(t, CS2_OP_DB_GETFIELD);
        CHECK(t->ints_stack_top == before + 1, "DB_GETFIELD(absent column) still pushes");
        CS2VM2_PopInt(t, &iv);
        CHECK(iv == 0, "DB_GETFIELD(absent column) == 0");

        CS2VM2_PushInt(t, 0);
        CS2VM2_PushInt(t, pack_col(0, 0, 0));
        CS2VM2_PushInt(t, 9999); /* tuple index past the end */
        call_db(t, CS2_OP_DB_GETFIELD);
        CHECK(t->ints_stack_top == before + 1, "DB_GETFIELD(index out of range) still pushes");
        CS2VM2_PopInt(t, &iv);
        CHECK(iv == 0, "DB_GETFIELD(index out of range) == 0");
    }

    /* DB_FINDALL_WITH_COUNT(table 0) -> 198 rows. */
    CS2VM2_PushInt(t, 0);
    call_db(t, CS2_OP_DB_FINDALL_WITH_COUNT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 198, "DB_FINDALL_WITH_COUNT(table0) == 198");

    /* DB_FIND_WITH_COUNT(col0 == 1) -> 1 row, and DB_FINDNEXT yields row 17.
     * Convention: value pushed first, dbcolumn last (on top). */
    CS2VM2_PushInt(t, 1);                  /* value */
    CS2VM2_PushInt(t, pack_col(0, 0, 0));  /* dbcolumn */
    call_db(t, CS2_OP_DB_FIND_WITH_COUNT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 1, "DB_FIND_WITH_COUNT(col0==1) == 1");

    call_db(t, CS2_OP_DB_FINDNEXT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 17, "DB_FINDNEXT -> row 17");

    call_db(t, CS2_OP_DB_FINDNEXT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == -1, "DB_FINDNEXT (exhausted) -> -1");

    RS_CS2Host_Free(&host);
    UITree_Free(tree);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    dat2_buildcache_free(bc);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);

    if( g_fail )
    {
        printf("DB test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("DB test: all passed\n");
    return 0;
}
