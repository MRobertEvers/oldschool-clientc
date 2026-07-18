#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_texture_bake.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2TextureLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat2BuildCache* bc;
    int texture_id;
    struct RSCache_Dat2Texture* def;
    struct RSCache_Dat2SpritePack** packs;
    int sprite_index;
};

static void
task_dat2_texture_load_clear_packs(struct Task_Dat2TextureLoad* task)
{
    int i;

    assert(task);

    if( task->packs && task->def )
    {
        for( i = 0; i < task->def->sprite_ids_count; i++ )
        {
            if( task->packs[i] )
                RSCache_Dat2SpritePackFree(task->packs[i]);
        }
    }
    free(task->packs);
    task->packs = NULL;
    task->def = NULL;
    task->sprite_index = 0;
}

static int
Task_Dat2TextureLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2TextureLoad* task = (struct Task_Dat2TextureLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Texture* torirs_texture = NULL;
    struct ToriRS_TextureLayer* layers = NULL;
    int i;

    PT_BEGIN(&task->pt);

    RSCache_IO_Dat2TextureGroupLoad(io, 0);
    PT_YIELD(&task->pt);

    archive = RSCache_IO_Dat2TextureGroupDecode(io, 0);
    if( !archive )
    {
        fprintf(stderr, "Failed to decode dat2 texture group for texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    dat2_buildcache_textures_init_from_archive(task->bc, archive, &task->texture_id, 1);
    RSCache_Dat2DiskArchiveFree(archive);

    task->def = dat2_buildcache_texture_get(task->bc, task->texture_id);
    if( !task->def )
    {
        fprintf(stderr, "Failed to load dat2 texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    if( task->def->sprite_ids_count <= 0 )
    {
        fprintf(stderr, "Dat2 texture %d has no sprite layers\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    task->packs = calloc((size_t)task->def->sprite_ids_count, sizeof(*task->packs));
    if( !task->packs )
    {
        fprintf(stderr, "Failed to allocate sprite packs for texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }
    task->sprite_index = 0;

    for( ; task->sprite_index < task->def->sprite_ids_count; task->sprite_index++ )
    {
        RSCache_IO_Dat2SpriteLoad(io, 0, task->def->sprite_ids[task->sprite_index]);
        PT_YIELD(&task->pt);

        archive = RSCache_IO_Dat2SpriteDecode(io, 0);
        if( !archive )
        {
            fprintf(
                stderr,
                "Failed to decode sprite %d for texture %d\n",
                task->def->sprite_ids[task->sprite_index],
                task->texture_id);
            task_dat2_texture_load_clear_packs(task);
            PT_EXIT(&task->pt);
        }

        task->packs[task->sprite_index] = RSCache_Dat2SpritePackNewDecode(
            (const unsigned char*)archive->data,
            archive->data_size,
            RSCACHE_SPRITELOAD_FLAG_NORMALIZE);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !task->packs[task->sprite_index] || task->packs[task->sprite_index]->count <= 0 )
        {
            fprintf(
                stderr,
                "Failed to decode sprite pack %d for texture %d\n",
                task->def->sprite_ids[task->sprite_index],
                task->texture_id);
            task_dat2_texture_load_clear_packs(task);
            PT_EXIT(&task->pt);
        }
    }

    layers = calloc((size_t)task->def->sprite_ids_count, sizeof(*layers));
    if( !layers )
    {
        fprintf(stderr, "Failed to allocate bake layers for texture %d\n", task->texture_id);
        task_dat2_texture_load_clear_packs(task);
        PT_EXIT(&task->pt);
    }

    for( i = 0; i < task->def->sprite_ids_count; i++ )
    {
        struct RSCache_Dat2SpritePack* pack = task->packs[i];
        struct RSCache_Dat2Sprite* sprite = &pack->sprites[0];

        layers[i].palette_pixels = sprite->palette_pixels;
        layers[i].width = sprite->width;
        layers[i].height = sprite->height;
        layers[i].palette = pack->palette;
        layers[i].palette_length = pack->palette_length;
        layers[i].blend_type = 0;
        if( i > 0 && task->def->sprite_types )
            layers[i].blend_type = task->def->sprite_types[i - 1];
    }

    torirs_texture = ToriRS_TextureBake(
        layers,
        task->def->sprite_ids_count,
        128,
        task->def->animation_direction,
        task->def->animation_speed,
        task->def->average_hsl);
    free(layers);
    task_dat2_texture_load_clear_packs(task);

    if( !torirs_texture )
    {
        fprintf(stderr, "Failed to bake dat2 texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_TextureAdd(&task->bc->base, task->texture_id, torirs_texture);

    PT_END(&task->pt);
}

static void
Task_Dat2TextureLoad_Free(struct ToriRS_Task* task_base)
{
    struct Task_Dat2TextureLoad* task = (struct Task_Dat2TextureLoad*)task_base;
    task_dat2_texture_load_clear_packs(task);
    free(task);
}

static struct ToriRS_TaskVTable Task_Dat2TextureLoad_VTable = {
    .run = Task_Dat2TextureLoad_Run,
    .free = Task_Dat2TextureLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat2TextureLoad(
    struct CacheProvider* provider,
    int texture_id)
{
    struct Dat2BuildCache* dat2_buildcache;
    struct Task_Dat2TextureLoad* task;

    assert(provider);

    dat2_buildcache = (struct Dat2BuildCache*)provider;
    if( CacheProvider_TextureHas(provider, texture_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat2TextureLoad_VTable;
    strcpy(task->task.name, "Dat2TextureLoad");
    task->bc = dat2_buildcache;
    task->texture_id = texture_id;
    PT_INIT(&task->pt);
    return &task->task;
}
