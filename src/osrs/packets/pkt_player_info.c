#include "pkt_player_info.h"

#include "osrs/rscache/bitbuffer.h"
#include "osrs/rscache/rsbuf.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define MASK_APPEARANCE 0x01
#define MASK_SEQUENCE 0x02
#define MASK_FACE_ENTITY 0x04
#define MASK_SAY 0x08
#define MASK_DAMAGE 0x10
#define MASK_FACE_COORD 0x20
#define MASK_CHAT 0x40
#define MASK_BIG_UPDATE 0x80
#define MASK_SPOTANIM 0x100
#define MASK_EXACT_MOVE 0x200
#define MASK_DAMAGE2 0x400

static struct PktPlayerInfoOp*
next_op(
    struct PktPlayerInfoReader* reader,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    assert(reader->current_op < ops_capacity);
    return &ops[reader->current_op++];
}

static void
push_op_set_local_player(struct PktPlayerInfoOp* op)
{
    op->kind = PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER;
}

static void
push_op_bits_count_reset(
    struct PktPlayerInfoOp* op,
    int count)
{
    op->kind = PKT_PLAYER_INFO_OPBITS_COUNT_RESET;
    op->_bitvalue = count;
}

static void
push_op_add_player_new_opbits_pid(
    struct PktPlayerInfoOp* op,
    int player_id)
{
    op->kind = PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID;
    op->_bitvalue = player_id;
}

static void
push_op_add_player_old_opbits_idx(
    struct PktPlayerInfoOp* op,
    int player_idx)
{
    op->kind = PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX;
    op->_bitvalue = player_idx;
}

static void
push_op_set_player_opbits_idx(
    struct PktPlayerInfoOp* op,
    int player_idx)
{
    op->kind = PKT_PLAYER_INFO_OP_SET_PLAYER_OPBITS_IDX;
    op->_bitvalue = player_idx;
}

static void
push_op_clear_player_opbits_idx(
    struct PktPlayerInfoOp* op,
    int player_idx)
{
    op->kind = PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX;
    op->_bitvalue = player_idx;
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

static void
push_op_sequence(
    struct PktPlayerInfoOp* op,
    uint16_t sequence_id,
    uint8_t delay)
{
    op->kind = PKT_PLAYER_INFO_OP_SEQUENCE;
    op->_sequence.sequence_id = sequence_id;
    op->_sequence.delay = delay;
}

static void
push_op_face_entity(
    struct PktPlayerInfoOp* op,
    int entity_id)
{
    op->kind = PKT_PLAYER_INFO_OP_FACE_ENTITY;
    op->_face_entity.entity_id = entity_id;
}

static void
push_op_damage(
    struct PktPlayerInfoOp* op,
    int damage_type,
    int damage,
    int health,
    int total_health)
{
    op->kind = PKT_PLAYER_INFO_OP_DAMAGE;
    op->_damage.damage_type = damage_type;
    op->_damage.damage = damage;
    op->_damage.health = health;
    op->_damage.total_health = total_health;
}

static void
push_op_damage2(
    struct PktPlayerInfoOp* op,
    int damage,
    int damage_type,
    int health,
    int total_health)
{
    op->kind = PKT_PLAYER_INFO_OP_DAMAGE2;
    op->_damage2.damage = damage;
    op->_damage2.damage_type = damage_type;
    op->_damage2.health = health;
    op->_damage2.total_health = total_health;
}

static void
push_op_face_coord(
    struct PktPlayerInfoOp* op,
    int target_x,
    int target_z)
{
    op->kind = PKT_PLAYER_INFO_OP_FACE_COORD;
    op->_face_coord.entity_id = -1;
    op->_face_coord.x = (int16_t)target_x;
    op->_face_coord.z = (int16_t)target_z;
}

static void
push_op_spotanim(
    struct PktPlayerInfoOp* op,
    int spotanim_id,
    int height_delay)
{
    op->kind = PKT_PLAYER_INFO_OP_SPOTANIM;
    op->_spotanim.spotanim_id = spotanim_id;
    op->_spotanim.delay = height_delay; /* Client.ts: height = >>16, cycleDelay = &0xffff */
}

int
pkt_player_info_reader_read(
    struct PktPlayerInfoReader* reader,
    struct PktPlayerInfo* pkt,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    reader->current_op = 0;
    reader->extended_count = 0;
    struct BitBuffer buf;
    struct RSBuffer rsbuf;
    rsbuf_init(&rsbuf, pkt->data, pkt->length);
    bitbuffer_init_from_rsbuf(&buf, &rsbuf);
    bits(&buf);

    push_op_set_local_player(next_op(reader, ops, ops_capacity));

    // Player local
    int info = gbits(&buf, 1);
    if( info != 0 )
    {
        int op = gbits(&buf, 2);
        switch( op )
        {
        case 0:
            reader->extended_queue[reader->extended_count++] = 2047;
            break;
        case 1:
        { // walkdir
            int walkdir = gbits(&buf, 3);
            push_bits_walkdir(next_op(reader, ops, ops_capacity), walkdir);

            // has extended info
            int has_extended_info = gbits(&buf, 1);
            if( has_extended_info )
            {
                reader->extended_queue[reader->extended_count++] = 2047;
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
                reader->extended_queue[reader->extended_count++] = 2047;
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
                reader->extended_queue[reader->extended_count++] = 2047;
            }
        }
        break;
        }
    }
    else
    {
        push_bits_info(next_op(reader, ops, ops_capacity), info);
    }

    // Player Old Vis
    int new_idx = 0;
    int count = gbits(&buf, 8);
    push_op_bits_count_reset(next_op(reader, ops, ops_capacity), count);
    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        push_op_add_player_old_opbits_idx(next_op(reader, ops, ops_capacity), old_idx);

        int info = gbits(&buf, 1);
        push_bits_info(next_op(reader, ops, ops_capacity), info);

        if( info != 0 )
        {
            int op = gbits(&buf, 2);
            switch( op )
            {
            case 0:
                //
                reader->extended_queue[reader->extended_count++] = new_idx;
                new_idx += 1;
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
                        reader->extended_queue[reader->extended_count++] = new_idx;
                    }
                    new_idx += 1;
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
                    reader->extended_queue[reader->extended_count++] = new_idx;
                }
                new_idx += 1;
            }
            break;
            case 3:
                push_op_clear_player_opbits_idx(next_op(reader, ops, ops_capacity), old_idx);
                break;
            }
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

        push_op_add_player_new_opbits_pid(next_op(reader, ops, ops_capacity), player_id);

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
            reader->extended_queue[reader->extended_count++] = new_idx;
        }

        new_idx++;
    }

    // Extended Info
    uint8_t* appearance_buf = NULL;
    rsbuf.position = buf.byte_position + (buf.bit_offset + 7) / 8;
    for( int i = 0; i < reader->extended_count; i++ )
    {
        int idx = reader->extended_queue[i];
        if( idx == 2047 )
        {
            push_op_set_local_player(next_op(reader, ops, ops_capacity));
        }
        else
        {
            push_op_set_player_opbits_idx(next_op(reader, ops, ops_capacity), idx);
        }

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
            /* Client.ts PlayerUpdate.ANIM: seqId = buf.g2(), 65535 -> -1; delay = buf.g1() */
            int seq_raw = g2(&rsbuf);
            uint16_t sequence_id = (uint16_t)(seq_raw & 0xFFFF);
            uint8_t delay = (uint8_t)g1(&rsbuf);
            push_op_sequence(next_op(reader, ops, ops_capacity), sequence_id, delay);
        }

        if( (mask & MASK_FACE_ENTITY) != 0 )
        {
            int entity_id = g2(&rsbuf);
            push_op_face_entity(next_op(reader, ops, ops_capacity), entity_id);
        }

        if( (mask & MASK_SAY) != 0 )
        {
            assert(0);
        }

        if( (mask & MASK_DAMAGE) != 0 )
        {
            int damage_type = g1(&rsbuf);
            int damage = g1(&rsbuf);
            int health = g1(&rsbuf);
            int total_health = g1(&rsbuf);
            push_op_damage(
                next_op(reader, ops, ops_capacity), damage_type, damage, health, total_health);
        }

        if( (mask & MASK_FACE_COORD) != 0 )
        {
            int target_x = g2(&rsbuf);
            int target_z = g2(&rsbuf);
            push_op_face_coord(next_op(reader, ops, ops_capacity), target_x, target_z);
        }

        if( (mask & MASK_CHAT) != 0 )
        {
            assert(0);
        }

        if( (mask & MASK_BIG_UPDATE) != 0 )
        {
            assert(0);
        }

        if( (mask & MASK_SPOTANIM) != 0 )
        {
            /* Client.ts 8062-8077: spotanimId = g2(), heightDelay = g4() */
            int spotanim_id = g2(&rsbuf);
            int height_delay = g4(&rsbuf);
            push_op_spotanim(next_op(reader, ops, ops_capacity), spotanim_id, height_delay);
        }

        if( (mask & MASK_EXACT_MOVE) != 0 )
        {
            assert(0);
        }

        if( (mask & MASK_DAMAGE2) != 0 )
        {
            int damage = g1(&rsbuf);
            int damage_type = g1(&rsbuf);
            int health = g1(&rsbuf);
            int total_health = g1(&rsbuf);
            push_op_damage2(
                next_op(reader, ops, ops_capacity), damage, damage_type, health, total_health);
        }
    }

    return reader->current_op;
}