#include "cachepack.h"

#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_config_spotanim.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ---- sequence ----------------------------------------------------------- */

/*
 * A sequence is the one config type whose *opcode numbers* move between eras, so
 * both directions go through the profile-aware entry points rather than a fixed
 * codec. Getting that wrong does not mis-size a field, it puts the maya id where
 * the frame sounds should be.
 *
 * Every key is written for every record (`key=default` when the stream does not
 * state it), straight from the decoder's presence bits. The held-item keys are
 * appearance values, spelled as LostCity spells them: `hide` (0, empty the
 * slot), an obj name (an obj-range value, obj + 512), or a bare number (a kit
 * value, which no OldSchool sequence uses). They used to be written as the obj
 * whose id equalled the RAW value -- woodcutting's bronze axe read
 * `lefthand=uncooked_pitta_bread`, and "hide" was `mcannonremains`, obj 0.
 */

#define SEQ_HELD_OBJ_BASE 512

static int
seq_codec(const struct CP_Ctx* ctx)
{
    return RSCache_Dat2ConfigSequenceCodecVersion(&ctx->profile);
}

static int
seq_is_rs2(const struct CP_Ctx* ctx)
{
    int codec = seq_codec(ctx);
    return codec == RSCACHE_CODEC_SEQUENCE_RS2_530 || codec == RSCACHE_CODEC_SEQUENCE_RS2_727;
}

static int
seq_applies_oldschool(const struct CP_Ctx* ctx)
{
    return !seq_is_rs2(ctx);
}

static int
seq_applies_v3(const struct CP_Ctx* ctx)
{
    return seq_codec(ctx) == RSCACHE_CODEC_SEQUENCE_V3;
}

static int
seq_applies_rs2(const struct CP_Ctx* ctx)
{
    return seq_is_rs2(ctx);
}

static int
seq_applies_rs2_727(const struct CP_Ctx* ctx)
{
    return seq_codec(ctx) == RSCACHE_CODEC_SEQUENCE_RS2_727;
}

/* In the order they are written. One key per RSCACHE_SEQ_FIELD_*. */
const struct CP_KeySpec cp_seq_keys[] = {
    { "frame", CP_KEY_LIST, NULL, NULL },
    { "framestep", 0, NULL, NULL },
    { "interleave", CP_KEY_LIST, NULL, NULL },
    { "stretches", 0, NULL, NULL },
    { "forcedpriority", 0, NULL, NULL },
    { "lefthand", 0, NULL, NULL },
    { "righthand", 0, NULL, NULL },
    { "maxloops", 0, NULL, NULL },
    { "precedence", 0, NULL, NULL },
    { "priority", 0, NULL, NULL },
    { "replymode", 0, NULL, NULL },
    { "chatframe", CP_KEY_LIST, NULL, NULL },
    { "mayaid", 0, seq_applies_oldschool, NULL },
    { "mayarange", 0, seq_applies_oldschool, NULL },
    { "mayamask", CP_KEY_LIST, seq_applies_oldschool, NULL },
    { "sound", CP_KEY_LIST, NULL, NULL },
    { "verticaloffset", 0, seq_applies_v3, NULL },
    { "crossworldsounds", 0, seq_applies_oldschool, NULL },
    { "debugname", 0, seq_applies_oldschool, NULL },
    { "rs2soundflag", 0, seq_applies_rs2, NULL },
    { "rs2tweened", 0, seq_applies_rs2_727, NULL },
    { "rs2unknown16", 0, seq_applies_rs2_727, NULL },
    { "rs2vorbissounds", 0, seq_applies_rs2_727, NULL },
    { NULL, 0, NULL, NULL },
};

static void
emit_held(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    int value)
{
    if( value == 0 )
        cp_lines_addf(out, "%s=hide", key);
    else if( value >= SEQ_HELD_OBJ_BASE )
        cp_emit_name(ctx, out, key, CP_TYPE_OBJ, value - SEQ_HELD_OBJ_BASE);
    else
        cp_lines_addf(out, "%s=%d", key, value);
}

static int
parse_held(
    struct CP_Ctx* ctx,
    const char* value,
    int* out)
{
    int obj = -1;

    if( strcmp(value, "hide") == 0 )
    {
        *out = 0;
        return 1;
    }
    if( cp_parse_int(value, out) )
        return 1;
    if( !cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &obj) )
        return 0;
    *out = obj + SEQ_HELD_OBJ_BASE;
    return 1;
}

int
cp_unpack_seq(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigSequence entry;
    const struct RSCache_Presence* has = &entry.present;

    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigSequenceDecodeProfile(&entry, &ctx->profile, (char*)record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "seq %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

#define SEQ_HAS(field) RSCache_PresenceHas(has, RSCACHE_SEQ_FIELD_##field)

    /* Frame id and its length are one thing to a reader and are written as one
     * line; the wire splits them into two parallel runs purely to pack better. */
    if( !SEQ_HAS(FRAMES) )
        cp_lines_add_default(out, "frame");
    else if( entry.frame_count == 0 )
        cp_lines_add_empty(out, "frame");
    else
    {
        for( int i = 0; i < entry.frame_count; i++ )
            cp_lines_addf(out, "frame=%d,%d", entry.frame_ids[i], entry.frame_lengths[i]);
    }

    if( SEQ_HAS(FRAME_STEP) )
        cp_lines_addf(out, "framestep=%d", entry.frame_step);
    else
        cp_lines_add_default(out, "framestep");

    if( !SEQ_HAS(INTERLEAVE) )
        cp_lines_add_default(out, "interleave");
    else if( entry.interleave_leave[0] == 9999999 )
        cp_lines_add_empty(out, "interleave");
    else
    {
        char buf[2048];
        int w = 0;
        for( int i = 0; entry.interleave_leave[i] != 9999999; i++ )
        {
            w += snprintf(buf + w, sizeof(buf) - (size_t)w, i ? ",%d" : "%d",
                          entry.interleave_leave[i]);
            assert(w < (int)sizeof(buf) - 8);
        }
        cp_lines_addf(out, "interleave=%s", buf);
    }

    if( SEQ_HAS(STRETCHES) )
        cp_lines_addf(out, "stretches=yes");
    else
        cp_lines_add_default(out, "stretches");

    if( SEQ_HAS(FORCED_PRIORITY) )
        cp_lines_addf(out, "forcedpriority=%d", entry.forced_priority);
    else
        cp_lines_add_default(out, "forcedpriority");

    if( SEQ_HAS(LEFT_HAND) )
        emit_held(ctx, out, "lefthand", entry.left_hand_item);
    else
        cp_lines_add_default(out, "lefthand");
    if( SEQ_HAS(RIGHT_HAND) )
        emit_held(ctx, out, "righthand", entry.right_hand_item);
    else
        cp_lines_add_default(out, "righthand");

    if( SEQ_HAS(MAX_LOOPS) )
        cp_lines_addf(out, "maxloops=%d", entry.max_loops);
    else
        cp_lines_add_default(out, "maxloops");
    if( SEQ_HAS(PRECEDENCE) )
        cp_lines_addf(out, "precedence=%d", entry.precedence_animating);
    else
        cp_lines_add_default(out, "precedence");
    if( SEQ_HAS(PRIORITY) )
        cp_lines_addf(out, "priority=%d", entry.priority);
    else
        cp_lines_add_default(out, "priority");
    if( SEQ_HAS(REPLY_MODE) )
        cp_lines_addf(out, "replymode=%d", entry.reply_mode);
    else
        cp_lines_add_default(out, "replymode");

    if( !SEQ_HAS(CHAT_FRAMES) )
        cp_lines_add_default(out, "chatframe");
    else if( entry.chat_frame_id_count == 0 )
        cp_lines_add_empty(out, "chatframe");
    else
    {
        for( int i = 0; i < entry.chat_frame_id_count; i++ )
            cp_lines_addf(out, "chatframe=%d", entry.chat_frame_ids[i]);
    }

    if( seq_applies_oldschool(ctx) )
    {
        if( SEQ_HAS(MAYA_ID) )
            cp_lines_addf(out, "mayaid=%d", entry.anim_maya_id);
        else
            cp_lines_add_default(out, "mayaid");
        if( SEQ_HAS(MAYA_RANGE) )
            cp_lines_addf(out, "mayarange=%d,%d", entry.anim_maya_start, entry.anim_maya_end);
        else
            cp_lines_add_default(out, "mayarange");
        if( !SEQ_HAS(MAYA_MASKS) )
            cp_lines_add_default(out, "mayamask");
        else
        {
            int masks = 0;
            for( int i = 0; i < 256; i++ )
            {
                if( entry.anim_maya_masks[i] )
                {
                    cp_lines_addf(out, "mayamask=%d", i);
                    masks++;
                }
            }
            if( masks == 0 )
                cp_lines_add_empty(out, "mayamask");
        }
    }

    if( !SEQ_HAS(FRAME_SOUNDS) )
        cp_lines_add_default(out, "sound");
    else if( entry.frame_sounds.count == 0 )
        cp_lines_add_empty(out, "sound");
    else
    {
        for( int i = 0; i < entry.frame_sounds.count; i++ )
        {
            const struct RSCache_Dat2ConfigFrameSound* s = &entry.frame_sounds.sounds[i];
            cp_lines_addf(out, "sound=%d,%d,%d,%d,%d,%d", entry.frame_sounds.frames[i], s->id,
                          s->loops, s->location, s->retain, s->weight);
        }
    }

    if( seq_applies_v3(ctx) )
    {
        if( SEQ_HAS(VERTICAL_OFFSET) )
            cp_lines_addf(out, "verticaloffset=%d", entry.vertical_offset);
        else
            cp_lines_add_default(out, "verticaloffset");
    }
    if( seq_applies_oldschool(ctx) )
    {
        if( SEQ_HAS(CROSS_WORLD_SOUNDS) )
            cp_lines_addf(out, "crossworldsounds=yes");
        else
            cp_lines_add_default(out, "crossworldsounds");
        if( SEQ_HAS(DEBUG_NAME) )
            cp_lines_add_str(out, "debugname", entry.debug_name);
        else
            cp_lines_add_default(out, "debugname");
    }
    if( seq_applies_rs2(ctx) )
    {
        if( SEQ_HAS(RS2_530_SOUND_FLAG) )
            cp_lines_addf(out, "rs2soundflag=yes");
        else
            cp_lines_add_default(out, "rs2soundflag");
    }
    if( seq_applies_rs2_727(ctx) )
    {
        if( SEQ_HAS(RS2_727_TWEENED) )
            cp_lines_addf(out, "rs2tweened=yes");
        else
            cp_lines_add_default(out, "rs2tweened");
        if( SEQ_HAS(RS2_727_UNKNOWN16) )
            cp_lines_addf(out, "rs2unknown16=yes");
        else
            cp_lines_add_default(out, "rs2unknown16");
        if( SEQ_HAS(RS2_727_VORBIS_SOUNDS) )
            cp_lines_addf(out, "rs2vorbissounds=yes");
        else
            cp_lines_add_default(out, "rs2vorbissounds");
    }

#undef SEQ_HAS

    RSCache_Dat2ConfigSequenceFreeInplace(&entry);
    return 1;
}

static void
frame_sounds_push(
    struct RSCache_Dat2ConfigFrameSoundMap* map,
    int frame,
    const struct RSCache_Dat2ConfigFrameSound* sound)
{
    if( map->count >= map->capacity )
    {
        int next = map->capacity ? map->capacity * 2 : 8;
        int* frames = realloc(map->frames, (size_t)next * sizeof(int));
        assert(frames);
        map->frames = frames;
        struct RSCache_Dat2ConfigFrameSound* sounds =
            realloc(map->sounds, (size_t)next * sizeof(*sounds));
        assert(sounds);
        map->sounds = sounds;
        map->capacity = next;
    }
    map->frames[map->count] = frame;
    map->sounds[map->count] = *sound;
    map->count++;
}

/* A `yes` flag key: the opcode carries no payload, so its presence is the value.
 * `no` would be a third state the stream cannot express. */
static int
parse_flag(const char* value)
{
    return strcmp(value, "yes") == 0;
}

uint32_t
cp_pack_seq(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Dat2ConfigSequence entry;
    memset(&entry, 0, sizeof(entry));
    /* The empty-record decode gives the client defaults and no presence: a field
     * is written only if a line below states it. */
    RSCache_Dat2ConfigSequenceDecodeProfile(
        &entry, &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    struct CP_IntList frame_ids = { 0 }, frame_lengths = { 0 };
    struct CP_IntList chat_frames = { 0 };
    struct CP_IntList interleave = { 0 };
    uint32_t written = 0;

#define SEQ_SET(field) RSCache_PresenceSet(&entry.present, RSCACHE_SEQ_FIELD_##field)

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        char scratch[2048];
        char* fields[8];
        int ok = 1;

        /* Not stated: the field keeps its client default and no opcode is
         * written. cp_keys_check_config has already made sure it is the only
         * line for its key. */
        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "frame") == 0 )
        {
            SEQ_SET(FRAMES);
            if( !cp_value_is_empty(value) )
            {
                if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 2) != 2 )
                    ok = 0;
                else
                {
                    int frame = 0, length = 0;
                    ok = cp_parse_int(fields[0], &frame) && cp_parse_int(fields[1], &length);
                    cp_intlist_push(&frame_ids, frame);
                    cp_intlist_push(&frame_lengths, length);
                }
            }
        }
        else if( strcmp(key, "chatframe") == 0 )
        {
            SEQ_SET(CHAT_FRAMES);
            if( !cp_value_is_empty(value) )
            {
                int frame = 0;
                ok = cp_parse_int(value, &frame);
                cp_intlist_push(&chat_frames, frame);
            }
        }
        else if( strcmp(key, "interleave") == 0 )
        {
            SEQ_SET(INTERLEAVE);
            if( !cp_value_is_empty(value) )
            {
                char big[4096];
                char* parts[256];
                if( strlen(value) >= sizeof(big) )
                    ok = 0;
                else
                {
                    int n = cp_split(value, big, parts, 256);
                    for( int f = 0; f < n && ok; f++ )
                    {
                        int v = 0;
                        ok = cp_parse_int(parts[f], &v);
                        cp_intlist_push(&interleave, v);
                    }
                }
            }
        }
        else if( strcmp(key, "framestep") == 0 )
        {
            SEQ_SET(FRAME_STEP);
            ok = cp_parse_int(value, &entry.frame_step);
        }
        else if( strcmp(key, "stretches") == 0 )
        {
            SEQ_SET(STRETCHES);
            ok = parse_flag(value);
            entry.stretches = true;
        }
        else if( strcmp(key, "forcedpriority") == 0 )
        {
            SEQ_SET(FORCED_PRIORITY);
            ok = cp_parse_int(value, &entry.forced_priority);
        }
        else if( strcmp(key, "lefthand") == 0 )
        {
            SEQ_SET(LEFT_HAND);
            ok = parse_held(ctx, value, &entry.left_hand_item);
        }
        else if( strcmp(key, "righthand") == 0 )
        {
            SEQ_SET(RIGHT_HAND);
            ok = parse_held(ctx, value, &entry.right_hand_item);
        }
        else if( strcmp(key, "maxloops") == 0 )
        {
            SEQ_SET(MAX_LOOPS);
            ok = cp_parse_int(value, &entry.max_loops);
        }
        else if( strcmp(key, "precedence") == 0 )
        {
            SEQ_SET(PRECEDENCE);
            ok = cp_parse_int(value, &entry.precedence_animating);
        }
        else if( strcmp(key, "priority") == 0 )
        {
            SEQ_SET(PRIORITY);
            ok = cp_parse_int(value, &entry.priority);
        }
        else if( strcmp(key, "replymode") == 0 )
        {
            SEQ_SET(REPLY_MODE);
            ok = cp_parse_int(value, &entry.reply_mode);
        }
        else if( strcmp(key, "mayaid") == 0 )
        {
            SEQ_SET(MAYA_ID);
            ok = cp_parse_int(value, &entry.anim_maya_id);
        }
        else if( strcmp(key, "mayarange") == 0 )
        {
            SEQ_SET(MAYA_RANGE);
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 2) != 2 )
                ok = 0;
            else
                ok = cp_parse_int(fields[0], &entry.anim_maya_start) &&
                     cp_parse_int(fields[1], &entry.anim_maya_end);
        }
        else if( strcmp(key, "mayamask") == 0 )
        {
            SEQ_SET(MAYA_MASKS);
            if( !entry.anim_maya_masks )
            {
                entry.anim_maya_masks = calloc(256, sizeof(bool));
                assert(entry.anim_maya_masks);
            }
            if( !cp_value_is_empty(value) )
            {
                int slot = 0;
                ok = cp_parse_int(value, &slot) && slot >= 0 && slot < 256;
                if( ok )
                    entry.anim_maya_masks[slot] = true;
            }
        }
        else if( strcmp(key, "sound") == 0 )
        {
            SEQ_SET(FRAME_SOUNDS);
            if( !cp_value_is_empty(value) )
            {
                if( strlen(value) >= sizeof(scratch) ||
                    cp_split(value, scratch, fields, 6) != 6 )
                {
                    ok = 0;
                }
                else
                {
                    int frame = 0;
                    struct RSCache_Dat2ConfigFrameSound sound;
                    memset(&sound, 0, sizeof(sound));
                    ok = cp_parse_int(fields[0], &frame) && cp_parse_int(fields[1], &sound.id) &&
                         cp_parse_int(fields[2], &sound.loops) &&
                         cp_parse_int(fields[3], &sound.location) &&
                         cp_parse_int(fields[4], &sound.retain) &&
                         cp_parse_int(fields[5], &sound.weight);
                    if( ok )
                        frame_sounds_push(&entry.frame_sounds, frame, &sound);
                }
            }
        }
        else if( strcmp(key, "verticaloffset") == 0 )
        {
            SEQ_SET(VERTICAL_OFFSET);
            ok = cp_parse_int(value, &entry.vertical_offset);
        }
        else if( strcmp(key, "crossworldsounds") == 0 )
        {
            SEQ_SET(CROSS_WORLD_SOUNDS);
            ok = parse_flag(value);
            entry.sounds_cross_world_view = true;
        }
        else if( strcmp(key, "debugname") == 0 )
        {
            char buf[1024];
            SEQ_SET(DEBUG_NAME);
            cp_unescape(value, buf, sizeof(buf));
            free(entry.debug_name);
            entry.debug_name = strdup(buf);
            assert(entry.debug_name);
        }
        else if( strcmp(key, "rs2soundflag") == 0 )
        {
            SEQ_SET(RS2_530_SOUND_FLAG);
            ok = parse_flag(value);
            entry.rs2_530_sound_flag = true;
        }
        else if( strcmp(key, "rs2tweened") == 0 )
        {
            SEQ_SET(RS2_727_TWEENED);
            ok = parse_flag(value);
            entry.rs2_727_tweened = true;
        }
        else if( strcmp(key, "rs2unknown16") == 0 )
        {
            SEQ_SET(RS2_727_UNKNOWN16);
            ok = parse_flag(value);
            entry.rs2_727_unknown16 = true;
        }
        else if( strcmp(key, "rs2vorbissounds") == 0 )
        {
            SEQ_SET(RS2_727_VORBIS_SOUNDS);
            ok = parse_flag(value);
            entry.rs2_727_vorbis_sounds = true;
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "seq [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: seq [%s]: bad value for %s\n", config->debugname, key);
            goto done;
        }
    }

#undef SEQ_SET

    entry.frame_ids = frame_ids.items;
    entry.frame_lengths = frame_lengths.items;
    entry.frame_count = frame_ids.count;
    entry.chat_frame_ids = chat_frames.items;
    entry.chat_frame_id_count = chat_frames.count;
    if( RSCache_PresenceHas(&entry.present, RSCACHE_SEQ_FIELD_INTERLEAVE) )
    {
        /* The decoder terminates the list with 9999999 and the encoder counts up to
         * it, so the sentinel has to be appended here too. */
        cp_intlist_push(&interleave, 9999999);
        entry.interleave_leave = interleave.items;
    }

    written = RSCache_Dat2ConfigSequenceEncode(&ctx->profile, &entry, out, out_capacity);

done:
    entry.frame_ids = NULL;
    entry.frame_lengths = NULL;
    entry.chat_frame_ids = NULL;
    entry.interleave_leave = NULL;
    RSCache_Dat2ConfigSequenceFreeInplace(&entry);
    cp_intlist_free(&frame_ids);
    cp_intlist_free(&frame_lengths);
    cp_intlist_free(&chat_frames);
    cp_intlist_free(&interleave);
    return written;
}

/* ---- spotanim ----------------------------------------------------------- */

/* One key per RSCACHE_SPOTANIM_FIELD_* except TERRAIN, which no OldSchool opcode
 * carries. The colour lists are one opcode each with a count: `recol1s`/`recol1d`
 * .. per pair, `recol=default` when absent, `recol=empty` when stated empty. */
const struct CP_KeySpec cp_spotanim_keys[] = {
    { "model", 0, NULL, NULL },
    { "anim", 0, NULL, NULL },
    { "resizeh", 0, NULL, NULL },
    { "resizev", 0, NULL, NULL },
    { "angle", 0, NULL, NULL },
    { "ambient", 0, NULL, NULL },
    { "contrast", 0, NULL, NULL },
    { "opcode10", 0, NULL, NULL },
    { "recol", CP_KEY_INDEXED, NULL, NULL },
    { "retex", CP_KEY_INDEXED, NULL, NULL },
    { "name", 0, NULL, NULL },
    { NULL, 0, NULL, NULL },
};

int
cp_unpack_spotanim(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigSpotanim* entry =
        RSCache_Dat2ConfigSpotanimNewDecodeProfile(&ctx->profile, (char*)record,
                                                   record_size);
    assert(entry);
    if( entry->_consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "spotanim %d: consumed %d of %d bytes", id,
                entry->_consumed, record_size);

#define SPOTANIM_HAS(field) RSCache_PresenceHas(&entry->present, RSCACHE_SPOTANIM_FIELD_##field)
#define SPOTANIM_INT(field, key, member)                                                    \
    do                                                                                      \
    {                                                                                       \
        if( SPOTANIM_HAS(field) )                                                           \
            cp_lines_addf(out, key "=%d", entry->member);                                   \
        else                                                                                \
            cp_lines_add_default(out, key);                                                 \
    } while( 0 )

    SPOTANIM_INT(MODEL, "model", model);
    if( SPOTANIM_HAS(ANIM) )
        cp_emit_name(ctx, out, "anim", CP_TYPE_SEQ, entry->anim);
    else
        cp_lines_add_default(out, "anim");
    SPOTANIM_INT(RESIZEH, "resizeh", resizeh);
    SPOTANIM_INT(RESIZEV, "resizev", resizev);
    SPOTANIM_INT(ANGLE, "angle", angle);
    SPOTANIM_INT(AMBIENT, "ambient", ambient);
    SPOTANIM_INT(CONTRAST, "contrast", contrast);
    if( SPOTANIM_HAS(UNKNOWN10) )
        cp_lines_addf(out, "opcode10=yes");
    else
        cp_lines_add_default(out, "opcode10");

    if( !SPOTANIM_HAS(RECOL) )
        cp_lines_add_default(out, "recol");
    else if( entry->recol_count == 0 )
        cp_lines_add_empty(out, "recol");
    else
        cp_emit_recols(out, entry->recol_s, entry->recol_d, entry->recol_count, "recol");
    if( !SPOTANIM_HAS(RETEX) )
        cp_lines_add_default(out, "retex");
    else if( entry->retex_count == 0 )
        cp_lines_add_empty(out, "retex");
    else
        cp_emit_recols(out, entry->retex_s, entry->retex_d, entry->retex_count, "retex");

    if( SPOTANIM_HAS(NAME) )
        cp_lines_add_str(out, "name", entry->name);
    else
        cp_lines_add_default(out, "name");

#undef SPOTANIM_INT
#undef SPOTANIM_HAS

    RSCache_Dat2ConfigSpotanimFree(entry);
    return 1;
}

uint32_t
cp_pack_spotanim(
    struct CP_Ctx* ctx,
    int id,
    const struct CP_Config* config,
    uint8_t* out,
    uint32_t out_capacity)
{
    /* The empty-record decode: client defaults, nothing stated. */
    struct RSCache_Dat2ConfigSpotanim* entry = RSCache_Dat2ConfigSpotanimNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    assert(entry);
    (void)id;

#define SPOTANIM_SET(field) RSCache_PresenceSet(&entry->present, RSCACHE_SPOTANIM_FIELD_##field)

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( cp_value_is_default(value) )
            continue;

        if( strcmp(key, "model") == 0 )
        {
            SPOTANIM_SET(MODEL);
            ok = cp_parse_int(value, &entry->model);
        }
        else if( strcmp(key, "anim") == 0 )
        {
            SPOTANIM_SET(ANIM);
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->anim);
        }
        else if( strcmp(key, "resizeh") == 0 )
        {
            SPOTANIM_SET(RESIZEH);
            ok = cp_parse_int(value, &entry->resizeh);
        }
        else if( strcmp(key, "resizev") == 0 )
        {
            SPOTANIM_SET(RESIZEV);
            ok = cp_parse_int(value, &entry->resizev);
        }
        else if( strcmp(key, "angle") == 0 )
        {
            SPOTANIM_SET(ANGLE);
            ok = cp_parse_int(value, &entry->angle);
        }
        else if( strcmp(key, "ambient") == 0 )
        {
            SPOTANIM_SET(AMBIENT);
            ok = cp_parse_int(value, &entry->ambient);
        }
        else if( strcmp(key, "contrast") == 0 )
        {
            SPOTANIM_SET(CONTRAST);
            ok = cp_parse_int(value, &entry->contrast);
        }
        else if( strcmp(key, "opcode10") == 0 )
        {
            /* A payload-free opcode: present is the value, so only `yes`. */
            SPOTANIM_SET(UNKNOWN10);
            ok = strcmp(value, "yes") == 0;
            entry->unknown10 = true;
        }
        else if( strcmp(key, "name") == 0 )
        {
            char buf[1024];
            SPOTANIM_SET(NAME);
            cp_unescape(value, buf, sizeof(buf));
            free(entry->name);
            entry->name = strdup(buf);
            assert(entry->name);
        }
        else if( strcmp(key, "recol") == 0 || strcmp(key, "retex") == 0 )
        {
            /* The bare stem carries only a marker; `default` was skipped above. */
            ok = cp_value_is_empty(value);
            if( key[2] == 'c' )
                SPOTANIM_SET(RECOL);
            else
                SPOTANIM_SET(RETEX);
        }
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* the `Ns` / `Nd` spellings, collected in the second pass below */
        }
        else
            cp_warn(ctx, &ctx->warn_unknown_key, "spotanim [%s]: unknown key %s",
                    config->debugname, key);

        if( !ok )
        {
            fprintf(stderr, "cachepack: spotanim [%s]: bad value for %s\n",
                    config->debugname, key);
            goto done;
        }
    }

    /*
     * The recolour slots are a fixed-size array here rather than an allocation, so
     * they are filled directly. The count is the highest slot named, which is what
     * the wire's count prefix means.
     */
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        size_t klen = strlen(key);
        if( klen < 7 )
            continue;
        int is_recol = strncmp(key, "recol", 5) == 0;
        int is_retex = strncmp(key, "retex", 5) == 0;
        if( !is_recol && !is_retex )
            continue;
        char side = key[klen - 1];
        if( side != 's' && side != 'd' )
            continue;
        char index_text[8];
        if( klen - 6 >= sizeof(index_text) )
            continue;
        memcpy(index_text, key + 5, klen - 6);
        index_text[klen - 6] = '\0';
        int slot = 0;
        if( !cp_parse_int(index_text, &slot) || slot <= 0 ||
            slot > RSCACHE_SPOTANIM_COLOUR_SLOTS )
        {
            fprintf(stderr, "cachepack: spotanim [%s]: bad slot in %s\n", config->debugname,
                    key);
            goto done;
        }
        int value = 0;
        if( !cp_parse_int(config->lines[i].value, &value) )
        {
            fprintf(stderr, "cachepack: spotanim [%s]: bad %s\n", config->debugname, key);
            goto done;
        }
        if( is_recol )
            SPOTANIM_SET(RECOL);
        else
            SPOTANIM_SET(RETEX);
        int* target = is_recol ? (side == 's' ? entry->recol_s : entry->recol_d)
                               : (side == 's' ? entry->retex_s : entry->retex_d);
        target[slot - 1] = value;
        int* count = is_recol ? &entry->recol_count : &entry->retex_count;
        if( slot > *count )
            *count = slot;
    }

#undef SPOTANIM_SET

    written = RSCache_Dat2ConfigSpotanimEncodeRevision(
        ctx->profile.revision, entry, out, out_capacity);

done:
    RSCache_Dat2ConfigSpotanimFree(entry);
    return written;
}
