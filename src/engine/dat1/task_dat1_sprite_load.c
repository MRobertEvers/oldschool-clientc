#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat1/dat1_tasks.h"
#include "engine/title_panel.h"
#include "engine/torirs_sprite_from_rscache.h"
#include "engine/torirs_types.h"

#include "asyncio.h"
#include "cache/rscache_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

struct Task_Dat1SpriteLoadFromSource
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;

    char name[64];
    char format[16];
    char data_filename[64];
    char index_filename[64];
    int atlas_index;
    int atlas_count;
    int crop_x;
    int crop_y;
    int crop_width;
    int crop_height;
    char transform[4][64];
    int transform_count;
    /** revconfig `archive=`: which dat1 jagfile holds the member. "title"
     *  selects the title-and-fonts archive; anything else means the media
     *  archive, which is where every sprite lived before the title screen. */
    char archive[32];
    /** Resolved from `archive` on the first step. */
    int from_title_archive;
};

static int
Task_Dat1SpriteLoadFromSource_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1SpriteLoadFromSource* task = (struct Task_Dat1SpriteLoadFromSource*)task_base;
    struct ToriRS_Sprite* sprite = NULL;
    int sprite_id;

    PT_BEGIN(&task->pt);

    /*
     * The title screen's art -- titlebox, titlebutton, the runes, the logo --
     * lives in the title-and-fonts archive rather than the media one, and is
     * the only reason `archive=` has ever had to mean anything here. That
     * archive is normally already decoded and cached: the fonts come out of it
     * and they load first, so this is a hit.
     */
    task->from_title_archive = strcmp(task->archive, "title") == 0;

    if( task->from_title_archive )
    {
        if( !dat1_buildcache_get_title_fonts_jagfile(task->bc) )
        {
            struct RSCache_FileListDat* title_jagfile = NULL;

            RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS);
            PT_YIELD(&task->pt);

            title_jagfile =
                RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_TITLE_AND_FONTS);
            if( !title_jagfile )
            {
                TORIRS_ERR("Failed to decode dat1 title jagfile for sprite %s\n", task->name);
                PT_EXIT(&task->pt);
            }

            dat1_buildcache_set_title_fonts_jagfile(task->bc, title_jagfile);
        }
    }
    else if( !dat1_buildcache_get_media_2d_graphics_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* media_jagfile = NULL;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_MEDIA_2D);
        PT_YIELD(&task->pt);

        media_jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_MEDIA_2D);
        if( !media_jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 media jagfile for sprite %s\n", task->name);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_media_2d_graphics_jagfile(task->bc, media_jagfile);
    }

    if( strcmp(task->format, TORIRS_TITLE_PANEL_FORMAT) == 0 )
    {
        /* One more decoder behind `format=`, beside pix8 and pix32: this
         * member is a JPEG holding half the title screen, and the panel is
         * that half plus its mirror. @see engine/title_panel.h. */
        struct RSCache_FileListDat* jagfile =
            task->from_title_archive ? dat1_buildcache_get_title_fonts_jagfile(task->bc)
                                     : dat1_buildcache_get_media_2d_graphics_jagfile(task->bc);
        int member = RSCache_FileListDatFindFileByName(jagfile, task->data_filename);
        if( member < 0 )
        {
            TORIRS_ERR("dat1 sprite %s: no member '%s' in the archive\n",
                task->name,
                task->data_filename);
            PT_EXIT(&task->pt);
        }
        sprite =
            ToriRS_TitlePanelFromJpeg(jagfile->files[member], jagfile->file_sizes[member]);
    }
    else
    {
        sprite = ToriRS_SpriteFromDat1Jagfile(
            task->from_title_archive ? dat1_buildcache_get_title_fonts_jagfile(task->bc)
                                     : dat1_buildcache_get_media_2d_graphics_jagfile(task->bc),
            task->format,
            task->data_filename,
            task->index_filename,
            task->atlas_index,
            task->atlas_count);
    }
    if( !sprite )
    {
        TORIRS_ERR("Failed to decode dat1 sprite %s (%s)\n", task->name, task->data_filename);
        PT_EXIT(&task->pt);
    }

    ToriRS_SpriteApplyCrop(
        sprite, task->crop_x, task->crop_y, task->crop_width, task->crop_height);
    for( int i = 0; i < task->transform_count; i++ )
        ToriRS_SpriteApplyTransform(sprite, task->transform[i]);

    strncpy(sprite->name, task->name, sizeof(sprite->name) - 1);
    sprite->name[sizeof(sprite->name) - 1] = '\0';

    sprite_id = dat1_buildcache_sprite_id_alloc(task->bc);
    CacheProvider_SpriteAdd(&task->bc->base, sprite_id, sprite);
    CacheProvider_SpriteNameMapPut(&task->bc->base, task->name, sprite_id);

    PT_END(&task->pt);
}

static void
Task_Dat1SpriteLoadFromSource_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1SpriteLoadFromSource_VTable = {
    .run = Task_Dat1SpriteLoadFromSource_Run,
    .free = Task_Dat1SpriteLoadFromSource_Free,
};

struct ToriRS_Task*
CreateTask_Dat1SpriteLoadFromSource(
    struct CacheProvider* provider,
    struct CacheProviderSpriteSource const* source)
{
    struct Dat1BuildCache* dat1_buildcache;
    struct Task_Dat1SpriteLoadFromSource* task;
    int existing_id;

    assert(provider);
    assert(source);
    assert(source->name && source->name[0]);

    dat1_buildcache = (struct Dat1BuildCache*)provider;
    existing_id = CacheProvider_SpriteIdByName(provider, source->name);
    if( existing_id >= 0 && CacheProvider_SpriteHas(provider, existing_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1SpriteLoadFromSource_VTable;
    strcpy(task->task.name, "Dat1SpriteLoadFromSource");
    task->bc = dat1_buildcache;
    strncpy(task->name, source->name, sizeof(task->name) - 1);
    if( source->format )
        strncpy(task->format, source->format, sizeof(task->format) - 1);
    if( source->data_filename )
        strncpy(task->data_filename, source->data_filename, sizeof(task->data_filename) - 1);
    if( source->index_filename && source->index_filename[0] )
        strncpy(task->index_filename, source->index_filename, sizeof(task->index_filename) - 1);
    else
        strcpy(task->index_filename, "index.dat");
    if( source->archive )
        strncpy(task->archive, source->archive, sizeof(task->archive) - 1);
    task->atlas_index = source->atlas_index;
    task->atlas_count = source->atlas_count;
    task->crop_x = source->crop_x;
    task->crop_y = source->crop_y;
    task->crop_width = source->crop_width;
    task->crop_height = source->crop_height;
    task->transform_count = source->transform_count > 4 ? 4 : source->transform_count;
    for( int i = 0; i < task->transform_count; i++ )
    {
        strncpy(task->transform[i], source->transform[i], sizeof(task->transform[i]) - 1);
        task->transform[i][sizeof(task->transform[i]) - 1] = '\0';
    }
    PT_INIT(&task->pt);
    return &task->task;
}
