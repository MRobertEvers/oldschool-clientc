#include "engine/dat2/dat2_tasks.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_types.h"

#include "asyncio.h"
#include "cache/rscache_io.h"
#include "osrs/palette.h"

#include <assert.h>
#include <math.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct TextureLayer
{
    const uint8_t* palette_pixels;
    int width;
    int height;
    const int* palette; /* RGB entries */
    int palette_length;
    int blend_type; /* from dat2 sprite_types; 0 = replace */
};

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
gamma_blend(
    int rgb,
    double gamma)
{
    double r = (rgb >> 16) / 256.0;
    double g = ((rgb >> 8) & 255) / 256.0;
    double b = (rgb & 255) / 256.0;
    r = pow(r, gamma);
    g = pow(g, gamma);
    b = pow(b, gamma);
    return ((int)(r * 256.0) << 16) | ((int)(g * 256.0) << 8) | (int)(b * 256.0);
}

static int
average_hsl_from_texels(
    const int* texels,
    int width,
    int height)
{
    int red = 0;
    int green = 0;
    int blue = 0;
    int colour_count = width * height;
    int i;

    assert(texels && width > 0 && height > 0);

    for( i = 0; i < colour_count; i++ )
    {
        red += (texels[i] >> 16) & 0xff;
        green += (texels[i] >> 8) & 0xff;
        blue += texels[i] & 0xff;
    }
    return palette_rgb_to_hsl16(
        ((red / colour_count) << 16) + ((green / colour_count) << 8) + (blue / colour_count));
}

static struct ToriRS_Texture*
texture_bake(
    const struct TextureLayer* layers,
    int layer_count,
    int dest_size,
    int animation_direction,
    int animation_speed,
    int average_hsl)
{
    struct ToriRS_Texture* texture;
    int* pixels;
    bool opaque;
    int i;

    assert(layers);
    assert(layer_count > 0);
    assert(dest_size == 64 || dest_size == 128);

    pixels = calloc((size_t)dest_size * (size_t)dest_size, sizeof(*pixels));
    if( !pixels )
        return NULL;

    opaque = true;

    for( i = 0; i < layer_count; i++ )
    {
        const struct TextureLayer* layer = &layers[i];
        int* adjusted_palette;
        int pi;
        int pixel_index;

        assert(layer->palette_pixels);
        assert(layer->palette);
        assert(layer->palette_length > 0);
        assert(layer->width > 0 && layer->height > 0);

        adjusted_palette = malloc((size_t)layer->palette_length * sizeof(*adjusted_palette));
        if( !adjusted_palette )
        {
            free(pixels);
            return NULL;
        }

        for( pi = 0; pi < layer->palette_length; pi++ )
        {
            int alpha = 0xff;
            if( (layer->palette[pi] & 0xf8f8ff) == 0 )
                alpha = 0;
            adjusted_palette[pi] = (alpha << 24) | gamma_blend(layer->palette[pi], 0.8);
        }

        for( pixel_index = 0; pixel_index < layer->width * layer->height; pixel_index++ )
        {
            int palette_index = layer->palette_pixels[pixel_index];
            assert(palette_index >= 0 && palette_index < layer->palette_length);
            if( (adjusted_palette[palette_index] & 0xf8f8ff) == 0 )
                opaque = false;
        }

        /* Only blend_type 0 (replace) is implemented, matching legacy decode. */
        if( layer->blend_type == 0 )
        {
            if( dest_size == layer->width )
            {
                for( pixel_index = 0; pixel_index < layer->width * layer->height; pixel_index++ )
                {
                    int palette_index = layer->palette_pixels[pixel_index];
                    pixels[pixel_index] = adjusted_palette[palette_index];
                }
            }
            else if( layer->width == 64 && dest_size == 128 )
            {
                int x;
                int y;
                pixel_index = 0;
                for( x = 0; x < dest_size; x++ )
                {
                    for( y = 0; y < dest_size; y++ )
                    {
                        int palette_index = layer->palette_pixels[((x >> 1) << 6) + (y >> 1)];
                        pixels[pixel_index++] = adjusted_palette[palette_index];
                    }
                }
            }
            else if( layer->width == 128 && dest_size == 64 )
            {
                int x;
                int y;
                pixel_index = 0;
                for( x = 0; x < dest_size; x++ )
                {
                    for( y = 0; y < dest_size; y++ )
                    {
                        int palette_index =
                            layer->palette_pixels[(y << 1) + ((x << 1) << 7)];
                        pixels[pixel_index++] = adjusted_palette[palette_index];
                    }
                }
            }
        }

        free(adjusted_palette);
    }

    texture = calloc(1, sizeof(*texture));
    if( !texture )
    {
        free(pixels);
        return NULL;
    }

    texture->texels = pixels;
    texture->width = dest_size;
    texture->height = dest_size;
    texture->opaque = opaque;
    texture->animation_direction = animation_direction;
    texture->animation_speed = animation_speed;
    texture->average_hsl = average_hsl;
    if( texture->average_hsl == 0 )
        texture->average_hsl = average_hsl_from_texels(pixels, dest_size, dest_size);

    return texture;
}

static struct ToriRS_Texture*
texture_from_sprite_packs(
    struct RSCache_Dat2SpritePack* const* packs,
    const struct RSCache_Dat2Texture* def,
    int dest_size)
{
    struct TextureLayer* layers;
    struct ToriRS_Texture* texture;
    int i;

    assert(packs);
    assert(def);
    assert(def->sprite_ids_count > 0);
    assert(dest_size == 64 || dest_size == 128);

    layers = calloc((size_t)def->sprite_ids_count, sizeof(*layers));
    if( !layers )
        return NULL;

    for( i = 0; i < def->sprite_ids_count; i++ )
    {
        struct RSCache_Dat2SpritePack* pack = packs[i];
        struct RSCache_Dat2Sprite* sprite;

        assert(pack);
        assert(pack->count > 0);
        sprite = &pack->sprites[0];

        layers[i].palette_pixels = sprite->palette_pixels;
        layers[i].width = sprite->width;
        layers[i].height = sprite->height;
        layers[i].palette = pack->palette;
        layers[i].palette_length = pack->palette_length;
        layers[i].blend_type = 0;
        if( i > 0 && def->sprite_types )
            layers[i].blend_type = def->sprite_types[i - 1];
    }

    texture = texture_bake(
        layers,
        def->sprite_ids_count,
        dest_size,
        def->animation_direction,
        def->animation_speed,
        def->average_hsl);
    free(layers);
    return texture;
}

static int
Task_Dat2TextureLoad_Run(
    struct ToriRS_Task* task_base,
    struct ToriRS_IO* io)
{
    struct Task_Dat2TextureLoad* task = (struct Task_Dat2TextureLoad*)task_base;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct ToriRS_Texture* torirs_texture = NULL;

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

    torirs_texture = texture_from_sprite_packs(task->packs, task->def, 128);
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
