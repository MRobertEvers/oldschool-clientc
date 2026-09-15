#include "engine/dat1/dat1_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/texture_palette_bake.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "toridraw_types.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Dat1 textures live in the CONFIGS "textures" jagfile as "<id>.dat" images
 * sharing one "index.dat" — the same layout the media jagfile uses for
 * sprites, so the Pix8 decoder reads them as-is (Pix3D.unpackTextures in the
 * reference does exactly that). Dat2 instead composes each texture from
 * sprite-pack layers named by a texture def; both end at the same indexed
 * bake.
 */

/* Scroll animation is not stored anywhere in a dat1 cache — the reference
 * hardcodes these two ids (water and lava) in Pix3D. */
static void
dat1_texture_anim_params(
    int texture_id,
    int* out_direction,
    int* out_speed)
{
    *out_direction = 0;
    *out_speed = 0;
    if( texture_id == 17 || texture_id == 24 )
    {
        *out_direction = TORIDRAW_TEXANIM_DIRECTION_V_DOWN;
        *out_speed = 2;
    }
}

struct Task_Dat1TextureLoad
{
    struct ToriRS_Task task;
    struct pt pt;
    struct Dat1BuildCache* bc;
    int texture_id;
};

/*
 * Pix8 stores the image trimmed to its non-empty bounds plus the full canvas
 * it belongs in (crop_width/crop_height at crop_x/crop_y) — e.g. texture 17 is
 * a 122x124 image inside the 128x128 texture. Expand it back so the baker sees
 * a square texture; untouched border stays palette index 0 (transparent).
 * Returns an owned index buffer of crop_width * crop_height.
 */
static uint8_t*
task_dat1_texture_expand_crop(const struct RSCache_Dat1Pix8* pix8)
{
    uint8_t* canvas = calloc((size_t)pix8->crop_width * (size_t)pix8->crop_height, 1);
    assert(canvas);

    for( int y = 0; y < pix8->height; y++ )
    {
        int dst_y = y + pix8->crop_y;
        if( dst_y < 0 || dst_y >= pix8->crop_height )
            continue;
        for( int x = 0; x < pix8->width; x++ )
        {
            int dst_x = x + pix8->crop_x;
            if( dst_x < 0 || dst_x >= pix8->crop_width )
                continue;
            canvas[dst_x + dst_y * pix8->crop_width] = pix8->pixels[x + y * pix8->width];
        }
    }
    return canvas;
}

static struct ToriRS_Texture*
task_dat1_texture_decode(struct Task_Dat1TextureLoad* task)
{
    struct RSCache_FileListDat* jagfile = dat1_buildcache_get_textures_jagfile(task->bc);
    struct RSCache_Dat1Pix8* pix8;
    struct ToriRS_Texture* texture;
    uint8_t* canvas;
    char name[16];
    int data_idx;
    int index_idx;
    int dest_size;
    int animation_direction;
    int animation_speed;

    assert(jagfile);

    snprintf(name, sizeof(name), "%d.dat", task->texture_id);
    data_idx = RSCache_FileListDatFindFileByName(jagfile, name);
    index_idx = RSCache_FileListDatFindFileByName(jagfile, "index.dat");
    if( data_idx < 0 || index_idx < 0 )
        return NULL;

    pix8 = RSCache_Dat1Pix8New(
        jagfile->files[data_idx],
        jagfile->file_sizes[data_idx],
        jagfile->files[index_idx],
        jagfile->file_sizes[index_idx],
        0);
    if( !pix8 )
        return NULL;

    /* The canvas, not the trimmed image, is the texture: 64px textures stay 64
     * and 128px stay 128 (the renderer picks its u/v shift per texture). */
    dest_size = pix8->crop_width == 64 ? 64 : 128;
    dat1_texture_anim_params(task->texture_id, &animation_direction, &animation_speed);

    if( getenv("TORIRS_TEX_DEBUG") )
        TORIRS_LOG("dat1_tex: id=%d %dx%d crop=%dx%d at %d,%d palette=%d dest=%d\n",
            task->texture_id,
            pix8->width,
            pix8->height,
            pix8->crop_width,
            pix8->crop_height,
            pix8->crop_x,
            pix8->crop_y,
            pix8->palette_count,
            dest_size);

    canvas = task_dat1_texture_expand_crop(pix8);
    if( !canvas )
    {
        RSCache_Dat1Pix8Free(pix8);
        return NULL;
    }

    texture = ToriRS_TextureBakeIndexed(
        canvas,
        pix8->crop_width,
        pix8->crop_height,
        pix8->palette,
        pix8->palette_count,
        dest_size,
        animation_direction,
        animation_speed,
        0);
    free(canvas);
    RSCache_Dat1Pix8Free(pix8);
    return texture;
}

static int
Task_Dat1TextureLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat1TextureLoad* task = (struct Task_Dat1TextureLoad*)task_base;
    struct ToriRS_Texture* torirs_texture = NULL;

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_textures_jagfile(task->bc) )
    {
        struct RSCache_FileListDat* jagfile = NULL;

        RSCache_IO_Dat1JagfileLoad(io, 0, RSCACHE_DAT1_CONFIG_TEXTURES);
        PT_YIELD(&task->pt);

        jagfile = RSCache_IO_Dat1JagfileDecode(io, 0, RSCACHE_DAT1_CONFIG_TEXTURES);
        if( !jagfile )
        {
            TORIRS_ERR("Failed to decode dat1 textures jagfile for texture %d\n",
                task->texture_id);
            PT_EXIT(&task->pt);
        }

        dat1_buildcache_set_textures_jagfile(task->bc, jagfile);
    }

    torirs_texture = task_dat1_texture_decode(task);
    if( !torirs_texture )
    {
        TORIRS_ERR("Failed to decode dat1 texture %d\n", task->texture_id);
        PT_EXIT(&task->pt);
    }

    CacheProvider_TextureAdd(&task->bc->base, task->texture_id, torirs_texture);

    PT_END(&task->pt);
}

static void
Task_Dat1TextureLoad_Free(struct ToriRS_Task* task_base)
{
    free(task_base);
}

static struct ToriRS_TaskVTable Task_Dat1TextureLoad_VTable = {
    .run = Task_Dat1TextureLoad_Run,
    .free = Task_Dat1TextureLoad_Free,
};

struct ToriRS_Task*
CreateTask_Dat1TextureLoad(
    struct CacheProvider* provider,
    int texture_id)
{
    struct Task_Dat1TextureLoad* task;

    assert(provider);

    if( CacheProvider_TextureHas(provider, texture_id) )
        return NULL;

    task = calloc(1, sizeof(*task));
    assert(task);
    task->task.vtable = &Task_Dat1TextureLoad_VTable;
    strncpy(task->task.name, "Dat1TextureLoad", sizeof(task->task.name) - 1);
    task->bc = (struct Dat1BuildCache*)provider;
    task->texture_id = texture_id;
    PT_INIT(&task->pt);
    return &task->task;
}
