#include "dat2_config_loc.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define LOC_READ_MODEL_ID(buf, flags)                                                              \
    ((flags) & RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS ? gbigsmart(buf) : g2(buf))

static void
decode_loc(
    struct RSCache_Dat2ConfigLoc* loc,
    char* data,
    int data_size,
    int flags);

struct RSCache_Dat2ConfigLoc*
RSCache_Dat2ConfigLocNewDecode(
    char* buffer,
    int buffer_size)
{
    struct RSCache_Dat2ConfigLoc* loc = malloc(sizeof(struct RSCache_Dat2ConfigLoc));
    assert(loc);
    memset(loc, 0, sizeof(struct RSCache_Dat2ConfigLoc));

    decode_loc(loc, buffer, buffer_size, RSCACHE_CONFIG_LOC_DECODE_DAT2);

    return loc;
}

void
RSCache_Dat2ConfigLocFree(struct RSCache_Dat2ConfigLoc* loc)
{
    if( !loc )
        return;

    RSCache_Dat2ConfigLocFreeInplace(loc);

    free(loc);
}

void
RSCache_Dat2ConfigLocFreeInplace(struct RSCache_Dat2ConfigLoc* loc)
{
    if( !loc )
        return;

    for( int i = 0; i < 10; i++ )
        free(loc->actions[i]);
    free(loc->name);
    free(loc->desc);

    free(loc->shapes);
    if( loc->models )
    {
        for( int i = 0; i < loc->shapes_and_model_count; i++ )
        {
            free(loc->models[i]);
        }
        free(loc->models);
    }

    free(loc->lengths);

    free(loc->recolors_from);
    free(loc->recolors_to);
    free(loc->retextures_from);
    free(loc->retextures_to);
    free(loc->transforms);
    free(loc->ambient_sound_ids);
    free(loc->random_seq_ids);
    free(loc->random_seq_delays);
    free(loc->campaign_ids);

    if( loc->params.values )
    {
        for( int i = 0; i < loc->params.count; i++ )
            free(loc->params.values[i]);
        free(loc->params.values);
    }
    free(loc->params.keys);
    free(loc->params.is_string);
}

void
RSCache_Dat2ConfigLocDecodeInplace(
    struct RSCache_Dat2ConfigLoc* loc,
    char* data,
    int data_size,
    int flags)
{
    decode_loc(loc, data, data_size, flags);
}

static void
init_loc(struct RSCache_Dat2ConfigLoc* loc)
{
    memset(loc, 0, sizeof(struct RSCache_Dat2ConfigLoc));

    loc->name = NULL;
    loc->size_x = 1;
    loc->size_z = 1;
    loc->blocks_walk = 2;
    loc->blocks_projectiles = 1;
    loc->is_interactive = -1;
    loc->contoured_ground = -1;
    loc->sharelight = 0;
    loc->occlude = 0;
    loc->seq_id = -1;
    loc->ambient = 0;
    loc->contrast = 0;
    loc->map_function_id = -1;
    loc->map_scene_id = -1;
    loc->shadowed = true;
    loc->wall_width = 16;
    loc->resize_x = 128;
    loc->resize_z = 128;
    loc->resize_height = 128;
    loc->offset_x = 0;
    loc->offset_z = 0;
    loc->offset_y = 0;
    loc->obstructs_ground = 0;
    loc->break_routefinding = 0;
    loc->support_items = -1;
    loc->transform_varbit = -1;
    loc->transform_varp = -1;
    loc->ambient_sound_id = -1;
    loc->ambient_sound_distance = 0;
    loc->ambient_sound_ticks_min = 0;
    loc->ambient_sound_ticks_max = 0;
    loc->ambient_sound_retain = 0;
    loc->seq_random_start = true;
    loc->contour_ground_type = 0;
    loc->contour_ground_param = -1;
    loc->recolor_count = 0;
    loc->recolors_from = NULL;
    loc->recolors_to = NULL;
    loc->retexture_count = 0;
    loc->retextures_from = NULL;
    loc->retextures_to = NULL;
    loc->transform_count = 0;
    loc->transforms = NULL;
    loc->ambient_sound_id_count = 0;
    loc->ambient_sound_ids = NULL;
    loc->random_seq_id_count = 0;
    loc->random_seq_ids = NULL;
    loc->random_seq_delays = NULL;
    loc->campaign_id_count = 0;
    loc->campaign_ids = NULL;
    loc->mirrored = 0;
}

static inline char*
gstringfl(
    struct RSCache_Buffer* buffer,
    int flags)
{
    if( flags & RSCACHE_CONFIG_LOC_DECODE_DAT )
    {
        return gstringnewline(buffer);
    }
    return gcstring(buffer);
}

static void
decode_loc(
    struct RSCache_Dat2ConfigLoc* loc,
    char* data,
    int data_size,
    int flags)
{
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    int actions_count = 0;

    init_loc(loc);

    while( true )
    {
        if( buffer.position >= buffer.size )
            break;

        int opcode = g1(&buffer) & 0xFF;
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            /**
             * This opcode defines several models associated with a single map loc.
             *
             * The map loc will specify which shape to use in it.
             */
            int count = g1(&buffer);
            if( count == 0 )
                break;

            loc->shapes = (int*)malloc(count * sizeof(int));
            loc->models = (int**)malloc(count * sizeof(int*));
            loc->lengths = (int*)malloc(count * sizeof(int));
            memset(loc->lengths, 0, count * sizeof(int));
            loc->shapes_and_model_count = count;
            for( int i = 0; i < count; i++ )
            {
                loc->models[i] = (int*)malloc(1 * sizeof(int));
                loc->models[i][0] = g2(&buffer);
                loc->shapes[i] = g1(&buffer);
                loc->lengths[i] = 1;
            }
            break;
        }
        case 2:
            loc->name = gstringfl(&buffer, flags);
            break;
        case 3:
            loc->desc = gstringfl(&buffer, flags);
            break;
        case 5:
        {
            /**
             * This is a single model loc.
             * Generally, always just draw the models.
             * shape_select is not used here.
             */
            int count = g1(&buffer);
            if( count == 0 )
                break;

            loc->shapes_and_model_count = 1;

            loc->shapes = NULL;
            loc->models = (int**)malloc(1 * sizeof(int*));
            loc->models[0] = (int*)malloc(count * sizeof(int));
            loc->lengths = (int*)malloc(1 * sizeof(int));
            loc->lengths[0] = count;
            for( int i = 0; i < count; i++ )
            {
                int model_id = g2(&buffer);
                loc->models[0][i] = model_id;
            }
            break;
        }
        case 14:
            loc->size_x = g1(&buffer);
            break;
        case 15:
            loc->size_z = g1(&buffer);
            break;
        case 17:
            loc->blocks_walk = 0;
            loc->blocks_projectiles = 0;
            break;
        case 18:
            loc->blocks_projectiles = 0;
            break;
        case 19:
            loc->is_interactive = g1(&buffer);
            break;
        case 21:
            loc->contoured_ground = 0;
            loc->contour_ground_type = 1;
            break;
        case 22:
            loc->sharelight = 1;
            break;
        case 23:
            loc->occlude = 1;
            break;
        case 24:
        {
            int seq_id = g2(&buffer);
            if( seq_id == 65535 )
            {
                seq_id = -1;
            }
            loc->seq_id = seq_id;
            break;
        }
        case 25:
            // disposeAlpha? - skip for now
            break;
        case 27:
            loc->blocks_walk = 1;
            break;
        case 28:
            loc->wall_width = g1(&buffer);
            break;
        case 29:
            loc->ambient = g1b(&buffer);
            break;
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        case 35:
        case 36:
        case 37:
        case 38:
        {
            int action_index = opcode - 30;
            char* action = gstringfl(&buffer, flags);
            actions_count++;
            // Check if action is "hidden" (case insensitive)
            if( action && strcasecmp(action, "hidden") == 0 )
            {
                free(action);
                loc->actions[action_index] = NULL;
            }
            else
            {
                loc->actions[action_index] = action;
            }
            break;
        }
        case 39:
            // Old Revisions multiply the contract by 5 instead of 25
            loc->contrast = g1b(&buffer) * 25;
            break;
        case 40:
        {
            int count = g1(&buffer);
            loc->recolor_count = count;
            if( count > 0 )
            {
                loc->recolors_from = malloc(count * sizeof(int));
                loc->recolors_to = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->recolors_from[i] = g2(&buffer);
                    loc->recolors_to[i] = g2(&buffer);
                }
            }
            break;
        }
        case 41:
        {
            int count = g1(&buffer);
            loc->retexture_count = count;
            if( count > 0 )
            {
                loc->retextures_from = malloc(count * sizeof(int));
                loc->retextures_to = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->retextures_from[i] = g2(&buffer);
                    loc->retextures_to[i] = g2(&buffer);
                }
            }
            break;
        }
        case 44:
        case 45:
            g2(&buffer); // Skip unsigned short
            break;
        case 60:
            loc->map_function_id = g2(&buffer);
            break;
        case 61:
            g2(&buffer); // Skip unsigned short
            break;
        case 62:
            loc->mirrored = 1;
            break;
        case 64:
            loc->shadowed = 0;
            break;
        case 65:
            loc->resize_x = g2(&buffer);
            break;
        case 66:
            loc->resize_height = g2(&buffer);
            break;
        case 67:
            loc->resize_z = g2(&buffer);
            break;
        case 68:
            // Client-TS from LostCity call this mapScene
            loc->map_scene_id = g2(&buffer);
            break;
        case 69:
            g1(&buffer); // Skip unsigned byte
            break;
        case 70:
            loc->offset_x = g2b(&buffer);
            break;
        case 71:
            loc->offset_y = g2b(&buffer);
            break;
        case 72:
            loc->offset_z = g2b(&buffer);
            break;
        case 73:
            loc->obstructs_ground = 1;
            break;
        case 74:
            loc->break_routefinding = 1;
            break;
        case 75:
            loc->support_items = g1(&buffer);
            break;
        case 77:
        case 92:
        {
            loc->transform_varbit = g2(&buffer);
            if( loc->transform_varbit == 65535 )
            {
                loc->transform_varbit = -1;
            }

            loc->transform_varp = g2(&buffer);
            if( loc->transform_varp == 65535 )
            {
                loc->transform_varp = -1;
            }

            int var3 = -1;
            if( opcode == 92 )
            {
                var3 = LOC_READ_MODEL_ID(&buffer, flags);
                if( var3 == 65535 )
                {
                    var3 = -1;
                }
            }

            int count = g1(&buffer);
            loc->transform_count = count + 2;
            loc->transforms = malloc((count + 2) * sizeof(int));

            for( int i = 0; i <= count; i++ )
            {
                int transform = LOC_READ_MODEL_ID(&buffer, flags);
                if( transform == 65535 )
                {
                    transform = -1;
                }
                loc->transforms[i] = transform;
            }

            loc->transforms[count + 1] = var3;
            break;
        }
        case 78:
        {
            loc->ambient_sound_id = g2(&buffer);
            loc->ambient_sound_distance = g1(&buffer);
            if( !(flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS) )
                loc->ambient_sound_retain = g1(&buffer);
            break;
        }
        case 79:
        {
            loc->ambient_sound_ticks_min = g2(&buffer);
            loc->ambient_sound_ticks_max = g2(&buffer);
            loc->ambient_sound_distance = g1(&buffer);
            if( !(flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS) )
                loc->ambient_sound_retain = g1(&buffer);
            int count = g1(&buffer);
            loc->ambient_sound_id_count = count;
            if( count > 0 )
            {
                loc->ambient_sound_ids = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->ambient_sound_ids[i] = g2(&buffer);
                }
            }
            break;
        }
        case 81:
        {
            loc->contoured_ground = g1(&buffer) * 256;
            loc->contour_ground_type = 2;
            loc->contour_ground_param = loc->contoured_ground;
            break;
        }
        case 82:
            loc->map_function_id = g2(&buffer);
            break;
        case 88:
        case 90:
        case 91:
        case 96:
        case 97:
        case 98:
        case 103:
        case 105:
        case 168:
        case 169:
        case 177:
        case 189:
            // Boolean flags - skip for now
            break;
        case 89:
            loc->seq_random_start = false;
            break;
        case 93:
        {
            loc->contour_ground_type = 3;
            loc->contour_ground_param = g2b(&buffer);
            break;
        }
        case 94:
            loc->contour_ground_type = 4;
            break;
        case 95:
        {
            loc->contour_ground_type = 5;
            // TODO: Check cache info for contour_ground_param
            g2(&buffer);
            break;
        }
        case 99:
        case 100:
        case 101:
        case 104:
        case 163:
        case 167:
        case 170:
        case 171:
        case 173:
        case 178:
        case 190:
        case 191:
            // Skip various data - just read the bytes
            if( opcode == 99 || opcode == 100 )
            {
                g1(&buffer);
                g2(&buffer);
            }
            else if( opcode == 101 || opcode == 104 || opcode == 178 )
            {
                g1(&buffer);
            }
            else if( opcode == 163 )
            {
                g1b(&buffer);
                g1b(&buffer);
                g1b(&buffer);
                g1b(&buffer);
            }
            else if( opcode == 167 || opcode == 173 )
            {
                g2(&buffer);
                if( opcode == 173 )
                {
                    g2(&buffer);
                }
            }
            else if( opcode == 170 || opcode == 171 )
            {
                gushortsmart(&buffer);
            }
            break;
        case 102:
            loc->map_scene_id = g2(&buffer);
            break;
        case 106:
        {
            int count = g1(&buffer);
            loc->random_seq_id_count = count;
            if( count > 0 )
            {
                loc->random_seq_ids = malloc(count * sizeof(int));
                loc->random_seq_delays = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->random_seq_ids[i] = LOC_READ_MODEL_ID(&buffer, flags);
                    loc->random_seq_delays[i] = g1(&buffer);
                }
            }
            break;
        }
        case 107:
            loc->map_function_id = g2(&buffer);
            break;
        case 150:
        case 151:
        case 152:
        case 153:
        case 154:
        {
            int action_index = opcode - 150;
            char* action = gstringfl(&buffer, flags);
            // Check if action is "hidden" (case insensitive)
            if( action && strcasecmp(action, "hidden") == 0 )
            {
                free(action);
                loc->actions[action_index] = NULL;
            }
            else
            {
                loc->actions[action_index] = action;
            }
            break;
        }
        case 160:
        {
            int count = g1(&buffer);
            loc->campaign_id_count = count;
            if( count > 0 )
            {
                loc->campaign_ids = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->campaign_ids[i] = g2(&buffer);
                }
            }
            break;
        }
        case 249:
            RSCache_BufferReadParams(&buffer, &loc->params);
            break;
        default:
            fprintf(
                stderr,
                "RSCache_Dat2ConfigLocDecode: unimplemented opcode %d at offset %d\n",
                opcode,
                buffer.position - 1);
            break;
        }
    }

    if( loc->break_routefinding )
    {
        loc->blocks_walk = 0;
        loc->blocks_projectiles = 0;
    }

    if( loc->is_interactive == -1 )
    {
        loc->is_interactive =
            (loc->models != NULL) && ((loc->shapes == NULL) || (loc->shapes[0] == 10));
        if( actions_count > 0 )
        {
            loc->is_interactive = true;
        }
    }
}
