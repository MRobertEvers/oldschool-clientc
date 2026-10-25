/* The npc sections every RS2-family version shares (info_rs_internal.h). */
#include "info_rs_internal.h"

#include "mem/torimem_rscache.h"
#include "net/rev/packets/pkt_info_bytes.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdbool.h>
#include <string.h>

int
rs_npc_info_old_vis(
    struct Net_BitBuffer* buf,
    struct RsExtendedQueue* q,
    RsprotInfoList* out)
{
/*
 * The list index below is a CONTRACT with the consumer, not a local counter.
 *
 * Extended-info blocks are addressed by position in the list both sides rebuild
 * during this packet, and this index defines those positions. It advances for
 * every entry kept or added, unconditionally. The consumer
 * (task_exec_entity_info.c) must therefore append exactly one entry per
 * increment -- a sentinel when it cannot resolve the slot -- or every later
 * block in the packet applies to the entity AFTER its intended target, silently.
 * See the invariant note on RS_EntitySync::active_players/active_npcs.
 */
    /* Tracked NPCs. */
    int new_idx = 0;
    int count = Net_BitBufferGbits(buf, 8);
    {
        struct RsprotInfoOp* op = rs_info_op(out);
        op->kind = RSPROT_OP_COUNT_RESET;
        op->value = (uint64_t)count;
    }
    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        int info = Net_BitBufferGbits(buf, 1);

        if( info == 0 )
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_ADD_OLD;
            op->value = (uint64_t)old_idx;
            op = rs_info_op(out);
            op->kind = RSPROT_OP_INFO;
            op->value = 0;
            new_idx++;
            continue;
        }

        int move_op = Net_BitBufferGbits(buf, 2);
        switch( move_op )
        {
        case 0:
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_ADD_OLD;
            op->value = (uint64_t)old_idx;
            op = rs_info_op(out);
            op->kind = RSPROT_OP_INFO;
            op->value = (uint64_t)info;
            rs_info_queue(q, new_idx);
            new_idx++;
            break;
        }
        case 1:
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_ADD_OLD;
            op->value = (uint64_t)old_idx;
            op = rs_info_op(out);
            op->kind = RSPROT_OP_INFO;
            op->value = (uint64_t)info;

            int walkdir = Net_BitBufferGbits(buf, 3);
            op = rs_info_op(out);
            op->kind = RSPROT_OP_WALKDIR;
            op->value = (uint64_t)walkdir;

            if( Net_BitBufferGbits(buf, 1) )
                rs_info_queue(q, new_idx);
            new_idx++;
            break;
        }
        case 2:
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_ADD_OLD;
            op->value = (uint64_t)old_idx;
            op = rs_info_op(out);
            op->kind = RSPROT_OP_INFO;
            op->value = (uint64_t)info;

            for( int r = 0; r < 2; r++ )
            {
                int rundir = Net_BitBufferGbits(buf, 3);
                op = rs_info_op(out);
                op->kind = RSPROT_OP_RUNDIR;
                op->value = (uint64_t)rundir;
            }

            if( Net_BitBufferGbits(buf, 1) )
                rs_info_queue(q, new_idx);
            new_idx++;
            break;
        }
        case 3:
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_CLEAR_OLD;
            op->value = (uint64_t)old_idx;
            break;
        }
        }
    }

    return new_idx;
}

void
rs_npc_info_extended(
    struct RsExtendedQueue const* q,
    uint8_t const* data,
    int length,
    int byte_pos,
    RsprotInfoList* out)
{
    struct RSCache_Buffer rsbuf;

    /* Extended info (byte-aligned). */
    RSCache_BufferInit(&rsbuf, (uint8_t*)data, (uint32_t)length);
    rsbuf.position = (uint32_t)byte_pos;
    for( int i = 0; i < q->count && (int)rsbuf.position < length; i++ )
    {
        if( out->overflow )
            break;
        int idx = q->idx[i];
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_SET_EXT;
            op->value = (uint64_t)idx;
        }

        if( (int)rsbuf.position >= length )
            break;
        int mask = g1(&rsbuf);

        if( (mask & RS_NPC_MASK_DAMAGE2) != 0 )
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_DAMAGE;
            op->damage.damage = (uint8_t)g1(&rsbuf);
            op->damage.damage_type = (uint8_t)g1(&rsbuf);
            op->damage.health = (uint8_t)g1(&rsbuf);
            op->damage.total_health = (uint8_t)g1(&rsbuf);
        }

        if( (mask & RS_NPC_MASK_ANIM) != 0 )
        {
            int seq_raw = g2(&rsbuf);
            int delay = g1(&rsbuf);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_SEQUENCE;
            op->sequence.sequence_id = seq_raw == 65535 ? -1 : seq_raw;
            op->sequence.delay = (uint8_t)delay;
        }

        if( (mask & RS_NPC_MASK_FACE_ENTITY) != 0 )
        {
            int entity_id = g2(&rsbuf);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_FACE_ENTITY;
            op->face_entity.entity_id = entity_id;
        }

        if( (mask & RS_NPC_MASK_SAY) != 0 )
        {
            char const* text = pkt_info_take_text(&rsbuf, out);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_SAY;
            op->say.text = text;
        }

        if( (mask & RS_NPC_MASK_DAMAGE) != 0 )
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_DAMAGE;
            /* Client.ts 8446-8447: damage first, then damageType (both g1). */
            op->damage.damage = (uint8_t)g1(&rsbuf);
            op->damage.damage_type = (uint8_t)g1(&rsbuf);
            op->damage.health = (uint8_t)g1(&rsbuf);
            op->damage.total_health = (uint8_t)g1(&rsbuf);
        }

        if( (mask & RS_NPC_MASK_CHANGE_TYPE) != 0 )
        {
            /* Client.ts 8455: transmogrify — g2 new npc type. */
            int npc_type = g2(&rsbuf);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_CHANGE_TYPE;
            op->change_type.npc_type = npc_type;
        }

        if( (mask & RS_NPC_MASK_SPOTANIM) != 0 )
        {
            /* Client.ts 8466: g2 id (65535 -> -1) + g4 heightDelay. */
            int spotanim_id = g2(&rsbuf);
            int height_delay = g4(&rsbuf);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_SPOTANIM;
            op->spotanim.spotanim_id = spotanim_id == 65535 ? -1 : spotanim_id;
            op->spotanim.height_delay = height_delay;
        }

        if( (mask & RS_NPC_MASK_FACE_COORD) != 0 )
        {
            int target_x = g2(&rsbuf);
            int target_z = g2(&rsbuf);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_FACE_COORD;
            op->face_coord.x = (int16_t)target_x;
            op->face_coord.z = (int16_t)target_z;
        }
    }
}
