#include "config_idk.h"

#include "../rsbuf.h"
#include "configs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CacheConfigIdk*
config_idk_new_decode(char* buffer, int buffer_size)
{
    struct CacheConfigIdk* idk = malloc(sizeof(struct CacheConfigIdk));
    config_idk_decode_inplace(idk, buffer, buffer_size);
    return idk;
}

void
config_idk_free(struct CacheConfigIdk* idk)
{
    free(idk);
}

static void
init_idk(struct CacheConfigIdk* idk)
{
    memset(idk, 0, sizeof(struct CacheConfigIdk));
    idk->body_part_id = -1;

    for( int i = 0; i < 10; i++ )
        idk->if_model_ids[i] = -1;
}

void
config_idk_decode_inplace(struct CacheConfigIdk* idk, char* buffer, int buffer_size)
{
    struct RSBuffer rsbuf = { .data = (uint8_t*)buffer, .size = buffer_size, .position = 0 };

    init_idk(idk);

    while( true )
    {
        if( rsbuf.position >= rsbuf.size )
        {
            printf(
                "config_idk_decode_inplace: Buffer position %d exceeded data size %d\n",
                rsbuf.position,
                rsbuf.size);
            return;
        }

        int opcode = g1(&rsbuf);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
            idk->body_part_id = g1(&rsbuf);
            break;
        case 2:
            idk->model_ids_count = g1(&rsbuf);
            idk->model_ids = malloc(idk->model_ids_count * sizeof(int));
            for( int i = 0; i < idk->model_ids_count; i++ )
                idk->model_ids[i] = g2(&rsbuf);
            break;
        case 3:
            idk->is_not_selectable = true;
            break;
        case 40:
            idk->recolor_count = g1(&rsbuf);
            idk->recolors_from = malloc(idk->recolor_count * sizeof(int));
            idk->recolors_to = malloc(idk->recolor_count * sizeof(int));
            for( int i = 0; i < idk->recolor_count; i++ )
            {
                idk->recolors_from[i] = g2(&rsbuf);
                idk->recolors_to[i] = g2(&rsbuf);
            }
            break;
        case 41:
            idk->retexture_count = g1(&rsbuf);
            idk->retextures_from = malloc(idk->retexture_count * sizeof(int));
            idk->retextures_to = malloc(idk->retexture_count * sizeof(int));
            for( int i = 0; i < idk->retexture_count; i++ )
            {
                idk->retextures_from[i] = g2(&rsbuf);
                idk->retextures_to[i] = g2(&rsbuf);
            }
            break;
        case 60 ... 69:
        {
            idk->if_model_ids[opcode - 60] = g2(&rsbuf);
            break;
        }
        }
    }
}

struct CacheConfigIdkTable*
config_idk_table_new(struct Cache* cache)
{
    struct CacheConfigIdkTable* table = malloc(sizeof(struct CacheConfigIdkTable));
    if( !table )
    {
        printf("config_object_table_new: Failed to allocate table\n");
        return NULL;
    }
    memset(table, 0, sizeof(struct CacheConfigIdkTable));

    table->archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_IDENTKIT);
    if( !table->archive )
    {
        printf("config_idk_table_new: Failed to load archive\n");
        goto error;
    }

    table->file_list = filelist_new_from_cache_archive(table->archive);
    if( !table->file_list )
    {
        printf("config_idk_table_new: Failed to load file list\n");
        goto error;
    }

    return table;
error:
    free(table);
    return NULL;
}

void
config_idk_table_free(struct CacheConfigIdkTable* table)
{
    if( !table )
        return;

    filelist_free(table->file_list);
    cache_archive_free(table->archive);

    free(table);
}

struct CacheConfigIdk*
config_idk_table_get(struct CacheConfigIdkTable* table, int id)
{
    if( id < 0 || id > table->file_list->file_count )
    {
        printf("config_idk_table_get: Invalid id %d\n", id);
        return NULL;
    }

    if( table->value )
    {
        // config_idk_free(table->value);
        table->value = NULL;
    }

    table->value = malloc(sizeof(struct CacheConfigIdk));
    memset(table->value, 0, sizeof(struct CacheConfigIdk));

    config_idk_decode_inplace(
        table->value, table->file_list->files[id], table->file_list->file_sizes[id]);

    table->value->_id = id;

    return table->value;
}