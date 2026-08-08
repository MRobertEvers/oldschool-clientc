#include "dat2_config_npc.h"

#include "dat2_entity_ops.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
decode_npc_type(
    struct RSCache_Dat2ConfigNpc* npc,
    int flags,
    struct RSCache_Buffer* buffer);

int
RSCache_Dat2ConfigNpcFlags(const struct RSCache* cache)
{
    int flags = 0;

    /* The head-icon bitfield is an OldSchool-only addition. Non-OSRS (RS2 dat1,
     * RS2 dat2) takes the RS2 opcode set and never reaches the OSRS threshold —
     * comparing an RS2 revision against it would be the D16 trap. */
    if( !RSCache_IsOsrs(cache) )
        return RSCACHE_CONFIG_NPC_DECODE_RS2;

    /* Default true when the cache is unidentified. Every OSRS dat2 cache the
     * client ships is at or past this gate, and the modern shape is what the
     * previous hardcoded `true` produced. */
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_NPC, 210, RSCACHE_NPC_ARCHIVE_REV_210, true) )
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS;

    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_NPC, 231, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV231_FOOTPRINT;
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_NPC, 233, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV233_OP111;
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_NPC, 234, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV234_FLAGS;
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_NPC, 235, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV235_OVERLAP;
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_NPC, 236, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV236_FLAGS;
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_NPC, 237, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
    {
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV237_INT_MODEL_IDS;
        flags |= RSCACHE_CONFIG_NPC_DECODE_REV237_ENTITY_OPS;
    }

    return flags;
}

uint32_t
RSCache_Dat2ConfigNpcEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigNpc* npc,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !npc || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* The decoder calloc's the struct, so every default is zero — there is no
     * init function assigning -1 or true to anything. A field is therefore
     * omitted exactly when it is zero. */

    if( npc->models_count > 0 )
    {
        bool use_int_ids =
            (RSCache_Dat2ConfigNpcFlags(cache) & RSCACHE_CONFIG_NPC_DECODE_REV237_INT_MODEL_IDS) !=
            0;
        if( !use_int_ids )
        {
            for( int i = 0; i < npc->models_count; i++ )
            {
                if( npc->models[i] < 0 || npc->models[i] > 0xFFFF )
                {
                    use_int_ids = true;
                    break;
                }
            }
        }
        if( use_int_ids )
        {
            p1(&buffer, 61);
            p1(&buffer, npc->models_count);
            for( int i = 0; i < npc->models_count; i++ )
                p4(&buffer, npc->models[i]);
        }
        else
        {
            p1(&buffer, 1);
            p1(&buffer, npc->models_count);
            for( int i = 0; i < npc->models_count; i++ )
                p2(&buffer, npc->models[i]);
        }
    }
    /* Emit whenever the pointer is non-NULL, including for the empty string. The
     * decoder leaves `name` NULL when opcode 2 is absent, so "" and absent are
     * genuinely different states — real records do carry an empty name (npc 325 in
     * cache.osrs230, for one), and skipping it loses information. */
    if( npc->name )
    {
        p1(&buffer, 2);
        pjstr(&buffer, npc->name, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( npc->size != 1 )
    {
        p1(&buffer, 12);
        p1(&buffer, npc->size);
    }
    if( npc->standing_animation != -1 )
    {
        p1(&buffer, 13);
        p2(&buffer, npc->standing_animation);
    }

    /* Opcode 17 carries the walk animation plus its three turn variants; opcode 14
     * carries the walk animation alone. Use the compact form when the turn
     * animations are absent, which is what the packer does. */
    if( npc->rotate180_animation != -1 || npc->rotate_left_animation != -1 ||
        npc->rotate_right_animation != -1 )
    {
        p1(&buffer, 17);
        p2(&buffer, npc->walking_animation);
        p2(&buffer, npc->rotate180_animation);
        p2(&buffer, npc->rotate_left_animation);
        p2(&buffer, npc->rotate_right_animation);
    }
    else if( npc->walking_animation != -1 )
    {
        p1(&buffer, 14);
        p2(&buffer, npc->walking_animation);
    }

    if( npc->idle_rotate_left_animation != -1 )
    {
        p1(&buffer, 15);
        p2(&buffer, npc->idle_rotate_left_animation);
    }
    if( npc->idle_rotate_right_animation != -1 )
    {
        p1(&buffer, 16);
        p2(&buffer, npc->idle_rotate_right_animation);
    }
    if( npc->category != 0 )
    {
        p1(&buffer, 18);
        p2(&buffer, npc->category);
    }

    for( int i = 0; i < 5; i++ )
    {
        /* Same reasoning as the name: an empty action is not an absent one. */
        if( npc->actions[i] )
        {
            p1(&buffer, 30 + i);
            pjstr(&buffer, npc->actions[i], RSCACHE_JSTR_TERMINATOR_NULL);
        }
    }

    if( npc->recolor_count > 0 )
    {
        p1(&buffer, 40);
        p1(&buffer, npc->recolor_count);
        for( int i = 0; i < npc->recolor_count; i++ )
        {
            p2(&buffer, npc->recolor_to_find[i]);
            p2(&buffer, npc->recolor_to_replace[i]);
        }
    }
    if( npc->retexture_count > 0 )
    {
        p1(&buffer, 41);
        p1(&buffer, npc->retexture_count);
        for( int i = 0; i < npc->retexture_count; i++ )
        {
            p2(&buffer, npc->retexture_to_find[i]);
            p2(&buffer, npc->retexture_to_replace[i]);
        }
    }

    if( npc->chathead_models_count > 0 )
    {
        bool use_int_ids =
            (RSCache_Dat2ConfigNpcFlags(cache) & RSCACHE_CONFIG_NPC_DECODE_REV237_INT_MODEL_IDS) !=
            0;
        if( !use_int_ids )
        {
            for( int i = 0; i < npc->chathead_models_count; i++ )
            {
                if( npc->chathead_models[i] < 0 || npc->chathead_models[i] > 0xFFFF )
                {
                    use_int_ids = true;
                    break;
                }
            }
        }
        if( use_int_ids )
        {
            p1(&buffer, 62);
            p1(&buffer, npc->chathead_models_count);
            for( int i = 0; i < npc->chathead_models_count; i++ )
                p4(&buffer, npc->chathead_models[i]);
        }
        else
        {
            p1(&buffer, 60);
            p1(&buffer, npc->chathead_models_count);
            for( int i = 0; i < npc->chathead_models_count; i++ )
                p2(&buffer, npc->chathead_models[i]);
        }
    }

    for( int i = 0; i < 6; i++ )
    {
        if( npc->stats[i] != 1 )
        {
            p1(&buffer, 74 + i);
            p2(&buffer, npc->stats[i]);
        }
    }

    if( !npc->is_minimap_visible )
        p1(&buffer, 93);
    if( npc->combat_level != -1 )
    {
        p1(&buffer, 95);
        p2(&buffer, npc->combat_level);
    }
    if( npc->width_scale != 128 )
    {
        p1(&buffer, 97);
        p2(&buffer, npc->width_scale);
    }
    if( npc->height_scale != 128 )
    {
        p1(&buffer, 98);
        p2(&buffer, npc->height_scale);
    }
    if( npc->render_priority == 1 || npc->has_render_priority )
        p1(&buffer, 99);
    if( npc->ambient != 0 )
    {
        p1(&buffer, 100);
        p1b(&buffer, npc->ambient);
    }
    if( npc->contrast != 0 )
    {
        p1(&buffer, 101);
        /* Stored pre-scaled by 5 (decode opcode 101); undo it for the wire.
         * Exact for every value the decoder can produce. */
        p1b(&buffer, npc->contrast / 5);
    }

    /* Opcode 102's shape is era dependent — the whole reason this encoder takes a
     * profile. Pre-210 it is a single u16 sprite index; from 210 it is a bitfield
     * plus a bigsmart/ushortsmart pair per set bit. */
    if( npc->head_icon_count > 0 )
    {
        bool rev210 =
            (RSCache_Dat2ConfigNpcFlags(cache) & RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS) != 0;

        if( !rev210 )
        {
            p1(&buffer, 102);
            p2(&buffer, npc->head_icon_sprite_index ? (int)(uint16_t)npc->head_icon_sprite_index[0]
                                                    : 0);
        }
        else
        {
            int bitfield = 0;
            for( int i = 0; i < npc->head_icon_count && i < 32; i++ )
            {
                if( npc->head_icon_archive_ids[i] != -1 )
                    bitfield |= (1 << i);
            }

            p1(&buffer, 102);
            p1(&buffer, bitfield);
            for( int i = 0; i < npc->head_icon_count; i++ )
            {
                if( (bitfield & (1 << i)) == 0 )
                    continue;
                pbigsmart(&buffer, npc->head_icon_archive_ids[i]);
                pushortsmart(&buffer, (int)npc->head_icon_sprite_index[i] + 1);
            }
        }
    }

    if( npc->rotation_speed != 32 )
    {
        p1(&buffer, 103);
        p2(&buffer, npc->rotation_speed);
    }
    if( !npc->is_interactable )
        p1(&buffer, 107);
    if( !npc->rotation_flag )
        p1(&buffer, 109);
    /* Opcode 111 is overloaded: pre-233 it sets BOTH isFollower and
     * lowPriorityFollowerOps; from 233 it sets renderPriority=2. Prefer the
     * dedicated 122/123 opcodes when only one of the follower flags is set. */
    if( npc->render_priority == 2 )
        p1(&buffer, 111);
    else if(
        npc->is_pet && npc->low_priority_follower_ops &&
        !(RSCache_Dat2ConfigNpcFlags(cache) & RSCACHE_CONFIG_NPC_DECODE_REV233_OP111) &&
        !(RSCache_Dat2ConfigNpcFlags(cache) & RSCACHE_CONFIG_NPC_DECODE_RS2) )
        p1(&buffer, 111);

    /* 115 carries the run animation plus turn variants, 114 the run alone. */
    if( npc->run_rotate180_animation != -1 || npc->run_rotate_left_animation != -1 ||
        npc->run_rotate_right_animation != -1 )
    {
        p1(&buffer, 115);
        p2(&buffer, npc->run_animation);
        p2(&buffer, npc->run_rotate180_animation);
        p2(&buffer, npc->run_rotate_left_animation);
        p2(&buffer, npc->run_rotate_right_animation);
    }
    else if( npc->run_animation != -1 )
    {
        p1(&buffer, 114);
        p2(&buffer, npc->run_animation);
    }

    if( npc->crawl_rotate180_animation != -1 || npc->crawl_rotate_left_animation != -1 ||
        npc->crawl_rotate_right_animation != -1 )
    {
        p1(&buffer, 117);
        p2(&buffer, npc->crawl_animation);
        p2(&buffer, npc->crawl_rotate180_animation);
        p2(&buffer, npc->crawl_rotate_left_animation);
        p2(&buffer, npc->crawl_rotate_right_animation);
    }
    else if( npc->crawl_animation != -1 )
    {
        p1(&buffer, 116);
        p2(&buffer, npc->crawl_animation);
    }

    /* Opcodes 106 and 118 both carry the varbit/varp pair and the config list;
     * 118 adds a trailing value that the decoder parks in the last config slot,
     * where 106 always leaves -1. Emit 106 when that slot is -1, 118 otherwise. */
    if( npc->configs_count >= 2 )
    {
        int trailing = npc->configs[npc->configs_count - 1];
        int listed = npc->configs_count - 1; /* entries the loop wrote, i.e. length+1 */

        p1(&buffer, trailing == -1 ? 106 : 118);
        p2(&buffer, npc->varbit_id == -1 ? 0xFFFF : npc->varbit_id);
        p2(&buffer, npc->varp_index == -1 ? 0xFFFF : npc->varp_index);
        if( trailing != -1 )
            p2(&buffer, trailing);
        p1(&buffer, listed - 1); /* the wire length is one less than entries written */
        for( int i = 0; i < listed; i++ )
            p2(&buffer, npc->configs[i] == -1 ? 0xFFFF : npc->configs[i]);
    }

    if( npc->low_priority_follower_ops &&
        !(RSCache_Dat2ConfigNpcFlags(cache) & RSCACHE_CONFIG_NPC_DECODE_RS2) )
        p1(&buffer, 123);
    if( npc->is_pet && !(RSCache_Dat2ConfigNpcFlags(cache) & RSCACHE_CONFIG_NPC_DECODE_RS2) )
        p1(&buffer, 122);
    if( npc->height != -1 )
    {
        p1(&buffer, 124);
        p2(&buffer, npc->height);
    }
    if( npc->footprint_size != -1 )
    {
        /* Only emit when explicitly set (not the post-decode default alone).
         * Records that omitted 126 and got the default will re-decode the same
         * default, so omitting here is semantic-preserving. Detect "explicit"
         * by comparing against the formula — if it matches the default, skip. */
        int default_fp = (int)(0.4f * (float)(npc->size * 128));
        if( npc->footprint_size != default_fp )
        {
            p1(&buffer, 126);
            p2(&buffer, npc->footprint_size);
        }
    }
    if( npc->unknown1 )
        p1(&buffer, 129);
    if( npc->idle_anim_restart )
        p1(&buffer, 130);
    if( npc->can_hide_for_overlap )
        p1(&buffer, 145);
    if( npc->overlap_tint_hsl != 39188 )
    {
        p1(&buffer, 146);
        p2(&buffer, npc->overlap_tint_hsl);
    }
    if( !npc->zbuf )
        p1(&buffer, 147);
    if( npc->entity_ops.sub_ops_count > 0 || npc->entity_ops.cond_ops_count > 0 ||
        npc->entity_ops.cond_sub_ops_count > 0 )
    {
        RSCache_EntityOpsEncode(&npc->entity_ops, &buffer, 30, 251, 252, 253);
    }
    if( npc->params.count > 0 )
    {
        p1(&buffer, 249);
        pparams(&buffer, &npc->params);
    }

    p1(&buffer, 0);
    return buffer.position;
}

void
RSCache_Dat2ConfigNpcInit(struct RSCache_Dat2ConfigNpc* npc)
{
    if( !npc )
        return;
    /* Match RuneLite NpcDefinition defaults so "absent" and "present at default"
     * stay distinguishable, and so clear-opcodes 93/107/109 are reproducible. */
    npc->bas_type_id = -1;
    npc->footprint_size = -1;
    /*
     * No movement sounds unless opcode 134 says otherwise. Zero would not do:
     * 0 is a real sound-effect id, so leaving these zeroed would give every npc
     * in the game the same sound.
     */
    npc->sound_idle = -1;
    npc->sound_crawl = -1;
    npc->sound_walk = -1;
    npc->sound_run = -1;
    npc->sound_radius = 0;
    /* Opcode 140 scales these; full volume when it is absent. */
    npc->ambient_sound_volume = 255;
    npc->zbuf = true;
    npc->size = 1;
    npc->standing_animation = -1;
    npc->idle_rotate_left_animation = -1;
    npc->idle_rotate_right_animation = -1;
    npc->walking_animation = -1;
    npc->rotate180_animation = -1;
    npc->rotate_left_animation = -1;
    npc->rotate_right_animation = -1;
    npc->run_animation = -1;
    npc->run_rotate180_animation = -1;
    npc->run_rotate_left_animation = -1;
    npc->run_rotate_right_animation = -1;
    npc->crawl_animation = -1;
    npc->crawl_rotate180_animation = -1;
    npc->crawl_rotate_left_animation = -1;
    npc->crawl_rotate_right_animation = -1;
    npc->is_minimap_visible = true;
    npc->combat_level = -1;
    npc->width_scale = 128;
    npc->height_scale = 128;
    npc->rotation_speed = 32;
    npc->is_interactable = true;
    npc->rotation_flag = true;
    npc->varbit_id = -1;
    npc->varp_index = -1;
    npc->height = -1;
    npc->overlap_tint_hsl = 39188;
    for( int i = 0; i < 6; i++ )
        npc->stats[i] = 1;
    RSCache_EntityOpsInit(&npc->entity_ops);
}

void
RSCache_Dat2ConfigNpcFinish(struct RSCache_Dat2ConfigNpc* npc, unsigned flags)
{
    if( !npc )
        return;
    /* Only knowable once the stream is exhausted: opcode 126 may or may not have
     * supplied a footprint, and the fallback is derived from `size`, which an
     * earlier or later opcode 12 could have changed. */
    if( (flags & RSCACHE_CONFIG_NPC_DECODE_REV231_FOOTPRINT) && npc->footprint_size == -1 )
        npc->footprint_size = (int)(0.4f * (float)(npc->size * 128));
}

struct RSCache_Dat2ConfigNpc*
RSCache_Dat2ConfigNpcNewDecodeProfile(
    const struct RSCache* cache,
    char* data,
    int data_size)
{
    struct RSCache_Dat2ConfigNpc* npc = calloc(1, sizeof(struct RSCache_Dat2ConfigNpc));
    if( !npc )
    {
        printf("RSCache_Dat2ConfigNpcNewDecode: Failed to allocate memory for NPCType\n");
        return NULL;
    }
    RSCache_Dat2ConfigNpcInit(npc);

    int flags = RSCache_Dat2ConfigNpcFlags(cache);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)(data), (uint32_t)(data_size));

    decode_npc_type(npc, flags, &buffer);

    RSCache_Dat2ConfigNpcFinish(npc, (unsigned)flags);

    return npc;
}

void
RSCache_Dat2ConfigNpcFreeInplace(struct RSCache_Dat2ConfigNpc* npc)
{
    if( !npc )
        return;

    free(npc->name);
    free(npc->models);
    free(npc->recolor_to_find);
    free(npc->recolor_to_replace);
    free(npc->retexture_to_find);
    free(npc->retexture_to_replace);
    free(npc->chathead_models);
    free(npc->head_icon_archive_ids);
    free(npc->head_icon_sprite_index);
    free(npc->actions[0]);
    free(npc->actions[1]);
    free(npc->actions[2]);
    free(npc->actions[3]);
    free(npc->actions[4]);
    free(npc->configs);
    RSCache_EntityOpsFreeInplace(&npc->entity_ops);
    for( int i = 0; i < npc->params.count; i++ )
        free(npc->params.values[i]);
    free(npc->params.keys);
    free(npc->params.values);
    free(npc->params.kinds);
}

void
RSCache_Dat2ConfigNpcFree(struct RSCache_Dat2ConfigNpc* npc)
{
    if( !npc )
        return;
    RSCache_Dat2ConfigNpcFreeInplace(npc);
    free(npc);
}

/**
 * @brief
 *
 * Runelite//Users/matthewevers/Documents/git_repos/runelite/cache/src/main/java/net/runelite/cache/definitions/loaders/NpcLoader.java
 *
 * @param npc
 * @param buffer
 */
/*
 * One opcode of an npc record.
 *
 * Lifted verbatim out of `decode_npc_type`'s loop: every case body is unchanged,
 * because `break` in a switch case already means "leave the switch", and falling
 * out of the switch here means the opcode was handled. Only two things differ —
 * a bail-out inside a case returns false instead of returning from the whole
 * decode, and the gated debug line lost its `(previous %d)` field, which was loop
 * state a per-opcode handler has no access to.
 *
 * `flags` is not optional. Opcode 102 *changes shape* at rev 210 and six further
 * gates ride on the same word (see RSCache_Dat2ConfigNpcFlags), so a caller that
 * passes 0 decodes a modern cache wrongly and silently.
 */
bool
RSCache_Dat2ConfigNpcDecodeOp(
    struct RSCache_Dat2ConfigNpc* npc,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    bool rev210_head_icons = (flags & RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS) != 0;
    bool rs2 = (flags & RSCACHE_CONFIG_NPC_DECODE_RS2) != 0;

    (void)rev210_head_icons;
    (void)rs2;

        switch( opcode )
        {
        case 1:
        {
            if( buffer->position >= buffer->size )
            {
                printf("decode_npc_type: Buffer overflow in case 1\n");
                return false;
            }
            int length = g1(buffer);
            npc->models = malloc(length * sizeof(int));
            if( !npc->models )
            {
                printf("decode_npc_type: Failed to allocate models array of size %d\n", length);
                return false;
            }
            npc->models_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                if( buffer->position + 1 >= buffer->size )
                {
                    printf(
                        "decode_npc_type: Buffer overflow while reading models at index %d\n", idx);
                    free(npc->models);
                    npc->models = NULL;
                    npc->models_count = 0;
                    return false;
                }
                npc->models[idx] = g2(buffer);
            }
            break;
        }
        case 2:
        {
            // Read string length first
            int str_len = 0;
            while( buffer->position + str_len < buffer->size &&
                   buffer->data[buffer->position + str_len] != '\0' )
            {
                str_len++;
            }
            if( buffer->position + str_len >= buffer->size )
            {
                printf("decode_npc_type: Buffer overflow while reading name string\n");
                return false;
            }
            npc->name = malloc(str_len + 1);
            if( !npc->name )
            {
                printf("decode_npc_type: Failed to allocate name string of length %d\n", str_len);
                return false;
            }
            memset(npc->name, 0, str_len + 1);
            greadto(buffer, npc->name, str_len + 1, str_len + 1);
            break;
        }
        case 12:
        {
            npc->size = g1(buffer);
            break;
        }
        case 13:
        {
            npc->standing_animation = g2(buffer);
            break;
        }
        case 14:
        {
            npc->walking_animation = g2(buffer);
            break;
        }
        case 15:
        {
            npc->idle_rotate_left_animation = g2(buffer);
            break;
        }
        case 16:
        {
            npc->idle_rotate_right_animation = g2(buffer);
            break;
        }
        case 17:
        {
            npc->walking_animation = g2(buffer);
            npc->rotate180_animation = g2(buffer);
            npc->rotate_left_animation = g2(buffer);
            npc->rotate_right_animation = g2(buffer);
            break;
        }
        case 18:
        {
            npc->category = g2(buffer);
            break;
        }
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        {
            int idx = opcode - 30;
            // Read string length first
            int str_len = 0;
            while( buffer->position + str_len < buffer->size &&
                   buffer->data[buffer->position + str_len] != '\0' )
            {
                str_len++;
            }
            if( buffer->position + str_len >= buffer->size )
            {
                return false;
            }
            npc->actions[idx] = malloc(str_len + 1);
            greadto(buffer, npc->actions[idx], str_len + 1, str_len + 1);

            // Check if string is "Hidden"
            if( strcmp(npc->actions[idx], "Hidden") == 0 )
            {
                free(npc->actions[idx]);
                npc->actions[idx] = NULL;
            }
            break;
        }
        case 40:
        {
            int length = g1(buffer);
            npc->recolor_to_find = malloc(length * sizeof(int));
            npc->recolor_to_replace = malloc(length * sizeof(int));
            npc->recolor_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->recolor_to_find[idx] = g2(buffer);
                npc->recolor_to_replace[idx] = g2(buffer);
            }
            break;
        }
        case 41:
        {
            int length = g1(buffer);
            npc->retexture_to_find = malloc(length * sizeof(int));
            npc->retexture_to_replace = malloc(length * sizeof(int));
            npc->retexture_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->retexture_to_find[idx] = g2(buffer);
                npc->retexture_to_replace[idx] = g2(buffer);
            }
            break;
        }
        case 60:
        {
            int length = g1(buffer);
            npc->chathead_models = malloc(length * sizeof(int));
            npc->chathead_models_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->chathead_models[idx] = g2(buffer);
            }
            break;
        }
        case 61:
        {
            /* Rev 237+: int model ids (replaces / supplements opcode 1's u16). */
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV237_INT_MODEL_IDS) )
                goto unknown_opcode;
            {
                int length = g1(buffer);
                free(npc->models);
                npc->models = malloc((size_t)length * sizeof(int));
                assert(npc->models);
                npc->models_count = length;
                for( int idx = 0; idx < length; ++idx )
                    npc->models[idx] = g4(buffer);
            }
            break;
        }
        case 62:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV237_INT_MODEL_IDS) )
                goto unknown_opcode;
            {
                int length = g1(buffer);
                free(npc->chathead_models);
                npc->chathead_models = malloc((size_t)length * sizeof(int));
                assert(npc->chathead_models);
                npc->chathead_models_count = length;
                for( int idx = 0; idx < length; ++idx )
                    npc->chathead_models[idx] = g4(buffer);
            }
            break;
        }
        case 74:
        {
            npc->stats[0] = g2(buffer);
            break;
        }
        case 75:
        {
            npc->stats[1] = g2(buffer);
            break;
        }
        case 76:
        {
            npc->stats[2] = g2(buffer);
            break;
        }
        case 77:
        {
            npc->stats[3] = g2(buffer);
            break;
        }
        case 78:
        {
            npc->stats[4] = g2(buffer);
            break;
        }
        case 79:
        {
            npc->stats[5] = g2(buffer);
            break;
        }
        case 93:
        {
            npc->is_minimap_visible = false;
            break;
        }
        case 95:
        {
            npc->combat_level = g2(buffer);
            break;
        }
        case 97:
        {
            npc->width_scale = g2(buffer);
            break;
        }
        case 98:
        {
            npc->height_scale = g2(buffer);
            break;
        }
        case 99:
        {
            npc->has_render_priority = true;
            npc->render_priority = 1;
            break;
        }
        case 100:
        {
            /* Signed. Reference NpcType opcode 100 is `g1b` (Client-TS
             * config/NpcType.ts) — a darkening ambient is a *negative* byte and
             * reading it unsigned turned -25 into 231, which saturates
             * calculateNormals' `ambient + 64` and renders the model white. */
            npc->ambient = g1b(buffer);
            break;
        }
        case 101:
        {
            /* Signed, and pre-scaled by 5 the way the reference stores it
             * (`g1b * 5`), matching obj opcode 114. loc opcode 39 uses 25. */
            npc->contrast = g1b(buffer) * 5;
            break;
        }
        case 102:
        {
            /* rev210_head_icons comes from the enclosing scope — see
             * RSCACHE_CONFIG_NPC_DECODE_REV210_HEAD_ICONS. It used to be
             * shadowed here by a hardcoded `true`. */
            int default_head_icon_archive = -1;

            if( !rev210_head_icons )
            {
                npc->head_icon_archive_ids = malloc(sizeof(int));
                npc->head_icon_sprite_index = malloc(sizeof(short));
                npc->head_icon_count = 1;
                npc->head_icon_archive_ids[0] = default_head_icon_archive;
                npc->head_icon_sprite_index[0] = (short)(g2(buffer));
            }
            else
            {
                int bitfield = g1(buffer);
                int len = 0;
                for( int var5 = bitfield; var5 != 0; var5 >>= 1 )
                {
                    ++len;
                }

                npc->head_icon_archive_ids = malloc(len * sizeof(int));
                npc->head_icon_sprite_index = malloc(len * sizeof(short));
                npc->head_icon_count = len;

                for( int i = 0; i < len; i++ )
                {
                    if( (bitfield & (1 << i)) == 0 )
                    {
                        npc->head_icon_archive_ids[i] = -1;
                        npc->head_icon_sprite_index[i] = -1;
                    }
                    else
                    {
                        npc->head_icon_archive_ids[i] = gbigsmart(buffer);
                        npc->head_icon_sprite_index[i] = gushortsmart(buffer) - 1;
                    }
                }
            }
            break;
        }
        case 103:
        {
            npc->rotation_speed = g2(buffer);
            break;
        }
        case 106:
        {
            npc->varbit_id = g2(buffer);
            if( npc->varbit_id == 0xFFFF )
            {
                npc->varbit_id = -1;
            }

            npc->varp_index = g2(buffer);
            if( npc->varp_index == 0xFFFF )
            {
                npc->varp_index = -1;
            }

            int length = g1(buffer);
            npc->configs = malloc((length + 2) * sizeof(int));
            npc->configs_count = length + 2;

            for( int idx = 0; idx <= length; ++idx )
            {
                npc->configs[idx] = g2(buffer);
                if( npc->configs[idx] == 0xFFFF )
                {
                    npc->configs[idx] = -1;
                }
            }

            npc->configs[length + 1] = -1;
            break;
        }
        case 107:
        {
            npc->is_interactable = false;
            break;
        }
        case 109:
        {
            npc->rotation_flag = false;
            break;
        }
        case 111:
        {
            /* Pre-233: isFollower + lowPriorityFollowerOps. From 233: renderPriority=2. */
            if( flags & RSCACHE_CONFIG_NPC_DECODE_REV233_OP111 )
            {
                npc->render_priority = 2;
                npc->has_render_priority = true;
            }
            else
            {
                npc->is_pet = true;
                npc->low_priority_follower_ops = true;
            }
            break;
        }
        case 114:
        {
            /* RS2 spends the same two bytes on a pair of shadow-colour modifiers. Nothing
             * reads them, but the width matches so the stream stays aligned either way. */
            if( rs2 )
            {
                g1(buffer);
                g1(buffer);
            }
            else
                npc->run_animation = g2(buffer);
            break;
        }
        case 115:
        {
            /* Two bytes in RS2 against four sequence ids in OldSchool — the six-byte
             * difference that loses most of a 643 npc record. */
            if( rs2 )
            {
                g1(buffer);
                g1(buffer);
            }
            else
            {
                npc->run_animation = g2(buffer);
                npc->run_rotate180_animation = g2(buffer);
                npc->run_rotate_left_animation = g2(buffer);
                npc->run_rotate_right_animation = g2(buffer);
            }
            break;
        }
        case 116:
        {
            npc->crawl_animation = g2(buffer);
            break;
        }
        case 117:
        {
            npc->crawl_animation = g2(buffer);
            npc->crawl_rotate180_animation = g2(buffer);
            npc->crawl_rotate_left_animation = g2(buffer);
            npc->crawl_rotate_right_animation = g2(buffer);
            break;
        }
        case 118:
        {
            npc->varbit_id = g2(buffer);
            if( npc->varbit_id == 0xFFFF )
            {
                npc->varbit_id = -1;
            }

            npc->varp_index = g2(buffer);
            if( npc->varp_index == 0xFFFF )
            {
                npc->varp_index = -1;
            }

            int var = g2(buffer);
            if( var == 0xFFFF )
            {
                var = -1;
            }

            int length = g1(buffer);
            npc->configs = malloc((length + 2) * sizeof(int));
            npc->configs_count = length + 2;

            for( int idx = 0; idx <= length; ++idx )
            {
                npc->configs[idx] = g2(buffer);
                if( npc->configs[idx] == 0xFFFF )
                {
                    npc->configs[idx] = -1;
                }
            }

            npc->configs[length + 1] = var;
            break;
        }
        case 122:
        {
            /* A flag in OldSchool, a hit-bar sprite id in RS2. */
            if( rs2 )
                g2(buffer);
            else
                npc->is_pet = true;
            break;
        }
        case 123:
        {
            /* A flag in OldSchool, an icon height in RS2. */
            if( rs2 )
                g2(buffer);
            else
                npc->low_priority_follower_ops = true;
            break;
        }
        case 124:
        {
            npc->height = g2(buffer);
            break;
        }
        case 126:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV231_FOOTPRINT) )
                goto unknown_opcode;
            npc->footprint_size = g2(buffer);
            break;
        }
        case 129:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV234_FLAGS) )
                goto unknown_opcode;
            npc->unknown1 = true;
            break;
        }
        case 130:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV236_FLAGS) )
                goto unknown_opcode;
            npc->idle_anim_restart = true;
            break;
        }

        /*
         * Opcodes this decoder consumes but does not store.
         *
         * Every 643 npc record uses at least one of them — before they were added, 13,636 of
         * 13,636 stopped short — so they are not optional padding, they are the reason the
         * type would not decode at all. Widths are NpcType.decodeOpcode's; the fields
         * themselves (shadow colours, cursors, sound ids, login-screen props) have no
         * consumer in this client, and inventing struct members for them would make the
         * encoder lossy in a way the round-trip suite would then have to be taught to
         * ignore. Consuming them keeps the stream aligned, which is what the rest of the
         * record depends on.
         *
         * Opcode 127 (`basTypeId`) is stored: a 643 npc's idle/walk come from the
         * BasType (config group 32) rather than opcodes 13/14. See dat2_config_bas.h.
         */
        case 127:
            npc->bas_type_id = g2(buffer);
            break;

        case 44:
        case 45:
        case 137:  /* attack cursor */
        case 138:  /* icon (u16 below the large-model-id era) */
        case 139:  /* icon */
        case 142:  /* map function id */
        case 144:
        case 164:  /* two shorts */
        case 170:  /* 170..175 are all a single short */
        case 171:
        case 172:
        case 173:
        case 174:
        case 175:
            g2(buffer);
            if( opcode == 164 )
                g2(buffer);
            break;

        case 146:
        {
            if( flags & RSCACHE_CONFIG_NPC_DECODE_REV235_OVERLAP )
                npc->overlap_tint_hsl = g2(buffer);
            else
                g2(buffer);
            break;
        }

        case 113: /* two shadow colours */
            g2(buffer);
            g2(buffer);
            break;

        case 140:
            npc->ambient_sound_volume = g1(buffer);
            break;

        case 119: /* loginScreenProps */
        case 125: /* spawnDirection */
        case 128:
        case 163:
        case 165:
        case 168:
            g1(buffer);
            break;

        /* Payload-free flags. */
        case 112:
        case 141:
        case 143:
        case 158:
        case 159:
        case 161:
        case 162:
            break;

        case 145:
        {
            if( flags & RSCACHE_CONFIG_NPC_DECODE_REV235_OVERLAP )
                npc->can_hide_for_overlap = true;
            break;
        }
        case 147:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV236_FLAGS) )
                goto unknown_opcode;
            npc->zbuf = false;
            break;
        }

        case 121:
        {
            /* Per-model translation: a count, then for each, the model index it applies to
             * and a signed xyz offset. */
            int count = g1(buffer);
            for( int idx = 0; idx < count; idx++ )
            {
                g1(buffer);
                g1b(buffer);
                g1b(buffer);
                g1b(buffer);
            }
            break;
        }

        case 134:
        {
            /*
             * Movement sounds: idle, crawl, walk, run, then an audible radius.
             *
             * The order is the movement speed order the rest of the npc record
             * uses (the walk/run animation opcodes are laid out the same way),
             * and 65535 is this format's "absent" for a u2 id.
             */
            npc->sound_idle = g2(buffer);
            npc->sound_crawl = g2(buffer);
            npc->sound_walk = g2(buffer);
            npc->sound_run = g2(buffer);
            npc->sound_radius = g1(buffer);
            if( npc->sound_idle == 65535 )
                npc->sound_idle = -1;
            if( npc->sound_crawl == 65535 )
                npc->sound_crawl = -1;
            if( npc->sound_walk == 65535 )
                npc->sound_walk = -1;
            if( npc->sound_run == 65535 )
                npc->sound_run = -1;
            break;
        }

        case 135: /* cursor op + cursor */
        case 136:
            g1(buffer);
            g2(buffer);
            break;

        case 150:
        case 151:
        case 152:
        case 153:
        case 154:
        {
            /* Members-only actions. The reference reads the string and then discards it
             * unless the account is a member, which this decoder has no notion of, so the
             * action is dropped and only the bytes are consumed — a NUL-terminated string,
             * scanned the same way the 30..34 actions above are. */
            while( buffer->position < buffer->size && buffer->data[buffer->position] != '\0' )
                buffer->position++;
            if( buffer->position >= buffer->size )
            {
                npc->_consumed = (int)buffer->position;
                return false;
            }
            buffer->position++; /* the terminator */
            break;
        }

        case 155: /* four signed bytes */
            g1b(buffer);
            g1b(buffer);
            g1b(buffer);
            g1b(buffer);
            break;

        case 160:
        {
            int count = g1(buffer);
            for( int idx = 0; idx < count; idx++ )
                g2(buffer);
            break;
        }

        case 249:
        {
            gparams(buffer, &npc->params);
            break;
        }
        case 251:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV237_ENTITY_OPS) )
                goto unknown_opcode;
            RSCache_EntityOpsDecodeSubOp(&npc->entity_ops, buffer);
            break;
        }
        case 252:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV237_ENTITY_OPS) )
                goto unknown_opcode;
            RSCache_EntityOpsDecodeCondOp(&npc->entity_ops, buffer);
            break;
        }
        case 253:
        {
            if( !(flags & RSCACHE_CONFIG_NPC_DECODE_REV237_ENTITY_OPS) )
                goto unknown_opcode;
            RSCache_EntityOpsDecodeCondSubOp(&npc->entity_ops, buffer);
            break;
        }
        default:
        unknown_opcode:
        {
            /*
             * Stop, do not continue. An unknown opcode has an unknown payload length, so
             * every read after it comes from the middle of that payload — the decoder keeps
             * going, "recognises" opcodes that are really payload bytes, and writes garbage
             * into real fields before finally running off the end. Falling through here is
             * what made 643's npc failures indistinguishable from each other: 12,762 of
             * 13,636 records reported the same buffer-overrun, with the actual unknown
             * opcode thousands of bytes and several bogus fields earlier.
             *
             * Leaving `_consumed` short is the signal the caller needs; the loc decoder has
             * taken this position since it was written.
             */
            /* Named only on request. The always-on signal is the short `_consumed`, which
             * every caller already checks; `cache.osrs239` alone hits this 13,784 times per
             * corpus scan (its own recorded gap), and printing that unconditionally buries
             * the rest of a test run. */
            if( getenv("RSCACHE_NPC_DEBUG") )
                fprintf(
                    stderr,
                    "npc: unimplemented opcode %d at offset %d\n",
                    opcode,
                    (int)buffer->position - 1);
            npc->_consumed = (int)buffer->position;
            return false;
        }
        }

    /* Fell out of the switch: a case handled this opcode. */
    return true;
}

static void
decode_npc_type(
    struct RSCache_Dat2ConfigNpc* npc,
    int flags,
    struct RSCache_Buffer* buffer)
{
    assert(npc);
    assert(buffer);
    assert(buffer->data);

    while( 1 )
    {
        if( buffer->position >= buffer->size )
        {
            printf(
                "decode_npc_type: Buffer position %d exceeded data size %d\n",
                buffer->position,
                buffer->size);
            return;
        }

        int opcode = g1(buffer);
        if( opcode == 0 )
        {
            npc->_consumed = (int)buffer->position;
            return;
        }

        if( !RSCache_Dat2ConfigNpcDecodeOp(npc, opcode, buffer, (unsigned)flags) )
            return;
    }
}

void
RSCache_Dat2ConfigNpcPrint(const struct RSCache_Dat2ConfigNpc* npc)
{
    printf("NPC Type Information:\n");
    printf("====================\n");

    // Print name
    printf("Name: %s\n", npc->name ? npc->name : "NULL");

    // Print models
    printf("Models (%d): ", npc->models_count);
    if( npc->models )
    {
        for( int i = 0; i < npc->models_count; i++ )
        {
            printf("%d ", npc->models[i]);
        }
    }
    printf("\n");

    // Print chathead_models
    printf("Chathead Models (%d): ", npc->chathead_models_count);
    if( npc->chathead_models )
    {
        for( int i = 0; i < npc->chathead_models_count; i++ )
        {
            printf("%d ", npc->chathead_models[i]);
        }
    }
    printf("\n");

    // Print animations
    printf("Animations:\n");
    printf("  Standing: %d\n", npc->standing_animation);
    printf("  Walking: %d\n", npc->walking_animation);
    printf("  Idle Rotate Left: %d\n", npc->idle_rotate_left_animation);
    printf("  Idle Rotate Right: %d\n", npc->idle_rotate_right_animation);
    printf("  Rotate 180: %d\n", npc->rotate180_animation);
    printf("  Rotate Left: %d\n", npc->rotate_left_animation);
    printf("  Rotate Right: %d\n", npc->rotate_right_animation);
    printf("  Run: %d\n", npc->run_animation);
    printf("  Run Rotate 180: %d\n", npc->run_rotate180_animation);
    printf("  Run Rotate Left: %d\n", npc->run_rotate_left_animation);
    printf("  Run Rotate Right: %d\n", npc->run_rotate_right_animation);
    printf("  Crawl: %d\n", npc->crawl_animation);
    printf("  Crawl Rotate 180: %d\n", npc->crawl_rotate180_animation);
    printf("  Crawl Rotate Left: %d\n", npc->crawl_rotate_left_animation);
    printf("  Crawl Rotate Right: %d\n", npc->crawl_rotate_right_animation);

    // Print actions
    printf("Actions:\n");
    for( int i = 0; i < 5; i++ )
    {
        printf("  Action %d: %s\n", i + 30, npc->actions[i] ? npc->actions[i] : "NULL");
    }

    // Print recoloring info
    printf("Recoloring (%d pairs):\n", npc->recolor_count);
    if( npc->recolor_to_find && npc->recolor_to_replace )
    {
        for( int i = 0; i < npc->recolor_count; i++ )
        {
            printf("  %d -> %d\n", npc->recolor_to_find[i], npc->recolor_to_replace[i]);
        }
    }

    // Print retexturing info
    printf("Retexturing (%d pairs):\n", npc->retexture_count);
    if( npc->retexture_to_find && npc->retexture_to_replace )
    {
        for( int i = 0; i < npc->retexture_count; i++ )
        {
            printf("  %d -> %d\n", npc->retexture_to_find[i], npc->retexture_to_replace[i]);
        }
    }

    // Print other properties
    printf("Properties:\n");
    printf("  Size: %d\n", npc->size);
    printf("  Combat Level: %d\n", npc->combat_level);
    printf("  Width Scale: %d\n", npc->width_scale);
    printf("  Height Scale: %d\n", npc->height_scale);
    printf("  Ambient: %d\n", npc->ambient);
    printf("  Contrast: %d\n", npc->contrast);

    // Print flags
    printf("Flags:\n");
    printf("  Is Minimap Visible: %s\n", npc->is_minimap_visible ? "true" : "false");
    printf("  Has Render Priority: %s\n", npc->has_render_priority ? "true" : "false");
    printf("  Is Interactable: %s\n", npc->is_interactable ? "true" : "false");
    printf("  Rotation Flag: %s\n", npc->rotation_flag ? "true" : "false");
    printf("  Is Pet: %s\n", npc->is_pet ? "true" : "false");
    printf("  Low Priority Follower Ops: %s\n", npc->low_priority_follower_ops ? "true" : "false");

    // Print special integers
    printf("Special Integers:\n");
    printf("  Rotation Speed: %d\n", npc->rotation_speed);
    printf("  Height: %d\n", npc->height);
    printf("  Category: %d\n", npc->category);
    printf("  Stats:\n");
    printf("    Stat 0: %d\n", npc->stats[0]);
    printf("    Stat 1: %d\n", npc->stats[1]);
    printf("    Stat 2: %d\n", npc->stats[2]);
    printf("    Stat 3: %d\n", npc->stats[3]);
    printf("    Stat 4: %d\n", npc->stats[4]);
    printf("    Stat 5: %d\n", npc->stats[5]);

    // Print configs
    printf("Configs (%d elements): ", npc->configs_count);
    if( npc->configs )
    {
        for( int i = 0; i < npc->configs_count; i++ )
        {
            printf("%d ", npc->configs[i]);
        }
    }
    printf("\n");

    // Print params
    printf("Params (%d entries):\n", npc->params.count);
    for( int i = 0; i < npc->params.count; i++ )
    {
        printf("  Key: %d, Value: ", npc->params.keys[i]);
        if( npc->params.kinds[i] == RSCACHE_PARAM_STRING )
        {
            printf("\"%s\" (string)\n", (char*)npc->params.values[i]);
        }
        else if( npc->params.kinds[i] == RSCACHE_PARAM_LONG )
        {
            printf("%lld (long)\n", (long long)*(int64_t*)npc->params.values[i]);
        }
        else
        {
            printf("%d (integer)\n", *(int*)npc->params.values[i]);
        }
    }
    printf("\n");
}
uint32_t
RSCache_Dat2ConfigNpcEncodeBound(const struct RSCache_Dat2ConfigNpc* npc)
{
    /*
     * A ceiling, not an exact size, and deliberately so: this type has 98 opcodes
     * and several are era-gated, so an exact figure would have to reproduce the
     * encoder's whole branch structure and would go stale the next time an opcode
     * is added. Instead the scalar opcodes get one flat allowance large enough for
     * all of them, and every *variable-length* part is measured from the record.
     *
     * The flat part: 98 opcodes at worst 1 byte of code plus 8 of payload is under
     * 900, so 2048 is roughly a 2x margin and still trivial next to a record.
     *
     * Adequacy is not taken on trust — `test_opcode_codec` writes a canary past
     * the bound and checks it on every npc in the cache, so a record that outgrew
     * this would fail the suite rather than corrupt the heap.
     */
    uint32_t need = 2048u;
    int i;

    if( !npc )
        return need;

    /* Model id arrays: worst case g4 per entry, plus a count byte and opcode. */
    need += (uint32_t)npc->models_count * 4u + 2u;
    need += (uint32_t)npc->chathead_models_count * 4u + 2u;
    /* Recolour and retexture pairs: two u16 each. */
    need += (uint32_t)npc->recolor_count * 4u + 2u;
    need += (uint32_t)npc->retexture_count * 4u + 2u;
    /* Head icons: a u16 pair per entry in the widest shape. */
    need += (uint32_t)npc->head_icon_count * 6u + 2u;
    /* The varp/varbit config list: a u16 per entry, plus its header. */
    need += (uint32_t)npc->configs_count * 2u + 8u;

    if( npc->name )
        need += (uint32_t)strlen(npc->name) + 2u;
    for( i = 0; i < 5; i++ )
    {
        if( npc->actions[i] )
            need += (uint32_t)strlen(npc->actions[i]) + 2u;
    }

    need += RSCache_EntityOpsBound(&npc->entity_ops);
    need += 1u + RSCache_BufferParamsBound(&npc->params);
    return need;
}
