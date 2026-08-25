#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/torirs_font_from_rscache.h"
#include "engine/torirs_types.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat1FontLoadByName
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    char font_name[64];
    /* Provider font_cache key: title-font slot 0-3 (dat1 fonts have no archive id). */
    int cache_font_id;
};

static int
Task_Dat1FontLoadByName_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1FontLoadByName* task = (struct Task_Dat1FontLoadByName*)task_base;
    struct ToriRS_Font* font = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_title_fonts_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* title_jagfile = NULL;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS);
        PT_YIELD(&task->pt);

        title_jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS);
        if( !title_jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 title jagfile for font %s\n", task->font_name);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_title_fonts_jagfile(task->bc, title_jagfile);
    }

    font = ToriRS_FontFromDat1Jagfile(
        dat1_buildcache_get_title_fonts_jagfile(task->bc), task->font_name);
    if( !font )
    {
        TORIRS_ERR("Failed to decode dat1 font %s\n", task->font_name);
        PT_EXIT(&task->pt);
    }

    CacheProvider_FontAdd(&task->bc->base, task->cache_font_id, font);

    PT_END(&task->pt);
}

static void
Task_Dat1FontLoadByName_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1FontLoadByName_VTable = {
    .run = Task_Dat1FontLoadByName_Run,
    .free = Task_Dat1FontLoadByName_Free,
};

struct ToriRS_Task*
CreateTask_Dat1FontLoadByName(
    struct CacheProvider* provider,
    char const* font_name,
    int cache_font_id)
{
    struct Dat1BuildCache* dat1_buildcache;
    struct Task_Dat1FontLoadByName* task;

    assert(provider);
    assert(font_name && font_name[0]);
    assert(cache_font_id >= 0);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    if( CacheProvider_FontHas(provider, cache_font_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1FontLoadByName_VTable;
    strcpy(task->task.name, "Dat1FontLoadByName");
    task->bc = dat1_buildcache;
    strncpy(task->font_name, font_name, sizeof(task->font_name) - 1);
    task->cache_font_id = cache_font_id;
    PT_INIT(&task->pt);
    return &task->task;
}
