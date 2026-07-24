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

/* Empirical (TORIRS_LOC_SCAN exact-consumption over the jan2026 OSRS cache,
 * 60601/60601 files): OSRS >= 220 payload shapes apply, model ids stay plain
 * u16 (LARGE_MODEL_IDS made 15k files misalign — OSRS locs do not big-smart).
 * The loc group "revision" in modern caches is a unix timestamp; any value
 * past this threshold is a modern OSRS cache. */
#define REV_LOC_OSRS_220_ARCHIVE_REV 2000

int
RSCache_Dat2ConfigLocFlagsForRevision(int revision)
{
    /* Retained entry point for callers holding only the loc group's archive
     * revision. Routed through the profile so the era rule lives in one place. */
    struct RSCache cache = RSCache_ProfileZero();
    RSCache_ProfileSetGroupRevision(&cache, RSCACHE_TYPE_LOC, revision);
    return RSCache_Dat2ConfigLocFlags(&cache);
}

int
RSCache_Dat2ConfigLocFlags(const struct RSCache* cache)
{
    int flags = RSCache_IsDat1(cache) ? RSCACHE_CONFIG_LOC_DECODE_DAT
                                      : RSCACHE_CONFIG_LOC_DECODE_DAT2;

    /* Default false when the cache is unidentified: this is the pre-220 payload
     * shape, and an unidentified cache with no archive revision is far more
     * likely to be an old one than a modern one (a modern cache always carries a
     * timestamp revision, which the threshold below catches). */
    if( RSCache_RevisionAtLeast(
            cache, RSCACHE_TYPE_LOC, 220, REV_LOC_OSRS_220_ARCHIVE_REV, false) )
        flags |= RSCACHE_CONFIG_LOC_DECODE_OSRS_220;

    /* The Kronos client omits a byte its stock contemporaries write. Not
     * revision ordered, so only an explicit profile can say so — this is the
     * flag's first and only source. */
    if( RSCache_HasQuirk(cache, RSCACHE_QUIRK_KRONOS) )
        flags |= RSCACHE_CONFIG_LOC_DECODE_KRONOS;

    /* LARGE_MODEL_IDS stays off for every profile: an exact-consumption scan of
     * the jan2026 OSRS cache (60601/60601 files) showed big-smart model ids
     * misaligning 15k of them. Kept as a flag because the field genuinely widens
     * in other lineages, but no declared revision sets it. */

    return flags;
}

struct RSCache_Dat2ConfigLoc*
RSCache_Dat2ConfigLocNewDecode(
    int revision,
    char* buffer,
    int buffer_size)
{
    struct RSCache_Dat2ConfigLoc* loc = malloc(sizeof(struct RSCache_Dat2ConfigLoc));
    assert(loc);
    memset(loc, 0, sizeof(struct RSCache_Dat2ConfigLoc));

    decode_loc(loc, buffer, buffer_size, RSCache_Dat2ConfigLocFlagsForRevision(revision));

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
                loc->models[i][0] = LOC_READ_MODEL_ID(&buffer, flags);
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
                int model_id = LOC_READ_MODEL_ID(&buffer, flags);
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
            int seq_id = LOC_READ_MODEL_ID(&buffer, flags);
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
        case 97:
        case 98:
        case 103:
        case 105:
        case 168:
        case 169:
        case 176:
        case 177:
        case 189:
            // Boolean flags - skip for now
            break;
        case 89:
            loc->seq_random_start = false;
            break;
        case 91:
            // soundDistanceFadeCurve (LocType.ts:433) - skip
            g1(&buffer);
            break;
        case 93:
        {
            if( flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220 )
            {
                // OSRS >= 220: sound fade in/out curve + duration (LocType.ts:436-440)
                g1(&buffer);
                g2(&buffer);
                g1(&buffer);
                g2(&buffer);
            }
            else
            {
                loc->contour_ground_type = 3;
                loc->contour_ground_param = g2b(&buffer);
            }
            break;
        }
        case 94:
            loc->contour_ground_type = 4;
            break;
        case 95:
        {
            if( flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220 )
            {
                // OSRS: crossworldsound byte (LocType.ts:448-449) - skip
                g1(&buffer);
            }
            else
            {
                loc->contour_ground_type = 5;
                // TODO: Check cache info for contour_ground_param
                g2(&buffer);
            }
            break;
        }
        case 96:
            if( flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220 )
                g1(&buffer); // OSRS: thickness byte (LocType.ts:459-460) - skip
            break;
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
            else if( opcode == 190 || opcode == 191 )
            {
                // deferredAmbientSwap / resetAmbientOnLoopRestart bytes
                // (LocType.ts:550-553) - skip
                g1(&buffer);
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
            /* Unknown payload length: the stream is misaligned from here on;
             * stop rather than churn garbage into later fields. */
            goto decode_done;
        }
    }

decode_done:
    loc->_consumed = (int)buffer.position;

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
