#include "dat2_config_obj.h"

#include "../rsbuffer.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
init_object(struct RSCache_Dat2ConfigObj* object);

uint32_t
RSCache_Dat2ConfigObjEncode(
    const struct RSCache_Dat2ConfigObj* object,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !object || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Compare against the decode defaults so a field the packer omitted stays
     * omitted. Opcodes are written in ascending order, which is how the packer
     * emits them, so most records come back byte-exact. */
    struct RSCache_Dat2ConfigObj defaults;
    init_object(&defaults);

#define OBJ_P_IF(condition, opcode_value, write_body)                                              \
    do                                                                                             \
    {                                                                                              \
        if( condition )                                                                            \
        {                                                                                          \
            p1(&buffer, (opcode_value));                                                           \
            write_body;                                                                            \
        }                                                                                          \
    } while( 0 )

    OBJ_P_IF(
        object->inventory_model_id != defaults.inventory_model_id,
        1,
        p2(&buffer, object->inventory_model_id));

    /* The default name is the literal "null"; a record that carries that name is
     * indistinguishable from one that omitted opcode 2. */
    if( object->name && strcmp(object->name, "null") != 0 )
    {
        p1(&buffer, 2);
        pjstr(&buffer, object->name, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( object->examine )
    {
        p1(&buffer, 3);
        pjstr(&buffer, object->examine, RSCACHE_JSTR_TERMINATOR_NULL);
    }

    OBJ_P_IF(object->zoom2d != defaults.zoom2d, 4, p2(&buffer, object->zoom2d));
    OBJ_P_IF(object->xan2d != defaults.xan2d, 5, p2(&buffer, object->xan2d));
    OBJ_P_IF(object->yan2d != defaults.yan2d, 6, p2(&buffer, object->yan2d));
    OBJ_P_IF(object->offset_x2d != defaults.offset_x2d, 7, p2b(&buffer, object->offset_x2d));
    OBJ_P_IF(object->offset_y2d != defaults.offset_y2d, 8, p2b(&buffer, object->offset_y2d));
    /* Opcode 9 is a string the decoder discards, so it cannot be reproduced. */
    if( object->stacking_behaviour == 1 )
        p1(&buffer, 11);
    OBJ_P_IF(object->cost != defaults.cost, 12, p4(&buffer, object->cost));
    OBJ_P_IF(object->wearpos_1 != defaults.wearpos_1, 13, p1b(&buffer, object->wearpos_1));
    OBJ_P_IF(object->wearpos_2 != defaults.wearpos_2, 14, p1b(&buffer, object->wearpos_2));
    if( object->is_members )
        p1(&buffer, 16);

    /* Opcode 23 carries the model *and* its offset as one record, so it has to be
     * emitted when either differs from the default. */
    if( object->male_model_0 != defaults.male_model_0 ||
        object->male_offset != defaults.male_offset )
    {
        p1(&buffer, 23);
        p2(&buffer, object->male_model_0);
        p1(&buffer, object->male_offset);
    }
    OBJ_P_IF(object->male_model_1 != defaults.male_model_1, 24, p2(&buffer, object->male_model_1));
    if( object->female_model_0 != defaults.female_model_0 ||
        object->female_offset != defaults.female_offset )
    {
        p1(&buffer, 25);
        p2(&buffer, object->female_model_0);
        p1(&buffer, object->female_offset);
    }
    OBJ_P_IF(
        object->female_model_1 != defaults.female_model_1, 26, p2(&buffer, object->female_model_1));
    OBJ_P_IF(object->wearpos_3 != defaults.wearpos_3, 27, p1(&buffer, object->wearpos_3));

    /* A NULL action is either an absent opcode or the literal "Hidden", which the
     * decoder normalises to NULL. Both re-encode as absent. */
    for( int i = 0; i < 5; i++ )
    {
        if( object->actions[i] )
        {
            p1(&buffer, 30 + i);
            pjstr(&buffer, object->actions[i], RSCACHE_JSTR_TERMINATOR_NULL);
        }
    }
    for( int i = 0; i < 5; i++ )
    {
        if( object->if_actions[i] )
        {
            p1(&buffer, 35 + i);
            pjstr(&buffer, object->if_actions[i], RSCACHE_JSTR_TERMINATOR_NULL);
        }
    }

    if( object->recolor_count > 0 )
    {
        p1(&buffer, 40);
        p1(&buffer, object->recolor_count);
        for( int i = 0; i < object->recolor_count; i++ )
        {
            p2(&buffer, object->recolors_from[i]);
            p2(&buffer, object->recolors_to[i]);
        }
    }
    if( object->retexture_count > 0 )
    {
        p1(&buffer, 41);
        p1(&buffer, object->retexture_count);
        for( int i = 0; i < object->retexture_count; i++ )
        {
            p2(&buffer, object->retextures_from[i]);
            p2(&buffer, object->retextures_to[i]);
        }
    }

    OBJ_P_IF(
        object->shift_click_drop_index != defaults.shift_click_drop_index,
        42,
        p1b(&buffer, object->shift_click_drop_index));

    /* Opcode 43: sub-action lists, each terminated by a zero index. */
    for( int action = 0; action < 5; action++ )
    {
        if( !object->sub_actions[action] )
            continue;

        int highest = -1;
        for( int sub = 0; sub < 20; sub++ )
        {
            if( object->sub_actions[action][sub] )
                highest = sub;
        }
        if( highest < 0 )
            continue;

        p1(&buffer, 43);
        p1(&buffer, action);
        for( int sub = 0; sub <= highest; sub++ )
        {
            if( !object->sub_actions[action][sub] )
                continue;
            /* Indices are stored one-based; zero terminates the list. */
            p1(&buffer, sub + 1);
            pjstr(&buffer, object->sub_actions[action][sub], RSCACHE_JSTR_TERMINATOR_NULL);
        }
        p1(&buffer, 0);
    }

    if( object->tradeable )
        p1(&buffer, 65);
    OBJ_P_IF(object->weight != defaults.weight, 75, p2b(&buffer, object->weight));
    OBJ_P_IF(object->male_model_2 != defaults.male_model_2, 78, p2(&buffer, object->male_model_2));
    OBJ_P_IF(
        object->female_model_2 != defaults.female_model_2, 79, p2(&buffer, object->female_model_2));
    OBJ_P_IF(
        object->male_head_model != defaults.male_head_model,
        90,
        p2(&buffer, object->male_head_model));
    OBJ_P_IF(
        object->female_head_model != defaults.female_head_model,
        91,
        p2(&buffer, object->female_head_model));
    OBJ_P_IF(
        object->male_head_model_2 != defaults.male_head_model_2,
        92,
        p2(&buffer, object->male_head_model_2));
    OBJ_P_IF(
        object->female_head_model_2 != defaults.female_head_model_2,
        93,
        p2(&buffer, object->female_head_model_2));
    OBJ_P_IF(object->category != defaults.category, 94, p2(&buffer, object->category));
    OBJ_P_IF(object->zan2d != defaults.zan2d, 95, p2(&buffer, object->zan2d));
    OBJ_P_IF(object->noted_id != defaults.noted_id, 97, p2(&buffer, object->noted_id));
    OBJ_P_IF(
        object->noted_template != defaults.noted_template, 98, p2(&buffer, object->noted_template));

    for( int i = 0; i < 10; i++ )
    {
        if( object->count_obj[i] != 0 || object->count_co[i] != 0 )
        {
            p1(&buffer, 100 + i);
            p2(&buffer, object->count_obj[i]);
            p2(&buffer, object->count_co[i]);
        }
    }

    OBJ_P_IF(object->resize_x != defaults.resize_x, 110, p2(&buffer, object->resize_x));
    OBJ_P_IF(object->resize_y != defaults.resize_y, 111, p2(&buffer, object->resize_y));
    OBJ_P_IF(object->resize_z != defaults.resize_z, 112, p2(&buffer, object->resize_z));
    OBJ_P_IF(object->ambient != defaults.ambient, 113, p1b(&buffer, object->ambient));
    /* The decoder multiplies the stored byte by 5, so divide going back out. */
    OBJ_P_IF(object->contrast != defaults.contrast, 114, p1b(&buffer, object->contrast / 5));
    OBJ_P_IF(object->team != defaults.team, 115, p2(&buffer, object->team));
    OBJ_P_IF(object->bought_id != defaults.bought_id, 139, p2(&buffer, object->bought_id));
    OBJ_P_IF(
        object->bought_template_id != defaults.bought_template_id,
        140,
        p2(&buffer, object->bought_template_id));
    OBJ_P_IF(
        object->placeholder_id != defaults.placeholder_id, 148, p2(&buffer, object->placeholder_id));
    OBJ_P_IF(
        object->placeholder_template_id != defaults.placeholder_template_id,
        149,
        p2(&buffer, object->placeholder_template_id));

    if( object->params.count > 0 )
    {
        p1(&buffer, 249);
        pparams(&buffer, &object->params);
    }

#undef OBJ_P_IF

    p1(&buffer, 0);

    /* init_object allocates the default name; release it rather than leak once per
     * encoded record. */
    free(defaults.name);

    return buffer.position;
}

static void
init_object(struct RSCache_Dat2ConfigObj* object)
{
    memset(object, 0, sizeof(struct RSCache_Dat2ConfigObj));

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

struct RSCache_Dat2ConfigObj*
RSCache_Dat2ConfigObjNewDecode(
    char* buffer,
    int buffer_size)
{
    struct RSCache_Dat2ConfigObj* object = malloc(sizeof(struct RSCache_Dat2ConfigObj));
    init_object(object);
    RSCache_Dat2ConfigObjDecodeInplace(object, buffer, buffer_size);
    return object;
}

void
RSCache_Dat2ConfigObjFree(struct RSCache_Dat2ConfigObj* object)
{
    free(object->name);
    free(object->examine);
    free(object);
}

void
RSCache_Dat2ConfigObjDecodeInplace(
    struct RSCache_Dat2ConfigObj* object,
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)(data), (uint32_t)(data_size));

    init_object(object);

    while( true )
    {
        if( buffer.position >= buffer.size )
        {
            printf(
                "RSCache_Dat2ConfigObjDecodeInplace: Buffer position %d exceeded data size %d\n",
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
            object->name = gcstring(&buffer);
            break;
        }
        case 3:
        {
            object->examine = gcstring(&buffer);
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
            gcstring(&buffer);
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
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        {
            object->actions[opcode - 30] = gcstring(&buffer);
            if( strcasecmp(object->actions[opcode - 30], "Hidden") == 0 )
            {
                free(object->actions[opcode - 30]);
                object->actions[opcode - 30] = NULL;
            }
            break;
        }
        case 35:
        case 36:
        case 37:
        case 38:
        case 39:
        {
            object->if_actions[opcode - 35] = gcstring(&buffer);

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
                char* string = gcstring(&buffer);
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
        case 100:
        case 101:
        case 102:
        case 103:
        case 104:
        case 105:
        case 106:
        case 107:
        case 108:
        case 109:
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
            object->ambient = g1b(&buffer);
            break;
        }
        case 114:
        {
            object->contrast = g1b(&buffer) * 5;
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
            RSCache_BufferReadParams(&buffer, &object->params);
            break;
        }
        default:
            break;
        }
    }
}
