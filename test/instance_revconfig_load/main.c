#include "3rd/minipt.h"
#include "core_task_await.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "platforms/platform_x_io_reactor.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/core/tasks/core_task.h"
#include "toriauxlib/core/tasks/task_instance_revconfig_load.h"
#include "toriauxlib/toriauxlib.h"
#include "toridraw/toridraw_scene.h"
#include "ui/ui_sprite_lookup.h"
#include "ui/uitree.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UI_DAT1_CACHE_INI "rev_245_2/rev_245_2_dat1_cache.ini"
#define UI_DAT1_UI_INI "rev_245_2/rev_245_2_dat1_ui.ini"
#define UI_DAT2_CACHE_INI "rev_245_2/rev_kronos_ui_cache.ini"
#define UI_DAT2_UI_INI "rev_245_2/rev_kronos_ui.ini"

// clang-format off
#define TEST_ASSERT(cond, msg) \
    do { \
        if( !(cond) ) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )
// clang-format on

/* --- TASK_AWAIT unit test --- */

struct AwaitChildTask
{
    struct pt thread;
    int yields_remaining;
    int yield_count;
};

static int
AwaitChildTask_Run(
    void* state,
    struct LibToriRS_IOContext* ctx)
{
    struct AwaitChildTask* task = state;
    (void)ctx;

    PT_BEGIN(&task->thread);
    task->yield_count = 0;
    while( task->yields_remaining > 0 )
    {
        task->yield_count++;
        task->yields_remaining--;
        PT_YIELD(&task->thread);
    }
    PT_END(&task->thread);
}

struct AwaitParentTask
{
    struct pt thread;
    struct AwaitChildTask child;
    bool completed;
};

static int
AwaitParentTask_Run(
    void* state,
    struct LibToriRS_IOContext* ctx)
{
    struct AwaitParentTask* task = state;

    PT_BEGIN(&task->thread);
    task->completed = false;
    TASK_AWAIT(&task->thread, AwaitChildTask_Run(&task->child, ctx));
    task->completed = true;
    PT_END(&task->thread);
}

static int
run_protothread_to_completion(
    int (*run_fn)(
        void*,
        struct LibToriRS_IOContext*),
    void* state,
    struct LibToriRS_IOQueue* io,
    struct LibToriPlatformX_IOReactor* reactor)
{
    struct LibToriRS_IOContext ctx = { .io = io };
    int wait_run = -1;
    int last_res = PT_YIELDED;

    while( last_res == PT_YIELDED || last_res == PT_WAITING )
    {
        if( wait_run >= 0 && !LibToriRS_IOQueueRunComplete(io, wait_run) )
        {
            LibToriPlatformX_IOReactorProcess(reactor, io);
            continue;
        }

        int run = LibToriRS_IOQueueBeginRun(io);
        last_res = run_fn(state, &ctx);

        switch( last_res )
        {
        case PT_YIELDED:
        case PT_WAITING:
            wait_run = run;
            LibToriPlatformX_IOReactorProcess(reactor, io);
            break;
        case PT_EXITED:
        case PT_ENDED:
            return 0;
        default:
            return -1;
        }
    }
    return last_res == PT_ENDED || last_res == PT_EXITED ? 0 : -1;
}

static int
test_task_await(void)
{
    struct LibToriRS_IOQueue* io = LibToriRS_IOQueueNew();
    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(NULL);
    TEST_ASSERT(io && reactor, "io/reactor alloc");

    struct AwaitParentTask parent;
    memset(&parent, 0, sizeof(parent));
    parent.child.yields_remaining = 3;

    int rc = run_protothread_to_completion(AwaitParentTask_Run, &parent, io, reactor);
    TEST_ASSERT(rc == 0, "await parent did not complete");
    TEST_ASSERT(parent.completed, "parent not marked completed");
    TEST_ASSERT(parent.child.yield_count == 3, "child did not yield 3 times");

    LibToriPlatformX_IOReactorFree(reactor);
    LibToriRS_IOQueueFree(io);
    fprintf(stderr, "ok: TASK_AWAIT unit test\n");
    return 0;
}

/* --- full pipeline --- */

static bool
chdir_to_config_root(void)
{
    static char const* const candidates[] = {
        "../../src2/programs/sdl2",
        "../../../src2/programs/sdl2",
        "../src2/programs/sdl2",
        NULL,
    };

    for( int i = 0; candidates[i]; i++ )
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", candidates[i], UI_DAT1_CACHE_INI);
        FILE* f = fopen(path, "rb");
        if( !f )
            continue;
        fclose(f);
        return chdir(candidates[i]) == 0;
    }
    return false;
}

static bool
uitree_has_type(
    struct UITree const* tree,
    enum StaticUIComponentType ty)
{
    assert(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type == ty )
            return true;
    }
    return false;
}

static bool
uitree_links_valid(struct UITree const* tree)
{
    assert(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( c->parent >= 0 && (uint32_t)c->parent >= tree->component_count )
            return false;
        if( c->first_child >= 0 && (uint32_t)c->first_child >= tree->component_count )
            return false;
        if( c->next_sibling >= 0 && (uint32_t)c->next_sibling >= tree->component_count )
            return false;
    }
    return true;
}

static uint32_t
count_items_of_kind(
    struct RevConfigItemBuffer const* items,
    enum RevConfigItemKind kind)
{
    uint32_t n = 0;
    assert(items);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind == kind )
            n++;
    }
    return n;
}

static int
run_pipeline_test(
    enum ToriAuxLibCacheMode mode,
    char const* label,
    char const* cache_ini,
    char const* ui_ini,
    bool expect_compass)
{
    struct LibToriRS_IOQueue* io = LibToriRS_IOQueueNew();
    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(NULL);
    TEST_ASSERT(io && reactor, "io/reactor alloc");

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL);
    TEST_ASSERT(scene != NULL, "scene alloc");

    struct ToriAuxLib* aux = ToriAuxLib_New(mode, scene);
    TEST_ASSERT(aux != NULL, "ToriAuxLib_New");

    struct UITree* tree = uitree_new(64);
    TEST_ASSERT(tree != NULL, "uitree_new");

    char const* files[2] = { cache_ini, ui_ini };
    struct Task_InstanceRevConfigLoad* load_task =
        Task_InstanceRevConfigLoad_New(ToriAuxLib_C(aux), scene, tree, NULL, files, 2, "fixed");
    TEST_ASSERT(load_task != NULL, "Task_InstanceRevConfigLoad_New");

    struct CoreTask* task = core_task_new(load_task, Task_InstanceRevConfigLoad_Run, NULL);
    TEST_ASSERT(task != NULL, "core_task_new");

    bool finished = false;
    struct LibToriRS_IOContext ctx = { .io = io };
    int wait_run = -1;
    int last_res = PT_YIELDED;

    while( last_res == PT_YIELDED || last_res == PT_WAITING )
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
        case PT_WAITING:
            wait_run = run;
            LibToriPlatformX_IOReactorProcess(reactor, io);
            break;
        case PT_EXITED:
        case PT_ENDED:
            finished = true;
            break;
        default:
            break;
        }
        if( finished )
            break;
    }

    core_task_free(task);

    TEST_ASSERT(finished, "load task did not finish");
    TEST_ASSERT(Task_InstanceRevConfigLoad_IsReady(load_task), "load task not ready");

    TEST_ASSERT(load_task->items != NULL, "items buffer missing");
    TEST_ASSERT(load_task->items->item_count > 0, "no items parsed");
    TEST_ASSERT(
        count_items_of_kind(load_task->items, RCITEM_CACHE_SPRITE) > 0, "no cache sprite items");
    TEST_ASSERT(
        count_items_of_kind(load_task->items, RCITEM_UICOMPONENT) > 0, "no uicomponent items");
    TEST_ASSERT(count_items_of_kind(load_task->items, RCITEM_UILAYOUT) > 0, "no uilayout items");

    if( expect_compass )
    {
        int sideicons_id =
            ui_sprite_lookup_find(&load_task->rc_ctx.sprite_lookup, "sideicons", NULL);
        if( sideicons_id >= 0 )
            TEST_ASSERT(sideicons_id > 0, "sideicons lookup invalid");
    }

    if( tree && tree->component_count > 0 )
    {
        TEST_ASSERT(tree->root_index >= 0, "tree missing root");
        TEST_ASSERT(uitree_links_valid(tree), "tree link integrity");
        if( expect_compass )
            TEST_ASSERT(
                uitree_has_type(tree, UIELEM_BUILTIN_COMPASS) ||
                    uitree_has_type(tree, UIELEM_BUILTIN_MINIMAP),
                "expected builtin UI node");
    }

    fprintf(
        stderr,
        "ok: %s items=%u tree_nodes=%u sprites=%d\n",
        label,
        load_task->items->item_count,
        tree ? tree->component_count : 0u,
        load_task->rc_ctx.sprite_lookup.count);

    Task_InstanceRevConfigLoad_Free(load_task);
    uitree_free(tree);
    ToriAuxLib_Free(aux);
    ToriDraw_SceneFree(scene);
    LibToriPlatformX_IOReactorFree(reactor);
    LibToriRS_IOQueueFree(io);
    return 0;
}

int
main(void)
{
    if( test_task_await() != 0 )
        return 1;

    if( !chdir_to_config_root() )
    {
        fprintf(stderr, "skip: config INI files not found (run from repo with rev configs)\n");
        fprintf(stderr, "All instance_revconfig_load tests passed (await only).\n");
        return 0;
    }

    if( run_pipeline_test(
            TORIAUXLIBCACHE_MODE_DAT1, "dat1", UI_DAT1_CACHE_INI, UI_DAT1_UI_INI, true) != 0 )
        return 1;

    if( run_pipeline_test(
            TORIAUXLIBCACHE_MODE_DAT2, "dat2", UI_DAT2_CACHE_INI, UI_DAT2_UI_INI, false) != 0 )
        return 1;

    printf("All instance_revconfig_load tests passed.\n");
    return 0;
}
