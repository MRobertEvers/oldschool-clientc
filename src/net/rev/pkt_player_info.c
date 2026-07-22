#include "pkt_player_info.h"

#include "net/bitbuffer.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdlib.h>
#include <string.h>

static struct PktPlayerInfoOp*
next_op(
    struct PktPlayerInfoReader* reader,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    assert(reader->current_op < ops_capacity);
    struct PktPlayerInfoOp* op = &ops[reader->current_op++];
    memset(op, 0, sizeof(*op));
    return op;
}

static void
queue_extended(
    struct PktPlayerInfoReader* reader,
    int idx)
{
    if( reader->extended_count < (int)(sizeof(reader->extended_queue) /
                                       sizeof(reader->extended_queue[0])) )
        reader->extended_queue[reader->extended_count++] = (uint16_t)idx;
}

/* Movement block shared by the local player and tracked players.
 * `remove_on_op3`: tracked players use op 3 as "remove"; the local player
 * uses it as a teleport. Returns the op read (or -1 when info bit clear). */
static int
read_movement(
    struct PktPlayerInfoReader* reader,
    struct Net_BitBuffer* buf,
    struct PktPlayerInfoOp* ops,
    int ops_capacity,
    int extended_idx,
    int remove_on_op3,
    int old_idx)
{
    int info = Net_BitBufferGbits(buf, 1);
    if( info == 0 )
    {
        struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OPBITS_INFO;
        op->_bitvalue = 0;
        return -1;
    }

    int move_op = Net_BitBufferGbits(buf, 2);
    switch( move_op )
    {
    case 0:
        queue_extended(reader, extended_idx);
        break;
    case 1:
    {
        int walkdir = Net_BitBufferGbits(buf, 3);
        struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OPBITS_WALKDIR;
        op->_bitvalue = (uint64_t)walkdir;
        if( Net_BitBufferGbits(buf, 1) )
            queue_extended(reader, extended_idx);
        break;
    }
    case 2:
    {
        for( int r = 0; r < 2; r++ )
        {
            int rundir = Net_BitBufferGbits(buf, 3);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OPBITS_RUNDIR;
            op->_bitvalue = (uint64_t)rundir;
        }
        if( Net_BitBufferGbits(buf, 1) )
            queue_extended(reader, extended_idx);
        break;
    }
    case 3:
        if( remove_on_op3 )
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
        }
        else
        {
            int level = Net_BitBufferGbits(buf, 2);
            int sx = Net_BitBufferGbits(buf, 7);
            int sz = Net_BitBufferGbits(buf, 7);
            int jump = Net_BitBufferGbits(buf, 1);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL;
            op->_local_xz_level.x = (int16_t)sx;
            op->_local_xz_level.z = (int16_t)sz;
            op->_local_xz_level.level = (uint8_t)level;
            op->_local_xz_level.jump = jump != 0;
            if( Net_BitBufferGbits(buf, 1) )
                queue_extended(reader, extended_idx);
        }
        break;
    }
    return move_op;
}

int
pkt_player_info_reader_read(
    struct PktPlayerInfoReader* reader,
    uint8_t const* data,
    int length,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    struct Net_BitBuffer buf;
    struct RSCache_Buffer rsbuf;

    reader->current_op = 0;
    reader->extended_count = 0;
    Net_BitBufferInit(&buf, data, length);

    /* Local player. */
    {
        struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER;
    }
    read_movement(reader, &buf, ops, ops_capacity, 2047, 0, -1);

    /* Tracked players. */
    int new_idx = 0;
    int count = Net_BitBufferGbits(&buf, 8);
    {
        struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OPBITS_COUNT_RESET;
        op->_bitvalue = (uint64_t)count;
    }
    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
        }

        int info = Net_BitBufferGbits(&buf, 1);
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;
        }
        if( info == 0 )
        {
            new_idx++;
            continue;
        }

        int move_op = Net_BitBufferGbits(&buf, 2);
        switch( move_op )
        {
        case 0:
            queue_extended(reader, new_idx);
            new_idx++;
            break;
        case 1:
        {
            int walkdir = Net_BitBufferGbits(&buf, 3);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OPBITS_WALKDIR;
            op->_bitvalue = (uint64_t)walkdir;
            if( Net_BitBufferGbits(&buf, 1) )
                queue_extended(reader, new_idx);
            new_idx++;
            break;
        }
        case 2:
        {
            for( int r = 0; r < 2; r++ )
            {
                int rundir = Net_BitBufferGbits(&buf, 3);
                struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
                op->kind = PKT_PLAYER_INFO_OPBITS_RUNDIR;
                op->_bitvalue = (uint64_t)rundir;
            }
            if( Net_BitBufferGbits(&buf, 1) )
                queue_extended(reader, new_idx);
            new_idx++;
            break;
        }
        case 3:
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            break;
        }
        }
    }

    /* New players: 11 (pid) + 5 (dx) + 5 (dz) + 1 (jump) + 1 (ext) = 23 bits. */
    while( Net_BitBufferBitPos(&buf) + 23 <= length * 8 )
    {
        int player_id = Net_BitBufferGbits(&buf, 11);
        if( player_id == 2047 )
            break;

        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID;
            op->_bitvalue = (uint64_t)player_id;
        }

        int dx = Net_BitBufferGbits(&buf, 5);
        if( dx > 15 )
            dx -= 32;
        int dz = Net_BitBufferGbits(&buf, 5);
        if( dz > 15 )
            dz -= 32;
        int jump = Net_BitBufferGbits(&buf, 1);
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_DELTA_XZ;
            op->_delta_xz.dx = (int16_t)dx;
            op->_delta_xz.dz = (int16_t)dz;
            op->_delta_xz.jump = jump != 0;
        }

        if( Net_BitBufferGbits(&buf, 1) )
            queue_extended(reader, new_idx);
        new_idx++;
    }

    /* Extended info (byte-aligned). */
    RSCache_BufferInit(&rsbuf, (uint8_t*)data, (uint32_t)length);
    rsbuf.position = (uint32_t)Net_BitBufferBytePos(&buf);
    for( int i = 0; i < reader->extended_count && (int)rsbuf.position < length; i++ )
    {
        if( reader->current_op >= ops_capacity )
            break;
        int idx = reader->extended_queue[i];
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = idx == 2047 ? PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER
                                   : PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX;
            op->_bitvalue = (uint64_t)idx;
        }

        if( (int)rsbuf.position >= length )
            break;
        int mask = g1(&rsbuf);
        if( (mask & PKT_PLAYER_MASK_BIG_UPDATE) != 0 && (int)rsbuf.position < length )
            mask += g1(&rsbuf) << 8;

        if( (mask & PKT_PLAYER_MASK_APPEARANCE) != 0 )
        {
            int len = g1(&rsbuf);
            uint8_t* blob = malloc(len > 0 ? (size_t)len : 1);
            assert(blob);
            greadto(&rsbuf, (char*)blob, len, len);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_APPEARANCE;
            op->_appearance.appearance = blob;
            op->_appearance.len = len;
        }

        if( (mask & PKT_PLAYER_MASK_SEQUENCE) != 0 )
        {
            int seq_raw = g2(&rsbuf);
            int delay = g1(&rsbuf);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_SEQUENCE;
            op->_sequence.sequence_id = seq_raw == 65535 ? -1 : seq_raw;
            op->_sequence.delay = (uint8_t)delay;
        }

        if( (mask & PKT_PLAYER_MASK_FACE_ENTITY) != 0 )
        {
            int entity_id = g2(&rsbuf);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_FACE_ENTITY;
            op->_face_entity.entity_id = entity_id;
        }

        if( (mask & PKT_PLAYER_MASK_SAY) != 0 )
        {
            char* text = gstringnewline(&rsbuf);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_SAY;
            op->_say.text = text;
        }

        if( (mask & PKT_PLAYER_MASK_DAMAGE) != 0 )
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_DAMAGE;
            op->_damage.damage_type = (uint8_t)g1(&rsbuf);
            op->_damage.damage = (uint8_t)g1(&rsbuf);
            op->_damage.health = (uint8_t)g1(&rsbuf);
            op->_damage.total_health = (uint8_t)g1(&rsbuf);
        }

        if( (mask & PKT_PLAYER_MASK_FACE_COORD) != 0 )
        {
            int target_x = g2(&rsbuf);
            int target_z = g2(&rsbuf);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_FACE_COORD;
            op->_face_coord.x = (int16_t)target_x;
            op->_face_coord.z = (int16_t)target_z;
        }

        if( (mask & PKT_PLAYER_MASK_CHAT) != 0 )
        {
            /* Client.ts 8140-8181: colourEffect g2, type g1, length g1, then
             * exactly `length` wordpacked bytes (v0's decoder dropped the
             * payload without consuming it — desyncing the rest). */
            int colour_effect = g2(&rsbuf);
            int type = g1(&rsbuf);
            int chat_len = g1(&rsbuf);
            uint8_t* payload = malloc(chat_len > 0 ? (size_t)chat_len : 1);
            assert(payload);
            greadto(&rsbuf, (char*)payload, chat_len, chat_len);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_CHAT;
            op->_chat.colour_effect = (uint16_t)colour_effect;
            op->_chat.type = (uint8_t)type;
            op->_chat.length = (uint8_t)chat_len;
            op->_chat.data = payload;
        }

        /* BIG_UPDATE consumed at mask read; extends the mask to 16 bits. */

        if( (mask & PKT_PLAYER_MASK_SPOTANIM) != 0 )
        {
            int spotanim_id = g2(&rsbuf);
            int height_delay = g4(&rsbuf);
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_SPOTANIM;
            op->_spotanim.spotanim_id = spotanim_id == 65535 ? -1 : spotanim_id;
            op->_spotanim.height_delay = height_delay;
        }

        if( (mask & PKT_PLAYER_MASK_EXACT_MOVE) != 0 )
        {
            /* Client.ts 8202-8211. */
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_EXACT_MOVE;
            op->_exactmove.start_x = (uint8_t)g1(&rsbuf);
            op->_exactmove.start_z = (uint8_t)g1(&rsbuf);
            op->_exactmove.end_x = (uint8_t)g1(&rsbuf);
            op->_exactmove.end_z = (uint8_t)g1(&rsbuf);
            op->_exactmove.end_cycle_delta = (uint16_t)g2(&rsbuf);
            op->_exactmove.start_cycle_delta = (uint16_t)g2(&rsbuf);
            op->_exactmove.facing = (uint8_t)g1(&rsbuf);
        }

        if( (mask & PKT_PLAYER_MASK_DAMAGE2) != 0 )
        {
            struct PktPlayerInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_DAMAGE2;
            op->_damage.damage = (uint8_t)g1(&rsbuf);
            op->_damage.damage_type = (uint8_t)g1(&rsbuf);
            op->_damage.health = (uint8_t)g1(&rsbuf);
            op->_damage.total_health = (uint8_t)g1(&rsbuf);
        }
    }

    return reader->current_op;
}

void
pkt_player_info_ops_free(
    struct PktPlayerInfoOp* ops,
    int op_count)
{
    for( int i = 0; i < op_count; i++ )
    {
        switch( ops[i].kind )
        {
        case PKT_PLAYER_INFO_OP_APPEARANCE:
            free(ops[i]._appearance.appearance);
            break;
        case PKT_PLAYER_INFO_OP_SAY:
            free(ops[i]._say.text);
            break;
        case PKT_PLAYER_INFO_OP_CHAT:
            free(ops[i]._chat.data);
            break;
        default:
            break;
        }
    }
}
