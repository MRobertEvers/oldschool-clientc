#include "pkt_npc_info.h"

#include "net/bitbuffer.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdlib.h>
#include <string.h>

static struct PktNpcInfoOp*
next_op(
    struct PktNpcInfoReader* reader,
    struct PktNpcInfoOp* ops,
    int ops_capacity)
{
    assert(reader->current_op < ops_capacity);
    struct PktNpcInfoOp* op = &ops[reader->current_op++];
    memset(op, 0, sizeof(*op));
    return op;
}

static void
queue_extended(
    struct PktNpcInfoReader* reader,
    int idx)
{
    if( reader->extended_count < (int)(sizeof(reader->extended_queue) /
                                       sizeof(reader->extended_queue[0])) )
        reader->extended_queue[reader->extended_count++] = (uint16_t)idx;
}

void
pkt_npc_info_reader_init(
    struct PktNpcInfoReader* reader,
    int slot_bits,
    int type_bits)
{
    assert(reader);
    memset(reader, 0, sizeof(*reader));
    reader->slot_bits = slot_bits > 0 ? slot_bits : PKT_NPC_INFO_SLOT_BITS_CLASSIC;
    reader->type_bits = type_bits > 0 ? type_bits : PKT_NPC_INFO_TYPE_BITS_CLASSIC;
}

int
pkt_npc_info_reader_read(
    struct PktNpcInfoReader* reader,
    uint8_t const* data,
    int length,
    struct PktNpcInfoOp* ops,
    int ops_capacity)
{
    struct Net_BitBuffer buf;
    struct RSCache_Buffer rsbuf;
    int slot_bits;
    int type_bits;
    int terminator;

    /* A zeroed reader would decode every field as 0 bits and invent entities
     * out of nothing, so an un-armed reader stops here instead. */
    assert(reader->slot_bits > 0 && reader->slot_bits <= PKT_NPC_INFO_BITS_MAX);
    assert(reader->type_bits > 0 && reader->type_bits <= PKT_NPC_INFO_BITS_MAX);
    slot_bits = reader->slot_bits;
    type_bits = reader->type_bits;
    terminator = (1 << slot_bits) - 1;

    reader->current_op = 0;
    reader->extended_count = 0;
    Net_BitBufferInit(&buf, data, length);

    /* Tracked NPCs. */
    int new_idx = 0;
    int count = Net_BitBufferGbits(&buf, 8);
    {
        struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
        op->kind = PKT_NPC_INFO_OPBITS_COUNT_RESET;
        op->_bitvalue = (uint64_t)count;
    }
    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        int info = Net_BitBufferGbits(&buf, 1);

        if( info == 0 )
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = 0;
            new_idx++;
            continue;
        }

        int move_op = Net_BitBufferGbits(&buf, 2);
        switch( move_op )
        {
        case 0:
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;
            queue_extended(reader, new_idx);
            new_idx++;
            break;
        }
        case 1:
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;

            int walkdir = Net_BitBufferGbits(&buf, 3);
            op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_WALKDIR;
            op->_bitvalue = (uint64_t)walkdir;

            if( Net_BitBufferGbits(&buf, 1) )
                queue_extended(reader, new_idx);
            new_idx++;
            break;
        }
        case 2:
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;

            for( int r = 0; r < 2; r++ )
            {
                int rundir = Net_BitBufferGbits(&buf, 3);
                op = next_op(reader, ops, ops_capacity);
                op->kind = PKT_NPC_INFO_OPBITS_RUNDIR;
                op->_bitvalue = (uint64_t)rundir;
            }

            if( Net_BitBufferGbits(&buf, 1) )
                queue_extended(reader, new_idx);
            new_idx++;
            break;
        }
        case 3:
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            break;
        }
        }
    }

    /* New NPCs: slot + type + 5 + 5 + 1 bits per record, terminated by an
     * all-ones slot. Both widths come from the revision (Client-TS
     * getNpcPosNewVis reads gBit(14) with terminator 16383, NOT the 13-bit /
     * 8191 layout of older revisions), while the 21-bit loop guard is the
     * reference's own fixed margin and stays put: it is what decides whether
     * the terminator is consumed before the byte-aligned extended-info
     * section, so changing it would shift that alignment. */
    while( Net_BitBufferBitPos(&buf) + 21 < length * 8 )
    {
        int npc_slot = Net_BitBufferGbits(&buf, slot_bits);
        if( npc_slot == terminator )
            break;

        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID;
            op->_bitvalue = (uint64_t)npc_slot;
        }

        int npc_type = Net_BitBufferGbits(&buf, type_bits);
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_NPCTYPE;
            op->_bitvalue = (uint64_t)npc_type;
        }

        int dx = Net_BitBufferGbits(&buf, 5);
        if( dx > 15 )
            dx -= 32;
        int dz = Net_BitBufferGbits(&buf, 5);
        if( dz > 15 )
            dz -= 32;
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_DELTA_XZ;
            op->_delta_xz.dx = (int16_t)dx;
            op->_delta_xz.dz = (int16_t)dz;
            op->_delta_xz.jump = false;
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
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX;
            op->_bitvalue = (uint64_t)idx;
        }

        if( (int)rsbuf.position >= length )
            break;
        int mask = g1(&rsbuf);

        if( (mask & PKT_NPC_MASK_DAMAGE2) != 0 )
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_DAMAGE;
            op->_damage.damage = (uint8_t)g1(&rsbuf);
            op->_damage.damage_type = (uint8_t)g1(&rsbuf);
            op->_damage.health = (uint8_t)g1(&rsbuf);
            op->_damage.total_health = (uint8_t)g1(&rsbuf);
        }

        if( (mask & PKT_NPC_MASK_ANIM) != 0 )
        {
            int seq_raw = g2(&rsbuf);
            int delay = g1(&rsbuf);
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SEQUENCE;
            op->_sequence.sequence_id = seq_raw == 65535 ? -1 : seq_raw;
            op->_sequence.delay = (uint8_t)delay;
        }

        if( (mask & PKT_NPC_MASK_FACE_ENTITY) != 0 )
        {
            int entity_id = g2(&rsbuf);
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_FACE_ENTITY;
            op->_face_entity.entity_id = entity_id;
        }

        if( (mask & PKT_NPC_MASK_SAY) != 0 )
        {
            char* text = gstringnewline(&rsbuf);
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SAY;
            op->_say.text = text;
        }

        if( (mask & PKT_NPC_MASK_DAMAGE) != 0 )
        {
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_DAMAGE;
            /* Client.ts 8446-8447: damage first, then damageType (both g1). */
            op->_damage.damage = (uint8_t)g1(&rsbuf);
            op->_damage.damage_type = (uint8_t)g1(&rsbuf);
            op->_damage.health = (uint8_t)g1(&rsbuf);
            op->_damage.total_health = (uint8_t)g1(&rsbuf);
        }

        if( (mask & PKT_NPC_MASK_CHANGE_TYPE) != 0 )
        {
            /* Client.ts 8455: transmogrify — g2 new npc type. */
            int npc_type = g2(&rsbuf);
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_CHANGE_TYPE;
            op->_change_type.npc_type = npc_type;
        }

        if( (mask & PKT_NPC_MASK_SPOTANIM) != 0 )
        {
            /* Client.ts 8466: g2 id (65535 -> -1) + g4 heightDelay. */
            int spotanim_id = g2(&rsbuf);
            int height_delay = g4(&rsbuf);
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SPOTANIM;
            op->_spotanim.spotanim_id = spotanim_id == 65535 ? -1 : spotanim_id;
            op->_spotanim.height_delay = height_delay;
        }

        if( (mask & PKT_NPC_MASK_FACE_COORD) != 0 )
        {
            int target_x = g2(&rsbuf);
            int target_z = g2(&rsbuf);
            struct PktNpcInfoOp* op = next_op(reader, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_FACE_COORD;
            op->_face_coord.x = (int16_t)target_x;
            op->_face_coord.z = (int16_t)target_z;
        }
    }

    return reader->current_op;
}

void
pkt_npc_info_ops_free(
    struct PktNpcInfoOp* ops,
    int op_count)
{
    for( int i = 0; i < op_count; i++ )
    {
        if( ops[i].kind == PKT_NPC_INFO_OP_SAY )
            free(ops[i]._say.text);
        else if( ops[i].kind == PKT_NPC_INFO_OP_NAME_CHANGE )
            free(ops[i]._name_change.name);
    }
}
