#include "task_dat2_textures_load.h"

#include "buildcache/dat2_buildcache.h"
#include "core/tapi/tapi_dat2.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2a/dat2a_textures.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"

#include <stdio.h>
#include <stdlib.h>

static void
task_dat2_textures_clear_current(struct Task_Dat2TexturesLoad* task)
{
    if( !task )
        return;

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

struct Task_Dat2TexturesLoad*
Task_Dat2TexturesLoad_New(struct ToriAuxLibCache* cache)
{
    struct Task_Dat2TexturesLoad* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache = cache;
    task->cur_texture_id = -1;
    return task;
}

void
Task_Dat2TexturesLoad_Free(struct Task_Dat2TexturesLoad* task)
{
    if( !task )
        return;
    task_dat2_textures_clear_current(task);
    if( task->filelist )
        RSCacheShared_FileListFree(task->filelist);
    free(task);
}

int
Task_Dat2TexturesLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2TexturesLoad* task = task_state;
    struct Dat2BuildCache* bc = NULL;
    struct RSCacheDat2Disk_ReferenceTable* textures_table = NULL;
    struct RSCacheDat2Disk_ArchiveReference* reference = NULL;
    struct RSCacheDat2Disk_Archive* textures_archive = NULL;
    int sprite_id = 0;

    PT_BEGIN(&task->thread);

    if( !task->cache || ToriAuxLibCache_Mode(task->cache) != TORIAUXLIBCACHE_MODE_DAT2 )
        PT_EXIT(&task->thread);

    bc = dat2(task->cache);
    if( !bc )
        PT_EXIT(&task->thread);

    DAT2_ENSURE_REFERENCE_TABLE(
        ctx, &task->thread, bc, RSCacheDat2Disk_Table_Textures);

    textures_table = dat2_buildcache_reference_table_get(bc, RSCacheDat2Disk_Table_Textures);
    if( !textures_table || textures_table->archive_count <= 0 )
        PT_EXIT(&task->thread);

    reference = &textures_table->archives[0];

    IO_REQUEST(ctx, 0, TAPIDat2_FetchArchive(ctx, RSCacheDat2Disk_Table_Textures, 0));
    PT_YIELD(&task->thread);

    textures_archive = TAPIDat2_DecodeArchive(ctx, 0, RSCacheDat2Disk_Table_Textures, 0);
    if( !textures_archive )
        PT_EXIT(&task->thread);

    task->filelist = RSCacheShared_FileListNewFromCacheArchive(textures_archive);
    RSCacheDat2Disk_ArchiveFree(textures_archive);
    LibToriRS_IOQueueClear(ctx->io);
    if( !task->filelist )
        PT_EXIT(&task->thread);

    task->texture_count = reference->children.count;
    if( task->filelist->file_count < task->texture_count )
        task->texture_count = task->filelist->file_count;

    for( ; task->texture_index < task->texture_count; task->texture_index++ )
    {
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
            sprite_id = task->cur_def->sprite_ids[task->sprite_index];
            if( task->cur_packs[task->sprite_index] )
                continue;

            IO_REQUEST(ctx, 0, TAPIDat2_FetchSprite(ctx, sprite_id));
            PT_YIELD(&task->thread);

            struct RSCacheDat2Disk_Archive* sprite_archive =
                TAPIDat2_DecodeSpriteArchive(ctx, 0, sprite_id);
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
            struct ToriAuxLibCore_Texture* gc_texture = ToriAuxLibCache_TextureNewFromDat2Definition(
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

    PT_END(&task->thread);
}
