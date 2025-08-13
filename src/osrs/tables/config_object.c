#include "config_object.h"

#include "../rsbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// public String name = "null";
// public String examine;
// public String unknown1;

// public int resizeX = 128;
// public int resizeY = 128;
// public int resizeZ = 128;

// public int xan2d = 0;
// public int yan2d = 0;
// public int zan2d = 0;

// public int cost = 1;
// public boolean isTradeable;
// public int stackable = 0;
// public int inventoryModel;

// public int wearPos1 = -1;
// public int wearPos2 = -1;
// public int wearPos3 = -1;

// public boolean members = false;

// public short[] colorFind;
// public short[] colorReplace;
// public short[] textureFind;
// public short[] textureReplace;

// public int zoom2d = 2000;
// public int xOffset2d = 0;
// public int yOffset2d = 0;

// public int ambient;
// public int contrast;

// public int[] countCo;
// public int[] countObj;

// public String[] options = new String[]{null, null, "Take", null, null};
// public String[][] subops;

// public String[] interfaceOptions = new String[]{null, null, null, null, "Drop"};

// public int maleModel0 = -1;
// public int maleModel1 = -1;
// public int maleModel2 = -1;
// public int maleOffset;
// public int maleHeadModel = -1;
// public int maleHeadModel2 = -1;

// public int femaleModel0 = -1;
// public int femaleModel1 = -1;
// public int femaleModel2 = -1;
// public int femaleOffset;
// public int femaleHeadModel = -1;
// public int femaleHeadModel2 = -1;

// public int category;

// public int notedID = -1;
// public int notedTemplate = -1;

// public int team;
// public int weight;

// public int shiftClickDropIndex = -2;

// public int boughtId = -1;
// public int boughtTemplateId = -1;

// public int placeholderId = -1;
// public int placeholderTemplateId = -1;

static void
init_object(struct CacheConfigObject* object)
{
    memset(object, 0, sizeof(struct CacheConfigObject));

    object->name = malloc(5);
    strcpy(object->name, "null");

    object->examine = NULL;

    object->resize_x = 128;
    object->resize_y = 128;
    object->resize_z = 128;
    object->xan2d = 0;
    object->yan2d = 0;
    object->zan2d = 0;
    object->cost = 1;

    object->tradeable = false;
    object->stacking_behaviour = 0;
    object->inventory_model_id = 0;
    object->wearpos_1 = -1;
    object->wearpos_2 = -1;
    object->wearpos_3 = -1;
    object->is_members = false;

    object->zoom2d = 2000;
    object->offset_x2d = 0;
    object->offset_y2d = 0;

    object->ambient = 0;
    object->contrast = 0;

    object->male_model_0 = -1;
    object->male_model_1 = -1;
    object->male_model_2 = -1;
    object->male_offset = 0;
    object->male_head_model = -1;
    object->male_head_model_2 = -1;

    object->female_model_0 = -1;
    object->female_model_1 = -1;
    object->female_model_2 = -1;
    object->female_offset = 0;
    object->female_head_model = -1;
    object->female_head_model_2 = -1;

    object->category = 0;
    object->noted_id = -1;
    object->noted_template = -1;
    object->team = 0;
    object->weight = 0;
    object->shift_click_drop_index = -2;
    object->bought_id = -1;
    object->bought_template_id = -1;
    object->placeholder_id = -1;
    object->placeholder_template_id = -1;
}

struct CacheConfigObject*
config_object_new_decode(char* buffer, int buffer_size)
{
    struct CacheConfigObject* object = malloc(sizeof(struct CacheConfigObject));
    init_object(object);
    config_object_decode_inplace(object, buffer, buffer_size);
    return object;
}

void
config_object_free(struct CacheConfigObject* object)
{
    free(object->name);
    free(object->examine);
    free(object);
}

void
config_object_decode_inplace(struct CacheConfigObject* object, char* data, int data_size)
{
    struct RSBuffer buffer = { .data = data, .size = data_size };

    init_object(object);

    while( true )
    {
        if( buffer.position >= buffer.size )
        {
            printf(
                "config_object_decode_inplace: Buffer position %d exceeded data size %d\n",
                buffer.position,
                buffer.size);
            return;
        }

        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            return;
        }

        switch( opcode )
        {
        case 1:
        {
            object->inventory_model_id = g2(&buffer);
            break;
        }
        case 2:
        {
            object->name = gstring(&buffer);
            break;
        }
        case 3:
        {
            object->examine = gstring(&buffer);
            break;
        }
        case 4:
        {
            object->zoom2d = g2(&buffer);
            break;
        }
        case 5:
        {
            object->xan2d = g2(&buffer);
            break;
        }
        case 6:
        {
            object->yan2d = g2(&buffer);
            break;
        }
        case 7:
        {
            object->offset_x2d = g2b(&buffer);
            break;
        }
        case 8:
        {
            object->offset_y2d = g2b(&buffer);
            break;
        }
        case 9:
        {
            gstring(&buffer);
            break;
        }
        case 11:
        {
            object->stacking_behaviour = 1;
            break;
        }
        case 12:
        {
            object->cost = g4(&buffer);
            break;
        }
        case 13:
        {
            object->wearpos_1 = g1b(&buffer);
            break;
        }
        case 14:
        {
            object->wearpos_2 = g1b(&buffer);
            break;
        }
        case 16:
        {
            object->is_members = true;
            break;
        }
        case 23:
        {
            object->male_model_0 = g2(&buffer);
            object->male_offset = g1(&buffer);
            break;
        }
        case 24:
        {
            object->male_model_1 = g2(&buffer);
            break;
        }
        case 25:
        {
            object->female_model_0 = g2(&buffer);
            object->female_offset = g1(&buffer);
            break;
        }
        case 26:
        {
            object->female_model_1 = g2(&buffer);
            break;
        }
        case 27:
        {
            object->wearpos_3 = g1(&buffer);
            break;
        }
        case 30 ... 34:
        {
            object->actions[opcode - 30] = gstring(&buffer);
            if( strcasecmp(object->actions[opcode - 30], "Hidden") == 0 )
            {
                free(object->actions[opcode - 30]);
                object->actions[opcode - 30] = NULL;
            }
            break;
        }
        case 35 ... 39:
        {
            object->if_actions[opcode - 35] = gstring(&buffer);

            break;
        }
        case 40:
        {
            int recolor_count = g1(&buffer);
            object->recolors_from = malloc(recolor_count * sizeof(int));
            object->recolors_to = malloc(recolor_count * sizeof(int));
            for( int i = 0; i < recolor_count; i++ )
            {
                object->recolors_from[i] = g2(&buffer);
                object->recolors_to[i] = g2(&buffer);
            }
            object->recolor_count = recolor_count;
            break;
        }
        case 41:
        {
            int retexture_count = g1(&buffer);
            object->retextures_from = malloc(retexture_count * sizeof(int));
            object->retextures_to = malloc(retexture_count * sizeof(int));
            for( int i = 0; i < retexture_count; i++ )
            {
                object->retextures_from[i] = g2(&buffer);
                object->retextures_to[i] = g2(&buffer);
            }
            object->retexture_count = retexture_count;
            break;
        }
        case 42:
        {
            object->shift_click_drop_index = g1b(&buffer);
            break;
        }
        case 43:
        {
            int action_id = g1(&buffer);
            bool valid = action_id >= 0 && action_id < 5;
            if( valid && !object->sub_actions[action_id] )
            {
                object->sub_actions[action_id] = (char**)malloc(20 * sizeof(char*));
                memset(object->sub_actions[action_id], 0, 20 * sizeof(char*));
            }

            while( true )
            {
                int sub_action_id = g1(&buffer) - 1;
                if( sub_action_id == -1 )
                {
                    break;
                }
                char* string = gstring(&buffer);
                if( valid && sub_action_id >= 0 && sub_action_id < 20 )
                {
                    object->sub_actions[action_id][sub_action_id] = string;
                }
                else
                {
                    free(string);
                }
            }
            break;
        }
        case 65:
        {
            object->tradeable = true;
            break;
        }
        case 75:
        {
            object->weight = g2b(&buffer);
            break;
        }
        case 78:
        {
            object->male_model_2 = g2(&buffer);
            break;
        }
        case 79:
        {
            object->female_model_2 = g2(&buffer);
            break;
        }
        case 90:
        {
            object->male_head_model = g2(&buffer);
            break;
        }
        case 91:
        {
            object->female_head_model = g2(&buffer);
            break;
        }
        case 92:
        {
            object->male_head_model_2 = g2(&buffer);
            break;
        }
        case 93:
        {
            object->female_head_model_2 = g2(&buffer);
            break;
        }
        case 94:
        {
            object->category = g2(&buffer);
            break;
        }
        case 95:
        {
            object->zan2d = g2(&buffer);
            break;
        }
        case 97:
        {
            object->noted_id = g2(&buffer);
            break;
        }
        case 98:
        {
            object->noted_template = g2(&buffer);
            break;
        }
        case 100 ... 109:
        {
            object->count_obj[opcode - 100] = g2(&buffer);
            object->count_co[opcode - 100] = g2(&buffer);
            break;
        }
        case 110:
        {
            object->resize_x = g2(&buffer);
            break;
        }
        case 111:
        {
            object->resize_y = g2(&buffer);
            break;
        }
        case 112:
        {
            object->resize_z = g2(&buffer);
            break;
        }
        case 113:
        {
            object->ambient = g2(&buffer);
            break;
        }
        case 114:
        {
            object->contrast = g2(&buffer);
            break;
        }
        case 115:
        {
            object->team = g2(&buffer);
            break;
        }
        case 139:
        {
            object->bought_id = g2(&buffer);
            break;
        }
        case 140:
        {
            object->bought_template_id = g2(&buffer);
            break;
        }
        case 148:
        {
            object->placeholder_id = g2(&buffer);
            break;
        }
        case 149:
        {
            object->placeholder_template_id = g2(&buffer);
            break;
        }
        case 249:
        {
            rsbuf_read_params(&buffer, &object->params);
            break;
        }
        default:
        {
            printf("ObjectType: Opcode %d not implemented\n", opcode);
            break;
        }
        }
    }
}

struct CacheConfigObjectTable*
config_object_table_new(struct Cache* cache)
{
    struct CacheConfigObjectTable* table = malloc(sizeof(struct CacheConfigObjectTable));
    if( !table )
    {
        printf("config_object_table_new: Failed to allocate table\n");
        return NULL;
    }
    memset(table, 0, sizeof(struct CacheConfigObjectTable));

    table->archive = cache_archive_new_load(cache, CACHE_CONFIGS, CONFIG_OBJECT);
    if( !table->archive )
    {
        printf("config_object_table_new: Failed to load archive\n");
        goto error;
    }

    table->file_list = filelist_new_from_cache_archive(table->archive);
    if( !table->file_list )
    {
        printf("config_object_table_new: Failed to load file list\n");
        goto error;
    }

    return table;
error:
    free(table);
    return NULL;
}

void
config_object_table_free(struct CacheConfigObjectTable* table)
{
    filelist_free(table->file_list);
    cache_archive_free(table->archive);
    free(table);
}

struct CacheConfigObject*
config_object_table_get_new(struct CacheConfigObjectTable* table, int id)
{
    if( id < 0 || id > table->file_list->file_count )
    {
        printf("config_object_table_get: Invalid id %d\n", id);
        return NULL;
    }

    if( table->value )
    {
        // config_object_free(table->value);
        table->value = NULL;
    }

    table->value = malloc(sizeof(struct CacheConfigObject));
    memset(table->value, 0, sizeof(struct CacheConfigObject));

    config_object_decode_inplace(
        table->value, table->file_list->files[id], table->file_list->file_sizes[id]);

    table->value->_id = id;

    return table->value;
}