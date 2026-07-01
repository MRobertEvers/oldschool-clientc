#include "3rd/minipt.h"
#include "core/tapi/tapi_config.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "platforms/platform_x_io_reactor.h"
#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "toriauxlib/core/tasks/core_task.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_DAT1_CACHE_INI "rev_245_2/rev_245_2_dat1_cache.ini"
#define UI_DAT1_UI_INI "rev_245_2/rev_245_2_dat1_ui.ini"
#define UI_DAT2_CACHE_INI "rev_245_2/rev_kronos_ui_cache.ini"
#define UI_DAT2_UI_INI "rev_245_2/rev_kronos_ui.ini"

// clang-format off
#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )
// clang-format on

struct Task_RevConfigIniLoad
{
    struct pt thread;
    char const* cache_ini;
    char const* ui_ini;
    struct RevConfigBuffer* cache_fields;
    struct RevConfigBuffer* ui_fields;
    struct RevConfigItemBuffer* items;
    bool ok;
};

static int
Task_RevConfigIniLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_RevConfigIniLoad* task = task_state;

    PT_BEGIN(&task->thread);

    task->ok = false;

    assert(!task->cache_fields);
    task->cache_fields = revconfig_buffer_new(1024);
    assert(!task->ui_fields);
    task->ui_fields = revconfig_buffer_new(1024);
    assert(!task->items);
    task->items = revconfig_item_buffer_new(256);

    assert(task->cache_fields);
    assert(task->ui_fields);
    assert(task->items);
    if( !task->cache_fields || !task->ui_fields || !task->items )
        PT_EXIT(&task->thread);

    TAPIConfig_FetchRevconfig(ctx, task->cache_ini);
    TAPIConfig_FetchRevconfig(ctx, task->ui_ini);
    PT_YIELD(&task->thread);

    void* data = NULL;
    int size = TAPIConfig_DecodeRevconfig(ctx, &data);
    if( size <= 0 || !data )
        PT_EXIT(&task->thread);
    revconfig_load_fields_from_ini_bytes(data, (size_t)size, task->cache_fields);
    free(data);
    data = NULL;
    size = TAPIConfig_DecodeRevconfig(ctx, &data);
    if( size <= 0 || !data )
        PT_EXIT(&task->thread);
    revconfig_load_fields_from_ini_bytes(data, (size_t)size, task->ui_fields);
    free(data);

    revconfig_items_build(task->cache_fields, task->items);
    revconfig_items_build(task->ui_fields, task->items);

    for( int i = 0; i < task->items->item_count; i++ )
    {
        struct RevConfigItem const* item = &task->items->items[i];
        if( item->kind == RCITEM_CACHE_SPRITE )
        {
        }
    }

    task->ok = true;
    PT_END(&task->thread);
}

static bool
run_task_to_completion(
    struct CoreTask* task,
    struct LibToriRS_IOQueue* io,
    struct LibToriPlatformX_IOReactor* reactor)
{
    assert(task);
    assert(io);
    assert(reactor);

    struct LibToriRS_IOContext ctx = {
        .io = io,
    };

    int wait_run = -1;
    int last_res = PT_YIELDED;

    while( last_res == PT_YIELDED )
    {
        if( wait_run >= 0 && !LibToriRS_IOQueueRunComplete(io, wait_run) )
        {
            LibToriPlatformX_IOReactorProcess(reactor, io);
            continue;
        }

        int run = LibToriRS_IOQueueBeginRun(io);
        last_res = task->task(task->state, &ctx);

        switch( last_res )
        {
        case PT_YIELDED:
            wait_run = run;
            LibToriPlatformX_IOReactorProcess(reactor, io);
            break;
        case PT_EXITED:
        case PT_ENDED:
            return true;
        default:
            return false;
        }
    }

    return last_res == PT_ENDED || last_res == PT_EXITED;
}

static uint32_t
count_fields_of_kind(
    struct RevConfigBuffer const* buffer,
    enum RevConfigFieldKind kind)
{
    uint32_t count = 0;
    if( !buffer )
        return 0;
    for( uint32_t i = 0; i < buffer->field_count; i++ )
    {
        if( buffer->fields[i].kind == kind )
            count++;
    }
    return count;
}

static bool
find_cache_item(
    struct RevConfigItemBuffer const* items,
    char const* name)
{
    if( !items || !name )
        return false;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_CACHE_SPRITE && strcmp(item->u.cache.name, name) == 0 )
            return true;
    }
    return false;
}

static bool
find_cache_font_item(
    struct RevConfigItemBuffer const* items,
    char const* name)
{
    if( !items || !name )
        return false;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_CACHE_FONT && strcmp(item->u.font.name, name) == 0 )
            return true;
    }
    return false;
}

static struct RevConfigFontItem const*
get_cache_font_item(
    struct RevConfigItemBuffer const* items,
    char const* name)
{
    if( !items || !name )
        return NULL;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_CACHE_FONT && strcmp(item->u.font.name, name) == 0 )
            return &item->u.font;
    }
    return NULL;
}

static bool
find_uicomponent(
    struct RevConfigItemBuffer const* items,
    char const* name)
{
    if( !items || !name )
        return false;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_UICOMPONENT && strcmp(item->u.uicomponent.name, name) == 0 )
            return true;
    }
    return false;
}

static bool
find_cache_item_with_archive_id(struct RevConfigItemBuffer const* items)
{
    if( !items )
        return false;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_CACHE_SPRITE && item->u.cache.archive_id >= 0 )
            return true;
    }
    return false;
}

static void
free_task_buffers(struct Task_RevConfigIniLoad* task)
{
    if( !task )
        return;
    revconfig_buffer_free(task->cache_fields);
    revconfig_buffer_free(task->ui_fields);
    revconfig_item_buffer_free(task->items);
    task->cache_fields = NULL;
    task->ui_fields = NULL;
    task->items = NULL;
}

static int
run_ini_pair_test(
    struct LibToriRS_IOQueue* io,
    struct LibToriPlatformX_IOReactor* reactor,
    char const* label,
    char const* cache_ini,
    char const* ui_ini,
    bool expect_dat1_spot_checks)
{
    struct Task_RevConfigIniLoad load_task;
    memset(&load_task, 0, sizeof(load_task));
    load_task.cache_ini = cache_ini;
    load_task.ui_ini = ui_ini;

    struct CoreTask* task = core_task_new(&load_task, Task_RevConfigIniLoad_Run, NULL);
    TEST_ASSERT(task != NULL, "core_task_new failed");

    bool finished = run_task_to_completion(task, io, reactor);
    core_task_free(task);

    TEST_ASSERT(finished, "task did not finish");
    TEST_ASSERT(load_task.ok, "task reported failure");

    TEST_ASSERT(load_task.cache_fields->field_count > 0, "cache_fields empty");
    TEST_ASSERT(load_task.ui_fields->field_count > 0, "ui_fields empty");
    TEST_ASSERT(
        count_fields_of_kind(load_task.cache_fields, RCFIELD_ITEMTYPE) > 0,
        "cache_fields missing item types");
    TEST_ASSERT(
        count_fields_of_kind(load_task.ui_fields, RCFIELD_ITEMTYPE) > 0,
        "ui_fields missing item types");
    TEST_ASSERT(load_task.items->item_count > 0, "items buffer empty after build");

    if( expect_dat1_spot_checks )
    {
        TEST_ASSERT(
            find_cache_item(load_task.items, "sideicons"), "dat1: sideicons cache item missing");
        TEST_ASSERT(
            find_uicomponent(load_task.items, "compass"), "dat1: compass component missing");
        TEST_ASSERT(find_cache_font_item(load_task.items, "b12"), "dat1: b12 font cache item missing");
        {
            struct RevConfigFontItem const* p11 = get_cache_font_item(load_task.items, "p11");
            struct RevConfigFontItem const* p12 = get_cache_font_item(load_task.items, "p12");
            struct RevConfigFontItem const* b12 = get_cache_font_item(load_task.items, "b12");
            struct RevConfigFontItem const* q8 = get_cache_font_item(load_task.items, "q8");
            TEST_ASSERT(p11 && p11->cache_font_id == 0, "dat1: p11 cache_font_id");
            TEST_ASSERT(p12 && p12->cache_font_id == 1, "dat1: p12 cache_font_id");
            TEST_ASSERT(b12 && b12->cache_font_id == 2, "dat1: b12 cache_font_id");
            TEST_ASSERT(q8 && q8->cache_font_id == 3, "dat1: q8 cache_font_id");
        }
        TEST_ASSERT(find_cache_item(load_task.items, "cross"), "dat1: cross sprite cache item missing");
        TEST_ASSERT(find_uicomponent(load_task.items, "cross"), "dat1: cross component missing");
        TEST_ASSERT(find_uicomponent(load_task.items, "minimenu"), "dat1: minimenu component missing");
    }
    else
    {
        TEST_ASSERT(
            find_cache_item_with_archive_id(load_task.items),
            "dat2: no cache item with archive_id");
        TEST_ASSERT(find_cache_font_item(load_task.items, "b12"), "dat2: b12 font cache item missing");
        {
            struct RevConfigFontItem const* b12 = get_cache_font_item(load_task.items, "b12");
            TEST_ASSERT(b12 && b12->cache_font_id == 2, "dat2: b12 cache_font_id");
            TEST_ASSERT(b12 && b12->archive_id == 496, "dat2: b12 archive_id");
        }
        TEST_ASSERT(find_cache_item(load_task.items, "cross"), "dat2: cross sprite cache item missing");
        TEST_ASSERT(find_uicomponent(load_task.items, "cross"), "dat2: cross component missing");
        TEST_ASSERT(find_uicomponent(load_task.items, "minimenu"), "dat2: minimenu component missing");
    }

    fprintf(
        stderr,
        "ok: %s cache_fields=%u ui_fields=%u items=%u\n",
        label,
        load_task.cache_fields->field_count,
        load_task.ui_fields->field_count,
        load_task.items->item_count);

    free_task_buffers(&load_task);
    LibToriRS_IOQueueClear(io);
    return 0;
}

int
main(void)
{
    struct LibToriRS_IOQueue* io = LibToriRS_IOQueueNew();
    TEST_ASSERT(io != NULL, "LibToriRS_IOQueueNew failed");

    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(NULL);
    TEST_ASSERT(reactor != NULL, "LibToriPlatformX_IOReactorNew failed");

    if( run_ini_pair_test(io, reactor, "dat1", UI_DAT1_CACHE_INI, UI_DAT1_UI_INI, true) != 0 )
        goto fail;

    if( run_ini_pair_test(io, reactor, "dat2", UI_DAT2_CACHE_INI, UI_DAT2_UI_INI, false) != 0 )
        goto fail;

    LibToriPlatformX_IOReactorFree(reactor);
    LibToriRS_IOQueueFree(io);
    printf("All revconfig_load tests passed.\n");
    return 0;

fail:
    LibToriPlatformX_IOReactorFree(reactor);
    LibToriRS_IOQueueFree(io);
    return 1;
}
