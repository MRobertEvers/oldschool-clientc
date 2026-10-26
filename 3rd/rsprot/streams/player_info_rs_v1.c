/*
 * PLAYER_INFO, rs family, version 1: the stream 245.2, 254 and 289 all send
 * (see info_rs.h). A port of v0's pkt_player_info with the gaps filled from
 * the webclient decoder: EXACT_MOVE decoded, the CHAT payload consumed and
 * carried.
 */
#include "info_rs_internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* The LOCAL player's movement block. Not shared with the tracked loop below:
 * op 3 there is "this player left", which has to be known before the entry is
 * announced to the consumer, whereas here it is a teleport.
 * Returns the op read (or -1 when the info bit is clear). */
static int
rs_player_read_movement(
    struct RsExtendedQueue* q,
    RsprotBuf* b,
    RsprotInfoList* out,
    int extended_idx)
{
    int info = rsprot_gbits(b, 1);
    if( info == 0 )
    {
        struct RsprotInfoOp* op = rs_info_op(out);
        op->kind = RSPROT_OP_INFO;
        op->value = 0;
        return -1;
    }

    int move_op = rsprot_gbits(b, 2);
    switch( move_op )
    {
    case 0:
        rs_info_queue(q, extended_idx);
        break;
    case 1:
    {
        int walkdir = rsprot_gbits(b, 3);
        struct RsprotInfoOp* op = rs_info_op(out);
        op->kind = RSPROT_OP_WALKDIR;
        op->value = (uint64_t)walkdir;
        if( rsprot_gbits(b, 1) )
            rs_info_queue(q, extended_idx);
        break;
    }
    case 2:
    {
        for( int r = 0; r < 2; r++ )
        {
            int rundir = rsprot_gbits(b, 3);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_RUNDIR;
            op->value = (uint64_t)rundir;
        }
        if( rsprot_gbits(b, 1) )
            rs_info_queue(q, extended_idx);
        break;
    }
    case 3:
    {
        int level = rsprot_gbits(b, 2);
        int sx = rsprot_gbits(b, 7);
        int sz = rsprot_gbits(b, 7);
        int jump = rsprot_gbits(b, 1);
        struct RsprotInfoOp* op = rs_info_op(out);
        op->kind = RSPROT_OP_LOCAL_XZLEVEL;
        op->xz_level.x = (int16_t)sx;
        op->xz_level.z = (int16_t)sz;
        op->xz_level.level = (uint8_t)level;
        op->xz_level.jump = jump != 0;
        if( rsprot_gbits(b, 1) )
            rs_info_queue(q, extended_idx);
        break;
    }
    }
    return move_op;
}

int
player_info_rs_v1(
    uint8_t const* data,
    int length,
    RsprotInfoList* out)
{
    struct RsExtendedQueue queue;
    struct RsExtendedQueue* q = &queue;
    RsprotBuf b;

    assert(data);
    assert(out);
    rsprot_info_list_begin(out);
    q->count = 0;
    rsprot_buf_wrap_read(&b, data, length);
    rsprot_buf_bit_begin(&b);

    /* Local player. */
    {
        struct RsprotInfoOp* op = rs_info_op(out);
        op->kind = RSPROT_OP_SET_LOCAL_PLAYER;
    }
    rs_player_read_movement(q, &b, out, RS_PLAYER_INFO_LOCAL_PLAYER_IDX);

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
    /* Tracked players. */
    int new_idx = 0;
    int count = rsprot_gbits(&b, 8);
    {
        struct RsprotInfoOp* op = rs_info_op(out);
        op->kind = RSPROT_OP_COUNT_RESET;
        op->value = (uint64_t)count;
    }
    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        /*
         * Read the info bit and the 2-bit op BEFORE emitting the ADD: op 3 is
         * "this player left", and a dropped entry is not part of the list being
         * rebuilt (Client.ts getPlayerOldVis appends `playerIds[playerCount++]`
         * in every branch except op 3). Emitting ADD unconditionally and CLEAR
         * after it makes the consumer append an entry `new_idx` never counted,
         * so from the drop onward every extended block resolves one entry early
         * and the list carried into the next packet as `old_list` keeps the
         * departed player's slot -- shifting every later old_idx by one.
         */
        int info = rsprot_gbits(&b, 1);
        int move_op = -1;
        if( info != 0 )
            move_op = (int)rsprot_gbits(&b, 2);

        if( move_op == 3 )
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_CLEAR_OLD;
            op->value = (uint64_t)old_idx;
            continue;
        }

        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_ADD_OLD;
            op->value = (uint64_t)old_idx;
            op = rs_info_op(out);
            op->kind = RSPROT_OP_INFO;
            op->value = (uint64_t)info;
        }
        if( info == 0 )
        {
            new_idx++;
            continue;
        }

        switch( move_op )
        {
        case 0:
            rs_info_queue(q, new_idx);
            new_idx++;
            break;
        case 1:
        {
            int walkdir = rsprot_gbits(&b, 3);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_WALKDIR;
            op->value = (uint64_t)walkdir;
            if( rsprot_gbits(&b, 1) )
                rs_info_queue(q, new_idx);
            new_idx++;
            break;
        }
        case 2:
        {
            for( int r = 0; r < 2; r++ )
            {
                int rundir = rsprot_gbits(&b, 3);
                struct RsprotInfoOp* op = rs_info_op(out);
                op->kind = RSPROT_OP_RUNDIR;
                op->value = (uint64_t)rundir;
            }
            if( rsprot_gbits(&b, 1) )
                rs_info_queue(q, new_idx);
            new_idx++;
            break;
        }
        /* case 3 -- the drop -- was handled above the ADD; see the note there. */
        }
    }

    /* New players: only the 11-bit pid is needed to reach the 2047 terminator;
       requiring a full 23-bit entry skips the terminator when the packet ends
       right after it, desyncing the byte-aligned extended-info section. */
    while( b.bit_rpos + 11 <= length * 8 )
    {
        int player_id = rsprot_gbits(&b, 11);
        if( player_id == RS_PLAYER_INFO_NEW_TERMINATOR )
            break;

        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_ADD_NEW;
            op->value = (uint64_t)player_id;
        }

        int dx = rsprot_gbits(&b, 5);
        if( dx > 15 )
            dx -= 32;
        int dz = rsprot_gbits(&b, 5);
        if( dz > 15 )
            dz -= 32;
        int jump = rsprot_gbits(&b, 1);
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_DELTA_XZ;
            op->delta_xz.dx = (int16_t)dx;
            op->delta_xz.dz = (int16_t)dz;
            op->delta_xz.jump = jump != 0;
        }

        if( rsprot_gbits(&b, 1) )
            rs_info_queue(q, new_idx);
        new_idx++;
    }

    /* Extended info (byte-aligned). A field the payload is too short for marks
     * `b`; the read stops at the next entry and the list says why. */
    rsprot_buf_bit_end(&b);
    for( int i = 0; i < q->count && rsprot_buf_readable(&b) > 0 && !b.err; i++ )
    {
        if( out->overflow )
            break;
        int idx = q->idx[i];
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = idx == RS_PLAYER_INFO_LOCAL_PLAYER_IDX ? RSPROT_OP_SET_LOCAL_PLAYER
                                   : RSPROT_OP_SET_EXT;
            op->value = (uint64_t)idx;
        }

        if( rsprot_buf_readable(&b) <= 0 )
            break;
        int mask = rsprot_g1(&b);
        if( (mask & RS_PLAYER_MASK_BIG_UPDATE) != 0 && rsprot_buf_readable(&b) > 0 )
            mask += rsprot_g1(&b) << 8;

        if( (mask & RS_PLAYER_MASK_APPEARANCE) != 0 )
        {
            int len = rsprot_g1(&b);
            uint8_t const* blob = rs_info_take_bytes(&b, out, len);
            if( !blob )
                break;
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_APPEARANCE;
            op->appearance.appearance = blob;
            op->appearance.len = len;
        }

        if( (mask & RS_PLAYER_MASK_SEQUENCE) != 0 )
        {
            int seq_raw = rsprot_g2_12(&b);
            int delay = rsprot_g1(&b);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_SEQUENCE;
            op->sequence.sequence_id = seq_raw == 65535 ? -1 : seq_raw;
            op->sequence.delay = (uint8_t)delay;
        }

        if( (mask & RS_PLAYER_MASK_FACE_ENTITY) != 0 )
        {
            int entity_id = rsprot_g2_12(&b);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_FACE_ENTITY;
            op->face_entity.entity_id = entity_id;
        }

        if( (mask & RS_PLAYER_MASK_SAY) != 0 )
        {
            char const* text = rs_info_take_text(&b, out);
            if( !text )
                break;
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_SAY;
            op->say.text = text;
        }

        if( (mask & RS_PLAYER_MASK_DAMAGE) != 0 )
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_DAMAGE;
            /* Client.ts 8126-8127: damage first, then damageType (both g1). */
            op->damage.damage = (uint8_t)rsprot_g1(&b);
            op->damage.damage_type = (uint8_t)rsprot_g1(&b);
            op->damage.health = (uint8_t)rsprot_g1(&b);
            op->damage.total_health = (uint8_t)rsprot_g1(&b);
        }

        if( (mask & RS_PLAYER_MASK_FACE_COORD) != 0 )
        {
            int target_x = rsprot_g2_12(&b);
            int target_z = rsprot_g2_12(&b);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_FACE_COORD;
            op->face_coord.x = (int16_t)target_x;
            op->face_coord.z = (int16_t)target_z;
        }

        if( (mask & RS_PLAYER_MASK_CHAT) != 0 )
        {
            /* Client.ts 8140-8181: colourEffect g2, type g1, length g1, then
             * exactly `length` wordpacked bytes (v0's decoder dropped the
             * payload without consuming it — desyncing the rest). */
            int colour_effect = rsprot_g2_12(&b);
            int type = rsprot_g1(&b);
            int chat_len = rsprot_g1(&b);
            uint8_t const* payload = rs_info_take_bytes(&b, out, chat_len);
            if( !payload )
                break;
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_CHAT;
            op->chat.colour_effect = (uint16_t)colour_effect;
            op->chat.type = (uint8_t)type;
            op->chat.length = (uint8_t)chat_len;
            op->chat.data = payload;
        }

        /* BIG_UPDATE consumed at mask read; extends the mask to 16 bits. */

        if( (mask & RS_PLAYER_MASK_SPOTANIM) != 0 )
        {
            int spotanim_id = rsprot_g2_12(&b);
            int height_delay = rsprot_g4_1234(&b);
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_SPOTANIM;
            op->spotanim.spotanim_id = spotanim_id == 65535 ? -1 : spotanim_id;
            op->spotanim.height_delay = height_delay;
        }

        if( (mask & RS_PLAYER_MASK_EXACT_MOVE) != 0 )
        {
            /* Client.ts 8202-8211. */
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_EXACT_MOVE;
            op->exact_move.start_x = (uint8_t)rsprot_g1(&b);
            op->exact_move.start_z = (uint8_t)rsprot_g1(&b);
            op->exact_move.end_x = (uint8_t)rsprot_g1(&b);
            op->exact_move.end_z = (uint8_t)rsprot_g1(&b);
            op->exact_move.end_cycle_delta = (uint16_t)rsprot_g2_12(&b);
            op->exact_move.start_cycle_delta = (uint16_t)rsprot_g2_12(&b);
            op->exact_move.facing = (uint8_t)rsprot_g1(&b);
        }

        if( (mask & RS_PLAYER_MASK_DAMAGE2) != 0 )
        {
            struct RsprotInfoOp* op = rs_info_op(out);
            op->kind = RSPROT_OP_DAMAGE2;
            op->damage.damage = (uint8_t)rsprot_g1(&b);
            op->damage.damage_type = (uint8_t)rsprot_g1(&b);
            op->damage.health = (uint8_t)rsprot_g1(&b);
            op->damage.total_health = (uint8_t)rsprot_g1(&b);
        }
    }

    rs_info_end(&b, out);
    return out->count;
}
