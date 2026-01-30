#include "pkt_npc_info.h"

#include "osrs/rscache/bitbuffer.h"
#include "osrs/rscache/rsbuf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define MASK_DAMAGE2 0x1
#define MASK_ANIM 0x2
#define MASK_FACE_ENTITY 0x4
#define MASK_SAY 0x8
#define MASK_DAMAGE 0x10
#define MASK_CHANGE_TYPE 0x20
#define MASK_SPOTANIM 0x40
#define MASK_FACE_COORD 0x80

static struct PktNpcInfoOp*
next_op(
    struct PktNpcInfoReader* reader,
    struct PktNpcInfoOp* ops,
    int ops_capacity)
{
    assert(reader->current_op < ops_capacity);
    return &ops[reader->current_op++];
}

static void
push_op_add_npc_new_opbits_pid(
    struct PktNpcInfoOp* op,
    int player_id)
{
    op->kind = PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID;
    op->_bitvalue = player_id;
}

static void
push_op_add_npc_old_opbits_idx(
    struct PktNpcInfoOp* op,
    int npc_idx)
{
    op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
    op->_bitvalue = npc_idx;
}

static void
push_op_set_npc_opbits_idx(
    struct PktNpcInfoOp* op,
    int npc_idx)
{
    op->kind = PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX;
    op->_bitvalue = npc_idx;
}

static void
push_op_clear_npc_opbits_idx(
    struct PktNpcInfoOp* op,
    int npc_idx)
{
    op->kind = PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX;
    op->_bitvalue = npc_idx;
}

static void
push_op_bits_count_reset(
    struct PktNpcInfoOp* op,
    int count)
{
    op->kind = PKT_NPC_INFO_OPBITS_COUNT_RESET;
    op->_bitvalue = count;
}

static void
push_bits_npc_type(
    struct PktNpcInfoOp* op,
    int npc_type)
{
    op->kind = PKT_NPC_INFO_OPBITS_NPCTYPE;
    op->_bitvalue = npc_type;
}

static void
push_bits_info(
    struct PktNpcInfoOp* op,
    int info)
{
    op->kind = PKT_NPC_INFO_OPBITS_INFO;
    op->_bitvalue = info;
}
static void
push_bits_walkdir(
    struct PktNpcInfoOp* op,
    int walkdir)
{
    op->kind = PKT_NPC_INFO_OPBITS_WALKDIR;
    op->_bitvalue = walkdir;
}

static void
push_bits_rundir(
    struct PktNpcInfoOp* op,
    int rundir)
{
    op->kind = PKT_NPC_INFO_OPBITS_RUNDIR;
    op->_bitvalue = rundir;
}

static void
push_bits_dx(
    struct PktNpcInfoOp* op,
    int dx)
{
    op->kind = PKT_NPC_INFO_OPBITS_DX;
    op->_bitvalue = dx;
}

static void
push_bits_dz(
    struct PktNpcInfoOp* op,
    int dz)
{
    op->kind = PKT_NPC_INFO_OPBITS_DZ;
    op->_bitvalue = dz;
}

int
pkt_npc_info_reader_read(
    struct PktNpcInfoReader* reader,
    struct PktNpcInfo* pkt,
    struct PktNpcInfoOp* ops,
    int ops_capacity)
{
    reader->current_op = 0;
    reader->extended_count = 0;
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, pkt->data, pkt->length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    // NPC Old Vis
    int npc_count = 0;
    int count = gbits(&buf, 8);
    push_op_bits_count_reset(next_op(reader, ops, ops_capacity), count);
    printf("NPC Old Vis count: %d\n", count);
    for( int i = 0; i < count; i++ )
    {
        int info = gbits(&buf, 1);

        if( info != 0 )
        {
            int op = gbits(&buf, 2);
            switch( op )
            {
            case 0:
            {
                push_op_add_npc_old_opbits_idx(next_op(reader, ops, ops_capacity), i);
                push_bits_info(next_op(reader, ops, ops_capacity), info);

                reader->extended_queue[reader->extended_count++] = npc_count;
                npc_count += 1;
                break;
            }
            case 1:
                // walkdir
                {
                    push_op_add_npc_old_opbits_idx(next_op(reader, ops, ops_capacity), i);
                    push_bits_info(next_op(reader, ops, ops_capacity), info);

                    int walkdir = gbits(&buf, 3);
                    push_bits_walkdir(next_op(reader, ops, ops_capacity), walkdir);

                    // has extended info
                    int has_extended_info = gbits(&buf, 1);
                    if( has_extended_info )
                    {
                        reader->extended_queue[reader->extended_count++] = npc_count;
                    }

                    npc_count += 1;
                }
                break;
            case 2:
            { // walkdir
                push_op_add_npc_old_opbits_idx(next_op(reader, ops, ops_capacity), i);
                push_bits_info(next_op(reader, ops, ops_capacity), info);

                int walkdir = gbits(&buf, 3);
                push_bits_walkdir(next_op(reader, ops, ops_capacity), walkdir);
                // rundir
                int rundir = gbits(&buf, 3);
                push_bits_rundir(next_op(reader, ops, ops_capacity), rundir);
                // has extended info
                int has_extended_info = gbits(&buf, 1);
                if( has_extended_info )
                {
                    reader->extended_queue[reader->extended_count++] = npc_count;
                }

                npc_count += 1;
            }
            break;
            case 3:
            {
                push_op_clear_npc_opbits_idx(next_op(reader, ops, ops_capacity), i);
                break;
            }
            //
            break;
            }
        }
        else
        {
            push_op_add_npc_old_opbits_idx(next_op(reader, ops, ops_capacity), i);
            push_bits_info(next_op(reader, ops, ops_capacity), info);

            npc_count += 1;
        }
    }

    // NPC New Vis
    while( ((buf.byte_position * 8) + buf.bit_offset + 21) < pkt->length * 8 )
    {
        int npc_id = gbits(&buf, 13);
        if( npc_id == 8191 )
        {
            break;
        }

        push_op_add_npc_new_opbits_pid(next_op(reader, ops, ops_capacity), npc_id);

        int npc_type = gbits(&buf, 11);
        push_bits_npc_type(next_op(reader, ops, ops_capacity), npc_type);

        // Later Revs have an "NPC_Large" packet
        // int dx = gbits(&buf, 8);
        // if( dx > 127 )
        //     dx -= 256;
        // Etc. Seen in Kronos 184 (OSRS)

        int dx = gbits(&buf, 5);
        if( dx > 15 )
            dx -= 32;
        push_bits_dx(next_op(reader, ops, ops_capacity), dx);
        int dy = gbits(&buf, 5);
        if( dy > 15 )
            dy -= 32;
        push_bits_dz(next_op(reader, ops, ops_capacity), dy);

        int has_extended_info = gbits(&buf, 1);
        if( has_extended_info )
        {
            reader->extended_queue[reader->extended_count++] = npc_count;
        }

        npc_count += 1;
    }

    // Extended Info
    uint8_t* appearance_buf = NULL;
    rsbuf.position = buf.byte_position + (buf.bit_offset + 7) / 8;
    for( int i = 0; i < reader->extended_count; i++ )
    {
        int idx = reader->extended_queue[i];

        push_op_set_npc_opbits_idx(next_op(reader, ops, ops_capacity), idx);

        int mask = g1(&rsbuf);
        // Info

        // Appearance
        // if( (mask & MASK_APPEARANCE) != 0 )
        // {
        //     int len = g1(&rsbuf);

        //     appearance_buf = malloc(len);
        //     greadto(&rsbuf, appearance_buf, len, len);

        //     push_op_appearance(next_op(reader, ops, ops_capacity), appearance_buf, len);
        // }

        if( (mask & MASK_DAMAGE2) != 0 )
        {
            int damage = g1(&rsbuf);
            int damage_type = g1(&rsbuf);
            int health = g1(&rsbuf);
            int total_health = g1(&rsbuf);
        }

        if( (mask & MASK_ANIM) != 0 )
        {
            assert(0);
            // int len = g1(&rsbuf);
            // uint8_t* sequence_buf = malloc(len);
            // greadto(&rsbuf, sequence_buf, len, len);
            // push_op_sequence(&ops[reader->current_op++], sequence_buf, len);
        }

        if( (mask & MASK_FACE_ENTITY) != 0 )
        {
            int target_id = g2(&rsbuf);
        }

        if( (mask & MASK_SAY) != 0 )
        {
            char* chat_message = gstringnewline(&rsbuf);
            printf("Chat message: %s\n", chat_message);
            free(chat_message);
        }

        if( (mask & MASK_DAMAGE) != 0 )
        {
            assert(0);
        }

        if( (mask & MASK_CHANGE_TYPE) != 0 )
        {
            assert(0);
        }

        if( (mask & MASK_SPOTANIM) != 0 )
        {
            assert(0);
        }

        if( (mask & MASK_FACE_COORD) != 0 )
        {
            int target_x = g2(&rsbuf);
            int target_z = g2(&rsbuf);
        }
    }

    return reader->current_op;
}