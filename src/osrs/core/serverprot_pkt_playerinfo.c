#include "osrs/core/serverprot_pkt_playerinfo.h"

#include "osrs/rscache/bitbuffer.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/wordpack.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Bit masks match Client-TS PlayerUpdate (ClientPlayer.ts). */
#define MASK_APPEARANCE 0x01  /* PlayerUpdate.APPEARANCE */
#define MASK_SEQUENCE 0x02    /* PlayerUpdate.ANIM */
#define MASK_FACE_ENTITY 0x04 /* PlayerUpdate.FACEENTITY */
#define MASK_SAY 0x08         /* PlayerUpdate.SAY */
#define MASK_DAMAGE 0x10      /* PlayerUpdate.HITMARK */
#define MASK_FACE_COORD 0x20  /* PlayerUpdate.FACESQUARE */
#define MASK_CHAT 0x40        /* PlayerUpdate.CHAT */
#define MASK_BIG_UPDATE 0x80  /* PlayerUpdate.BIG_UPDATE */
#define MASK_SPOTANIM 0x100   /* PlayerUpdate.SPOTANIM */
#define MASK_EXACT_MOVE 0x200 /* PlayerUpdate.EXACTMOVE */
#define MASK_DAMAGE2 0x400    /* PlayerUpdate.HITMARK2 */

static void
push_op_exact_move(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    uint8_t start_x,
    uint8_t start_z,
    uint8_t end_x,
    uint8_t end_z,
    uint16_t move_end_raw,
    uint16_t move_start_raw,
    uint8_t facing)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_EXACT_MOVE;
    op->_exactmove.forcemove_start_x = start_x;
    op->_exactmove.forcemove_start_z = start_z;
    op->_exactmove.forcemove_end_x = end_x;
    op->_exactmove.forcemove_end_z = end_z;
    op->_exactmove.forcemove_startcycle = move_start_raw;
    op->_exactmove.forcemove_endcycle = move_end_raw;
    op->_exactmove.forcemove_facedirection = facing;
}

static struct PktServerProt_PlayerInfo_v1_Op*
next_op(
    struct PktServerProt_PlayerInfoReader_v1* reader,
    struct PktServerProt_PlayerInfo_v1_Op* ops,
    int ops_capacity)
{
    assert(reader->current_op < ops_capacity);
    return &ops[reader->current_op++];
}

static void
push_op_set_local_player(struct PktServerProt_PlayerInfo_v1_Op* op)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SET_LOCAL_PLAYER;
}

static void
push_op_bits_count_reset(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int count)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_COUNT_RESET;
    op->_bitvalue = count;
}

static void
push_op_add_player_new_opbits_pid(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int player_id)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_ADD_PLAYER_NEW_OPBITS_PID;
    op->_bitvalue = player_id;
}

static void
push_op_add_player_old_opbits_idx(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int player_idx)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_ADD_PLAYER_OLD_OPBITS_IDX;
    op->_bitvalue = player_idx;
}

static void
push_op_set_player_opbits_idx(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int player_idx)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SET_PLAYER_OPBITS_IDX;
    op->_bitvalue = player_idx;
}

static void
push_op_clear_player_opbits_idx(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int player_idx)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_CLEAR_PLAYER_OPBITS_IDX;
    op->_bitvalue = player_idx;
}

static void
push_bits_info(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int info)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_INFO;
    op->_bitvalue = info;
}
static void
push_bits_walkdir(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int walkdir)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_WALKDIR;
    op->_bitvalue = walkdir;
}

static void
push_bits_rundir(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int rundir)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OPBITS_RUNDIR;
    op->_bitvalue = rundir;
}

static void
push_op_local_xz_level(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int x,
    int z,
    int level,
    bool jump)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_LOCAL_XZLEVEL;
    op->_local_xz_level.x = x;
    op->_local_xz_level.z = z;
    op->_local_xz_level.level = level;
    op->_local_xz_level.jump = jump;
}

static void
push_op_delta_xz(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int dx,
    int dz,
    bool jump)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_DELTA_XZ;
    op->_delta_xz.dx = dx;
    op->_delta_xz.dz = dz;
    op->_delta_xz.jump = jump;
}

static void
push_op_appearance(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    uint8_t* appearance_buf,
    int len)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_APPEARANCE;
    op->_appearance.appearance = appearance_buf;
    op->_appearance.len = len;
}

static void
push_op_sequence(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    uint16_t sequence_id,
    uint8_t delay)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SEQUENCE;
    op->_sequence.sequence_id = sequence_id;
    op->_sequence.delay = delay;
}

static void
push_op_face_entity(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int entity_id)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_FACE_ENTITY;
    op->_face_entity.entity_id = entity_id;
}

static void
push_op_say(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    char* chat_message)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SAY;
    op->_say.text = chat_message;
}

static void
push_op_chat(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int colour_effect,
    int type,
    int length,
    char* text)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_CHAT;
    op->_chat.color = (uint16_t)colour_effect;
    op->_chat.type = (uint8_t)type;
    op->_chat.length = (uint8_t)length;
    op->_chat.text = (uint8_t*)text;
}

static void
push_op_damage(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int damage_type,
    int damage,
    int health,
    int total_health)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_DAMAGE;
    op->_damage.damage_type = damage_type;
    op->_damage.damage = damage;
    op->_damage.health = health;
    op->_damage.total_health = total_health;
}

static void
push_op_damage2(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int damage,
    int damage_type,
    int health,
    int total_health)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_DAMAGE2;
    op->_damage2.damage = damage;
    op->_damage2.damage_type = damage_type;
    op->_damage2.health = health;
    op->_damage2.total_health = total_health;
}

static void
push_op_face_coord(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int target_x,
    int target_z)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_FACE_COORD;
    op->_face_coord.entity_id = -1;
    op->_face_coord.x = (int16_t)target_x;
    op->_face_coord.z = (int16_t)target_z;
}

static void
push_op_spotanim(
    struct PktServerProt_PlayerInfo_v1_Op* op,
    int spotanim_id,
    int height_delay)
{
    op->kind = PKT_SERVER_PROT_PLAYER_INFO_V1_OP_SPOTANIM;
    op->_spotanim.spotanim_id = spotanim_id;
    op->_spotanim.delay = height_delay; /* Client.ts: height = >>16, cycleDelay = &0xffff */
}

int
pkt_player_info_reader_read(
    struct PktServerProt_PlayerInfoReader_v1* reader,
    struct PktServerProt_PlayerInfo_v1* pkt,
    struct PktServerProt_PlayerInfo_v1_Op* ops,
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
            int rundir_one = gbits(&buf, 3);
            push_bits_rundir(next_op(reader, ops, ops_capacity), rundir_one);
            int rundir_two = gbits(&buf, 3);
            push_bits_rundir(next_op(reader, ops, ops_capacity), rundir_two);

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
            int sx = gbits(&buf, 7);
            int sz = gbits(&buf, 7);
            int jump = gbits(&buf, 1);
            push_op_local_xz_level(next_op(reader, ops, ops_capacity), sx, sz, level, jump);

            int has_extended_info = gbits(&buf, 1);
            if( has_extended_info )
            {
                reader->extended_queue[reader->extended_count++] = 2047;
            }
        }
        break;
        }
    }

    // Player Old Vis
    int new_idx = 0;
    int count = gbits(&buf, 8);
    push_op_bits_count_reset(next_op(reader, ops, ops_capacity), count);
    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        push_op_add_player_old_opbits_idx(next_op(reader, ops, ops_capacity), old_idx);

        int player_info = gbits(&buf, 1);
        push_bits_info(next_op(reader, ops, ops_capacity), player_info);

        if( player_info != 0 )
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
            {
                // rundir
                int rundir_one = gbits(&buf, 3);
                push_bits_rundir(next_op(reader, ops, ops_capacity), rundir_one);
                int rundir_two = gbits(&buf, 3);
                push_bits_rundir(next_op(reader, ops, ops_capacity), rundir_two);
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

    /* Player New Vis — Client.ts getPlayerNewVis: while (bitPos + 10 < size * 8).
     * Guarantees 11 bits before each iteration; non-terminator body reads 23 bits total. */
    while( (buf.byte_position * 8) + buf.bit_offset + 10 < pkt->length * 8 )
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
        int dy = gbits(&buf, 5);
        if( dy > 15 )
            dy -= 32;
        int jump = gbits(&buf, 1);
        push_op_delta_xz(next_op(reader, ops, ops_capacity), dx, dy, jump);

        int has_extended_info = gbits(&buf, 1);
        if( has_extended_info )
        {
            reader->extended_queue[reader->extended_count++] = new_idx;
        }

        new_idx++;
    }

    // Extended Info (byte-aligned); do not read past buffer
    uint8_t* appearance_buf = NULL;
    rsbuf.position = buf.byte_position + (buf.bit_offset + 7) / 8;
    for( int i = 0; i < reader->extended_count && rsbuf.position < pkt->length; i++ )
    {
        if( reader->current_op >= ops_capacity )
            break;
        int idx = reader->extended_queue[i];
        if( idx == 2047 )
        {
            push_op_set_local_player(next_op(reader, ops, ops_capacity));
        }
        else
        {
            push_op_set_player_opbits_idx(next_op(reader, ops, ops_capacity), idx);
        }

        if( rsbuf.position >= pkt->length )
            break;
        int mask = g1(&rsbuf);
        if( (mask & MASK_BIG_UPDATE) != 0 )
        {
            mask += g1(&rsbuf) << 8;
        }

        /* Info — order matches Client.ts getPlayerExtendedDecode */

        if( (mask & MASK_APPEARANCE) != 0 )
        {
            int len = g1(&rsbuf);

            appearance_buf = malloc(len);
            greadto(&rsbuf, appearance_buf, len, len);

            push_op_appearance(next_op(reader, ops, ops_capacity), appearance_buf, len);
        }

        if( (mask & MASK_SEQUENCE) != 0 )
        {
            /* PlayerUpdate.ANIM: seqId = g2(), delay = g1() */
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
            char* chat_message = gstringnewlineorend(&rsbuf);
            push_op_say(next_op(reader, ops, ops_capacity), chat_message);
        }

        if( (mask & MASK_DAMAGE) != 0 )
        {
            /* PlayerUpdate.HITMARK: damage, damageType, health, totalHealth */
            int damage = g1(&rsbuf);
            int damage_type = g1(&rsbuf);
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
            int colour_effect = g2(&rsbuf);
            int type = g1(&rsbuf);
            int length = g1(&rsbuf);
            char* chat_text = wordpack_unpack(&rsbuf, length);
            push_op_chat(
                next_op(reader, ops, ops_capacity), colour_effect, type, length, chat_text);
        }

        /* MASK_BIG_UPDATE: mask second byte only; no payload here. */

        if( (mask & MASK_SPOTANIM) != 0 )
        {
            int spotanim_id = g2(&rsbuf);
            int height_delay = g4(&rsbuf);
            push_op_spotanim(next_op(reader, ops, ops_capacity), spotanim_id, height_delay);
        }

        if( (mask & MASK_EXACT_MOVE) != 0 )
        {
            /* PlayerUpdate.EXACTMOVE — Client.ts getPlayerExtendedDecode */
            uint8_t sx = (uint8_t)g1(&rsbuf);
            uint8_t sz = (uint8_t)g1(&rsbuf);
            uint8_t ex = (uint8_t)g1(&rsbuf);
            uint8_t ez = (uint8_t)g1(&rsbuf);
            uint16_t move_end = (uint16_t)g2(&rsbuf);
            uint16_t move_start = (uint16_t)g2(&rsbuf);
            uint8_t facing = (uint8_t)g1(&rsbuf);
            push_op_exact_move(
                next_op(reader, ops, ops_capacity), sx, sz, ex, ez, move_end, move_start, facing);
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

    (void)appearance_buf;
    return reader->current_op;
}
