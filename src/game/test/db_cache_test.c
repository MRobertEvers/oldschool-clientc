/*
 * DB_* opcode integration test. Drives the real async load pipeline against the
 * osrs230 cache (DBROW config kind 38 + DBTABLEINDEX cache table 21), then
 * exercises the DB opcodes through RS_CS2Host_Exec and checks the results
 * against values decoded straight from the cache:
 *   - table 0 is the quest list; row 0 = "Animal Magnetism"; row 3997 -> table 80.
 */

#include "asyncio.h"
#include <assert.h>
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
    assert(task);
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

/* Two clientscript tasks may yield against one shared host while their IO is
 * in flight. A wait record on RS_CS2Host lets the second task overwrite the
 * first, so both retry as if they had never loaded and violate the VM's
 * one-opcode/one-yield contract. Keep the two missing enum ids distinct so the
 * test specifically catches that cross-thread overwrite. */
static void
test_await_isolation(
    struct RS_CS2Host* host)
{
    struct CS2VM2 vm_a;
    struct CS2VM2 vm_b;
    struct CS2VM_HostRequest req_a = { 0 };
    struct CS2VM_HostRequest req_b = { 0 };
    struct CS2VM2_Thread* a;
    struct CS2VM2_Thread* b;
    int value;

    CS2VM2_Init(&vm_a);
    CS2VM2_Init(&vm_b);
    CS2VM2_BindHost(&vm_a, host, RS_CS2Host_Exec);
    CS2VM2_BindHost(&vm_b, host, RS_CS2Host_Exec);
    a = CS2VM2_ThreadMain(&vm_a);
    b = CS2VM2_ThreadMain(&vm_b);

    req_a.kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP;
    req_a.u.enum_lookup.enum_id = 1000000001;
    req_a.u.enum_lookup.input_type = 'i';
    req_a.u.enum_lookup.output_type = 'i';
    req_b = req_a;
    req_b.u.enum_lookup.enum_id = 1000000002;

    CHECK(RS_CS2Host_Exec(a, &req_a) == CS2VM_EXECNO_YIELD, "thread A parks its enum load");
    CHECK(RS_CS2Host_Exec(b, &req_b) == CS2VM_EXECNO_YIELD, "thread B parks a distinct enum load");

    CHECK(RS_CS2Host_Exec(a, &req_a) == CS2VM_EXECNO_OK,
          "thread A retry survives thread B's interleaved wait");
    value = 0;
    CHECK(CS2VM2_PopInt(a, &value) == CS2VM_EXECNO_OK && value == -1,
          "thread A completes a failed load with the enum default");

    CHECK(RS_CS2Host_Exec(b, &req_b) == CS2VM_EXECNO_OK,
          "thread B retains its own wait after thread A completes");
    value = 0;
    CHECK(CS2VM2_PopInt(b, &value) == CS2VM_EXECNO_OK && value == -1,
          "thread B completes a failed load with the enum default");

    host->has_pending = false;
    CS2VM2_Free(&vm_b);
    CS2VM2_Free(&vm_a);
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

    struct RSCache profile;
    if( !RSCache_ProfileByName("osrs230", &profile) )
    {
        printf("SKIP: osrs230 profile unavailable\n");
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    /* The provider carries the same identity as the disk: every dat2 load task
     * branches on it, and CacheProvider_Profile asserts rather than guessing.
     * Without this the first load task (the table index) aborts before the
     * first CHECK, which is how this test had been failing. */
    CacheProvider_SetProfile(provider, &profile);

    if( RSCache_MapLocsEncrypted(&profile) )
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
    run_task(queue, io, px, CreateTask_DbTableLoad(provider, 0));
    run_task(queue, io, px, CreateTask_DbRowLoad(provider, 0));
    run_task(queue, io, px, CreateTask_DbRowLoad(provider, 3997));

    CHECK(CacheProvider_DbTableHas(provider, 0), "table 0 (DBTABLE) loaded");
    CHECK(CacheProvider_DbTableIndexHas(provider, 0), "table 0 index loaded");
    CHECK(CacheProvider_DbRowHas(provider, 0), "row 0 loaded");
    CHECK(CacheProvider_DbRowHas(provider, 3997), "row 3997 loaded");

    struct UITree* tree = UITree_New(1024);
    struct InvManager invs;
    InvManager_Init(&invs);
    struct RS_CS2Host host;
    RS_CS2Host_Init(&host, tree, provider, &invs, NULL, NULL, NULL);

    struct CS2VM2 vm;
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, RS_CS2Host_Exec);
    struct CS2VM2_Thread* t = CS2VM2_ThreadMain(&vm);

    test_await_isolation(&host);

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
    sv = NULL; /* pool-owned; released by CS2VM2_Free below */

    /* DB_GETFIELDCOUNT(row 0, col 23) -> 4 tuples (push row, col). */
    CS2VM2_PushInt(t, 0);
    CS2VM2_PushInt(t, pack_col(0, 23, 0));
    call_db(t, CS2_OP_DB_GETFIELDCOUNT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 4, "DB_GETFIELDCOUNT(row0,col23) == 4");

    /* DB_GETFIELD must push even when nothing resolves — it pops three args and
     * the caller's next opcode reads its result, so skipping the push underflows
     * some later, unrelated opcode (this aborted script 4029 on the live server).
     * An absent column and an out-of-range index both answer the default, and
     * the default is -1: across the 9,368 decompiled clientscripts every guard
     * on a db_getfield of a column with no table default compares against -1
     * (44 `dbrow`, 18 `obj`, 6 `graphic`, 6 plain `int`, ...). Zero appears only
     * in `> 0` / `!= 0` guards, which a real value satisfies too. */
    {
        int before = t->ints_stack_top;

        CS2VM2_PushInt(t, 0);
        CS2VM2_PushInt(t, pack_col(0, 250, 0)); /* column id neither row nor table lists */
        CS2VM2_PushInt(t, 0);
        call_db(t, CS2_OP_DB_GETFIELD);
        CHECK(t->ints_stack_top == before + 1, "DB_GETFIELD(absent column) still pushes");
        CS2VM2_PopInt(t, &iv);
        CHECK(iv == -1, "DB_GETFIELD(absent column) == -1");

        CS2VM2_PushInt(t, 0);
        CS2VM2_PushInt(t, pack_col(0, 0, 0));
        CS2VM2_PushInt(t, 9999); /* tuple index past the end */
        call_db(t, CS2_OP_DB_GETFIELD);
        CHECK(t->ints_stack_top == before + 1, "DB_GETFIELD(index out of range) still pushes");
        CS2VM2_PopInt(t, &iv);
        CHECK(iv == -1, "DB_GETFIELD(index out of range) == -1");
    }

    /*
     * A column the ROW does not list falls back to the TABLE — the only record
     * that states that column's field types and its default values.
     *
     * The arity is the half that bites. A whole-tuple read of an N-field column
     * owes the stack N values; answering with a single integer shifts every
     * local the script's multi-assignment writes. That is exactly what left the
     * rev-230 skill guide's rows iconless: `skill_features.sprite` is 5 fields
     * (graphic,int,int,int,int) and no row in the table sets it, so
     * `$sprite, $w, $h, $dx, $dy = db_getfield(...)` read four values that were
     * never pushed, `$sprite` came back 0 instead of -1, and script 9347 took
     * the cc_setgraphic branch — with sprite 0 — instead of cc_setobject.
     *
     * Both columns are discovered from the cache rather than named, so this
     * check does not depend on which revision's table 0 is on disk.
     */
    {
        struct RSCache_Dat2ConfigDbTable* dbt = CacheProvider_DbTableGet(provider, 0);
        struct RSCache_Dat2ConfigDbRow* r0 = CacheProvider_DbRowGet(provider, 0);
        int block_ints = t->ints_stack_top;
        int block_strs = t->strs_stack_top;
        int col_defaulted = -1; /* multi-field, table declares defaults */
        int col_typed = -1;     /* multi-field, no defaults anywhere */

        for( int c = 0; dbt && r0 && c < dbt->column_count; c++ )
        {
            int row_has = (c < r0->column_count && r0->columns[c].present);
            if( row_has || !dbt->columns[c].present || dbt->columns[c].type_count < 2 )
                continue;
            if( dbt->columns[c].tuple_count > 0 && dbt->columns[c].values )
            {
                if( col_defaulted < 0 )
                    col_defaulted = c;
            }
            else if( col_typed < 0 )
                col_typed = c;
        }
        CHECK(col_defaulted >= 0, "table 0 has a defaulted multi-field column row 0 omits");
        CHECK(col_typed >= 0, "table 0 has a defaultless multi-field column row 0 omits");

        for( int which = 0; which < 2; which++ )
        {
            int c = which == 0 ? col_defaulted : col_typed;
            struct RSCache_DbColumn* col;
            int int_before, str_before, want_ints = 0, want_strs = 0;
            char label[96];
            if( c < 0 )
                continue;
            col = &dbt->columns[c];
            for( int f = 0; f < col->type_count; f++ )
            {
                if( RSCache_DbTypeIsString(col->types[f]) )
                    want_strs++;
                else
                    want_ints++;
            }

            int_before = t->ints_stack_top;
            str_before = t->strs_stack_top;
            CS2VM2_PushInt(t, 0);
            CS2VM2_PushInt(t, pack_col(0, c, 0)); /* nibble 0 = whole tuple */
            CS2VM2_PushInt(t, 0);
            call_db(t, CS2_OP_DB_GETFIELD);
            snprintf(
                label,
                sizeof(label),
                "DB_GETFIELD(row0,col%d,whole) pushes %d ints + %d strings",
                c,
                want_ints,
                want_strs);
            CHECK(
                t->ints_stack_top == int_before + want_ints &&
                    t->strs_stack_top == str_before + want_strs,
                label);

            /* Drain whatever it pushed, newest first, so the next case starts
             * from the same stack. */
            while( t->strs_stack_top > str_before )
            {
                char* discard = NULL;
                CS2VM2_PopStr(t, &discard);
            }
            while( t->ints_stack_top > int_before )
                CS2VM2_PopInt(t, &iv);
        }

        /* The defaulted column's first field is the table's declared default,
         * not a type default: read it back on its own (nibble N selects field
         * N-1) and compare against the record. */
        if( col_defaulted >= 0 &&
            !RSCache_DbTypeIsString(dbt->columns[col_defaulted].types[0]) )
        {
            CS2VM2_PushInt(t, 0);
            CS2VM2_PushInt(t, pack_col(0, col_defaulted, 1));
            CS2VM2_PushInt(t, 0);
            call_db(t, CS2_OP_DB_GETFIELD);
            CS2VM2_PopInt(t, &iv);
            CHECK(
                iv == dbt->columns[col_defaulted].values[0].int_value,
                "DB_GETFIELD(omitted column) == the table's declared default");
        }

        /* DB_GETFIELDCOUNT sees the same fallback: the default block's tuples. */
        if( col_defaulted >= 0 )
        {
            CS2VM2_PushInt(t, 0);
            CS2VM2_PushInt(t, pack_col(0, col_defaulted, 0));
            call_db(t, CS2_OP_DB_GETFIELDCOUNT);
            CS2VM2_PopInt(t, &iv);
            CHECK(
                iv == dbt->columns[col_defaulted].tuple_count,
                "DB_GETFIELDCOUNT(omitted column) == the table's default tuple count");
        }

        CHECK(
            t->ints_stack_top == block_ints && t->strs_stack_top == block_strs,
            "fallback checks left both stacks where they found them");
    }

    /* DB_FINDALL_WITH_COUNT(table 0) -> 198 rows. */
    CS2VM2_PushInt(t, 0);
    call_db(t, CS2_OP_DB_FINDALL_WITH_COUNT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 198, "DB_FINDALL_WITH_COUNT(table0) == 198");

    /* DB_FIND_WITH_COUNT(col0 == 1) -> 1 row, and DB_FINDNEXT yields row 17.
     * Source order is db_find*(dbcolumn, value, type_tag), so dbcolumn goes on
     * first and the tag ends up on top. The tag names the stack the value is on:
     * 2 = string, anything else = int. This site used to push only two values —
     * written before the find family grew its third argument, and unnoticed
     * because the test could not reach here at all. */
    CS2VM2_PushInt(t, pack_col(0, 0, 0));  /* dbcolumn */
    CS2VM2_PushInt(t, 1);                  /* value */
    CS2VM2_PushInt(t, 0);                  /* type tag: int stack */
    call_db(t, CS2_OP_DB_FIND_WITH_COUNT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 1, "DB_FIND_WITH_COUNT(col0==1) == 1");

    call_db(t, CS2_OP_DB_FINDNEXT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == 17, "DB_FINDNEXT -> row 17");

    call_db(t, CS2_OP_DB_FINDNEXT);
    CS2VM2_PopInt(t, &iv);
    CHECK(iv == -1, "DB_FINDNEXT (exhausted) -> -1");

    CS2VM2_Free(&vm);
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
