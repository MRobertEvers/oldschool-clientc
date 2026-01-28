#include "pkt_player_info.h"

#include "osrs/rscache/bitbuffer.h"
#include "osrs/rscache/rsbuf.h"

#include <assert.h>
#include <stdlib.h>

#define MASK_APPEARANCE 0x01
#define MASK_SEQUENCE 0x02

static struct PktPlayerInfoOp*
next_op(
    struct PktPlayerInfoReader* reader,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    assert(reader->current_op < ops_capacity);
    return &ops[reader->current_op++];
}

static uint16_t
masked_pid(
    int num,
    bool is_entity_id)
{
    if( is_entity_id )
    {
        return num | 0x8000;
    }
    else
    {
        return num & 0x7FFF;
    }
}

static uint16_t
unmasked_pid(int num)
{
    return num & 0x7FFF;
}

static bool
is_entity_id(int num)
{
    return num & 0x8000;
}

static void
push_mode_local_player(struct PktPlayerInfoOp* op)
{
    op->kind = PKT_PLAYER_INFO_MODE_LOCAL_PLAYER;
}

static void
push_mode_player_entity_idx(
    struct PktPlayerInfoOp* op,
    int player_idx)
{
    op->kind = PKT_PLAYER_INFO_MODE_PLAYER_ENTITY_IDX;
    op->_bitvalue = player_idx;
}

static void
push_mode_player_entity_id(
    struct PktPlayerInfoOp* op,
    int player_entity_id)
{
    op->kind = PKT_PLAYER_INFO_MODE_PLAYER_ENTITY_ID;
    op->_bitvalue = player_entity_id;
}

static void
push_bits_info(
    struct PktPlayerInfoOp* op,
    int info)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_INFO;
    op->_bitvalue = info;
}
static void
push_bits_walkdir(
    struct PktPlayerInfoOp* op,
    int walkdir)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_WALKDIR;
    op->_bitvalue = walkdir;
}

static void
push_bits_rundir(
    struct PktPlayerInfoOp* op,
    int rundir)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_RUNDIR;
    op->_bitvalue = rundir;
}

static void
push_bits_level(
    struct PktPlayerInfoOp* op,
    int level)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_LEVEL;
    op->_bitvalue = level;
}

static void
push_bits_local_x(
    struct PktPlayerInfoOp* op,
    int local_x)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_LOCAL_X;
    op->_bitvalue = local_x;
}

static void
push_bits_local_z(
    struct PktPlayerInfoOp* op,
    int local_z)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_LOCAL_Z;
    op->_bitvalue = local_z;
}

static void
push_bits_jump(
    struct PktPlayerInfoOp* op,
    int jump)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_JUMP;
    op->_bitvalue = jump;
}

static void
push_bits_dx(
    struct PktPlayerInfoOp* op,
    int dx)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_DX;
    op->_bitvalue = dx;
}

static void
push_bits_dz(
    struct PktPlayerInfoOp* op,
    int dz)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_DZ;
    op->_bitvalue = dz;
}

static void
push_op_appearance(
    struct PktPlayerInfoOp* op,
    uint8_t* appearance_buf,
    int len)
{
    op->kind = PKT_PLAYER_INFO_OP_APPEARANCE;
    op->_appearance.appearance = appearance_buf;
    op->_appearance.len = len;
}

int
pkt_player_info_reader_read(
    struct PktPlayerInfoReader* reader,
    struct PktPlayerInfo* pkt,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    reader->current_op = 0;
    reader->player_queue_size = 0;
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, pkt->data, pkt->length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    push_mode_local_player(next_op(reader, ops, ops_capacity));

    // Player local
    int info = gbits(&buf, 1);
    if( info != 0 )
    {
        int op = gbits(&buf, 2);
        switch( op )
        {
        case 0:

            break;
        case 1:
        { // walkdir
            int walkdir = gbits(&buf, 3);
            push_bits_walkdir(next_op(reader, ops, ops_capacity), walkdir);

            // has extended info
            int has_extended_info = gbits(&buf, 1);
            if( has_extended_info )
            {
                reader->player_queue[reader->player_queue_size++] = 2047;
            }
        }
        break;
        case 2:
        { //
            // walkdir
            int walkdir = gbits(&buf, 3);
            push_bits_walkdir(next_op(reader, ops, ops_capacity), walkdir);
            // rundir
            int rundir = gbits(&buf, 3);
            push_bits_rundir(next_op(reader, ops, ops_capacity), rundir);

            // has extended info
            int has_extended_info = gbits(&buf, 1);
            if( has_extended_info )
            {
                reader->player_queue[reader->player_queue_size++] = 2047;
            }
        }
        break;
        case 3:
        {
            int level = gbits(&buf, 2);
            push_bits_level(next_op(reader, ops, ops_capacity), level);
            int sx = gbits(&buf, 7);
            push_bits_local_x(next_op(reader, ops, ops_capacity), sx);
            int sz = gbits(&buf, 7);
            push_bits_local_z(next_op(reader, ops, ops_capacity), sz);
            int jump = gbits(&buf, 1);
            push_bits_jump(next_op(reader, ops, ops_capacity), jump);

            int has_extended_info = gbits(&buf, 1);
            if( has_extended_info )
            {
                reader->player_queue[reader->player_queue_size++] = 2047;
            }
        }
        break;
        }
    }
    else
    {
        push_bits_info(next_op(reader, ops, ops_capacity), info);
        reader->player_queue[reader->player_queue_size++] = masked_pid(2047, false);
    }

    // Player Old Vis
    int count = gbits(&buf, 8);
    for( int i = 0; i < count; i++ )
    {
        push_mode_player_entity_idx(next_op(reader, ops, ops_capacity), i);

        int info = gbits(&buf, 1);
        push_bits_info(next_op(reader, ops, ops_capacity), info);

        if( info != 0 )
        {
            int op = gbits(&buf, 2);
            switch( op )
            {
            case 0:
                //
                break;
            case 1:
                // walkdir
                {
                    int walkdir = gbits(&buf, 3);
                    push_bits_walkdir(next_op(reader, ops, ops_capacity), walkdir);
                    // has extended info
                    int has_extended_info = gbits(&buf, 1);
                    if( has_extended_info )
                    {
                        reader->player_queue[reader->player_queue_size++] = masked_pid(i, false);
                    }
                }
                break;
            case 2:
            { // walkdir
                int walkdir = gbits(&buf, 3);
                push_bits_walkdir(next_op(reader, ops, ops_capacity), walkdir);
                // rundir
                int rundir = gbits(&buf, 3);
                push_bits_rundir(next_op(reader, ops, ops_capacity), rundir);
                // has extended info
                int has_extended_info = gbits(&buf, 1);
                if( has_extended_info )
                {
                    reader->player_queue[reader->player_queue_size++] = masked_pid(i, false);
                }
            }
            break;
            case 3:
                //
                break;
            }
        }
        else
        {
            reader->player_queue[reader->player_queue_size++] = masked_pid(i, false);
        }
    }

    // Player New Vis
    while( ((buf.byte_position * 8) + buf.bit_offset + 10) < pkt->length * 8 )
    {
        int player_id = gbits(&buf, 11);
        if( player_id == 2047 )
        {
            break;
        }

        push_mode_player_entity_id(next_op(reader, ops, ops_capacity), player_id);

        int dx = gbits(&buf, 5);
        if( dx > 15 )
            dx -= 32;
        push_bits_dx(next_op(reader, ops, ops_capacity), dx);
        int dy = gbits(&buf, 5);
        if( dy > 15 )
            dy -= 32;
        push_bits_dz(next_op(reader, ops, ops_capacity), dy);
        int jump = gbits(&buf, 1);
        push_bits_jump(next_op(reader, ops, ops_capacity), jump);

        int has_extended_info = gbits(&buf, 1);
        if( has_extended_info )
        {
            reader->player_queue[reader->player_queue_size++] = masked_pid(player_id, true);
        }
    }

    // Extended Info
    uint8_t* appearance_buf = NULL;
    rsbuf.position = buf.byte_position + (buf.bit_offset + 7) / 8;
    for( int i = 0; i < reader->player_queue_size; i++ )
    {
        int id = reader->player_queue[i];
        if( is_entity_id(id) )
            push_mode_player_entity_id(next_op(reader, ops, ops_capacity), unmasked_pid(id));
        else
            push_mode_player_entity_idx(next_op(reader, ops, ops_capacity), unmasked_pid(id));

        int mask = g1(&rsbuf);
        if( (mask & 0x80) != 0 )
        {
            mask += g1(&rsbuf) << 8;
        }

        // Info

        // Appearance
        if( (mask & MASK_APPEARANCE) != 0 )
        {
            int len = g1(&rsbuf);

            appearance_buf = malloc(len);
            greadto(&rsbuf, appearance_buf, len, len);

            push_op_appearance(next_op(reader, ops, ops_capacity), appearance_buf, len);
        }

        if( (mask & MASK_SEQUENCE) != 0 )
        {
            // int len = g1(&rsbuf);
            // uint8_t* sequence_buf = malloc(len);
            // greadto(&rsbuf, sequence_buf, len, len);
            // push_op_sequence(&ops[reader->current_op++], sequence_buf, len);
        }

        break;
    }

    return reader->current_op;
}