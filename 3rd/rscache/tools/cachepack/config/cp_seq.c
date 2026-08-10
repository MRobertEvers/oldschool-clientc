#include "cachepack.h"

#include "datatypes/dat2_config_sequence.h"
#include "datatypes/dat2_config_spotanim.h"

#include <stdlib.h>
#include <string.h>

/* ---- sequence ----------------------------------------------------------- */

/*
 * A sequence is the one config type whose *opcode numbers* move between eras, so
 * both directions go through the profile-aware entry points rather than a fixed
 * codec. Getting that wrong does not mis-size a field, it puts the maya id where
 * the frame sounds should be.
 */

int
cp_unpack_seq(
    struct CP_Ctx* ctx,
    int id,
    const uint8_t* record,
    int record_size,
    struct CP_Lines* out)
{
    struct RSCache_Dat2ConfigSequence entry;
    memset(&entry, 0, sizeof(entry));
    RSCache_Dat2ConfigSequenceDecodeProfile(&entry, &ctx->profile, (char*)record, record_size);
    if( entry._consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "seq %d: consumed %d of %d bytes", id,
                entry._consumed, record_size);

    /* Frame id and its length are one thing to a reader and are written as one
     * line; the wire splits them into two parallel runs purely to pack better. */
    for( int i = 0; i < entry.frame_count; i++ )
        cp_lines_addf(out, "frame=%d,%d", entry.frame_ids[i], entry.frame_lengths[i]);

    if( entry.frame_step != -1 )
        cp_lines_addf(out, "framestep=%d", entry.frame_step);
    if( entry.interleave_leave )
    {
        char buf[2048];
        int w = 0;
        for( int i = 0; entry.interleave_leave[i] != 9999999; i++ )
        {
            w += snprintf(buf + w, sizeof(buf) - (size_t)w, i ? ",%d" : "%d",
                          entry.interleave_leave[i]);
            if( w >= (int)sizeof(buf) - 8 )
                break;
        }
        if( w > 0 )
            cp_lines_addf(out, "interleave=%s", buf);
    }
    if( entry.stretches )
        cp_lines_addf(out, "stretches=yes");
    if( entry.forced_priority != 5 )
        cp_lines_addf(out, "forcedpriority=%d", entry.forced_priority);
    cp_emit_ref(ctx, out, "lefthand", CP_TYPE_OBJ, entry.left_hand_item, -1);
    cp_emit_ref(ctx, out, "righthand", CP_TYPE_OBJ, entry.right_hand_item, -1);
    if( entry.max_loops != 99 )
        cp_lines_addf(out, "maxloops=%d", entry.max_loops);
    if( entry.precedence_animating != -1 )
        cp_lines_addf(out, "precedence=%d", entry.precedence_animating);
    if( entry.priority != -1 )
        cp_lines_addf(out, "priority=%d", entry.priority);
    if( entry.reply_mode != 2 )
        cp_lines_addf(out, "replymode=%d", entry.reply_mode);

    for( int i = 0; i < entry.chat_frame_id_count; i++ )
        cp_lines_addf(out, "chatframe=%d", entry.chat_frame_ids[i]);

    if( entry.anim_maya_id )
        cp_lines_addf(out, "mayaid=%d", entry.anim_maya_id);
    if( entry.anim_maya_start || entry.anim_maya_end )
        cp_lines_addf(out, "mayarange=%d,%d", entry.anim_maya_start, entry.anim_maya_end);
    if( entry.anim_maya_masks )
    {
        for( int i = 0; i < 256; i++ )
        {
            if( entry.anim_maya_masks[i] )
                cp_lines_addf(out, "mayamask=%d", i);
        }
    }

    for( int i = 0; i < entry.frame_sounds.count; i++ )
    {
        const struct RSCache_Dat2ConfigFrameSound* s = &entry.frame_sounds.sounds[i];
        cp_lines_addf(out, "sound=%d,%d,%d,%d,%d,%d", entry.frame_sounds.frames[i], s->id,
                      s->loops, s->location, s->retain, s->weight);
    }

    if( entry.vertical_offset )
        cp_lines_addf(out, "verticaloffset=%d", entry.vertical_offset);
    if( entry.sounds_cross_world_view )
        cp_lines_addf(out, "crossworldsounds=yes");
    if( entry.debug_name )
        cp_lines_add_str(out, "debugname", entry.debug_name);

    RSCache_Dat2ConfigSequenceFreeInplace(&entry);
    return 1;
}

static int
frame_sounds_push(
    struct RSCache_Dat2ConfigFrameSoundMap* map,
    int frame,
    const struct RSCache_Dat2ConfigFrameSound* sound)
{
    if( map->count >= map->capacity )
    {
        int next = map->capacity ? map->capacity * 2 : 8;
        int* frames = realloc(map->frames, (size_t)next * sizeof(int));
        if( frames )
            map->frames = frames;
        struct RSCache_Dat2ConfigFrameSound* sounds =
            realloc(map->sounds, (size_t)next * sizeof(*sounds));
        if( sounds )
            map->sounds = sounds;
        if( !frames || !sounds )
            return 0;
        map->capacity = next;
    }
    map->frames[map->count] = frame;
    map->sounds[map->count] = *sound;
    map->count++;
    return 1;
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
    /* Seed RuneLite defaults via the empty-record decode so absent keys leave
     * the encoder's skip-default conditions alone (same pattern as spotanim). */
    RSCache_Dat2ConfigSequenceDecodeProfile(
        &entry, &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    entry.id = id;

    struct CP_IntList frame_ids = { 0 }, frame_lengths = { 0 };
    struct CP_IntList chat_frames = { 0 };
    struct CP_IntList interleave = { 0 };
    uint32_t written = 0;

    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        char scratch[2048];
        char* fields[8];
        int ok = 1;

        if( strcmp(key, "frame") == 0 )
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
        else if( strcmp(key, "chatframe") == 0 )
        {
            int frame = 0;
            ok = cp_parse_int(value, &frame);
            cp_intlist_push(&chat_frames, frame);
        }
        else if( strcmp(key, "interleave") == 0 )
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
        else if( strcmp(key, "framestep") == 0 )
            ok = cp_parse_int(value, &entry.frame_step);
        else if( strcmp(key, "stretches") == 0 )
            ok = cp_parse_bool(value, &entry.stretches);
        else if( strcmp(key, "forcedpriority") == 0 )
            ok = cp_parse_int(value, &entry.forced_priority);
        else if( strcmp(key, "lefthand") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry.left_hand_item);
        else if( strcmp(key, "righthand") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_OBJ, value, &entry.right_hand_item);
        else if( strcmp(key, "maxloops") == 0 )
            ok = cp_parse_int(value, &entry.max_loops);
        else if( strcmp(key, "precedence") == 0 )
            ok = cp_parse_int(value, &entry.precedence_animating);
        else if( strcmp(key, "priority") == 0 )
            ok = cp_parse_int(value, &entry.priority);
        else if( strcmp(key, "replymode") == 0 )
            ok = cp_parse_int(value, &entry.reply_mode);
        else if( strcmp(key, "mayaid") == 0 )
            ok = cp_parse_int(value, &entry.anim_maya_id);
        else if( strcmp(key, "mayarange") == 0 )
        {
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 2) != 2 )
                ok = 0;
            else
                ok = cp_parse_int(fields[0], &entry.anim_maya_start) &&
                     cp_parse_int(fields[1], &entry.anim_maya_end);
        }
        else if( strcmp(key, "mayamask") == 0 )
        {
            int slot = 0;
            ok = cp_parse_int(value, &slot) && slot >= 0 && slot < 256;
            if( ok )
            {
                if( !entry.anim_maya_masks )
                    entry.anim_maya_masks = calloc(256, sizeof(bool));
                if( entry.anim_maya_masks )
                    entry.anim_maya_masks[slot] = true;
                else
                    ok = 0;
            }
        }
        else if( strcmp(key, "sound") == 0 )
        {
            if( strlen(value) >= sizeof(scratch) || cp_split(value, scratch, fields, 6) != 6 )
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
                    ok = frame_sounds_push(&entry.frame_sounds, frame, &sound);
            }
        }
        else if( strcmp(key, "verticaloffset") == 0 )
            ok = cp_parse_int(value, &entry.vertical_offset);
        else if( strcmp(key, "crossworldsounds") == 0 )
            ok = cp_parse_bool(value, &entry.sounds_cross_world_view);
        else if( strcmp(key, "debugname") == 0 )
        {
            char buf[1024];
            cp_unescape(value, buf, sizeof(buf));
            free(entry.debug_name);
            entry.debug_name = strdup(buf);
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

    entry.frame_ids = frame_ids.items;
    entry.frame_lengths = frame_lengths.items;
    entry.frame_count = frame_ids.count;
    entry.chat_frame_ids = chat_frames.items;
    entry.chat_frame_id_count = chat_frames.count;
    if( interleave.count > 0 )
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
    if( !entry )
        return 0;
    if( entry->_consumed != record_size )
        cp_warn(ctx, &ctx->warn_short_decode, "spotanim %d: consumed %d of %d bytes", id,
                entry->_consumed, record_size);

    cp_lines_addf(out, "model=%d", entry->model);
    cp_emit_ref(ctx, out, "anim", CP_TYPE_SEQ, entry->anim, -1);
    if( entry->resizeh != 128 )
        cp_lines_addf(out, "resizeh=%d", entry->resizeh);
    if( entry->resizev != 128 )
        cp_lines_addf(out, "resizev=%d", entry->resizev);
    if( entry->angle )
        cp_lines_addf(out, "angle=%d", entry->angle);
    if( entry->ambient )
        cp_lines_addf(out, "ambient=%d", entry->ambient);
    if( entry->contrast )
        cp_lines_addf(out, "contrast=%d", entry->contrast);
    if( entry->unknown10 )
        cp_lines_addf(out, "opcode10=yes");
    cp_emit_recols(out, entry->recol_s, entry->recol_d, entry->recol_count, "recol");
    cp_emit_recols(out, entry->retex_s, entry->retex_d, entry->retex_count, "retex");
    if( entry->name )
        cp_lines_add_str(out, "name", entry->name);

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
    struct RSCache_Dat2ConfigSpotanim* entry = RSCache_Dat2ConfigSpotanimNewDecodeProfile(
        &ctx->profile, (char*)cp_empty_record, (int)sizeof(cp_empty_record));
    if( !entry )
        return 0;

    uint32_t written = 0;
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        const char* value = config->lines[i].value;
        int ok = 1;

        if( strcmp(key, "model") == 0 )
            ok = cp_parse_int(value, &entry->model);
        else if( strcmp(key, "anim") == 0 )
            ok = cp_resolve_ref(ctx, CP_TYPE_SEQ, value, &entry->anim);
        else if( strcmp(key, "resizeh") == 0 )
            ok = cp_parse_int(value, &entry->resizeh);
        else if( strcmp(key, "resizev") == 0 )
            ok = cp_parse_int(value, &entry->resizev);
        else if( strcmp(key, "angle") == 0 )
            ok = cp_parse_int(value, &entry->angle);
        else if( strcmp(key, "ambient") == 0 )
            ok = cp_parse_int(value, &entry->ambient);
        else if( strcmp(key, "contrast") == 0 )
            ok = cp_parse_int(value, &entry->contrast);
        else if( strcmp(key, "opcode10") == 0 )
            ok = cp_parse_bool(value, &entry->unknown10);
        else if( strcmp(key, "name") == 0 )
        {
            char buf[1024];
            cp_unescape(value, buf, sizeof(buf));
            free(entry->name);
            entry->name = strdup(buf);
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
            continue;
        int value = 0;
        if( !cp_parse_int(config->lines[i].value, &value) )
        {
            fprintf(stderr, "cachepack: spotanim [%s]: bad %s\n", config->debugname, key);
            goto done;
        }
        int* target = is_recol ? (side == 's' ? entry->recol_s : entry->recol_d)
                               : (side == 's' ? entry->retex_s : entry->retex_d);
        target[slot - 1] = value;
        int* count = is_recol ? &entry->recol_count : &entry->retex_count;
        if( slot > *count )
            *count = slot;
    }

    written = RSCache_Dat2ConfigSpotanimEncodeRevision(
        ctx->profile.revision, entry, out, out_capacity);

done:
    RSCache_Dat2ConfigSpotanimFree(entry);
    return written;
}
