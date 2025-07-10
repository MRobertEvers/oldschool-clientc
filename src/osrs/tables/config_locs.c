#include "config_locs.h"

#include "osrs/rsbuf.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

struct CacheConfigLocation*
config_locs_new_decode(char* buffer, int buffer_size)
{
    struct CacheConfigLocation* loc = malloc(sizeof(struct CacheConfigLocation));
    if( !loc )
    {
        printf("config_locs_new_decode: Failed to allocate loc\n");
        return NULL;
    }
    memset(loc, 0, sizeof(struct CacheConfigLocation));

    decode_loc(loc, buffer, buffer_size);

    return loc;
}

void
config_locs_free(struct CacheConfigLocation* loc)
{
    free_loc(loc);
}

static void
init_loc(struct CacheConfigLocation* loc)
{
    memset(loc, 0, sizeof(struct CacheConfigLocation));
    //   this.lowDetail = false;
    //     this.name = "null";
    //     this.sizeX = 1;
    //     this.sizeY = 1;
    //     this.clipType = 2;
    //     this.blocksProjectile = true;
    //     this.isInteractive = -1;
    //     this.contouredGround = -1;
    //     this.mergeNormals = false;
    //     this.modelClipped = false;
    //     this.seqId = -1;
    //     this.decorDisplacement = LocType.DEFAULT_DECOR_DISPLACEMENT;
    //     this.ambient = 0;
    //     this.contrast = 0;
    //     this.actions = new Array(5);
    //     this.mapFunctionId = -1;
    //     this.mapSceneId = -1;
    //     this.flipMapSceneSprite = false;
    //     this.isRotated = false;
    //     this.clipped = true;
    //     this.modelSizeX = 128;
    //     this.modelSizeHeight = 128;
    //     this.modelSizeY = 128;
    //     this.offsetX = 0;
    //     this.offsetHeight = 0;
    //     this.offsetY = 0;
    //     this.obstructsGround = false;
    //     this.isHollow = false;
    //     this.supportItems = -1;
    //     this.transformVarbit = -1;
    //     this.transformVarp = -1;
    //     this.ambientSoundId = -1;
    //     this.ambientSoundDistance = 0;
    //     this.ambientSoundChangeTicksMin = 0;
    //     this.ambientSoundChangeTicksMax = 0;
    //     this.ambientSoundRetain = 0;
    //     this.seqRandomStart = true;

    //     this.contourGroundType = 0;
    //     this.contourGroundParam = -1;

    loc->name = NULL;
    loc->size_x = 1;
    loc->size_y = 1;
    loc->clip_type = 2;
    loc->blocks_projectiles = 1;
    loc->is_interactive = -1;
    loc->contoured_ground = -1;
    loc->merge_normals = 0;
    loc->model_clipped = 0;
    loc->seq_id = -1;
    loc->ambient = 0;
    loc->contrast = 0;
    loc->map_function_id = -1;
    loc->map_scene_id = -1;
    loc->clipped = 1;
    loc->model_size_x = 128;
    loc->model_size_height = 128;
    loc->model_size_y = 128;
    loc->offset_x = 0;
    loc->offset_height = 0;
    loc->offset_y = 0;
    loc->obstructs_ground = 0;
    loc->is_hollow = 0;
    loc->support_items = -1;
    loc->transform_varbit = -1;
    loc->transform_varp = -1;
    loc->ambient_sound_id = -1;
    loc->ambient_sound_distance = 0;
    loc->ambient_sound_ticks_min = 0;
    loc->ambient_sound_ticks_max = 0;
    loc->ambient_sound_retain = 0;
    loc->seq_random_start = 1;
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
    loc->param_keys = NULL;
    loc->param_values = NULL;

    loc->rotated = 0;
    loc->campaign_ids = NULL;
    loc->param_keys = NULL;
    loc->param_values = NULL;
}

void
decode_loc(struct CacheConfigLocation* loc, char* data, int data_size)
{
    struct RSBuffer buffer = { .data = (uint8_t*)data, .size = data_size, .position = 0 };

    init_loc(loc);

    while( true )
    {
        if( buffer.position >= buffer.size )
        {
            printf(
                "decode_npc_type: Buffer position %d exceeded data size %d\n",
                buffer.position,
                buffer.size);
            return;
        }

        int opcode = rsbuf_g1(&buffer) & 0xFF;
        if( opcode == 0 )
        {
            // printf("decode_npc_type: Reached end of data (opcode 0)\n");
            return;
        }

        switch( opcode )
        {
        case 1:
        {
            int count = rsbuf_g1(&buffer);
            if( count == 0 )
                break;

            loc->types = (int*)malloc(count * sizeof(int));
            loc->models = (int**)malloc(count * sizeof(int*));
            loc->lengths = (int*)malloc(count * sizeof(int));
            memset(loc->lengths, 0, count * sizeof(int));
            for( int i = 0; i < count; i++ )
            {
                loc->models[i] = (int*)malloc(1 * sizeof(int));
                loc->models[i][0] = rsbuf_g2(&buffer);
                loc->types[i] = rsbuf_g1(&buffer);
                loc->lengths[i] = 1;
            }
            break;
        }
        case 2:
            loc->name = rsbuf_read_string(&buffer);
            break;
        case 3:
            loc->desc = rsbuf_read_string(&buffer);
            break;
        case 5:
        {
            int count = rsbuf_g1(&buffer);
            if( count == 0 )
                break;

            loc->types = NULL;
            loc->models = (int**)malloc(1 * sizeof(int*));
            loc->models[0] = (int*)malloc(count * sizeof(int));
            loc->lengths = (int*)malloc(1 * sizeof(int));
            loc->lengths[0] = count;
            for( int i = 0; i < count; i++ )
            {
                int model_id = rsbuf_g2(&buffer);
                if( model_id == 34824 )
                {
                    int lll = 0;
                }
                loc->models[0][i] = model_id;
            }
            break;
        }
        case 14:
            loc->size_x = rsbuf_g1(&buffer);
            break;
        case 15:
            loc->size_y = rsbuf_g1(&buffer);
            break;
        case 17:
            loc->clip_type = 0;
            loc->blocks_projectiles = 0;
            break;
        case 18:
            loc->blocks_projectiles = 0;
            break;
        case 19:
            loc->is_interactive = rsbuf_g1(&buffer);
            break;
        case 21:
            loc->contoured_ground = 0;
            loc->contour_ground_type = 1;
            break;
        case 22:
            loc->merge_normals = 1;
            break;
        case 23:
            loc->model_clipped = 1;
            break;
        case 24:
        {
            int seq_id = rsbuf_g2(&buffer);
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
            loc->clip_type = 1;
            break;
        case 28:
            // decorDisplacement - skip for now
            rsbuf_g1(&buffer);
            break;
        case 29:
            loc->ambient = rsbuf_g1b(&buffer);
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
            char* action = rsbuf_read_string(&buffer);
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
            loc->contrast = rsbuf_g1b(&buffer) * 25;
            break;
        case 40:
        {
            int count = rsbuf_g1(&buffer);
            loc->recolor_count = count;
            if( count > 0 )
            {
                loc->recolors_from = malloc(count * sizeof(int));
                loc->recolors_to = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->recolors_from[i] = rsbuf_g2(&buffer);
                    loc->recolors_to[i] = rsbuf_g2(&buffer);
                }
            }
            break;
        }
        case 41:
        {
            int count = rsbuf_g1(&buffer);
            loc->retexture_count = count;
            if( count > 0 )
            {
                loc->retextures_from = malloc(count * sizeof(int));
                loc->retextures_to = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->retextures_from[i] = rsbuf_g2(&buffer);
                    loc->retextures_to[i] = rsbuf_g2(&buffer);
                }
            }
            break;
        }
        case 44:
        case 45:
            rsbuf_g2(&buffer); // Skip unsigned short
            break;
        case 60:
            loc->map_function_id = rsbuf_g2(&buffer);
            break;
        case 61:
            rsbuf_g2(&buffer); // Skip unsigned short
            break;
        case 62:
            loc->rotated = 1;
            break;
        case 64:
            loc->clipped = 0;
            break;
        case 65:
            loc->model_size_x = rsbuf_g2(&buffer);
            break;
        case 66:
            loc->model_size_height = rsbuf_g2(&buffer);
            break;
        case 67:
            loc->model_size_y = rsbuf_g2(&buffer);
            break;
        case 68:
            loc->map_scene_id = rsbuf_g2(&buffer);
            break;
        case 69:
            rsbuf_g1(&buffer); // Skip unsigned byte
            break;
        case 70:
            loc->offset_x = rsbuf_g2b(&buffer);
            break;
        case 71:
            loc->offset_height = rsbuf_g2b(&buffer);
            break;
        case 72:
            loc->offset_y = rsbuf_g2b(&buffer);
            break;
        case 73:
            loc->obstructs_ground = 1;
            break;
        case 74:
            loc->is_hollow = 1;
            break;
        case 75:
            loc->support_items = rsbuf_g1(&buffer);
            break;
        case 77:
        case 92:
        {
            loc->transform_varbit = rsbuf_g2(&buffer);
            if( loc->transform_varbit == 65535 )
            {
                loc->transform_varbit = -1;
            }

            loc->transform_varp = rsbuf_g2(&buffer);
            if( loc->transform_varp == 65535 )
            {
                loc->transform_varp = -1;
            }

            int var3 = -1;
            if( opcode == 92 )
            {
                var3 = rsbuf_read_big_smart(&buffer);
                if( var3 == 65535 )
                {
                    var3 = -1;
                }
            }

            int count = rsbuf_g1(&buffer);
            loc->transform_count = count + 2;
            loc->transforms = malloc((count + 2) * sizeof(int));

            for( int i = 0; i <= count; i++ )
            {
                int transform = rsbuf_read_big_smart(&buffer);
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
            loc->ambient_sound_id = rsbuf_g2(&buffer);
            loc->ambient_sound_distance = rsbuf_g1(&buffer);
            // TODO: Check cache info for ambient_sound_retain
            loc->ambient_sound_retain = rsbuf_g1(&buffer);
            break;
        }
        case 79:
        {
            loc->ambient_sound_ticks_min = rsbuf_g2(&buffer);
            loc->ambient_sound_ticks_max = rsbuf_g2(&buffer);
            loc->ambient_sound_distance = rsbuf_g1(&buffer);
            // TODO: Check cache info for ambient_sound_retain
            loc->ambient_sound_retain = rsbuf_g1(&buffer);
            int count = rsbuf_g1(&buffer);
            loc->ambient_sound_id_count = count;
            if( count > 0 )
            {
                loc->ambient_sound_ids = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->ambient_sound_ids[i] = rsbuf_g2(&buffer);
                }
            }
            break;
        }
        case 81:
        {
            loc->contoured_ground = rsbuf_g1(&buffer) * 256;
            loc->contour_ground_type = 2;
            loc->contour_ground_param = loc->contoured_ground;
            break;
        }
        case 82:
            // TODO: Check cache info for map_function_id
            rsbuf_g2(&buffer);
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
            loc->seq_random_start = 0;
            break;
        case 93:
        {
            loc->contour_ground_type = 3;
            loc->contour_ground_param = rsbuf_g2b(&buffer);
            break;
        }
        case 94:
            loc->contour_ground_type = 4;
            break;
        case 95:
        {
            loc->contour_ground_type = 5;
            // TODO: Check cache info for contour_ground_param
            rsbuf_g2(&buffer);
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
                rsbuf_g1(&buffer);
                rsbuf_g2(&buffer);
            }
            else if( opcode == 101 || opcode == 104 || opcode == 178 )
            {
                rsbuf_g1(&buffer);
            }
            else if( opcode == 163 )
            {
                rsbuf_g1b(&buffer);
                rsbuf_g1b(&buffer);
                rsbuf_g1b(&buffer);
                rsbuf_g1b(&buffer);
            }
            else if( opcode == 167 || opcode == 173 )
            {
                rsbuf_g2(&buffer);
                if( opcode == 173 )
                {
                    rsbuf_g2(&buffer);
                }
            }
            else if( opcode == 170 || opcode == 171 )
            {
                rsbuf_read_unsigned_short_smart(&buffer);
            }
            break;
        case 102:
            loc->map_scene_id = rsbuf_g2(&buffer);
            break;
        case 106:
        {
            int count = rsbuf_g1(&buffer);
            loc->random_seq_id_count = count;
            if( count > 0 )
            {
                loc->random_seq_ids = malloc(count * sizeof(int));
                loc->random_seq_delays = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->random_seq_ids[i] = rsbuf_read_big_smart(&buffer);
                    loc->random_seq_delays[i] = rsbuf_g1(&buffer);
                }
            }
            break;
        }
        case 107:
            loc->map_function_id = rsbuf_g2(&buffer);
            break;
        case 150:
        case 151:
        case 152:
        case 153:
        case 154:
        {
            int action_index = opcode - 150;
            char* action = rsbuf_read_string(&buffer);
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
            int count = rsbuf_g1(&buffer);
            loc->campaign_id_count = count;
            if( count > 0 )
            {
                loc->campaign_ids = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->campaign_ids[i] = rsbuf_g2(&buffer);
                }
            }
            break;
        }
        case 249:
            // TODO: Implement params reading
            break;
        default:
            printf("LocType: Opcode %d not implemented\n", opcode);
            break;
        }
    }
}

void
free_loc(struct CacheConfigLocation* loc)
{
    for( int i = 0; i < 5; i++ )
        free(loc->actions[i]);
    free(loc->name);
    free(loc->desc);

    // Free allocated arrays
    free(loc->types);
    if( loc->models )
    {
        for( int i = 0; i < loc->recolor_count; i++ )
        {
            free(loc->models[i]);
        }
        free(loc->models);
    }

    free(loc->recolors_from);
    free(loc->recolors_to);
    free(loc->retextures_from);
    free(loc->retextures_to);
    free(loc->transforms);
    free(loc->ambient_sound_ids);
    free(loc->random_seq_ids);
    free(loc->random_seq_delays);
    free(loc->campaign_ids);
    free(loc->param_keys);
    free(loc->param_values);

    free(loc);
}

void
print_loc(struct CacheConfigLocation* loc)
{
    printf("Loc: %s\n", loc->name ? loc->name : "NULL");
    if( loc->desc )
    {
        printf("Desc: %s\n", loc->desc);
    }

    for( int i = 0; i < 5; i++ )
    {
        if( loc->actions[i] )
            printf("Action %d: %s\n", i, loc->actions[i]);
    }
}