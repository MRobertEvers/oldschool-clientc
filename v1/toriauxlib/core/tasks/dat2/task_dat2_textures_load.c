#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2a/dat2a_textures.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2TexturesLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* cache;
    struct RSCacheShared_FileList* filelist;
    int texture_count;
    int texture_index;
    int sprite_index;
    struct RSCacheDat2A_Texture* cur_def;
    int cur_texture_id;
    struct RSCacheDat2A_SpritePack** cur_packs;
};

static void
task_dat2_textures_clear_current(struct Task_Dat2TexturesLoad* task)
{
    assert(task);

    if( task->cur_packs && task->cur_def )
    {
        for( int k = 0; k < task->cur_def->sprite_ids_count; k++ )
        {
            if( task->cur_packs[k] )
                RSCacheDat2A_SpritePackFree(task->cur_packs[k]);
        }
    }
    free(task->cur_packs);
    task->cur_packs = NULL;

    if( task->cur_def )
        RSCacheDat2A_TextureDefinitionFree(task->cur_def);
    task->cur_def = NULL;
    task->cur_texture_id = -1;
    task->sprite_index = 0;
}

static int
Task_Dat2TexturesLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2TexturesLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2TexturesLoad, base);
    struct Dat2BuildCache* bc = NULL;
    struct RSCacheDat2Disk_ReferenceTable* textures_table = NULL;
    struct RSCacheDat2Disk_ArchiveReference* reference = NULL;
    struct RSCacheDat2Disk_Archive* textures_archive = NULL;

    if( task->cache && ToriAuxLibCache_Mode(task->cache) == TORIAUXLIBCACHE_MODE_DAT2 )
        bc = dat2(task->cache);

    PT_BEGIN(&task->pt);

    if( !bc )
        PT_EXIT(&task->pt);

    DAT2_ENSURE_REFERENCE_TABLE(ctx, &task->pt, bc, RSCacheDat2Disk_Table_Textures);

    textures_table = dat2_buildcache_reference_table_get(bc, RSCacheDat2Disk_Table_Textures);
    if( !textures_table || textures_table->archive_count <= 0 )
        PT_EXIT(&task->pt);

    reference = &textures_table->archives[0];

    IO_REQUEST(ctx, 0, TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Textures, 0));
    PT_YIELD(&task->pt);

    textures_table = dat2_buildcache_reference_table_get(bc, RSCacheDat2Disk_Table_Textures);
    if( !textures_table || textures_table->archive_count <= 0 )
        PT_EXIT(&task->pt);
    reference = &textures_table->archives[0];

    textures_archive = TAPIDat2_DecodeArchive(ctx, 0, RSCacheDat2Disk_Table_Textures, 0);
    if( !textures_archive )
        PT_EXIT(&task->pt);

    task->filelist = RSCacheShared_FileListNewFromCacheArchive(textures_archive);
    RSCacheDat2Disk_ArchiveFree(textures_archive);
    LibToriRS_IOQueueClear(ctx->io);
    if( !task->filelist )
        PT_EXIT(&task->pt);

    task->texture_count = reference->children.count;
    if( task->filelist->file_count < task->texture_count )
        task->texture_count = task->filelist->file_count;

    for( ; task->texture_index < task->texture_count; task->texture_index++ )
    {
        textures_table = dat2_buildcache_reference_table_get(bc, RSCacheDat2Disk_Table_Textures);
        if( !textures_table || textures_table->archive_count <= 0 )
            PT_EXIT(&task->pt);
        reference = &textures_table->archives[0];

        if( !task->cur_def )
        {
            task->cur_texture_id = reference->children.files[task->texture_index].id;
            task->cur_def = RSCacheDat2A_TextureDefinitionNewDecode(
                (const unsigned char*)task->filelist->files[task->texture_index],
                task->filelist->file_sizes[task->texture_index]);
            if( !task->cur_def )
                continue;

            if( task->cur_def->sprite_ids_count > 0 )
            {
                task->cur_packs = calloc(
                    (size_t)task->cur_def->sprite_ids_count,
                    sizeof(struct RSCacheDat2A_SpritePack*));
                if( !task->cur_packs )
                {
                    task_dat2_textures_clear_current(task);
                    continue;
                }
            }
            task->sprite_index = 0;
        }

        for( ; task->cur_def && task->sprite_index < task->cur_def->sprite_ids_count;
             task->sprite_index++ )
        {
            if( task->cur_packs[task->sprite_index] )
                continue;

            IO_REQUEST(ctx, 0, TAPIDat2_FetchSprite(ctx, task->cur_def->sprite_ids[task->sprite_index]));
            PT_YIELD(&task->pt);

            struct RSCacheDat2Disk_Archive* sprite_archive =
                TAPIDat2_DecodeSpriteArchive(ctx, 0, task->cur_def->sprite_ids[task->sprite_index]);
            LibToriRS_IOQueueClear(ctx->io);
            if( !sprite_archive )
                continue;

            task->cur_packs[task->sprite_index] = RSCacheDat2A_SpritePackNewDecode(
                (const unsigned char*)sprite_archive->data,
                sprite_archive->data_size,
                SPRITELOAD_FLAG_NORMALIZE);
            RSCacheDat2Disk_ArchiveFree(sprite_archive);
        }

        if( task->cur_def )
        {
            struct ToriAuxLibCore_Texture* gc_texture =
                ToriAuxLibCache_TextureNewFromDat2Definition(
                    task->cur_def,
                    task->cur_packs,
                    task->cur_def->animation_direction,
                    task->cur_def->animation_speed);
            if( gc_texture )
                ToriAuxLibCache_SubmitTexture(task->cache, task->cur_texture_id, gc_texture);
        }

        task_dat2_textures_clear_current(task);
    }

    if( task->filelist )
    {
        RSCacheShared_FileListFree(task->filelist);
        task->filelist = NULL;
    }

    PT_END(&task->pt);
}

static void
Task_Dat2TexturesLoad_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2TexturesLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2TexturesLoad, base);
    task_dat2_textures_clear_current(task);
    if( task->filelist )
        RSCacheShared_FileListFree(task->filelist);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_textures_load_vtable = {
    .run_fn = Task_Dat2TexturesLoad_Run,
    .free_fn = Task_Dat2TexturesLoad_Free,
};

struct LibToriRS_Task*
Task_Dat2TexturesLoad_New(struct ToriAuxLibCache* cache)
{
    struct Task_Dat2TexturesLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat2_textures_load_vtable;
    task->cache = cache;
    task->cur_texture_id = -1;
    PT_INIT(&task->pt);
    return &task->base;
}
