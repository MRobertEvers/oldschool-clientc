#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "datatypes/dat2_component.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ClientScriptLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* cache;
    int* script_ids;
    int script_count;
    int script_index;
};

static bool
toriauxlibcache_script_id_list_add(
    int** ids,
    int* count,
    int* cap,
    int script_id)
{
    int i;

    if( script_id < 0 )
        return true;

    for( i = 0; i < *count; i++ )
    {
        if( (*ids)[i] == script_id )
            return true;
    }

    if( *count >= *cap )
    {
        int new_cap = *cap < 8 ? 8 : *cap * 2;
        int* grown = realloc(*ids, (size_t)new_cap * sizeof(int));
        if( !grown )
            return false;
        *ids = grown;
        *cap = new_cap;
    }

    (*ids)[(*count)++] = script_id;
    return true;
}

static void
toriauxlibcache_collect_hook_script_id(
    ComponentScriptVar const* hook,
    int hook_len,
    int** ids,
    int* count,
    int* cap)
{
    if( !hook || hook_len <= 0 || hook[0].type != SCRIPT_VAR_INT )
        return;
    (void)toriauxlibcache_script_id_list_add(ids, count, cap, hook[0].value.i);
}

int
toriauxlibcache_collect_component_hook_script_ids(
    struct Dat2BuildCache_InterfaceArchive* iface,
    int** script_ids_out,
    int* script_count_out)
{
    int* ids = NULL;
    int count = 0;
    int cap = 0;
    int i;

    if( script_ids_out )
        *script_ids_out = NULL;
    if( script_count_out )
        *script_count_out = 0;
    assert(iface);
    assert(script_ids_out);
    assert(script_count_out);

    for( i = 0; i < iface->component_count; i++ )
    {
        struct RSCache_Dat2Component* comp = iface->components[i];
        if( !comp )
            continue;
        toriauxlibcache_collect_hook_script_id(comp->onLoad, comp->onLoadLen, &ids, &count, &cap);
        toriauxlibcache_collect_hook_script_id(comp->onClick, comp->onClickLen, &ids, &count, &cap);
        toriauxlibcache_collect_hook_script_id(
            comp->onVarpTransmit, comp->onVarpTransmitLen, &ids, &count, &cap);
        toriauxlibcache_collect_hook_script_id(
            comp->onInvTransmit, comp->onInvTransmitLen, &ids, &count, &cap);
    }

    *script_ids_out = ids;
    *script_count_out = count;
    return count;
}

static int
Task_Dat2ClientScriptLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2ClientScriptLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2ClientScriptLoad, base);
    struct Dat2BuildCache* bc = NULL;
    struct RSCacheDat2A_ClientScript* decoded = NULL;

    if( task->cache && ToriAuxLibCache_Mode(task->cache) == TORIAUXLIBCACHE_MODE_DAT2 )
        bc = dat2(task->cache);

    PT_BEGIN(&task->pt);

    if( !bc )
        PT_EXIT(&task->pt);

    for( ; task->script_index < task->script_count; task->script_index++ )
    {
        if( task->script_ids[task->script_index] < 0 )
            continue;
        if( ToriAuxLibCore_ClientScriptGet(
                ToriAuxLibCache_Core(task->cache), task->script_ids[task->script_index]) )
            continue;
        if( dat2_buildcache_clientscript_has(bc, task->script_ids[task->script_index]) )
        {
            ToriAuxLibCache_SubmitClientScriptFromDat2(
                task->cache, task->script_ids[task->script_index]);
            continue;
        }

        IO_REQUEST(ctx, 0, TAPIDat2_FetchClientScript(ctx, task->script_ids[task->script_index]));
        PT_YIELD(&task->pt);

        decoded = dat2_buildcache_clientscript_decode_from_archive(
            TAPIDat2_DecodeClientScriptArchive(ctx, 0, task->script_ids[task->script_index]),
            task->script_ids[task->script_index],
            ToriAuxLibCache_ClientscriptDecodeFlags(task->cache));
        if( decoded )
        {
            dat2_buildcache_clientscript_add(bc, task->script_ids[task->script_index], decoded);
            ToriAuxLibCache_SubmitClientScriptFromDat2(
                task->cache, task->script_ids[task->script_index]);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    PT_END(&task->pt);
}

static void
Task_Dat2ClientScriptLoad_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2ClientScriptLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2ClientScriptLoad, base);
    free(task->script_ids);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_clientscript_load_vtable = {
    .run_fn = Task_Dat2ClientScriptLoad_Run,
    .free_fn = Task_Dat2ClientScriptLoad_Free,
};

struct LibToriRS_Task*
Task_Dat2ClientScriptLoad_New(
    struct ToriAuxLibCache* cache,
    const int* script_ids,
    int script_count)
{
    struct Task_Dat2ClientScriptLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_dat2_clientscript_load_vtable;
    task->cache = cache;
    task->script_count = script_count;
    if( script_count > 0 && script_ids )
    {
        task->script_ids = malloc((size_t)script_count * sizeof(int));
        if( !task->script_ids )
        {
            free(task);
            return NULL;
        }
        memcpy(task->script_ids, script_ids, (size_t)script_count * sizeof(int));
    }
    PT_INIT(&task->pt);
    return &task->base;
}
