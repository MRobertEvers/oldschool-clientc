#include "dat2_config_loc.h"

#include "dat2_entity_ops.h"

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
RSCache_Dat2ConfigLocCodecVersion(const struct RSCache* cache)
{
    /* An explicit pin from the revision module wins — that is the point of the pin. */
    int derived = RSCache_IsRs2Dat2(cache) ? RSCACHE_CODEC_LOC_RS2 : RSCACHE_CODEC_LOC_OSRS;
    return RSCache_CodecVersionOr(cache, RSCACHE_TYPE_LOC, derived);
}

int
RSCache_Dat2ConfigLocFlags(const struct RSCache* cache)
{
    assert(cache);
    int flags = RSCache_IsDat1(cache) ? RSCACHE_CONFIG_LOC_DECODE_DAT
                                      : RSCACHE_CONFIG_LOC_DECODE_DAT2;

    /* Default false when the cache is unidentified: this is the pre-220 payload
     * shape, and an unidentified cache with no archive revision is far more
     * likely to be an old one than a modern one (a modern cache always carries a
     * timestamp revision, which the threshold below catches). */
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_LOC, 220, REV_LOC_OSRS_220_ARCHIVE_REV, false) )
        flags |= RSCACHE_CONFIG_LOC_DECODE_OSRS_220;

    /* The Kronos client omits a byte its stock contemporaries write. Not
     * revision ordered, so only an explicit profile can say so — this is the
     * flag's first and only source. */
    if( RSCache_HasQuirk(cache, RSCACHE_QUIRK_KRONOS) )
        flags |= RSCACHE_CONFIG_LOC_DECODE_KRONOS;

    /* The codec version decides the stream shape; the flag is how the shared decoder body
     * is told which one it is. Routing it through CodecVersion rather than testing the
     * epoch here means a revision module can pin it explicitly. */
    int codec = RSCache_Dat2ConfigLocCodecVersion(cache);
    if( codec == RSCACHE_CODEC_LOC_RS2 || codec == RSCACHE_CODEC_LOC_RS2_727 ||
        codec == RSCACHE_CODEC_LOC_RS2_530 )
        flags |= RSCACHE_CONFIG_LOC_DECODE_RS2;

    if( codec == RSCACHE_CODEC_LOC_RS2_530 )
        flags |= RSCACHE_CONFIG_LOC_DECODE_RS2_530;

    if( codec == RSCACHE_CODEC_LOC_RS2 || codec == RSCACHE_CODEC_LOC_RS2_727 )
        flags |= RSCACHE_CONFIG_LOC_DECODE_RS2_NESTED_MODELS;

    /* ObjectDefinition.method7965 in the supplied 727 client reads the nested
     * opcode-1 model ids, opcode-24 animation, opcode-77/92 transforms and
     * opcode-106 animation alternatives with readBigSmart(). */
    if( codec == RSCACHE_CODEC_LOC_RS2_727 )
        flags |= RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS;

    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_LOC, 237, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
    {
        flags |= RSCACHE_CONFIG_LOC_DECODE_REV237_INT_MODEL_IDS;
        flags |= RSCACHE_CONFIG_LOC_DECODE_REV237_ENTITY_OPS;
    }

    /* LARGE_MODEL_IDS stays off for every profile: an exact-consumption scan of
     * the jan2026 OSRS cache (60601/60601 files) showed big-smart model ids
     * misaligning 15k of them. Kept as a flag because the field genuinely widens
     * in other lineages, but no declared revision sets it. */

    return flags;
}

struct RSCache_Dat2ConfigLoc*
RSCache_Dat2ConfigLocNewDecodeProfile(
    const struct RSCache* cache,
    char* buffer,
    int buffer_size)
{
    assert(cache);
    struct RSCache_Dat2ConfigLoc* loc = malloc(sizeof(struct RSCache_Dat2ConfigLoc));
    assert(loc);
    memset(loc, 0, sizeof(struct RSCache_Dat2ConfigLoc));

    decode_loc(loc, buffer, buffer_size, RSCache_Dat2ConfigLocFlags(cache));

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

    RSCache_EntityOpsFreeInplace(&loc->entity_ops);

    if( loc->params.values )
    {
        for( int i = 0; i < loc->params.count; i++ )
            free(loc->params.values[i]);
        free(loc->params.values);
    }
    free(loc->params.keys);
    free(loc->params.kinds);
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

void
RSCache_Dat2ConfigLocInit(struct RSCache_Dat2ConfigLoc* loc)
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
    loc->force_approach = 0;
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

    loc->sound_distance_fade_curve = 0;
    loc->sound_fade_in_curve = 0;
    loc->sound_fade_in_duration = 300;
    loc->sound_fade_out_curve = 0;
    loc->sound_fade_out_duration = 300;
    loc->unknown1 = false;
    loc->sound_visibility = 2;
    loc->raise = 0;
    RSCache_EntityOpsInit(&loc->entity_ops);
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

/* Model ids are plain u16 unless the era widens them, mirroring
 * LOC_READ_MODEL_ID on the decode side. */
#define LOC_WRITE_MODEL_ID(buf, flags, value)                                                      \
    do                                                                                             \
    {                                                                                              \
        if( (flags) & RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS )                                  \
            pbigsmart((buf), (value));                                                             \
        else                                                                                       \
            p2((buf), (value));                                                                    \
    } while( 0 )

static void
loc_pstringfl(
    struct RSCache_Buffer* buffer,
    const char* value,
    int flags)
{
    pjstr(
        buffer,
        value,
        (flags & RSCACHE_CONFIG_LOC_DECODE_DAT) ? RSCACHE_JSTR_TERMINATOR_NEWLINE
                                                : RSCACHE_JSTR_TERMINATOR_NULL);
}

uint32_t
RSCache_Dat2ConfigLocEncodeFlags(
    const struct RSCache_Dat2ConfigLoc* loc,
    int flags,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !loc || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    struct RSCache_Dat2ConfigLoc defaults;
    RSCache_Dat2ConfigLocInit(&defaults);

    /* Opcode 1 carries one model per shape; opcode 5 carries several models under a
     * single group and no shapes. The decoder distinguishes them by whether it
     * allocated `shapes`, so that is what selects the form here. Rev 237+ uses
     * opcodes 6/7 with g4 when any model id exceeds u16. */
    if( loc->shapes_and_model_count > 0 && loc->models )
    {
        bool use_int_ids = false;
        int total_models = 0;
        for( int i = 0; i < loc->shapes_and_model_count; i++ )
        {
            int group = loc->lengths ? loc->lengths[i] : 1;
            total_models += group;
            for( int j = 0; j < group; j++ )
                if( loc->models[i][j] < 0 || loc->models[i][j] > 0xFFFF )
                    use_int_ids = true;
        }

        /*
         * An RS2/rev-727 shape group can hold SEVERAL models; the modern form is
         * one (model, shape) pair per entry and cannot nest. Writing only
         * `models[i][0]` — which this did — drops every model after the first
         * with no diagnostic: 86 of them across the rs2012 QBD lane, including
         * the glow overlay that is the only thing distinguishing the finished
         * arena floor from the one already on the map.
         *
         * A single shape-10 group is exactly opcode 5's meaning, and both this
         * decoder and the reference merge a centrepiece's whole list, so that
         * case converts. Only a group that already holds one model keeps
         * opcode 1 — the form is not interchangeable there, since 5 answers
         * every shape the map asks for and 1 answers only its own.
         */
        bool flat = !loc->shapes;
        if( !flat && total_models > loc->shapes_and_model_count &&
            loc->shapes_and_model_count == 1 && loc->shapes[0] == 10 )
            flat = true;
        /* The entry count is one byte in every form. */
        assert(total_models <= 0xFF);

        if( !flat )
        {
            p1(&buffer, use_int_ids ? 6 : 1);
            p1(&buffer, total_models);
            for( int i = 0; i < loc->shapes_and_model_count; i++ )
            {
                int group = loc->lengths ? loc->lengths[i] : 1;
                /* A group of N repeats its shape N times. Every builder that
                 * merges by shape reads that back whole; one that stops at the
                 * first match still draws what the truncating encoder gave it. */
                for( int j = 0; j < group; j++ )
                {
                    if( use_int_ids )
                        p4(&buffer, loc->models[i][j]);
                    else
                        LOC_WRITE_MODEL_ID(&buffer, flags, loc->models[i][j]);
                    p1(&buffer, loc->shapes[i]);
                }
            }
        }
        else
        {
            int group = loc->lengths ? loc->lengths[0] : 1;
            p1(&buffer, use_int_ids ? 7 : 5);
            p1(&buffer, group);
            for( int i = 0; i < group; i++ )
            {
                if( use_int_ids )
                    p4(&buffer, loc->models[0][i]);
                else
                    LOC_WRITE_MODEL_ID(&buffer, flags, loc->models[0][i]);
            }
        }
    }

    if( loc->name )
    {
        p1(&buffer, 2);
        loc_pstringfl(&buffer, loc->name, flags);
    }
    if( loc->desc )
    {
        p1(&buffer, 3);
        loc_pstringfl(&buffer, loc->desc, flags);
    }

    if( loc->size_x != defaults.size_x )
    {
        p1(&buffer, 14);
        p1(&buffer, loc->size_x);
    }
    if( loc->size_z != defaults.size_z )
    {
        p1(&buffer, 15);
        p1(&buffer, loc->size_z);
    }

    /* blocks_walk / blocks_projectiles are driven by three opcodes rather than
     * carrying values: 17 zeroes both, 27 sets walk to 1, 18 clears projectiles.
     * Defaults are walk 2, projectiles 1. */
    if( loc->blocks_walk == 0 && loc->blocks_projectiles == 0 )
    {
        p1(&buffer, 17);
    }
    else
    {
        if( loc->blocks_walk == 1 )
            p1(&buffer, 27);
        if( loc->blocks_projectiles == 0 )
            p1(&buffer, 18);
    }

    if( loc->is_interactive != defaults.is_interactive )
    {
        p1(&buffer, 19);
        p1(&buffer, loc->is_interactive);
    }

    /* contour_ground_type is a small enum the decoder derives from five different
     * opcodes. Map it back, but note 93 and 95 mean something else entirely once the
     * >= 220 payloads apply, so those two are only reachable pre-220. */
    switch( loc->contour_ground_type )
    {
    case 1:
        p1(&buffer, 21);
        break;
    case 2:
        p1(&buffer, 81);
        p1(&buffer, loc->contoured_ground / 256);
        break;
    case 3:
        if( !(flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220) )
        {
            p1(&buffer, 93);
            p2b(&buffer, loc->contour_ground_param);
        }
        break;
    case 4:
        /* RS2: contour type. OSRS: opcode 94 is unknown1 (emitted below). */
        if( flags & RSCACHE_CONFIG_LOC_DECODE_RS2 )
            p1(&buffer, 94);
        break;
    case 5:
        if( !(flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220) )
        {
            p1(&buffer, 95);
            /* The decoder discards this value, so it cannot be reproduced. */
            p2(&buffer, 0);
        }
        break;
    default:
        break;
    }

    if( loc->sharelight )
        p1(&buffer, 22);
    if( loc->occlude )
        p1(&buffer, 23);

    if( loc->seq_id != defaults.seq_id )
    {
        p1(&buffer, 24);
        LOC_WRITE_MODEL_ID(&buffer, flags, loc->seq_id == -1 ? 65535 : loc->seq_id);
    }

    if( loc->wall_width != defaults.wall_width )
    {
        p1(&buffer, 28);
        p1(&buffer, loc->wall_width);
    }
    if( loc->ambient != defaults.ambient )
    {
        p1(&buffer, 29);
        p1b(&buffer, loc->ambient);
    }

    /* Actions 0..4 are writable through either opcode 30+i or 150+i, and the
     * decoder stores both in the same slots. Emit the 30-range; a record that used
     * the 150-range round-trips semantically but not byte-exactly. */
    for( int i = 0; i < 9; i++ )
    {
        if( loc->actions[i] )
        {
            p1(&buffer, 30 + i);
            loc_pstringfl(&buffer, loc->actions[i], flags);
        }
    }

    if( loc->contrast != defaults.contrast )
    {
        p1(&buffer, 39);
        /* Undo the decoder's era-dependent pre-scale (opcode 39). */
        p1b(&buffer, loc->contrast / ((flags & RSCACHE_CONFIG_LOC_DECODE_DAT) ? 5 : 25));
    }

    if( loc->recolor_count > 0 )
    {
        p1(&buffer, 40);
        p1(&buffer, loc->recolor_count);
        for( int i = 0; i < loc->recolor_count; i++ )
        {
            p2(&buffer, loc->recolors_from[i]);
            p2(&buffer, loc->recolors_to[i]);
        }
    }
    if( loc->retexture_count > 0 )
    {
        p1(&buffer, 41);
        p1(&buffer, loc->retexture_count);
        for( int i = 0; i < loc->retexture_count; i++ )
        {
            p2(&buffer, loc->retextures_from[i]);
            p2(&buffer, loc->retextures_to[i]);
        }
    }

    /* map_function_id is settable by 60, 82 and 107; map_scene_id by 68
     * (and historically 102 before rev237 EntityOps reclaimed that opcode).
     * Emit the lowest opcode of each group. */
    if( loc->map_function_id != defaults.map_function_id )
    {
        p1(&buffer, 60);
        p2(&buffer, loc->map_function_id);
    }
    /* Opcode 61 sits between 60 and 62 on the wire and is written here so a
     * record that states one round-trips byte-exactly. `defaults.category` is 0
     * — the same "unstated" the npc and obj encoders test against. */
    if( loc->category != defaults.category )
    {
        p1(&buffer, 61);
        p2(&buffer, loc->category);
    }
    if( loc->mirrored )
        p1(&buffer, 62);
    if( !loc->shadowed )
        p1(&buffer, 64);
    if( loc->resize_x != defaults.resize_x )
    {
        p1(&buffer, 65);
        p2(&buffer, loc->resize_x);
    }
    if( loc->resize_height != defaults.resize_height )
    {
        p1(&buffer, 66);
        p2(&buffer, loc->resize_height);
    }
    if( loc->resize_z != defaults.resize_z )
    {
        p1(&buffer, 67);
        p2(&buffer, loc->resize_z);
    }
    if( loc->map_scene_id != defaults.map_scene_id )
    {
        p1(&buffer, 68);
        p2(&buffer, loc->map_scene_id);
    }
    if( loc->force_approach != defaults.force_approach )
    {
        p1(&buffer, 69);
        p1(&buffer, loc->force_approach);
    }
    if( loc->offset_x != defaults.offset_x )
    {
        p1(&buffer, 70);
        p2b(&buffer, loc->offset_x);
    }
    if( loc->offset_y != defaults.offset_y )
    {
        p1(&buffer, 71);
        p2b(&buffer, loc->offset_y);
    }
    if( loc->offset_z != defaults.offset_z )
    {
        p1(&buffer, 72);
        p2b(&buffer, loc->offset_z);
    }
    if( loc->obstructs_ground )
        p1(&buffer, 73);
    if( loc->break_routefinding )
        p1(&buffer, 74);
    if( loc->support_items != defaults.support_items )
    {
        p1(&buffer, 75);
        p1(&buffer, loc->support_items);
    }

    /* Opcodes 77 and 92 both carry the transform varbit/varp and the transform
     * list; 92 adds a value the decoder parks in the last slot, where 77 leaves
     * -1. Same shape as npc's 106/118 pair. */
    if( loc->transform_count >= 2 )
    {
        int trailing = loc->transforms[loc->transform_count - 1];
        int listed = loc->transform_count - 1;

        p1(&buffer, trailing == -1 ? 77 : 92);
        p2(&buffer, loc->transform_varbit == -1 ? 65535 : loc->transform_varbit);
        p2(&buffer, loc->transform_varp == -1 ? 65535 : loc->transform_varp);
        if( trailing != -1 )
            LOC_WRITE_MODEL_ID(&buffer, flags, trailing);
        p1(&buffer, listed - 1);
        for( int i = 0; i < listed; i++ )
            LOC_WRITE_MODEL_ID(&buffer, flags, loc->transforms[i] == -1 ? 65535
                                                                       : loc->transforms[i]);
    }

    /*
     * Ambient sound. Opcodes 78 and 79 are **not** mutually exclusive: 78 carries a
     * single sound id, 79 carries the retrigger interval plus a list of ids, and
     * real records carry both (loc 16433 in cache.osrs230 does, with 79 first). They
     * write to different fields, so emitting only one loses the other.
     *
     * Both also write distance and retain; the later opcode wins on decode, and
     * since both are written from the same struct values that is consistent either
     * way. 79 goes first to match the observed packing order.
     *
     * The retain byte is absent on Kronos builds — the one place the quirk flag
     * changes the *encode* as well as the decode.
     */
    bool kronos = (flags & RSCACHE_CONFIG_LOC_DECODE_KRONOS) != 0;
    if( loc->ambient_sound_id_count > 0 || loc->ambient_sound_ticks_min != 0 ||
        loc->ambient_sound_ticks_max != 0 )
    {
        p1(&buffer, 79);
        p2(&buffer, loc->ambient_sound_ticks_min);
        p2(&buffer, loc->ambient_sound_ticks_max);
        p1(&buffer, loc->ambient_sound_distance);
        if( !kronos )
            p1(&buffer, loc->ambient_sound_retain);
        p1(&buffer, loc->ambient_sound_id_count);
        for( int i = 0; i < loc->ambient_sound_id_count; i++ )
            p2(&buffer, loc->ambient_sound_ids[i]);
    }
    if( loc->ambient_sound_id != defaults.ambient_sound_id )
    {
        p1(&buffer, 78);
        p2(&buffer, loc->ambient_sound_id);
        p1(&buffer, loc->ambient_sound_distance);
        if( !kronos )
            p1(&buffer, loc->ambient_sound_retain);
    }

    if( !loc->seq_random_start )
        p1(&buffer, 89);

    if( (flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220) &&
        loc->sound_distance_fade_curve != defaults.sound_distance_fade_curve )
    {
        p1(&buffer, 91);
        p1(&buffer, loc->sound_distance_fade_curve);
    }

    if( (flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220) &&
        (loc->sound_fade_in_curve != defaults.sound_fade_in_curve ||
         loc->sound_fade_in_duration != defaults.sound_fade_in_duration ||
         loc->sound_fade_out_curve != defaults.sound_fade_out_curve ||
         loc->sound_fade_out_duration != defaults.sound_fade_out_duration) )
    {
        p1(&buffer, 93);
        p1(&buffer, loc->sound_fade_in_curve);
        p2(&buffer, loc->sound_fade_in_duration);
        p1(&buffer, loc->sound_fade_out_curve);
        p2(&buffer, loc->sound_fade_out_duration);
    }

    if( loc->unknown1 && !(flags & RSCACHE_CONFIG_LOC_DECODE_RS2) )
        p1(&buffer, 94);

    if( (flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220) &&
        loc->sound_visibility != defaults.sound_visibility )
    {
        p1(&buffer, 95);
        p1(&buffer, loc->sound_visibility);
    }

    if( (flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220) && loc->raise != defaults.raise )
    {
        p1(&buffer, 96);
        p1(&buffer, loc->raise);
    }

    if( (flags & RSCACHE_CONFIG_LOC_DECODE_REV237_ENTITY_OPS) &&
        (loc->entity_ops.sub_ops_count > 0 || loc->entity_ops.cond_ops_count > 0 ||
         loc->entity_ops.cond_sub_ops_count > 0) )
    {
        RSCache_EntityOpsEncode(&loc->entity_ops, &buffer, 30, 100, 101, 102);
    }

    if( loc->random_seq_id_count > 0 )
    {
        p1(&buffer, 106);
        p1(&buffer, loc->random_seq_id_count);
        for( int i = 0; i < loc->random_seq_id_count; i++ )
        {
            LOC_WRITE_MODEL_ID(&buffer, flags, loc->random_seq_ids[i]);
            p1(&buffer, loc->random_seq_delays[i]);
        }
    }

    if( loc->campaign_id_count > 0 )
    {
        p1(&buffer, 160);
        p1(&buffer, loc->campaign_id_count);
        for( int i = 0; i < loc->campaign_id_count; i++ )
            p2(&buffer, loc->campaign_ids[i]);
    }

    if( loc->params.count > 0 )
    {
        p1(&buffer, 249);
        pparams(&buffer, &loc->params);
    }

    p1(&buffer, 0);

    RSCache_Dat2ConfigLocFreeInplace(&defaults);
    return buffer.position;
}

uint32_t
RSCache_Dat2ConfigLocEncode(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigLoc* loc,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_Dat2ConfigLocEncodeFlags(
        loc, RSCache_Dat2ConfigLocFlags(cache), out, out_capacity);
}

/*
 * RS2 (643) nested model list: `u8 count`, then per entry `u8 shape`, `u8 model_count`,
 * `model_count x u16 model`. One shape owns a list of models, where OSRS has each model
 * name its own shape. See RSCACHE_CONFIG_LOC_DECODE_RS2.
 *
 * Returns false when the record would run past its end, so the caller can stop and leave
 * `_consumed` short rather than churn garbage into later fields.
 */
static bool
loc_read_models_rs2(
    struct RSCache_Dat2ConfigLoc* loc,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    if( buffer->position >= buffer->size )
        return false;

    int count = g1(buffer);
    if( count == 0 )
        return true;

    loc->shapes_and_model_count = count;
    loc->shapes = (int*)malloc((size_t)count * sizeof(int));
    loc->models = (int**)malloc((size_t)count * sizeof(int*));
    loc->lengths = (int*)malloc((size_t)count * sizeof(int));
    if( !loc->shapes || !loc->models || !loc->lengths )
        return false;
    memset(loc->models, 0, (size_t)count * sizeof(int*));

    for( int i = 0; i < count; i++ )
    {
        if( buffer->position + 2 > buffer->size )
            return false;
        loc->shapes[i] = g1(buffer);

        int model_count = g1(buffer);
        loc->lengths[i] = model_count;
        loc->models[i] = model_count > 0 ? (int*)malloc((size_t)model_count * sizeof(int)) : NULL;
        if( model_count > 0 && !loc->models[i] )
            return false;

        for( int j = 0; j < model_count; j++ )
        {
            if( buffer->position >= buffer->size )
                return false;
            uint32_t width =
                ((flags & RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS) &&
                 (buffer->data[buffer->position] & 0x80))
                    ? 4u
                    : 2u;
            if( buffer->position + width > buffer->size )
                return false;
            loc->models[i][j] = LOC_READ_MODEL_ID(buffer, flags);
        }
    }
    return true;
}

/** Consume a nested model list without storing it — opcode 5's second block. */
static bool
loc_skip_models_rs2(
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    if( buffer->position >= buffer->size )
        return false;

    int count = g1(buffer);
    for( int i = 0; i < count; i++ )
    {
        if( buffer->position + 2 > buffer->size )
            return false;
        (void)g1(buffer); /* shape */
        int model_count = g1(buffer);
        for( int j = 0; j < model_count; j++ )
        {
            if( buffer->position >= buffer->size )
                return false;
            uint32_t width =
                ((flags & RSCACHE_CONFIG_LOC_DECODE_LARGE_MODEL_IDS) &&
                 (buffer->data[buffer->position] & 0x80))
                    ? 4u
                    : 2u;
            if( buffer->position + width > buffer->size )
                return false;
            (void)LOC_READ_MODEL_ID(buffer, flags);
        }
    }
    return true;
}

/*
 * One opcode of a loc record.
 *
 * Lifted verbatim out of `decode_loc`'s loop: every case body is unchanged except
 * that `goto decode_done` — the bail-out for an opcode whose payload width is
 * unknown — became `return false`, which stops the stream the same way. Falling
 * out of the switch means the opcode was handled.
 *
 * `flags` is not optional: opcode 1 takes a different shape under RS2, and
 * `gstringfl` reads strings differently per era.
 */
bool
RSCache_Dat2ConfigLocDecodeOp(
    struct RSCache_Dat2ConfigLoc* loc,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
        if( getenv("RSCACHE_OPCODE_CENSUS") )
            fprintf(stderr, "census LOC %d\n", opcode);

        switch( opcode )
        {
        case 1:
        {
            if( flags & RSCACHE_CONFIG_LOC_DECODE_RS2_NESTED_MODELS )
            {
                if( !loc_read_models_rs2(loc, buffer, flags) )
                    return false;
                break;
            }
            /**
             * This opcode defines several models associated with a single map loc.
             *
             * The map loc will specify which shape to use in it.
             */
            int count = g1(buffer);
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
                loc->models[i][0] = LOC_READ_MODEL_ID(buffer, flags);
                loc->shapes[i] = g1(buffer);
                loc->lengths[i] = 1;
            }
            break;
        }
        case 2:
            loc->name = gstringfl(buffer, flags);
            break;
        case 3:
            loc->desc = gstringfl(buffer, flags);
            break;
        case 5:
        {
            if( flags & RSCACHE_CONFIG_LOC_DECODE_RS2_NESTED_MODELS )
            {
                /* Two nested blocks. The first is kept; the second is consumed and
                 * dropped, which is what the reference does with both — it only needs the
                 * stream to stay aligned. Storing the first keeps the models a world
                 * render actually draws. */
                if( !loc_read_models_rs2(loc, buffer, flags) )
                    return false;
                if( !loc_skip_models_rs2(buffer, flags) )
                    return false;
                break;
            }
            /**
             * This is a single model loc.
             * Generally, always just draw the models.
             * shape_select is not used here.
             */
            int count = g1(buffer);
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
                int model_id = LOC_READ_MODEL_ID(buffer, flags);
                loc->models[0][i] = model_id;
            }
            break;
        }
        case 6:
        {
            if( !(flags & RSCACHE_CONFIG_LOC_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            /* Like opcode 1, but model ids are g4. */
            int count = g1(buffer);
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
                loc->models[i][0] = g4(buffer);
                loc->shapes[i] = g1(buffer);
                loc->lengths[i] = 1;
            }
            break;
        }
        case 7:
        {
            if( !(flags & RSCACHE_CONFIG_LOC_DECODE_REV237_INT_MODEL_IDS) )
                return false;
            /* Like opcode 5, but model ids are g4. */
            int count = g1(buffer);
            if( count == 0 )
                break;

            loc->shapes_and_model_count = 1;
            loc->shapes = NULL;
            loc->models = (int**)malloc(1 * sizeof(int*));
            loc->models[0] = (int*)malloc(count * sizeof(int));
            loc->lengths = (int*)malloc(1 * sizeof(int));
            loc->lengths[0] = count;
            for( int i = 0; i < count; i++ )
                loc->models[0][i] = g4(buffer);
            break;
        }
        case 14:
            loc->size_x = g1(buffer);
            break;
        case 15:
            loc->size_z = g1(buffer);
            break;
        case 17:
            loc->blocks_walk = 0;
            loc->blocks_projectiles = 0;
            break;
        case 18:
            loc->blocks_projectiles = 0;
            break;
        case 19:
            loc->is_interactive = g1(buffer);
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
            int seq_id = LOC_READ_MODEL_ID(buffer, flags);
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
            loc->wall_width = g1(buffer);
            break;
        case 29:
            loc->ambient = g1b(buffer);
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
            char* action = gstringfl(buffer, flags);
            loc->_actions_seen++;
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
            /* Stored pre-scaled, the way both references store it, so no
             * consumer has to know which era a record came from: dat1 uses
             * `g1b * 5` (Client-TS LocType), dat2/OSRS `readByte() * 25`. The
             * multiplier used to be 25 unconditionally, which made every dat1
             * loc five times as attenuated as the reference. */
            loc->contrast =
                g1b(buffer) * ((flags & RSCACHE_CONFIG_LOC_DECODE_DAT) ? 5 : 25);
            break;
        case 40:
        {
            int count = g1(buffer);
            loc->recolor_count = count;
            if( count > 0 )
            {
                loc->recolors_from = malloc(count * sizeof(int));
                loc->recolors_to = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->recolors_from[i] = g2(buffer);
                    loc->recolors_to[i] = g2(buffer);
                }
            }
            break;
        }
        case 41:
        {
            int count = g1(buffer);
            loc->retexture_count = count;
            if( count > 0 )
            {
                loc->retextures_from = malloc(count * sizeof(int));
                loc->retextures_to = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->retextures_from[i] = g2(buffer);
                    loc->retextures_to[i] = g2(buffer);
                }
            }
            break;
        }
        case 42:
        {
            /* RS2 per-model recolour palette indices.  OSRS has no equivalent
             * loc opcode; consume it so the record remains aligned.  Importers
             * can bake the selected palette into converted models when visual
             * parity requires it. */
            int count = g1(buffer);
            if( buffer->position + (uint32_t)count > buffer->size )
                return false;
            buffer->position += (uint32_t)count;
            break;
        }
        case 44:
        case 45:
            g2(buffer); // Skip unsigned short
            break;
        case 60:
            loc->map_function_id = g2(buffer);
            break;
        case 61:
            loc->category = g2(buffer);
            break;
        case 62:
            loc->mirrored = 1;
            break;
        case 64:
            loc->shadowed = 0;
            break;
        case 65:
            loc->resize_x = g2(buffer);
            break;
        case 66:
            loc->resize_height = g2(buffer);
            break;
        case 67:
            loc->resize_z = g2(buffer);
            break;
        case 68:
            // Client-TS from LostCity call this mapScene
            loc->map_scene_id = g2(buffer);
            break;
        case 69:
            loc->force_approach = g1(buffer);
            break;
        case 70:
            loc->offset_x = g2b(buffer);
            break;
        case 71:
            loc->offset_y = g2b(buffer);
            break;
        case 72:
            loc->offset_z = g2b(buffer);
            break;
        case 73:
            loc->obstructs_ground = 1;
            break;
        case 74:
            loc->break_routefinding = 1;
            break;
        case 75:
            loc->support_items = g1(buffer);
            break;
        case 77:
        case 92:
        {
            loc->transform_varbit = g2(buffer);
            if( loc->transform_varbit == 65535 )
            {
                loc->transform_varbit = -1;
            }

            loc->transform_varp = g2(buffer);
            if( loc->transform_varp == 65535 )
            {
                loc->transform_varp = -1;
            }

            int var3 = -1;
            if( opcode == 92 )
            {
                var3 = LOC_READ_MODEL_ID(buffer, flags);
                if( var3 == 65535 )
                {
                    var3 = -1;
                }
            }

            int count = g1(buffer);
            loc->transform_count = count + 2;
            loc->transforms = malloc((count + 2) * sizeof(int));

            for( int i = 0; i <= count; i++ )
            {
                int transform = LOC_READ_MODEL_ID(buffer, flags);
                if( transform == 65535 )
                {
                    transform = -1;
                }
                loc->transforms[i] = transform;
            }

            loc->transforms[count + 1] = var3;
            break;
        }
        /*
         * The ambient-sound `retain` byte is an OldSchool 220 addition, not a universal one.
         * LocType.decodeOpcode gates it on `game === "oldschool" && revision >= 220`, so RS2
         * writes neither opcode with it — reading it there costs a byte and desyncs the
         * record from that point on.
         */
        case 78:
        {
            loc->ambient_sound_id = g2(buffer);
            loc->ambient_sound_distance = g1(buffer);
            if( !(flags &
                  (RSCACHE_CONFIG_LOC_DECODE_KRONOS | RSCACHE_CONFIG_LOC_DECODE_RS2)) )
                loc->ambient_sound_retain = g1(buffer);
            break;
        }
        case 79:
        {
            loc->ambient_sound_ticks_min = g2(buffer);
            loc->ambient_sound_ticks_max = g2(buffer);
            loc->ambient_sound_distance = g1(buffer);
            if( !(flags &
                  (RSCACHE_CONFIG_LOC_DECODE_KRONOS | RSCACHE_CONFIG_LOC_DECODE_RS2)) )
                loc->ambient_sound_retain = g1(buffer);
            int count = g1(buffer);
            loc->ambient_sound_id_count = count;
            if( count > 0 )
            {
                loc->ambient_sound_ids = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->ambient_sound_ids[i] = g2(buffer);
                }
            }
            break;
        }
        case 81:
        {
            loc->contoured_ground = g1(buffer) * 256;
            loc->contour_ground_type = 2;
            loc->contour_ground_param = loc->contoured_ground;
            break;
        }
        case 82:
            /* OldSchool stores the map-function id here; RS2 uses the same opcode as a bare
             * flag with no payload (LocType.decodeOpcode branches on `game === "oldschool"`
             * and reads nothing otherwise). Its map function is opcode 60 or 107. */
            if( !(flags & RSCACHE_CONFIG_LOC_DECODE_RS2) )
                loc->map_function_id = g2(buffer);
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
            /* A payload-free members flag in the RS2-era LocType; the byte read here is an
             * OldSchool-only sound-distance-fade curve. Both readings are attested against
             * their own era, so this is gated rather than picked. */
            if( !(flags & RSCACHE_CONFIG_LOC_DECODE_RS2) )
            {
                if( flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220 )
                    loc->sound_distance_fade_curve = g1(buffer);
                else
                    g1(buffer);
            }
            break;
        case 93:
        {
            if( flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220 )
            {
                loc->sound_fade_in_curve = g1(buffer);
                loc->sound_fade_in_duration = g2(buffer);
                loc->sound_fade_out_curve = g1(buffer);
                loc->sound_fade_out_duration = g2(buffer);
            }
            else
            {
                loc->contour_ground_type = 3;
                loc->contour_ground_param = g2b(buffer);
            }
            break;
        }
        case 94:
            if( flags & RSCACHE_CONFIG_LOC_DECODE_RS2 )
                loc->contour_ground_type = 4;
            else
                loc->unknown1 = true;
            break;
        case 95:
        {
            if( flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220 )
            {
                loc->sound_visibility = g1(buffer);
            }
            else
            {
                loc->contour_ground_type = 5;
                if( !(flags & RSCACHE_CONFIG_LOC_DECODE_RS2_530) )
                    loc->contour_ground_param = g2b(buffer);
            }
            break;
        }
        case 96:
            if( flags & RSCACHE_CONFIG_LOC_DECODE_OSRS_220 )
                loc->raise = g1(buffer);
            break;
        case 99:
        case 104:
        case 163:
        case 167:
        case 170:
        case 171:
        case 173:
        case 178:
        case 190:
        case 191:
            /* Skip various data - just read the bytes */
            if( opcode == 99 )
            {
                g1(buffer);
                g2(buffer);
            }
            else if( opcode == 104 || opcode == 178 )
            {
                g1(buffer);
            }
            else if( opcode == 163 )
            {
                g1b(buffer);
                g1b(buffer);
                g1b(buffer);
                g1b(buffer);
            }
            else if( opcode == 167 || opcode == 173 )
            {
                g2(buffer);
                if( opcode == 173 )
                {
                    g2(buffer);
                }
            }
            else if( opcode == 170 || opcode == 171 )
            {
                gushortsmart(buffer);
            }
            else if( opcode == 190 || opcode == 191 )
            {
                /* deferredAmbientSwap / resetAmbientOnLoopRestart. Payload-free in the
                 * RS2-era LocType (the reference notes both as "unknown starts 731", i.e.
                 * after 643); a byte each in the OldSchool era. */
                if( !(flags & RSCACHE_CONFIG_LOC_DECODE_RS2) )
                    g1(buffer);
            }
            break;
        case 100:
            if( flags & RSCACHE_CONFIG_LOC_DECODE_REV237_ENTITY_OPS )
            {
                RSCache_EntityOpsDecodeSubOp(&loc->entity_ops, buffer);
            }
            else
            {
                /* RS2 / pre-237: skip payload (g1 + g2). */
                g1(buffer);
                g2(buffer);
            }
            break;
        case 101:
            if( flags & RSCACHE_CONFIG_LOC_DECODE_REV237_ENTITY_OPS )
            {
                RSCache_EntityOpsDecodeCondOp(&loc->entity_ops, buffer);
            }
            else
            {
                g1(buffer);
            }
            break;
        case 102:
            if( flags & RSCACHE_CONFIG_LOC_DECODE_REV237_ENTITY_OPS )
            {
                RSCache_EntityOpsDecodeCondSubOp(&loc->entity_ops, buffer);
            }
            else
            {
                loc->map_scene_id = g2(buffer);
            }
            break;
        case 106:
        {
            int count = g1(buffer);
            loc->random_seq_id_count = count;
            if( count > 0 )
            {
                loc->random_seq_ids = malloc(count * sizeof(int));
                loc->random_seq_delays = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->random_seq_ids[i] = LOC_READ_MODEL_ID(buffer, flags);
                    loc->random_seq_delays[i] = g1(buffer);
                }
            }
            break;
        }
        case 107:
            loc->map_function_id = g2(buffer);
            break;
        case 150:
        case 151:
        case 152:
        case 153:
        case 154:
        {
            int action_index = opcode - 150;
            char* action = gstringfl(buffer, flags);
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
            int count = g1(buffer);
            loc->campaign_id_count = count;
            if( count > 0 )
            {
                loc->campaign_ids = malloc(count * sizeof(int));
                for( int i = 0; i < count; i++ )
                {
                    loc->campaign_ids[i] = g2(buffer);
                }
            }
            break;
        }
        case 249:
            RSCache_BufferReadParams(buffer, &loc->params);
            break;
        default:
            fprintf(
                stderr,
                "RSCache_Dat2ConfigLocDecode: unimplemented opcode %d at offset %d\n",
                opcode,
                buffer->position - 1);
            /* Unknown payload length: the stream is misaligned from here on;
             * stop rather than churn garbage into later fields. */
            return false;
        }

    /* Fell out of the switch: a case handled this opcode. */
    return true;
}

void
RSCache_Dat2ConfigLocFinish(struct RSCache_Dat2ConfigLoc* loc, unsigned flags)
{
    (void)flags;
    if( !loc )
        return;

    if( loc->break_routefinding )
    {
        loc->blocks_walk = 0;
        loc->blocks_projectiles = 0;
    }

    if( loc->is_interactive == -1 )
    {
        loc->is_interactive =
            (loc->models != NULL) && ((loc->shapes == NULL) || (loc->shapes[0] == 10));
        /*
         * `_actions_seen`, not a count of non-NULL `actions[]`. An action spelled
         * "hidden" is counted and *then* stored as NULL, so a loc whose only action
         * is hidden has zero non-NULL slots but is still interactive. Counting the
         * array would silently make those locs unclickable.
         */
        if( loc->_actions_seen > 0 )
            loc->is_interactive = true;
    }

    /* LocType.raiseobject (opcode 75): Client-TS defaults -1 → blockwalk ? 1 : 0.
     * When set, ground stacks on the loc's tile render at the model height. */
    if( loc->support_items == -1 )
        loc->support_items = loc->blocks_walk ? 1 : 0;
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

    RSCache_Dat2ConfigLocInit(loc);

    while( true )
    {
        if( buffer.position >= buffer.size )
            break;

        int opcode = g1(&buffer) & 0xFF;
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigLocDecodeOp(loc, opcode, &buffer, (unsigned)flags) )
            break;
    }

    loc->_consumed = (int)buffer.position;
    RSCache_Dat2ConfigLocFinish(loc, (unsigned)flags);
}

uint32_t
RSCache_Dat2ConfigLocEncodeBound(const struct RSCache_Dat2ConfigLoc* loc)
{
    /*
     * A ceiling, on the same principle as the npc bound: one flat allowance
     * covering every scalar opcode, then each variable-length part measured from
     * the record. 100 opcodes at worst 1 code byte plus 8 of payload is under 900,
     * so 2048 leaves better than a 2x margin.
     *
     * `test_opcode_codec` writes a canary past this bound and checks it on every
     * loc in the cache, so an inadequate figure fails the suite rather than
     * corrupting the heap.
     */
    uint32_t need = 2048u;
    int i;

    if( !loc )
        return need;

    /* Opcode 1/5/6/7 carry one model list per shape; worst case g4 per id, plus a
     * shape byte and per-list count. */
    need += (uint32_t)loc->shapes_and_model_count * 8u + 4u;
    if( loc->lengths )
    {
        for( i = 0; i < loc->shapes_and_model_count; i++ )
            need += (uint32_t)loc->lengths[i] * 4u + 2u;
    }
    need += (uint32_t)loc->recolor_count * 4u + 2u;
    need += (uint32_t)loc->retexture_count * 4u + 2u;
    need += (uint32_t)loc->transform_count * 2u + 8u;
    need += (uint32_t)loc->ambient_sound_id_count * 2u + 4u;
    need += (uint32_t)loc->random_seq_id_count * 4u + 4u;
    need += (uint32_t)loc->campaign_id_count * 2u + 4u;

    if( loc->name )
        need += (uint32_t)strlen(loc->name) + 2u;
    if( loc->desc )
        need += (uint32_t)strlen(loc->desc) + 2u;
    for( i = 0; i < 10; i++ )
    {
        if( loc->actions[i] )
            need += (uint32_t)strlen(loc->actions[i]) + 2u;
    }

    need += RSCache_EntityOpsBound(&loc->entity_ops);
    need += 1u + RSCache_BufferParamsBound(&loc->params);
    return need;
}
