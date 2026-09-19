/*
 * PLAYER_INFO and NPC_INFO for LostCity build 289.
 *
 * A whole reader per revision, not one reader with a revision flag in it.
 * These two packets are bit streams: every field's position is decided by the
 * width of every field before it, so a build's layout is one indivisible thing
 * and a decoder shared across builds is a decoder that must be told, at each
 * field, which build it is reading. That is how a one-bit difference becomes a
 * conditional in the middle of a hot loop, then two, and eventually nobody can
 * read either revision's layout off the page. The rev table already has the
 * seam for this -- `player_info_read` / `npc_info_read`, which osrs239 uses the
 * same way -- so 289 gets its own stream and 254's (net/rev/packets/) stays
 * exactly the 254 layout, unable to drift.
 *
 * ## What 289 changed from 254
 *
 * One field, in one place: the NEW-NPC record carries a jump flag between its
 * (dx, dz) and its extended-info bit.
 *
 *   254  slot(14) type(11) dx(5) dz(5)          extended(1)
 *   289  slot(14) type(11) dx(5) dz(5) jump(1)  extended(1)
 *
 * It is the teleport-vs-walk distinction the old-vis section always had,
 * extended to an npc appearing for the first time (webclient
 * Client.getNpcPosNewVis, branch 289). Missing it does not produce a slightly
 * wrong npc: every record after it in the section is read one bit off, the
 * section runs past the end of the packet, and the client aborts in
 * Net_BitBufferGbits.
 *
 * Everything else -- the local-player block, the old-vis movement ops, the
 * new-player record, and both extended-info blocks with all their masks -- is
 * byte-for-byte what 254 sends. Verified by diffing the two Client.ts
 * decoders: outside that one bit the only differences are renamed methods and
 * a static `loopCycle`.
 *
 * The op vocabulary (PktPlayerInfoOp / PktNpcInfoOp) is deliberately NOT
 * duplicated. That is the interface to the consumer, not part of the wire, and
 * it is the same question answered for both builds.
 *
 * ## The appearance block
 *
 * A third 289 delta, and it lives with the other appearance encodings rather
 * than here: the block gained a two-byte SKILL LEVEL after the combat level
 * (APPEARANCE_ENC_CLASSIC_LC289). It is only reachable through the rev table's
 * `appearance_decode` hook, which is what lc289_appearance_decode below is.
 */

#include "net/bitbuffer.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"

#include <assert.h>
#include <rsbuffer.h>
#include <stdlib.h>
#include <string.h>

/*
 * Per-call decode state. The rev hook takes no reader handle -- it is
 * (data, len, ops, cap) and nothing else -- so the queue of entries owed an
 * extended-info block lives here, on the stack, for the length of one packet.
 */
struct Lc289EntityDecode
{
    uint16_t extended_queue[2048];
    int extended_count;
    int current_op;
    /** Set when the packet asked for more ops than the caller's array holds.
     *  Everything past that point is discarded, so the extended-info blocks no
     *  longer line up with the list positions they address. */
    int overflowed;
    /** Where a write goes once the array is full, so a packet claiming more ops
     *  than fit cannot reach past the end of it. One decode runs at a time, so
     *  the two op shapes share it. */
    union
    {
        struct PktPlayerInfoOp player;
        struct PktNpcInfoOp npc;
    } op_sink;
};

static void
lc289_queue_extended(
    struct Lc289EntityDecode* dec,
    int idx)
{
    int cap = (int)(sizeof(dec->extended_queue) / sizeof(dec->extended_queue[0]));
    if( dec->extended_count < cap )
        dec->extended_queue[dec->extended_count++] = (uint16_t)idx;
}

/* ------------------------------------------------------------ PLAYER_INFO */

static struct PktPlayerInfoOp*
lc289_next_player_op(
    struct Lc289EntityDecode* dec,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    /* A guard, not an assert -- the op count is the server's choice, so running
     * out of room is hostile input rather than a caller bug, and the extended
     * loop below checks capacity once per entry then emits one op per bit of a
     * server-supplied mask. The shipping lane compiles -DNDEBUG, so an assert
     * here is no bounds check at all. See next_op in pkt_player_info.c. */
    assert(ops_capacity > 0);
    if( dec->current_op >= ops_capacity )
    {
        dec->overflowed = 1;
        memset(&dec->op_sink.player, 0, sizeof(dec->op_sink.player));
        return &dec->op_sink.player;
    }
    struct PktPlayerInfoOp* op = &ops[dec->current_op++];
    memset(op, 0, sizeof(*op));
    return op;
}

/*
 * The local player's own movement block.
 *
 * Its op 3 is a teleport (level + scene x/z + jump), where a tracked player's
 * op 3 means "drop this player". Same three bits, opposite meanings, which is
 * why the local block is written out here rather than folded into the tracked
 * loop below.
 */
static void
lc289_read_local_movement(
    struct Lc289EntityDecode* dec,
    struct Net_BitBuffer* buf,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    int info = Net_BitBufferGbits(buf, 1);
    if( info == 0 )
    {
        struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OPBITS_INFO;
        op->_bitvalue = 0;
        return;
    }

    switch( Net_BitBufferGbits(buf, 2) )
    {
    case 0:
        lc289_queue_extended(dec, PKT_PLAYER_INFO_LOCAL_PLAYER_IDX);
        break;
    case 1:
    {
        int walkdir = Net_BitBufferGbits(buf, 3);
        struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OPBITS_WALKDIR;
        op->_bitvalue = (uint64_t)walkdir;
        if( Net_BitBufferGbits(buf, 1) )
            lc289_queue_extended(dec, PKT_PLAYER_INFO_LOCAL_PLAYER_IDX);
        break;
    }
    case 2:
    {
        for( int r = 0; r < 2; r++ )
        {
            int rundir = Net_BitBufferGbits(buf, 3);
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OPBITS_RUNDIR;
            op->_bitvalue = (uint64_t)rundir;
        }
        if( Net_BitBufferGbits(buf, 1) )
            lc289_queue_extended(dec, PKT_PLAYER_INFO_LOCAL_PLAYER_IDX);
        break;
    }
    case 3:
    {
        int level = Net_BitBufferGbits(buf, 2);
        int sx = Net_BitBufferGbits(buf, 7);
        int sz = Net_BitBufferGbits(buf, 7);
        int jump = Net_BitBufferGbits(buf, 1);
        struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL;
        op->_local_xz_level.x = (int16_t)sx;
        op->_local_xz_level.z = (int16_t)sz;
        op->_local_xz_level.level = (uint8_t)level;
        op->_local_xz_level.jump = jump != 0;
        if( Net_BitBufferGbits(buf, 1) )
            lc289_queue_extended(dec, PKT_PLAYER_INFO_LOCAL_PLAYER_IDX);
        break;
    }
    }
}

static void
lc289_read_player_extended(
    struct Lc289EntityDecode* dec,
    uint8_t const* data,
    int length,
    int byte_pos,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    struct RSCache_Buffer rsbuf;

    RSCache_BufferInit(&rsbuf, (uint8_t*)data, (uint32_t)length);
    rsbuf.position = (uint32_t)byte_pos;

    for( int i = 0; i < dec->extended_count && (int)rsbuf.position < length; i++ )
    {
        if( dec->current_op >= ops_capacity )
            break;

        int idx = dec->extended_queue[i];
        {
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = idx == PKT_PLAYER_INFO_LOCAL_PLAYER_IDX
                           ? PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER
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
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_APPEARANCE;
            op->_appearance.appearance = blob;
            op->_appearance.len = len;
        }

        if( (mask & PKT_PLAYER_MASK_SEQUENCE) != 0 )
        {
            int seq_raw = g2(&rsbuf);
            int delay = g1(&rsbuf);
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_SEQUENCE;
            op->_sequence.sequence_id = seq_raw == 65535 ? -1 : seq_raw;
            op->_sequence.delay = (uint8_t)delay;
        }

        if( (mask & PKT_PLAYER_MASK_FACE_ENTITY) != 0 )
        {
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_FACE_ENTITY;
            op->_face_entity.entity_id = g2(&rsbuf);
        }

        if( (mask & PKT_PLAYER_MASK_SAY) != 0 )
        {
            char* text = gstringnewline(&rsbuf);
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_SAY;
            op->_say.text = text;
        }

        if( (mask & PKT_PLAYER_MASK_DAMAGE) != 0 )
        {
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_DAMAGE;
            op->_damage.damage = (uint8_t)g1(&rsbuf);
            op->_damage.damage_type = (uint8_t)g1(&rsbuf);
            op->_damage.health = (uint8_t)g1(&rsbuf);
            op->_damage.total_health = (uint8_t)g1(&rsbuf);
        }

        if( (mask & PKT_PLAYER_MASK_FACE_COORD) != 0 )
        {
            int target_x = g2(&rsbuf);
            int target_z = g2(&rsbuf);
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_FACE_COORD;
            op->_face_coord.x = (int16_t)target_x;
            op->_face_coord.z = (int16_t)target_z;
        }

        if( (mask & PKT_PLAYER_MASK_CHAT) != 0 )
        {
            /* colourEffect g2, type g1, length g1, then exactly `length`
             * wordpacked bytes. The payload has to be consumed even when it is
             * not wanted -- it is inside the block, not after it. */
            int colour_effect = g2(&rsbuf);
            int type = g1(&rsbuf);
            int chat_len = g1(&rsbuf);
            uint8_t* payload = malloc(chat_len > 0 ? (size_t)chat_len : 1);
            assert(payload);
            greadto(&rsbuf, (char*)payload, chat_len, chat_len);
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_CHAT;
            op->_chat.colour_effect = (uint16_t)colour_effect;
            op->_chat.type = (uint8_t)type;
            op->_chat.length = (uint8_t)chat_len;
            op->_chat.data = payload;
        }

        /* BIG_UPDATE was consumed at the mask read; it extends the mask to 16
         * bits and carries no payload of its own. */

        if( (mask & PKT_PLAYER_MASK_SPOTANIM) != 0 )
        {
            int spotanim_id = g2(&rsbuf);
            int height_delay = g4(&rsbuf);
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_SPOTANIM;
            op->_spotanim.spotanim_id = spotanim_id == 65535 ? -1 : spotanim_id;
            op->_spotanim.height_delay = height_delay;
        }

        if( (mask & PKT_PLAYER_MASK_EXACT_MOVE) != 0 )
        {
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
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
            struct PktPlayerInfoOp* op = lc289_next_player_op(dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_DAMAGE2;
            op->_damage.damage = (uint8_t)g1(&rsbuf);
            op->_damage.damage_type = (uint8_t)g1(&rsbuf);
            op->_damage.health = (uint8_t)g1(&rsbuf);
            op->_damage.total_health = (uint8_t)g1(&rsbuf);
        }
    }
}

int
lc289_player_info_read(
    uint8_t const* data,
    int length,
    struct PktPlayerInfoOp* ops,
    int ops_capacity)
{
    struct Lc289EntityDecode dec;
    struct Net_BitBuffer buf;
    int new_idx = 0;
    int count;

    assert(data);
    assert(ops);

    memset(&dec, 0, sizeof(dec));
    Net_BitBufferInit(&buf, data, length);

    {
        struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER;
    }
    lc289_read_local_movement(&dec, &buf, ops, ops_capacity);

    /*
     * `new_idx` below is a CONTRACT with the consumer, not a local counter.
     *
     * Extended-info blocks are addressed by POSITION in the list both sides
     * rebuild during this packet, and this index defines those positions. It
     * advances for every entry kept or added, unconditionally. The consumer
     * (task_exec_entity_info.c) must append exactly one entry per increment --
     * a sentinel when it cannot resolve the slot -- or every later block in the
     * packet lands on the entity AFTER its intended target, silently.
     */
    count = Net_BitBufferGbits(&buf, 8);
    {
        struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
        op->kind = PKT_PLAYER_INFO_OPBITS_COUNT_RESET;
        op->_bitvalue = (uint64_t)count;
    }

    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        /*
         * The info bit and the 2-bit op have to be read BEFORE the ADD is
         * emitted, because op 3 is "this player left" -- the reference does not
         * carry a dropped entry into the list it is rebuilding
         * (Client.ts getPlayerOldVis: every branch but op 3 does
         * `playerIds[playerCount++] = index`). Emitting ADD first and CLEAR
         * afterwards makes the consumer append an entry the decoder's own
         * `new_idx` never counted, so from the drop onward
         *   - every extended block resolves `active_players[new_idx]` one entry
         *     early, landing appearance/chat/hits on the player before it, and
         *   - the list handed to the NEXT packet as `old_list` keeps the dead
         *     player's slot, shifting every old_idx after it by one and
         *     count-shrinking a live player off the tail.
         * The npc reader below already reads-then-emits for this reason.
         */
        int info = Net_BitBufferGbits(&buf, 1);
        int move_op = -1;
        if( info != 0 )
            move_op = (int)Net_BitBufferGbits(&buf, 2);

        if( move_op == 3 )
        {
            struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_CLEAR_PLAYER_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            continue;
        }

        {
            struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_ADD_PLAYER_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = lc289_next_player_op(&dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;
        }
        if( info == 0 )
        {
            new_idx++;
            continue;
        }

        switch( move_op )
        {
        case 0:
            lc289_queue_extended(&dec, new_idx);
            new_idx++;
            break;
        case 1:
        {
            int walkdir = Net_BitBufferGbits(&buf, 3);
            struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OPBITS_WALKDIR;
            op->_bitvalue = (uint64_t)walkdir;
            if( Net_BitBufferGbits(&buf, 1) )
                lc289_queue_extended(&dec, new_idx);
            new_idx++;
            break;
        }
        case 2:
        {
            for( int r = 0; r < 2; r++ )
            {
                int rundir = Net_BitBufferGbits(&buf, 3);
                struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
                op->kind = PKT_PLAYER_INFO_OPBITS_RUNDIR;
                op->_bitvalue = (uint64_t)rundir;
            }
            if( Net_BitBufferGbits(&buf, 1) )
                lc289_queue_extended(&dec, new_idx);
            new_idx++;
            break;
        }
        }
    }

    /*
     * New players: pid(11) dx(5) dz(5) jump(1) extended(1), terminated by pid
     * 2047. The loop guard asks only for the pid, not a whole record: a packet
     * that ends immediately after the terminator would otherwise leave it
     * unconsumed and the byte-aligned extended-info section would start one
     * record early.
     */
    while( Net_BitBufferBitPos(&buf) + 11 <= length * 8 )
    {
        int player_id = Net_BitBufferGbits(&buf, 11);
        if( player_id == PKT_PLAYER_INFO_NEW_TERMINATOR )
            break;

        {
            struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
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
            struct PktPlayerInfoOp* op = lc289_next_player_op(&dec, ops, ops_capacity);
            op->kind = PKT_PLAYER_INFO_OP_DELTA_XZ;
            op->_delta_xz.dx = (int16_t)dx;
            op->_delta_xz.dz = (int16_t)dz;
            op->_delta_xz.jump = jump != 0;
        }

        if( Net_BitBufferGbits(&buf, 1) )
            lc289_queue_extended(&dec, new_idx);
        new_idx++;
    }

    lc289_read_player_extended(
        &dec, data, length, Net_BitBufferBytePos(&buf), ops, ops_capacity);
    return dec.current_op;
}

/* ------------------------------------------------------------- APPEARANCE */

int
lc289_appearance_decode(
    uint8_t const* data,
    int length,
    struct PktPlayerAppearance* out)
{
    assert(data);
    assert(out);
    return PktPlayerAppearance_DecodeAs(
        out, APPEARANCE_ENC_CLASSIC_LC289, data, length);
}

/* --------------------------------------------------------------- NPC_INFO */

static struct PktNpcInfoOp*
lc289_next_npc_op(
    struct Lc289EntityDecode* dec,
    struct PktNpcInfoOp* ops,
    int ops_capacity)
{
    /* A guard, not an assert -- see lc289_next_player_op above. */
    assert(ops_capacity > 0);
    if( dec->current_op >= ops_capacity )
    {
        dec->overflowed = 1;
        memset(&dec->op_sink.npc, 0, sizeof(dec->op_sink.npc));
        return &dec->op_sink.npc;
    }
    struct PktNpcInfoOp* op = &ops[dec->current_op++];
    memset(op, 0, sizeof(*op));
    return op;
}

static void
lc289_read_npc_extended(
    struct Lc289EntityDecode* dec,
    uint8_t const* data,
    int length,
    int byte_pos,
    struct PktNpcInfoOp* ops,
    int ops_capacity)
{
    struct RSCache_Buffer rsbuf;

    RSCache_BufferInit(&rsbuf, (uint8_t*)data, (uint32_t)length);
    rsbuf.position = (uint32_t)byte_pos;

    for( int i = 0; i < dec->extended_count && (int)rsbuf.position < length; i++ )
    {
        if( dec->current_op >= ops_capacity )
            break;

        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX;
            op->_bitvalue = (uint64_t)dec->extended_queue[i];
        }

        if( (int)rsbuf.position >= length )
            break;
        int mask = g1(&rsbuf);

        if( (mask & PKT_NPC_MASK_DAMAGE2) != 0 )
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
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
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SEQUENCE;
            op->_sequence.sequence_id = seq_raw == 65535 ? -1 : seq_raw;
            op->_sequence.delay = (uint8_t)delay;
        }

        if( (mask & PKT_NPC_MASK_FACE_ENTITY) != 0 )
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_FACE_ENTITY;
            op->_face_entity.entity_id = g2(&rsbuf);
        }

        if( (mask & PKT_NPC_MASK_SAY) != 0 )
        {
            char* text = gstringnewline(&rsbuf);
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SAY;
            op->_say.text = text;
        }

        if( (mask & PKT_NPC_MASK_DAMAGE) != 0 )
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_DAMAGE;
            op->_damage.damage = (uint8_t)g1(&rsbuf);
            op->_damage.damage_type = (uint8_t)g1(&rsbuf);
            op->_damage.health = (uint8_t)g1(&rsbuf);
            op->_damage.total_health = (uint8_t)g1(&rsbuf);
        }

        if( (mask & PKT_NPC_MASK_CHANGE_TYPE) != 0 )
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_CHANGE_TYPE;
            op->_change_type.npc_type = g2(&rsbuf);
        }

        if( (mask & PKT_NPC_MASK_SPOTANIM) != 0 )
        {
            int spotanim_id = g2(&rsbuf);
            int height_delay = g4(&rsbuf);
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_SPOTANIM;
            op->_spotanim.spotanim_id = spotanim_id == 65535 ? -1 : spotanim_id;
            op->_spotanim.height_delay = height_delay;
        }

        if( (mask & PKT_NPC_MASK_FACE_COORD) != 0 )
        {
            int target_x = g2(&rsbuf);
            int target_z = g2(&rsbuf);
            struct PktNpcInfoOp* op = lc289_next_npc_op(dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_FACE_COORD;
            op->_face_coord.x = (int16_t)target_x;
            op->_face_coord.z = (int16_t)target_z;
        }
    }
}

int
lc289_npc_info_read(
    uint8_t const* data,
    int length,
    struct PktNpcInfoOp* ops,
    int ops_capacity)
{
    struct Lc289EntityDecode dec;
    struct Net_BitBuffer buf;
    int new_idx = 0;
    int count;

    assert(data);
    assert(ops);

    memset(&dec, 0, sizeof(dec));
    Net_BitBufferInit(&buf, data, length);

    /* Tracked npcs. See the `new_idx` contract note in the player reader --
     * the same rule governs this list. */
    count = Net_BitBufferGbits(&buf, 8);
    {
        struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
        op->kind = PKT_NPC_INFO_OPBITS_COUNT_RESET;
        op->_bitvalue = (uint64_t)count;
    }

    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        int info = Net_BitBufferGbits(&buf, 1);

        if( info == 0 )
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = 0;
            new_idx++;
            continue;
        }

        switch( Net_BitBufferGbits(&buf, 2) )
        {
        case 0:
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;
            lc289_queue_extended(&dec, new_idx);
            new_idx++;
            break;
        }
        case 1:
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;

            int walkdir = Net_BitBufferGbits(&buf, 3);
            op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_WALKDIR;
            op->_bitvalue = (uint64_t)walkdir;

            if( Net_BitBufferGbits(&buf, 1) )
                lc289_queue_extended(&dec, new_idx);
            new_idx++;
            break;
        }
        case 2:
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_INFO;
            op->_bitvalue = (uint64_t)info;

            for( int r = 0; r < 2; r++ )
            {
                int rundir = Net_BitBufferGbits(&buf, 3);
                op = lc289_next_npc_op(&dec, ops, ops_capacity);
                op->kind = PKT_NPC_INFO_OPBITS_RUNDIR;
                op->_bitvalue = (uint64_t)rundir;
            }

            if( Net_BitBufferGbits(&buf, 1) )
                lc289_queue_extended(&dec, new_idx);
            new_idx++;
            break;
        }
        case 3:
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX;
            op->_bitvalue = (uint64_t)old_idx;
            break;
        }
        }
    }

    /*
     * New npcs -- THE ONE PLACE 289 DIFFERS FROM 254:
     *
     *   slot(14) type(11) dx(5) dz(5) jump(1) extended(1)
     *
     * terminated by an all-ones slot (16383). The 21-bit loop guard is the
     * reference client's own fixed margin and is NOT widened to match the extra
     * bit: it is what decides whether the terminator is consumed before the
     * byte-aligned extended-info section, so moving it would shift that
     * alignment rather than protect anything.
     */
    while( Net_BitBufferBitPos(&buf) + 21 < length * 8 )
    {
        int npc_slot = Net_BitBufferGbits(&buf, PKT_NPC_INFO_SLOT_BITS_CLASSIC);
        if( npc_slot == (1 << PKT_NPC_INFO_SLOT_BITS_CLASSIC) - 1 )
            break;

        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID;
            op->_bitvalue = (uint64_t)npc_slot;
        }

        {
            int npc_type = Net_BitBufferGbits(&buf, PKT_NPC_INFO_TYPE_BITS_CLASSIC);
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OPBITS_NPCTYPE;
            op->_bitvalue = (uint64_t)npc_type;
        }

        int dx = Net_BitBufferGbits(&buf, 5);
        if( dx > 15 )
            dx -= 32;
        int dz = Net_BitBufferGbits(&buf, 5);
        if( dz > 15 )
            dz -= 32;
        int jump = Net_BitBufferGbits(&buf, 1);
        {
            struct PktNpcInfoOp* op = lc289_next_npc_op(&dec, ops, ops_capacity);
            op->kind = PKT_NPC_INFO_OP_DELTA_XZ;
            op->_delta_xz.dx = (int16_t)dx;
            op->_delta_xz.dz = (int16_t)dz;
            op->_delta_xz.jump = jump != 0;
        }

        if( Net_BitBufferGbits(&buf, 1) )
            lc289_queue_extended(&dec, new_idx);
        new_idx++;
    }

    lc289_read_npc_extended(
        &dec, data, length, Net_BitBufferBytePos(&buf), ops, ops_capacity);
    return dec.current_op;
}
