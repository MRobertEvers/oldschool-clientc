#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_sprite_from_rscache.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2SpriteLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int sprite_id;
};

static int
Task_Dat2SpriteLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2SpriteLoad* task = (struct Task_Dat2SpriteLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Sprite* sprite = NULL;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2SpriteLoad(io, 0, task->sprite_id);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2SpriteDecode(io, 0);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 sprite archive %d\n", task->sprite_id);
        PT_EXIT(&task->pt);
    }

    sprite = ToriRS_SpriteFromDat2Archive(archive, task->sprite_id);
    if( !sprite )
    {
        fprintf(stderr, "Failed to convert dat2 sprite %d\n", task->sprite_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_SpriteAdd(&task->bc->base, task->sprite_id, sprite);

    PT_END(&task->pt);
}

static void
Task_Dat2SpriteLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2SpriteLoad_VTable = {
    .run = Task_Dat2SpriteLoad_Run,
    .free = Task_Dat2SpriteLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2SpriteLoad(
    struct CacheProvider* provider,
    int sprite_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2SpriteLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_SpriteHas(provider, sprite_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SpriteLoad_VTable;
    strcpy(task->task.name, "Dat2SpriteLoad");
    task->bc = dat2_buildcache;
    task->sprite_id = sprite_id;
    PT_INIT(&task->pt);
    return &task->task;
}

#define DAT2_SPRITE_NAME_MAX 63

struct Task_Dat2SpriteLoadByName
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    char archive_name[DAT2_SPRITE_NAME_MAX + 1];
    int sprite_id;
};

static int
dat2_resolve_sprite_archive_by_name(
    struct RSCache_ReferenceTable* table,
    char* name)
{
    int name_hash;
    int i;

    assert(table);
    assert(name);
    name_hash = RSCache_ArchiveNameHashDat2(name);
    for( i = 0; i < table->archive_count; i++ )
    {
        if( table->archives[i].identifier == name_hash )
            return table->archives[i].index;
    }
    return -1;
}

static int
Task_Dat2SpriteLoadByName_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2SpriteLoadByName* task = (struct Task_Dat2SpriteLoadByName*)task_base;
    struct RSCache_ReferenceTable* table = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Sprite* sprite = NULL;

    PT_BEGIN(&task->pt);

    if( !dat2_buildcache_reference_table_has(task->bc, RSCACHE_DAT2_TABLE_SPRITES) )
    {
        RSCache_IO_Dat2ReferenceTableLoad(io, 0, RSCACHE_DAT2_TABLE_SPRITES);
        PT_YIELD(&task->pt);

        table = RSCache_IO_Dat2ReferenceTableDecode(io, 0);
        if( !table )
        {
            fprintf(stderr, "Failed to load sprites reference table\n");
            PT_EXIT(&task->pt);
        }
        dat2_buildcache_reference_table_add(task->bc, RSCACHE_DAT2_TABLE_SPRITES, table);
    }

    table = dat2_buildcache_reference_table_get(task->bc, RSCACHE_DAT2_TABLE_SPRITES);
    assert(table);

    task->sprite_id = dat2_resolve_sprite_archive_by_name(table, task->archive_name);
    if( task->sprite_id < 0 )
    {
        fprintf(
            stderr,
            "Dat2SpriteLoadByName: archive '%s' not found in sprites table\n",
            task->archive_name);
        PT_EXIT(&task->pt);
    }

    CacheProvider_SpriteNameMapPut(&task->bc->base, task->archive_name, task->sprite_id);

    if( !CacheProvider_SpriteHas(&task->bc->base, task->sprite_id) )
    {
        RSCache_IO_Dat2SpriteLoad(io, 0, task->sprite_id);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2SpriteDecode(io, 0);
        if( !archive )
        {
            fprintf(stderr, "Failed to decode dat2 sprite archive %d\n", task->sprite_id);
            PT_EXIT(&task->pt);
        }

        sprite = ToriRS_SpriteFromDat2Archive(archive, task->sprite_id);
        if( !sprite )
        {
            fprintf(stderr, "Failed to convert dat2 sprite %d\n", task->sprite_id);
            PT_EXIT(&task->pt);
        }

        CacheProvider_SpriteAdd(&task->bc->base, task->sprite_id, sprite);
    }

    PT_END(&task->pt);
}

static void
Task_Dat2SpriteLoadByName_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat2SpriteLoadByName_VTable = {
    .run = Task_Dat2SpriteLoadByName_Run,
    .free = Task_Dat2SpriteLoadByName_Free,
};

struct ToriRS_Task*
CreateTask_Dat2SpriteLoadByName(
    struct CacheProvider* provider,
    char const* archive_name)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2SpriteLoadByName* task;
    int existing_id;

    assert(provider);
    assert(archive_name);
    assert(archive_name[0] != '\0');

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    existing_id = CacheProvider_SpriteIdByName(provider, archive_name);
    if( existing_id >= 0 && CacheProvider_SpriteHas(provider, existing_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2SpriteLoadByName_VTable;
    strcpy(task->task.name, "Dat2SpriteLoadByName");
    task->bc = dat2_buildcache;
    task->sprite_id = -1;
    strncpy(task->archive_name, archive_name, DAT2_SPRITE_NAME_MAX);
    task->archive_name[DAT2_SPRITE_NAME_MAX] = '\0';
    PT_INIT(&task->pt);
    return &task->task;
}
