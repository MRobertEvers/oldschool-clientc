/*
 * PLAYER_INFO / NPC_INFO -- revision 241's entity streams, client side. A COPY
 * of osrs239_entity_info.c (read that file's header for the design: a stateful
 * codec emitting the shared op arrays), changed to 241's wire.
 *
 * PROVENANCE. RSProt stops at 239, so the 241 changes come from RSProx's
 * osrs-241 PlayerInfoClient.kt / NpcInfoClient.kt (protocol/osrs-241, model/
 * info/...). What moved since 239:
 *
 *  - PLAYER_INFO: the bit sections are unchanged. Every extended-info flag was
 *    renumbered (the continuation bits are now 0x80 / 0x2000), the block order
 *    was reshuffled, most byte transforms changed, the appearance block is
 *    length g1Alt2 + REVERSED bytes (gdataAlt1), spot anims gained a trailing
 *    loop byte, and FACE_PATHINGENTITY is gone.
 *
 *  - NPC_INFO is NPC_INFO_SMALL_V6. High resolution is unchanged; the
 *    low-resolution add record was rebuilt (dz first, a 2-bit spawn-clock
 *    width selector, a 2-bit type width selector), and the extended-info flags
 *    and order changed the same way the player's did, with a new
 *    BODY_CUSTOMISATION_V4 block and a loop byte on spot anims.
 */

#include "mem/torirs_mem.h"
#include "net/bitbuffer.h"
#include "torirs_env.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_info.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

#define V5_PLAYER_SLOTS 2048

/* `unmodifiedFlags`: bit 0 is "inactive this cycle", bit 1 "inactive next".
 * The whole array shifts right one place at the end of every packet, which is
 * how a skip run written in one tick is consumed in the next. */
#define V5_CUR_CYCLE_INACTIVE 0x1
#define V5_NEXT_CYCLE_INACTIVE 0x2

/* Player extended-info flags, revision 241 (RSProx PlayerInfoClient). 0x1 is
 * RSProx's UNUSED_FLAGS -- no block is known for it -- and 0x200 is unassigned. */
#define V5_PLAYER_UNUSED 0x1
#define V5_PLAYER_APPEARANCE 0x2
#define V5_PLAYER_SPOTANIM 0x4
#define V5_PLAYER_RESET 0x8
#define V5_PLAYER_SAY 0x10
#define V5_PLAYER_FACE 0x20
#define V5_PLAYER_SEQUENCE 0x40
#define V5_PLAYER_EXT_SHORT 0x80
#define V5_PLAYER_MOVE_SPEED 0x100
#define V5_PLAYER_CHAT 0x400
#define V5_PLAYER_EXACT_MOVE 0x800
#define V5_PLAYER_TINTING 0x1000
#define V5_PLAYER_EXT_MEDIUM 0x2000
#define V5_PLAYER_TEMP_MOVE_SPEED 0x4000
#define V5_PLAYER_MINIMENU 0x8000 /* NAME_EXTRAS */
#define V5_PLAYER_TRANSPARENCY 0x10000
#define V5_PLAYER_HITMARKS 0x20000
#define V5_PLAYER_FREEZE 0x40000
#define V5_PLAYER_HEADBARS 0x80000

/* Npc extended-info flags, revision 241 (RSProx NpcInfoClient). A different set
 * from the player's, as at 239. 0x1 and 0x200000 are RSProx's UNUSED_FLAGS. */
#define V5_NPC_UNUSED_1 0x1
#define V5_NPC_TRANSFORMATION 0x2
#define V5_NPC_SPOTANIM 0x4
#define V5_NPC_SEQUENCE 0x8
#define V5_NPC_EXT_SHORT 0x10
#define V5_NPC_FACING 0x20
#define V5_NPC_SAY 0x40
#define V5_NPC_FREEZE 0x80
#define V5_NPC_OPS 0x100
#define V5_NPC_EXACT_MOVE 0x200
#define V5_NPC_NAME_CHANGE 0x400
#define V5_NPC_LEVEL_CHANGE 0x800
#define V5_NPC_BODY_CUSTOMISATION_V4 0x1000
#define V5_NPC_EXT_MEDIUM 0x2000
#define V5_NPC_TINTING 0x4000
#define V5_NPC_TRANSPARENCY 0x8000
#define V5_NPC_HITMARKS 0x10000
#define V5_NPC_EXT_INT 0x40000
#define V5_NPC_HEAD_CUSTOMISATION 0x80000
#define V5_NPC_BODY_CUSTOMISATION_LEGACY 0x100000 /* RSProx BODY_CUSTOMISATION_V2 */
#define V5_NPC_UNUSED_200000 0x200000
#define V5_NPC_BAS_CHANGE 0x400000
#define V5_NPC_HEADICON_CUSTOMISATION 0x800000
#define V5_NPC_BODY_CUSTOMISATION 0x1000000 /* RSProx BODY_CUSTOMISATION_V3 */
#define V5_NPC_HEADBARS 0x2000000

/* ------------------------------------------------------------------ */
/* Player-stream state                                                 */
/* ------------------------------------------------------------------ */

struct V5PlayerState
{
    int local_index;
    int seeded;
    /* Non-zero while the slot is in high resolution — the reference's
     * `cachedPlayers[idx] != null`. */
    uint8_t high_res[V5_PLAYER_SLOTS];
    uint8_t flags[V5_PLAYER_SLOTS];
    /* Absolute CoordGrid, (level << 28) | (x << 14) | z. */
    int32_t coord[V5_PLAYER_SLOTS];
    int32_t low_res_pos[V5_PLAYER_SLOTS];
    /* class95.field1344 in the authoritative client: the persistent
     * traversal used when a moving update has no temporary override. */
    int8_t move_speed[V5_PLAYER_SLOTS];
};

static struct V5PlayerState g_player;

/*
 * The GPI init block, which rides at the FRONT of the login REBUILD.
 *
 * 30 bits of absolute coordinate for the local player, then 18 bits of rough
 * position for each of the other 2047 slots in index order — the local slot is
 * skipped, so there are 2046 of them and the count is structural rather than
 * stated. This is also the only reset point the stream has: a re-login without
 * it leaves the previous session's high-resolution set in place, and every
 * packet after it reads a section for players who are not there.
 */
/*
 * The local player's slot, from the login response.
 *
 * Stated before the init block arrives because the block's own layout depends
 * on it — the local index is the one slot the 2046 rough positions skip — and
 * because there is no UPDATE_PID at this revision to state it later.
 */
void
osrs241_playerinfo_set_local(int local_index)
{
    memset(&g_player, 0, sizeof(g_player));
    g_player.local_index = local_index;
}

/*
 * The table above, parked and restored by a host that decodes SEVERAL players'
 * streams in one process (the server's in-process script runner,
 * scriptrun/scriptrun_core.c): each bot's table is loaded before its packets and
 * saved after, so one bot's high-resolution set never reads another's stream.
 * A client decodes one stream and never calls these.
 */
size_t
osrs241_playerinfo_state_size(void)
{
    return sizeof(g_player);
}

void
osrs241_playerinfo_state_save(void* out)
{
    assert(out);
    memcpy(out, &g_player, sizeof(g_player));
}

void
osrs241_playerinfo_state_load(void const* in)
{
    assert(in);
    memcpy(&g_player, in, sizeof(g_player));
}

void
osrs241_playerinfo_init(
    uint8_t const* data,
    int len)
{
    struct Net_BitBuffer buf;
    int local_index = g_player.local_index;

    memset(&g_player, 0, sizeof(g_player));
    memset(
        g_player.move_speed,
        PKT_PLAYER_TRAVERSAL_WALK,
        sizeof(g_player.move_speed));
    g_player.local_index = local_index;
    g_player.seeded = 1;
    if( local_index < 0 || local_index >= V5_PLAYER_SLOTS || !data || len <= 0 )
        return;

    Net_BitBufferInit(&buf, data, len);
    g_player.coord[local_index] = (int32_t)Net_BitBufferGbits(&buf, 30);
    g_player.high_res[local_index] = 1;
    for( int idx = 1; idx < V5_PLAYER_SLOTS; idx++ )
    {
        int packed;
        int level;
        int rough_x;
        int rough_z;

        if( idx == local_index )
            continue;
        packed = Net_BitBufferGbits(&buf, 18);
        level = packed >> 16;
        rough_x = (packed >> 8) & 0xff;
        rough_z = packed & 0xff;
        g_player.low_res_pos[idx] = (int32_t)((rough_x << 14) + rough_z + (level << 28));
    }
}

/* ------------------------------------------------------------------ */
/* Shared bit helpers                                                  */
/* ------------------------------------------------------------------ */

/** `readStationary`: a 2-bit width selector, then that many bits of run. */
static int
read_stationary(struct Net_BitBuffer* buf)
{
    switch( Net_BitBufferGbits(buf, 2) )
    {
    case 0: return 0;
    case 1: return Net_BitBufferGbits(buf, 5);
    case 2: return Net_BitBufferGbits(buf, 8);
    default: return Net_BitBufferGbits(buf, 11);
    }
}

/* The 8 walk and 16 run directions, as (dx, dz) pairs. v5 states movement as a
 * direction CODE, and the classic stream states it as a path step; the two are
 * only the same thing for the 8 walk directions, so both are converted to a
 * coordinate here and the coordinate is what the op carries. */
static const int8_t k_walk_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int8_t k_walk_dz[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
static const int8_t k_run_dx[16] = { -2, -1, 0, 1, 2, -2, 2, -2, 2, -2, 2, -2, -1, 0, 1, 2 };
static const int8_t k_run_dz[16] = { -2, -2, -2, -2, -2, -1, -1, 0, 0, 1, 1, 2, 2, 2, 2, 2 };

/* ------------------------------------------------------------------ */
/* Player stream                                                       */
/* ------------------------------------------------------------------ */

struct V5PlayerReader
{
    uint8_t cycle_high[V5_PLAYER_SLOTS];
    struct PktPlayerInfoOp* ops;
    int cap;
    int count;
    int extended[V5_PLAYER_SLOTS];
    int extended_count;
    /* Index of this cycle's ABS_XZLEVEL op for each player. Extended-info is
     * decoded after all bit sections, so the traversal block patches the
     * already-staged coordinate op exactly as class109.field1570 does. */
    int movement_op[V5_PLAYER_SLOTS];
};

static struct PktPlayerInfoOp*
player_op(struct V5PlayerReader* r, enum PktPlayerInfoOpKind kind)
{
    struct PktPlayerInfoOp* op;

    if( r->count >= r->cap )
        return NULL;
    op = &r->ops[r->count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    return op;
}

/** Name whose record follows. The local player has its own op because the
 * executor resolves it through `esync.local_pid` rather than by index. */
static void
player_target(struct V5PlayerReader* r, int idx)
{
    struct PktPlayerInfoOp* op;

    if( idx == g_player.local_index )
    {
        player_op(r, PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER);
        return;
    }
    /* The index IS the pid at this revision: v5 addresses players by their slot
     * in the client's own 1..2047 table, and there is no separate id space to
     * translate through. */
    op = player_op(r, PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID);
    if( op )
        op->_bitvalue = (uint64_t)idx;
}

/** Emit the slot's new absolute coordinate. The executor subtracts the scene
 * origin; this layer does not know it. */
static void
player_move_to(struct V5PlayerReader* r, int idx, int jump)
{
    int op_index = r->count;
    struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_ABS_XZLEVEL);
    int32_t coord = g_player.coord[idx];

    if( !op )
        return;
    op->_local_xz_level.x = (int16_t)((coord >> 14) & 0x3fff);
    op->_local_xz_level.z = (int16_t)(coord & 0x3fff);
    op->_local_xz_level.level = (uint8_t)((coord >> 28) & 0x3);
    op->_local_xz_level.jump = jump ? true : false;
    op->_local_xz_level.has_move_speed = true;
    op->_local_xz_level.move_speed = g_player.move_speed[idx];
    r->movement_op[idx] = op_index;
}

static void
player_set_move_speed(
    struct V5PlayerReader* r,
    int idx,
    int speed,
    int persistent)
{
    int op_index;

    if( persistent )
        g_player.move_speed[idx] = (int8_t)speed;
    op_index = r->movement_op[idx];
    if( op_index < 0 || op_index >= r->count )
        return;

    /* Temporary traversal is authoritative even for position opcode3:
     * class174.field2477 (127) snaps, while explicit crawl/walk/run queues
     * locomotion. Opcode3 describes the coordinate width, not a forced jump. */
    r->ops[op_index]._local_xz_level.has_move_speed = true;
    r->ops[op_index]._local_xz_level.move_speed = (int8_t)speed;
    if( !persistent || speed == PKT_PLAYER_TRAVERSAL_SNAP )
        r->ops[op_index]._local_xz_level.jump = speed == PKT_PLAYER_TRAVERSAL_SNAP;
}

static void
player_queue_extended(struct V5PlayerReader* r, int idx)
{
    if( r->extended_count < V5_PLAYER_SLOTS )
        r->extended[r->extended_count++] = idx;
}

/* `getLowResolutionPlayerPosition`. Returns 1 when the slot was promoted to
 * high resolution this tick (which is also what sets its next-cycle bit). */
static int
player_low_res(
    struct V5PlayerReader* r,
    struct Net_BitBuffer* buf,
    int idx)
{
    int opcode = Net_BitBufferGbits(buf, 2);

    if( opcode == 0 )
    {
        int fine_x;
        int fine_z;
        int extended;
        int32_t rough;
        int level;

        /* The recursion is the wire's, not a convenience: a promotion may be
         * preceded by one more low-resolution update for the same slot. */
        if( Net_BitBufferGbits(buf, 1) != 0 )
            player_low_res(r, buf, idx);
        rough = g_player.low_res_pos[idx];
        fine_x = Net_BitBufferGbits(buf, 13);
        fine_z = Net_BitBufferGbits(buf, 13);
        extended = Net_BitBufferGbits(buf, 1);
        level = (rough >> 28) & 0x3;
        g_player.coord[idx] =
            (int32_t)((level << 28) | (((((rough >> 14) & 0xff) << 13) + fine_x) << 14) |
                      ((((rough & 0xff) << 13) + fine_z) & 0x3fff));
        g_player.high_res[idx] = 1;
        player_target(r, idx);
        player_move_to(r, idx, 1);
        if( extended )
            player_queue_extended(r, idx);
        return 1;
    }
    if( opcode == 1 )
    {
        int level_delta = Net_BitBufferGbits(buf, 2);
        int32_t rough = g_player.low_res_pos[idx];

        g_player.low_res_pos[idx] =
            (int32_t)(((((rough >> 28) + level_delta) & 3) << 28) + (rough & 0x0fffffff));
        return 0;
    }
    if( opcode == 2 )
    {
        int packed = Net_BitBufferGbits(buf, 5);
        int level_delta = packed >> 3;
        int move = packed & 7;
        int32_t rough = g_player.low_res_pos[idx];
        int level = ((rough >> 28) + level_delta) & 3;
        int rough_x = (rough >> 14) & 0xff;
        int rough_z = rough & 0xff;

        rough_x += k_walk_dx[move];
        rough_z += k_walk_dz[move];
        g_player.low_res_pos[idx] = (int32_t)((rough_x << 14) + rough_z + (level << 28));
        return 0;
    }
    {
        int packed = Net_BitBufferGbits(buf, 18);
        int level_delta = packed >> 16;
        int dx = (packed >> 8) & 0xff;
        int dz = packed & 0xff;
        int32_t rough = g_player.low_res_pos[idx];
        int level = ((rough >> 28) + level_delta) & 3;
        int rough_x = (dx + (rough >> 14)) & 0xff;
        int rough_z = (dz + rough) & 0xff;

        g_player.low_res_pos[idx] = (int32_t)((rough_x << 14) + rough_z + (level << 28));
        return 0;
    }
}

/* `getHighResolutionPlayerPosition`. */
static void
player_high_res(
    struct V5PlayerReader* r,
    struct Net_BitBuffer* buf,
    int idx)
{
    int extended = Net_BitBufferGbits(buf, 1);
    int opcode;
    int32_t coord = g_player.coord[idx];
    int level = (coord >> 28) & 0x3;
    int cur_x = (coord >> 14) & 0x3fff;
    int cur_z = coord & 0x3fff;

    if( extended )
        player_queue_extended(r, idx);
    opcode = Net_BitBufferGbits(buf, 2);

    if( opcode == 0 )
    {
        if( extended )
        {
            /* Stood still and has something to say about it. Naming the target
             * is still required — the extended block that follows is keyed on
             * the queue, not on anything in the block itself. */
            player_target(r, idx);
            return;
        }
        if( idx == g_player.local_index )
        {
            /* The reference throws here, and so does a real client: opcode 0
             * with no extended info means "drop to low resolution", which the
             * local player cannot do. */
            TORIRS_LOG("osrs241: PLAYER_INFO dropped the local index to low res\n");
            return;
        }
        g_player.low_res_pos[idx] =
            (int32_t)((level << 28) | (cur_z >> 13) | ((cur_x >> 13) << 14));
        g_player.high_res[idx] = 0;
        g_player.move_speed[idx] = PKT_PLAYER_TRAVERSAL_WALK;
        {
            struct PktPlayerInfoOp* op =
                player_op(r, PKT_PLAYER_INFO_OP_REMOVE_PLAYER_PID);

            if( op )
                op->_bitvalue = (uint64_t)idx;
        }
        if( Net_BitBufferGbits(buf, 1) != 0 )
            player_low_res(r, buf, idx);
        return;
    }

    if( opcode == 1 )
    {
        int move = Net_BitBufferGbits(buf, 3);
        cur_x += k_walk_dx[move];
        cur_z += k_walk_dz[move];
    }
    else if( opcode == 2 )
    {
        int move = Net_BitBufferGbits(buf, 4);
        cur_x += k_run_dx[move];
        cur_z += k_run_dz[move];
    }
    else
    {
        /* Teleport: a near form carrying a 12-bit signed delta and a far form
         * carrying a 30-bit one. Both are DELTAS against the client's own copy,
         * which is the trap the server side of this already records — an
         * absolute coordinate here moves the player by their whole world
         * position every tick. */
        if( Net_BitBufferGbits(buf, 1) == 0 )
        {
            int packed = Net_BitBufferGbits(buf, 12);
            int dlevel = packed >> 10;
            int dx = (packed >> 5) & 31;
            int dz = packed & 31;

            if( dx > 15 )
                dx -= 32;
            if( dz > 15 )
                dz -= 32;
            cur_x += dx;
            cur_z += dz;
            level = (level + dlevel) & 0x3;
        }
        else
        {
            int packed = Net_BitBufferGbits(buf, 30);
            int dlevel = (packed >> 28) & 0x3;
            int dx = (packed >> 14) & 0x3fff;
            int dz = packed & 0x3fff;

            cur_x = (cur_x + dx) & 0x3fff;
            cur_z = (cur_z + dz) & 0x3fff;
            level = (level + dlevel) & 0x3;
        }
    }

    g_player.coord[idx] = (int32_t)((level << 28) | ((cur_x & 0x3fff) << 14) | (cur_z & 0x3fff));
    player_target(r, idx);
    /* opcode 3 is a jump (the client re-centres its scene on one); 1 and 2 are
     * ordinary steps the world interpolates. */
    player_move_to(r, idx, opcode == 3);
}

/*
 * One of the four bit sections. `want_inactive` selects which cycle bit a slot
 * must carry to be described here, and `low_res` which table is walked.
 *
 * An empty section emits ZERO bytes — entering and leaving bit mode round the
 * same byte cursor to itself — so the four sections are not four markers on the
 * wire, and there are no separators to look for.
 */
static void
player_section(
    struct V5PlayerReader* r,
    uint8_t const* data,
    int len,
    int* byte_pos,
    int want_inactive,
    int low_res)
{
    struct Net_BitBuffer buf;
    int skipped = 0;

    Net_BitBufferInit(&buf, data + *byte_pos, len - *byte_pos);
    for( int idx = 1; idx < V5_PLAYER_SLOTS; idx++ )
    {
        int is_high = r->cycle_high[idx] != 0;
        int inactive = (g_player.flags[idx] & V5_CUR_CYCLE_INACTIVE) != 0;

        if( low_res == is_high )
            continue;
        if( inactive != want_inactive )
            continue;
        if( skipped > 0 )
        {
            --skipped;
            g_player.flags[idx] |= V5_NEXT_CYCLE_INACTIVE;
            continue;
        }
        if( Net_BitBufferGbits(&buf, 1) == 0 )
        {
            skipped = read_stationary(&buf);
            g_player.flags[idx] |= V5_NEXT_CYCLE_INACTIVE;
            continue;
        }
        if( low_res )
        {
            if( player_low_res(r, &buf, idx) )
                g_player.flags[idx] |= V5_NEXT_CYCLE_INACTIVE;
        }
        else
        {
            player_high_res(r, &buf, idx);
        }
    }
    *byte_pos += Net_BitBufferBytePos(&buf);
}

/* The appearance block at 241 (RSProx PlayerInfoClient, flag APPEARANCE):
 * `g1Alt2` length (`-n`), then the body as `gdataAlt1` -- the bytes in REVERSE
 * order, untransformed. 239 was `g1Alt3` length and `gdataAlt2` (every byte
 * +128, in order). Both are obfuscation rather than structure, and both are
 * load-bearing. */
static int
player_appearance_block(
    struct V5PlayerReader* r,
    uint8_t const* data,
    int len,
    int* pos)
{
    int block_len;
    uint8_t* copy;

    if( *pos >= len )
        return 0;
    block_len = (-data[(*pos)++]) & 0xff;
    if( *pos + block_len > len )
        return 0;
    copy = (uint8_t*)ToriMem_Alloc(ToriMem_Net(), (size_t)(block_len > 0 ? block_len : 1));
    assert(copy);
    for( int i = 0; i < block_len; i++ )
        copy[block_len - 1 - i] = data[*pos + i];
    *pos += block_len;
    {
        struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_APPEARANCE);

        if( !op )
        {
            ToriMem_Free(ToriMem_Net(), copy);
            return 0;
        }
        op->_appearance.appearance = copy;
        op->_appearance.len = block_len;
    }
    return 1;
}

/** pSmart1or2, on the byte-aligned tail. */
static int
tail_smart(uint8_t const* data, int len, int* pos)
{
    int value;

    if( *pos >= len )
        return 0;
    value = data[(*pos)++];
    if( value < 0x80 )
        return value;
    if( *pos >= len )
        return 0;
    return ((value << 8) | data[(*pos)++]) & 0x7fff;
}

/* JagByteBuf.gSmart2or4null, used by Face.Entity's target index. */
static int
tail_smart2or4null(uint8_t const* data, int len, int* pos)
{
    int value;

    if( *pos >= len )
        return -1;
    if( (data[*pos] & 0x80) != 0 )
    {
        if( *pos + 4 > len )
        {
            *pos = len;
            return -1;
        }
        value = ((data[*pos] & 0x7f) << 24) | (data[*pos + 1] << 16) |
                (data[*pos + 2] << 8) | data[*pos + 3];
        *pos += 4;
        return value;
    }
    if( *pos + 2 > len )
    {
        *pos = len;
        return -1;
    }
    value = (data[*pos] << 8) | data[*pos + 1];
    *pos += 2;
    return value == 32767 ? -1 : value;
}

static int
tail_smart1or2null(uint8_t const* data, int len, int* pos)
{
    int value = tail_smart(data, len, pos);

    return value - 1;
}

static int
tail_g1(uint8_t const* data, int len, int* pos)
{
    return *pos < len ? data[(*pos)++] : 0;
}

static int
tail_g1_alt1(uint8_t const* data, int len, int* pos)
{
    return (tail_g1(data, len, pos) - 128) & 0xff;
}

static int
tail_g1_alt2(uint8_t const* data, int len, int* pos)
{
    return (-tail_g1(data, len, pos)) & 0xff;
}

static int
tail_g1_alt3(uint8_t const* data, int len, int* pos)
{
    return (128 - tail_g1(data, len, pos)) & 0xff;
}

static int
tail_g2(uint8_t const* data, int len, int* pos)
{
    int hi = tail_g1(data, len, pos);
    int lo = tail_g1(data, len, pos);

    return (hi << 8) | lo;
}

static int
tail_g2_alt1(uint8_t const* data, int len, int* pos)
{
    int lo = tail_g1(data, len, pos);
    int hi = tail_g1(data, len, pos);

    return (hi << 8) | lo;
}

static int
tail_g2_alt2(uint8_t const* data, int len, int* pos)
{
    int hi = tail_g1(data, len, pos);
    int lo = (tail_g1(data, len, pos) - 128) & 0xff;

    return (hi << 8) | lo;
}

static int
tail_g2_alt3(uint8_t const* data, int len, int* pos)
{
    int lo = (tail_g1(data, len, pos) - 128) & 0xff;
    int hi = tail_g1(data, len, pos);

    return (hi << 8) | lo;
}

static uint32_t
tail_g4(uint8_t const* data, int len, int* pos)
{
    uint32_t b0 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b1 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b2 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b3 = (uint32_t)tail_g1(data, len, pos);

    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

static uint32_t
tail_g4_alt1(uint8_t const* data, int len, int* pos)
{
    uint32_t b0 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b1 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b2 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b3 = (uint32_t)tail_g1(data, len, pos);

    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static char*
tail_string(uint8_t const* data, int len, int* pos)
{
    int start = *pos;
    char* text;

    while( *pos < len && data[*pos] != 0 )
        (*pos)++;
    text = (char*)ToriMem_Alloc(ToriMem_Net(), (size_t)(*pos - start) + 1);
    assert(text);
    memcpy(text, data + start, (size_t)(*pos - start));
    text[*pos - start] = '\0';
    if( *pos < len )
        (*pos)++;
    return text;
}

/* Signed views of the transformed single bytes (RSProt g1sAlt1..3). */
static int
tail_g1s_alt1(uint8_t const* data, int len, int* pos)
{
    return (int)(int8_t)tail_g1_alt1(data, len, pos);
}

static int
tail_g1s_alt2(uint8_t const* data, int len, int* pos)
{
    return (int)(int8_t)tail_g1_alt2(data, len, pos);
}

static int
tail_g1s_alt3(uint8_t const* data, int len, int* pos)
{
    return (int)(int8_t)tail_g1_alt3(data, len, pos);
}

/* One facing block, shared by the player and npc tails: they differ only in
 * the header byte's transform, which the caller has already applied. */
static void
player_face_block(
    struct V5PlayerReader* r,
    uint8_t const* data,
    int len,
    int* pos,
    int header)
{
    int kind = (header >> 3) & 0x7;
    bool instant = ((header >> 6) & 1) != 0;
    int movement_mode = header & 0x7;

    if( kind == 0 ) /* Entity */
    {
        int type = tail_smart(data, len, pos);
        int index = tail_smart2or4null(data, len, pos);
        int fallback_angle = tail_smart(data, len, pos);
        struct PktPlayerInfoOp* op;

        if( type == 3 )
        {
            op = player_op(r, PKT_PLAYER_INFO_OP_FACE_ANGLE);
            if( op )
            {
                /* No arbitrary-world-entity registry: the gamepack's
                 * missing-target path applies the fallback angle. */
                op->_face_angle.angle = (uint16_t)fallback_angle;
                op->_face_angle.instant = instant;
                op->_face_angle.movement_mode = (uint8_t)movement_mode;
                op->_face_angle.modern = true;
            }
        }
        else
        {
            op = player_op(r, PKT_PLAYER_INFO_OP_FACE_ENTITY);
            if( op )
            {
                op->_face_entity.entity_id =
                    type == 1 ? index : type == 2 && index >= 0 ? 32768 + index : -1;
                op->_face_entity.fallback_angle = (uint16_t)fallback_angle;
                op->_face_entity.has_fallback_angle = true;
                op->_face_entity.instant = instant;
                op->_face_entity.movement_mode = (uint8_t)movement_mode;
                op->_face_entity.modern = true;
            }
        }
    }
    else if( kind == 1 ) /* Loc */
    {
        int x = tail_smart(data, len, pos);
        int z = tail_smart(data, len, pos);
        int size = tail_smart(data, len, pos);
        struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_FACE_COORD);

        if( op )
        {
            op->_face_coord.x = (int16_t)(x * 2 + (size & 0xf));
            op->_face_coord.z = (int16_t)(z * 2 + ((size >> 4) & 0xf));
            op->_face_coord.instant = instant;
            op->_face_coord.movement_mode = (uint8_t)movement_mode;
            op->_face_coord.modern = true;
        }
    }
    else if( kind == 2 ) /* Angle */
    {
        int angle = tail_smart(data, len, pos);
        struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_FACE_ANGLE);

        if( op )
        {
            op->_face_angle.angle = (uint16_t)angle;
            op->_face_angle.instant = instant;
            op->_face_angle.movement_mode = (uint8_t)movement_mode;
            op->_face_angle.modern = true;
        }
    }
    else if( kind == 3 ) /* Reset */
    {
        struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_FACE_ENTITY);

        if( op )
        {
            op->_face_entity.entity_id = -1;
            op->_face_entity.movement_mode = (uint8_t)movement_mode;
            op->_face_entity.modern = true;
        }
    }
}

/*
 * The extended-info tail, byte-aligned, in the order the indices were flagged.
 *
 * Blocks are read in RSProx 241's decodeExtendedInfoBlocks order:
 *   SAY, MOVE_SPEED, FACE, NAME_EXTRAS, SPOTANIM, HEADBARS, APPEARANCE,
 *   TEMP_MOVE_SPEED, CHAT, HITMARKS, TINTING, SEQUENCE, EXACT_MOVE,
 *   TRANSPARENCY, RESET, FREEZE
 * Nothing on the wire separates them, so a block read out of place is read as
 * the next one -- which is why every block names its 241 accessors.
 *
 * A flag this reader has no decoder for ENDS the tail rather than skipping it:
 * the block's length is not on the wire, so there is no way to step over one
 * and stay aligned.
 */
static void
player_extended(
    struct V5PlayerReader* r,
    uint8_t const* data,
    int len,
    int pos)
{
    for( int i = 0; i < r->extended_count && pos < len; i++ )
    {
        int idx = r->extended[i];
        int flag = data[pos++];
        int known;

        if( (flag & V5_PLAYER_EXT_SHORT) != 0 && pos < len )
            flag |= data[pos++] << 8;
        if( (flag & V5_PLAYER_EXT_MEDIUM) != 0 && pos < len )
            flag |= data[pos++] << 16;

        if( torirs_env_net_debug() )
            TORIRS_LOG("osrs241: player %d extended flag=0x%x at %d/%d\n", idx, flag,
                    pos, len);

        known = V5_PLAYER_EXT_SHORT | V5_PLAYER_EXT_MEDIUM | V5_PLAYER_APPEARANCE |
                V5_PLAYER_SPOTANIM | V5_PLAYER_RESET | V5_PLAYER_SAY | V5_PLAYER_FACE |
                V5_PLAYER_SEQUENCE | V5_PLAYER_MOVE_SPEED | V5_PLAYER_CHAT |
                V5_PLAYER_EXACT_MOVE | V5_PLAYER_TINTING | V5_PLAYER_TEMP_MOVE_SPEED |
                V5_PLAYER_MINIMENU | V5_PLAYER_TRANSPARENCY | V5_PLAYER_HITMARKS |
                V5_PLAYER_FREEZE | V5_PLAYER_HEADBARS;
        if( (flag & ~known) != 0 )
        {
            /* Checked BEFORE the blocks at 241: RSProx throws on its unused
             * flag, and a block with no known layout cannot be stepped over. */
            TORIRS_LOG("osrs241: PLAYER_INFO extended flag 0x%x has undecoded blocks "
                "(0x%x); tail stopped\n",
                flag,
                flag & ~known);
            return;
        }

        {
            struct PktPlayerInfoOp* op =
                player_op(r, idx == g_player.local_index
                                 ? PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER
                                 : PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID);

            if( op && idx != g_player.local_index )
                op->_bitvalue = (uint64_t)idx;
        }

        /* decodeSay: gjstr. */
        if( (flag & V5_PLAYER_SAY) != 0 )
        {
            char* text = tail_string(data, len, &pos);
            struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_SAY);

            if( op )
                op->_say.text = text;
            else
                ToriMem_Free(ToriMem_Net(), text);
        }
        /* decodeMoveSpeed: g1sAlt1. Persistent, and also this cycle's traversal
         * when the player moved (class109.method3804's field1570 update). */
        if( (flag & V5_PLAYER_MOVE_SPEED) != 0 )
        {
            int speed = pos < len ? tail_g1s_alt1(data, len, &pos) : PKT_PLAYER_TRAVERSAL_WALK;

            player_set_move_speed(r, idx, speed, 1);
        }
        /* decodeFacing: g1Alt3 header, then the kind's smarts. */
        if( (flag & V5_PLAYER_FACE) != 0 )
            player_face_block(r, data, len, &pos, tail_g1_alt3(data, len, &pos));
        /* decodeNameExtras: three NUL-ended strings, global UI state in the
         * reference; structural here. */
        if( (flag & V5_PLAYER_MINIMENU) != 0 )
        {
            for( int option = 0; option < 3; option++ )
            {
                char* ignored = tail_string(data, len, &pos);
                ToriMem_Free(ToriMem_Net(), ignored);
            }
        }
        /* decodeSpotanims: g1Alt2 count, per entry g1Alt2 slot, g2Alt2 id,
         * g4 (height << 16 | delay), g1 loop -- the loop byte is new at 241. */
        if( (flag & V5_PLAYER_SPOTANIM) != 0 )
        {
            int entries = tail_g1_alt2(data, len, &pos);

            for( int e = 0; e < entries; e++ )
            {
                struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_SPOTANIM);
                int slot = tail_g1_alt2(data, len, &pos);
                int spot = tail_g2_alt2(data, len, &pos);
                uint32_t height_delay = tail_g4(data, len, &pos);
                int loop = tail_g1(data, len, &pos);

                if( op )
                {
                    op->_spotanim.slot = (uint8_t)slot;
                    op->_spotanim.spotanim_id = spot == 65535 ? -1 : spot;
                    op->_spotanim.height_delay = (int32_t)height_delay;
                    op->_spotanim.loop = loop == 1;
                }
            }
        }
        /* decodeHeadbar: g1 count, per bar smart type, smart end time and, unless
         * it is the 0x7FFF removal, smart start time, g1Alt3 start fill and --
         * when the bar has a duration -- g1Alt1 end fill. */
        if( (flag & V5_PLAYER_HEADBARS) != 0 )
        {
            int bars = tail_g1(data, len, &pos);

            for( int b = 0; b < bars; b++ )
            {
                int type = tail_smart(data, len, &pos);
                int duration = tail_smart(data, len, &pos);
                struct PktPlayerInfoOp* op = NULL;

                if( b == 0 )
                    op = player_op(r, PKT_PLAYER_INFO_OP_HEADBAR);
                if( duration == 32767 )
                {
                    if( op )
                    {
                        op->_headbar.type = type;
                        op->_headbar.remove = true;
                    }
                }
                else
                {
                    int start_delay = tail_smart(data, len, &pos);
                    int start_fill = tail_g1_alt3(data, len, &pos);
                    int end_fill = duration > 0 ? tail_g1_alt1(data, len, &pos) : start_fill;

                    if( op )
                    {
                        op->_headbar.type = type;
                        op->_headbar.duration = duration;
                        op->_headbar.start_delay = start_delay;
                        op->_headbar.start_fill = (uint8_t)start_fill;
                        op->_headbar.end_fill = (uint8_t)end_fill;
                        op->_headbar.remove = false;
                    }
                }
            }
        }
        if( (flag & V5_PLAYER_APPEARANCE) != 0 )
        {
            if( !player_appearance_block(r, data, len, &pos) )
                return;
        }
        /* decodeTemporaryMoveSpeed: g1sAlt1, this cycle only. */
        if( (flag & V5_PLAYER_TEMP_MOVE_SPEED) != 0 )
        {
            int speed = pos < len ? tail_g1s_alt1(data, len, &pos) : PKT_PLAYER_TRAVERSAL_WALK;

            player_set_move_speed(r, idx, speed, 0);
        }
        /* decodeChat: g2Alt1 colour/effects, g1Alt1 mod icon, g1Alt2 autotyper,
         * g1Alt1 huffman length, gdataAlt1 (reversed) payload, then g1Alt3 per
         * pattern colour. */
        if( (flag & V5_PLAYER_CHAT) != 0 )
        {
            int colour_effect = tail_g2_alt1(data, len, &pos);
            int type = tail_g1_alt1(data, len, &pos);
            int payload_len;
            uint8_t* payload = NULL;
            int pattern_len = 0;

            (void)tail_g1_alt2(data, len, &pos); /* auto-typed */
            payload_len = tail_g1_alt1(data, len, &pos);
            if( payload_len > 0 && pos + payload_len <= len )
            {
                payload = (uint8_t*)ToriMem_Alloc(ToriMem_Net(), (size_t)payload_len);
                assert(payload);
                for( int b = 0; b < payload_len; b++ )
                    payload[payload_len - b - 1] = data[pos + b];
            }
            pos += payload_len;
            if( pos > len )
                pos = len;
            if( (colour_effect >> 8) >= 13 && (colour_effect >> 8) <= 20 )
                pattern_len = (colour_effect >> 8) - 12;
            for( int p = 0; p < pattern_len; p++ )
                (void)tail_g1_alt3(data, len, &pos);
            {
                struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_CHAT);

                if( op )
                {
                    op->_chat.colour_effect = (uint16_t)colour_effect;
                    op->_chat.type = (uint8_t)type;
                    op->_chat.length = (uint8_t)payload_len;
                    op->_chat.data = payload;
                }
                else
                    ToriMem_Free(ToriMem_Net(), payload);
            }
        }
        /* decodeHitmark: g1 count (239: g1Alt3), then per hit four smarts. */
        if( (flag & V5_PLAYER_HITMARKS) != 0 )
        {
            int hits = tail_g1(data, len, &pos);

            for( int h = 0; h < hits && pos < len; h++ )
            {
                int type = tail_smart(data, len, &pos);
                int value = tail_smart(data, len, &pos);
                int delay = tail_smart(data, len, &pos);
                int slots = tail_smart(data, len, &pos);
                struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_DAMAGE);

                if( op )
                {
                    op->_damage.damage_type = type;
                    op->_damage.damage = value;
                    op->_damage.delay = (uint16_t)delay;
                    op->_damage.slots = (uint16_t)slots;
                }
            }
        }
        /* decodeTinting: g2 start, g2Alt1 end, g1sAlt2 hue, g1s saturation,
         * g1sAlt2 lightness, g1Alt3 weight. */
        if( (flag & V5_PLAYER_TINTING) != 0 )
        {
            int start = tail_g2(data, len, &pos);
            int end = tail_g2_alt1(data, len, &pos);
            int hue = tail_g1s_alt2(data, len, &pos);
            int saturation = (int)(int8_t)tail_g1(data, len, &pos);
            int lightness = tail_g1s_alt2(data, len, &pos);
            int weight = tail_g1_alt3(data, len, &pos);
            struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_TINT);

            if( op )
            {
                op->_tint.start_cycle_delta = (uint16_t)start;
                op->_tint.end_cycle_delta = (uint16_t)end;
                op->_tint.hue = (int8_t)hue;
                op->_tint.saturation = (int8_t)saturation;
                op->_tint.lightness = (int8_t)lightness;
                op->_tint.weight = (int8_t)weight;
            }
        }
        /* decodeSequence: g2Alt2 id, g1Alt1 delay. */
        if( (flag & V5_PLAYER_SEQUENCE) != 0 )
        {
            int seq = tail_g2_alt2(data, len, &pos);
            int delay = tail_g1_alt1(data, len, &pos);
            struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_SEQUENCE);

            if( op )
            {
                op->_sequence.sequence_id = seq == 65535 ? -1 : seq;
                op->_sequence.delay = (uint8_t)delay;
            }
        }
        /* decodeExactMove: g1sAlt1 dx1, g1s dz1, g1sAlt1 dx2, g1sAlt3 dz2,
         * g2Alt1 delay1, g2Alt2 delay2, g2Alt2 direction. */
        if( (flag & V5_PLAYER_EXACT_MOVE) != 0 )
        {
            struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_EXACT_MOVE);
            int sx = tail_g1s_alt1(data, len, &pos);
            int sz = (int)(int8_t)tail_g1(data, len, &pos);
            int ex = tail_g1s_alt1(data, len, &pos);
            int ez = tail_g1s_alt3(data, len, &pos);
            int start = tail_g2_alt1(data, len, &pos);
            int end = tail_g2_alt2(data, len, &pos);
            int facing = tail_g2_alt2(data, len, &pos);

            if( op )
            {
                op->_exactmove.start_x = (int16_t)sx;
                op->_exactmove.start_z = (int16_t)sz;
                op->_exactmove.end_x = (int16_t)ex;
                op->_exactmove.end_z = (int16_t)ez;
                op->_exactmove.end_cycle_delta = (uint16_t)start;
                op->_exactmove.start_cycle_delta = (uint16_t)end;
                op->_exactmove.facing = (uint16_t)facing;
                op->_exactmove.facing_is_yaw = true;
                op->_exactmove.relative = true;
            }
            /* As at 239: this cycle's staged move is never applied
             * (class109.method3804 clears field1582 after method3132). */
            {
                int op_index = r->movement_op[idx];

                if( op_index >= 0 && op_index < r->count )
                    r->ops[op_index]._local_xz_level.route_only = true;
            }
        }
        /* decodeTransparency: g2sAlt3 start, g2sAlt3 end, g1sAlt1 start alpha,
         * g1sAlt1 end alpha, g1Alt3 use-start. */
        if( (flag & V5_PLAYER_TRANSPARENCY) != 0 )
        {
            (void)tail_g2_alt3(data, len, &pos);
            (void)tail_g2_alt3(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
            (void)tail_g1_alt3(data, len, &pos);
        }
        /* decodePlayerReset: g1. */
        if( (flag & V5_PLAYER_RESET) != 0 )
            (void)tail_g1(data, len, &pos);
        /* decodeFreeze: g2Alt1 delay, g2Alt1 duration, g1Alt3 cancel. */
        if( (flag & V5_PLAYER_FREEZE) != 0 )
        {
            (void)tail_g2_alt1(data, len, &pos);
            (void)tail_g2_alt1(data, len, &pos);
            (void)tail_g1_alt3(data, len, &pos);
        }
    }
}

int
osrs241_player_info_read(
    uint8_t const* data,
    int len,
    struct PktPlayerInfoOp* ops,
    int cap)
{
    struct V5PlayerReader r;
    int pos = 0;

    if( !g_player.seeded )
    {
        /* No init block means no high-resolution set and no coordinates: every
         * section below would walk the wrong slots. Dropping is the only
         * answer that cannot corrupt the table. */
        TORIRS_LOG("osrs241: PLAYER_INFO before the GPI init block; dropped\n");
        return 0;
    }

    memset(&r, 0, sizeof(r));
    memcpy(r.cycle_high, g_player.high_res, sizeof(r.cycle_high));
    for( int idx = 0; idx < V5_PLAYER_SLOTS; idx++ )
        r.movement_op[idx] = -1;
    r.ops = ops;
    r.cap = cap;

    /* Sections, in wire order: high-res active, high-res inactive, low-res
     * inactive, low-res active. The inactive/active sense INVERTS between the
     * high and low halves, which is not symmetry lost — it is what makes a
     * player skipped in one half describable in the other. */
    player_section(&r, data, len, &pos, 0, 0);
    player_section(&r, data, len, &pos, 1, 0);
    player_section(&r, data, len, &pos, 1, 1);
    player_section(&r, data, len, &pos, 0, 1);

    for( int idx = 1; idx < V5_PLAYER_SLOTS; idx++ )
        g_player.flags[idx] = (uint8_t)(g_player.flags[idx] >> 1);

    player_extended(&r, data, len, pos);
    return r.count;
}

/* ------------------------------------------------------------------ */
/* Npc stream                                                          */
/* ------------------------------------------------------------------ */

struct V5NpcReader
{
    struct PktNpcInfoOp* ops;
    int cap;
    int count;
    int extended[256];
    int extended_count;
};

static struct PktNpcInfoOp*
npc_op(struct V5NpcReader* r, enum PktNpcInfoOpKind kind)
{
    struct PktNpcInfoOp* op;

    if( r->count >= r->cap )
        return NULL;
    op = &r->ops[r->count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    return op;
}

static void
npc_queue_extended(struct V5NpcReader* r, int list_idx)
{
    if( r->extended_count < (int)(sizeof(r->extended) / sizeof(r->extended[0])) )
        r->extended[r->extended_count++] = list_idx;
}

/*
 * The npc's BODY_CUSTOMISATION (RSProx V3) and V4 blocks share a shape: a flag
 * byte whose 0x1 resets to the type's models and 0x2 carries a model list, then
 * colour, texture and player-composition lists. Only the model list has a home
 * in the op; the rest is consumed. `models_count_fn` / `model_fn` are the
 * per-block accessors.
 */
static void
npc_body_models(
    struct PktNpcInfoOp* op,
    int model)
{
    if( op && op->_body.model_count < PKT_NPC_BODY_CUSTOM_MODELS_MAX )
        op->_body.models[op->_body.model_count++] = model;
}

/*
 * The npc extended tail at 241 (RSProx NpcInfoClient.processExtendedInfo),
 * blocks in this order:
 *   SPOTANIM, FREEZE, EXACT_MOVE, BODY_CUSTOMISATION_V3, LEVEL_CHANGE,
 *   HEAD_CUSTOMISATION, HEADBARS, SEQUENCE, BODY_CUSTOMISATION_V2, TINTING,
 *   BODY_CUSTOMISATION_V4, FACING, HEADICON_CUSTOMISATION, SAY, TRANSPARENCY,
 *   NAME_CHANGE, OPS, TRANSFORMATION, HITMARKS, BAS_CHANGE
 * Same rule as the player's: an undecoded flag ends the tail.
 */
static void
npc_extended(
    struct V5NpcReader* r,
    uint8_t const* data,
    int len,
    int pos)
{
    for( int i = 0; i < r->extended_count && pos < len; i++ )
    {
        int flag = data[pos++];
        int known;

        if( (flag & V5_NPC_EXT_SHORT) != 0 && pos < len )
            flag |= data[pos++] << 8;
        if( (flag & V5_NPC_EXT_MEDIUM) != 0 && pos < len )
            flag |= data[pos++] << 16;
        if( (flag & V5_NPC_EXT_INT) != 0 && pos < len )
            flag |= data[pos++] << 24;

        known = V5_NPC_EXT_SHORT | V5_NPC_EXT_MEDIUM | V5_NPC_EXT_INT |
                V5_NPC_TRANSFORMATION | V5_NPC_SPOTANIM | V5_NPC_SEQUENCE | V5_NPC_FACING |
                V5_NPC_SAY | V5_NPC_FREEZE | V5_NPC_OPS | V5_NPC_EXACT_MOVE |
                V5_NPC_NAME_CHANGE | V5_NPC_LEVEL_CHANGE | V5_NPC_BODY_CUSTOMISATION_V4 |
                V5_NPC_TINTING | V5_NPC_TRANSPARENCY | V5_NPC_HITMARKS |
                V5_NPC_HEAD_CUSTOMISATION | V5_NPC_BODY_CUSTOMISATION_LEGACY |
                V5_NPC_BAS_CHANGE | V5_NPC_HEADICON_CUSTOMISATION |
                V5_NPC_BODY_CUSTOMISATION | V5_NPC_HEADBARS;
        if( (flag & ~known) != 0 )
        {
            TORIRS_LOG("osrs241: NPC_INFO extended flag 0x%x has undecoded blocks (0x%x); "
                "tail stopped\n",
                flag,
                flag & ~known);
            return;
        }

        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX);

            if( op )
                op->_bitvalue = (uint64_t)r->extended[i];
        }

        /* decodeSpotanim: g1Alt3 count, per entry g1Alt2 slot, g2Alt3 id,
         * g4Alt1 (height << 16 | delay), g1 loop (new at 241). */
        if( (flag & V5_NPC_SPOTANIM) != 0 )
        {
            int entries = tail_g1_alt3(data, len, &pos);

            for( int e = 0; e < entries && pos < len; e++ )
            {
                int slot = tail_g1_alt2(data, len, &pos);
                int spot = tail_g2_alt3(data, len, &pos);
                int height_delay = (int)tail_g4_alt1(data, len, &pos);
                int loop = tail_g1(data, len, &pos);
                struct PktNpcInfoOp* op;

                op = npc_op(r, PKT_NPC_INFO_OP_SPOTANIM);
                if( op )
                {
                    op->_spotanim.spotanim_id = spot == 65535 ? -1 : spot;
                    op->_spotanim.height_delay = height_delay;
                    op->_spotanim.slot = (uint8_t)slot;
                    op->_spotanim.loop = loop == 1;
                }
            }
        }
        /* decodeFreeze: g2Alt1 delay, g2 duration, g1Alt2 cancel. */
        if( (flag & V5_NPC_FREEZE) != 0 )
        {
            (void)tail_g2_alt1(data, len, &pos);
            (void)tail_g2(data, len, &pos);
            (void)tail_g1_alt2(data, len, &pos);
        }
        /* decodeExactMove: g1sAlt3 dx1, g1s dz1, g1sAlt1 dx2, g1sAlt1 dz2,
         * g2Alt3 delay1, g2Alt3 delay2, g2Alt3 direction. */
        if( (flag & V5_NPC_EXACT_MOVE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_EXACT_MOVE);
            int sx = tail_g1s_alt3(data, len, &pos);
            int sz = (int)(int8_t)tail_g1(data, len, &pos);
            int ex = tail_g1s_alt1(data, len, &pos);
            int ez = tail_g1s_alt1(data, len, &pos);
            int start = tail_g2_alt3(data, len, &pos);
            int end = tail_g2_alt3(data, len, &pos);
            int facing = tail_g2_alt3(data, len, &pos);

            if( op )
            {
                op->_exactmove.start_x = (int16_t)sx;
                op->_exactmove.start_z = (int16_t)sz;
                op->_exactmove.end_x = (int16_t)ex;
                op->_exactmove.end_z = (int16_t)ez;
                op->_exactmove.end_cycle_delta = (uint16_t)start;
                op->_exactmove.start_cycle_delta = (uint16_t)end;
                op->_exactmove.facing = (uint16_t)facing;
                op->_exactmove.facing_is_yaw = true;
                op->_exactmove.relative = true;
            }
        }
        /* decodeBodyCustomisationV3: g1Alt3 flag; 0x2 g1Alt1 count + g4Alt1
         * models; 0x4 g1Alt2 count + g2Alt3; 0x8 g1 count + g2Alt1; 0x20 g1Alt2
         * body type, g1Alt1 count + g2Alt2 ident kits. 0x10 is a bare bit. */
        if( (flag & V5_NPC_BODY_CUSTOMISATION) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_BODY_CUSTOM);
            int body = tail_g1_alt3(data, len, &pos);

            if( op )
                op->_body.reset = (body & 1) != 0 || (body & 2) == 0;
            if( (body & 1) == 0 )
            {
                if( (body & 2) != 0 )
                {
                    int models = tail_g1_alt1(data, len, &pos);
                    for( int m = 0; m < models; m++ )
                        npc_body_models(op, (int)tail_g4_alt1(data, len, &pos));
                }
                if( (body & 4) != 0 )
                {
                    int colours = tail_g1_alt2(data, len, &pos);
                    for( int c = 0; c < colours; c++ )
                        (void)tail_g2_alt3(data, len, &pos);
                }
                if( (body & 8) != 0 )
                {
                    int textures = tail_g1(data, len, &pos);
                    for( int t = 0; t < textures; t++ )
                        (void)tail_g2_alt1(data, len, &pos);
                }
                if( (body & 0x20) != 0 )
                {
                    int kits;

                    (void)tail_g1_alt2(data, len, &pos); /* body type */
                    kits = tail_g1_alt1(data, len, &pos);
                    for( int e = 0; e < kits; e++ )
                        (void)tail_g2_alt2(data, len, &pos);
                }
            }
        }
        /* decodeCombatLevelChange: g4. */
        if( (flag & V5_NPC_LEVEL_CHANGE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_LEVEL_CHANGE);
            uint32_t level = tail_g4(data, len, &pos);

            if( op )
                op->_bitvalue = level;
        }
        /* decodeHeadCustomisationV2: g1Alt3 flag; 0x2 g1 count + g4Alt1 models;
         * 0x4 / 0x8 replacement lists whose LENGTHS come from the NpcType (no
         * count on the wire); 0x10 g1Alt3 mirror. */
        if( (flag & V5_NPC_HEAD_CUSTOMISATION) != 0 )
        {
            int head = tail_g1_alt3(data, len, &pos);

            if( (head & 1) == 0 )
            {
                if( (head & 2) != 0 )
                {
                    int models = tail_g1(data, len, &pos);
                    for( int m = 0; m < models; m++ )
                        (void)tail_g4_alt1(data, len, &pos);
                }
                if( (head & (4 | 8)) != 0 )
                {
                    /* This pure decoder has no cache access, and lying about a
                     * length would corrupt every later npc block. */
                    TORIRS_LOG("osrs241: NPC head customisation needs NpcType array lengths; "
                            "tail stopped\n");
                    return;
                }
                if( (head & 0x10) != 0 )
                    (void)tail_g1_alt3(data, len, &pos);
            }
        }
        /* decodeHeadbars: g1 count; per bar smart type, smart end time, then
         * unless 0x7FFF: smart start time, g1Alt3 start fill, g1 end fill. */
        if( (flag & V5_NPC_HEADBARS) != 0 )
        {
            int bars = tail_g1(data, len, &pos);

            for( int b = 0; b < bars; b++ )
            {
                int type = tail_smart(data, len, &pos);
                int duration = tail_smart(data, len, &pos);
                struct PktNpcInfoOp* op = NULL;

                if( b == 0 )
                    op = npc_op(r, PKT_NPC_INFO_OP_HEADBAR);
                if( duration == 32767 )
                {
                    if( op )
                    {
                        op->_headbar.type = type;
                        op->_headbar.remove = true;
                    }
                }
                else
                {
                    int start_delay = tail_smart(data, len, &pos);
                    int start_fill = tail_g1_alt3(data, len, &pos);
                    int end_fill = duration > 0 ? tail_g1(data, len, &pos) : start_fill;

                    if( op )
                    {
                        op->_headbar.type = type;
                        op->_headbar.duration = duration;
                        op->_headbar.start_delay = start_delay;
                        op->_headbar.start_fill = (uint8_t)start_fill;
                        op->_headbar.end_fill = (uint8_t)end_fill;
                        op->_headbar.remove = false;
                    }
                }
            }
        }
        /* decodeSequence: g2Alt1 id, g1Alt1 delay. */
        if( (flag & V5_NPC_SEQUENCE) != 0 )
        {
            int seq = tail_g2_alt1(data, len, &pos);
            int delay = tail_g1_alt1(data, len, &pos);
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SEQUENCE);

            if( op )
            {
                op->_sequence.sequence_id = seq == 65535 ? -1 : seq;
                op->_sequence.delay = (uint8_t)delay;
            }
        }
        /* decodeBodyCustomisationV2 (the legacy block): g1 flag; 0x2 g1Alt2
         * count + g4Alt1 models; 0x4 / 0x8 NpcType-length lists; 0x10 g1Alt3. */
        if( (flag & V5_NPC_BODY_CUSTOMISATION_LEGACY) != 0 )
        {
            int body = tail_g1(data, len, &pos);

            if( (body & 1) == 0 )
            {
                if( (body & 2) != 0 )
                {
                    int models = tail_g1_alt2(data, len, &pos);
                    for( int m = 0; m < models; m++ )
                        (void)tail_g4_alt1(data, len, &pos);
                }
                if( (body & (4 | 8)) != 0 )
                {
                    TORIRS_LOG("osrs241: NPC legacy body customisation needs NpcType array "
                            "lengths; tail stopped\n");
                    return;
                }
                if( (body & 0x10) != 0 )
                    (void)tail_g1_alt3(data, len, &pos);
            }
        }
        /* decodeTinting: g2Alt1 start, g2 end, g1Alt1 hue, g1Alt1 saturation,
         * g1Alt2 lightness, g1Alt3 weight. */
        if( (flag & V5_NPC_TINTING) != 0 )
        {
            int start = tail_g2_alt1(data, len, &pos);
            int end = tail_g2(data, len, &pos);
            int hue = tail_g1_alt1(data, len, &pos);
            int saturation = tail_g1_alt1(data, len, &pos);
            int lightness = tail_g1_alt2(data, len, &pos);
            int weight = tail_g1_alt3(data, len, &pos);
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_TINT);

            if( op )
            {
                op->_tint.start_cycle_delta = (uint16_t)start;
                op->_tint.end_cycle_delta = (uint16_t)end;
                op->_tint.hue = (int8_t)hue;
                op->_tint.saturation = (int8_t)saturation;
                op->_tint.lightness = (int8_t)lightness;
                op->_tint.weight = (int8_t)weight;
            }
        }
        /* decodeBodyCustomisationV4 (new at 241): g1Alt1 flag; 0x2 g1Alt3 count
         * + g4 models; 0x4 g1 "one colour" switch -> g2Alt3 colour, and unless
         * that colour is present and not 65535, g1Alt1 count + g2Alt1; 0x8 g1
         * count + g2Alt1; 0x20 g1Alt2 body type, g1Alt1 count + g2Alt3. */
        if( (flag & V5_NPC_BODY_CUSTOMISATION_V4) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_BODY_CUSTOM);
            int body = tail_g1_alt1(data, len, &pos);

            if( op )
                op->_body.reset = (body & 1) != 0 || (body & 2) == 0;
            if( (body & 1) == 0 )
            {
                if( (body & 2) != 0 )
                {
                    int models = tail_g1_alt3(data, len, &pos);
                    for( int m = 0; m < models; m++ )
                        npc_body_models(op, (int)tail_g4(data, len, &pos));
                }
                if( (body & 4) != 0 )
                {
                    int recol_all = -1;

                    if( tail_g1(data, len, &pos) == 1 )
                        recol_all = tail_g2_alt3(data, len, &pos);
                    if( recol_all < 0 || recol_all == 65535 )
                    {
                        int colours = tail_g1_alt1(data, len, &pos);
                        for( int c = 0; c < colours; c++ )
                            (void)tail_g2_alt1(data, len, &pos);
                    }
                }
                if( (body & 8) != 0 )
                {
                    int textures = tail_g1(data, len, &pos);
                    for( int t = 0; t < textures; t++ )
                        (void)tail_g2_alt1(data, len, &pos);
                }
                if( (body & 0x20) != 0 )
                {
                    int kits;

                    (void)tail_g1_alt2(data, len, &pos); /* body type */
                    kits = tail_g1_alt1(data, len, &pos);
                    for( int e = 0; e < kits; e++ )
                        (void)tail_g2_alt3(data, len, &pos);
                }
            }
        }
        /* decodeFacing: g1Alt2 header (the player's is g1Alt3). */
        if( (flag & V5_NPC_FACING) != 0 )
        {
            int header = tail_g1_alt2(data, len, &pos);
            int kind = (header >> 3) & 0x7;
            bool instant = ((header >> 6) & 1) != 0;
            int movement_mode = header & 0x7;

            if( kind == 0 ) /* Entity */
            {
                int type = tail_smart(data, len, &pos);
                int index = tail_smart2or4null(data, len, &pos);
                int fallback_angle = tail_smart(data, len, &pos);
                struct PktNpcInfoOp* op;

                if( type == 3 )
                {
                    op = npc_op(r, PKT_NPC_INFO_OP_FACE_ANGLE);
                    if( op )
                    {
                        op->_face_angle.angle = (uint16_t)fallback_angle;
                        op->_face_angle.instant = instant;
                        op->_face_angle.movement_mode = (uint8_t)movement_mode;
                        op->_face_angle.modern = true;
                    }
                }
                else
                {
                    op = npc_op(r, PKT_NPC_INFO_OP_FACE_ENTITY);
                    if( op )
                    {
                        op->_face_entity.entity_id =
                            type == 1 ? index : type == 2 && index >= 0 ? 32768 + index : -1;
                        op->_face_entity.fallback_angle = (uint16_t)fallback_angle;
                        op->_face_entity.has_fallback_angle = true;
                        op->_face_entity.instant = instant;
                        op->_face_entity.movement_mode = (uint8_t)movement_mode;
                        op->_face_entity.modern = true;
                    }
                }
            }
            else if( kind == 1 ) /* Loc */
            {
                int x = tail_smart(data, len, &pos);
                int z = tail_smart(data, len, &pos);
                int size = tail_smart(data, len, &pos);
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_FACE_COORD);

                if( op )
                {
                    op->_face_coord.x = (int16_t)(x * 2 + (size & 0xf));
                    op->_face_coord.z = (int16_t)(z * 2 + ((size >> 4) & 0xf));
                    op->_face_coord.instant = instant;
                    op->_face_coord.movement_mode = (uint8_t)movement_mode;
                    op->_face_coord.modern = true;
                }
            }
            else if( kind == 2 ) /* Angle */
            {
                int angle = tail_smart(data, len, &pos);
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_FACE_ANGLE);

                if( op )
                {
                    op->_face_angle.angle = (uint16_t)angle;
                    op->_face_angle.instant = instant;
                    op->_face_angle.movement_mode = (uint8_t)movement_mode;
                    op->_face_angle.modern = true;
                }
            }
            else if( kind == 3 ) /* Reset */
            {
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_FACE_ENTITY);

                if( op )
                {
                    op->_face_entity.entity_id = -1;
                    op->_face_entity.movement_mode = (uint8_t)movement_mode;
                    op->_face_entity.modern = true;
                }
            }
        }
        /* decodeHeadiconCustomisation: g1 slot mask (239: g1Alt3), then per set
         * slot gSmart2or4null group, gSmart1or2null index. */
        if( (flag & V5_NPC_HEADICON_CUSTOMISATION) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_HEADICON_CUSTOM);
            int slots = tail_g1(data, len, &pos);

            if( op )
                op->_headicon.mask = (uint8_t)slots;
            for( int slot = 0; slot < 8; slot++ )
            {
                int group = -1;
                int index = -1;

                if( (slots & (1 << slot)) != 0 )
                {
                    group = tail_smart2or4null(data, len, &pos);
                    index = tail_smart1or2null(data, len, &pos);
                }
                if( op )
                {
                    op->_headicon.group[slot] = group;
                    op->_headicon.index[slot] = (int16_t)index;
                }
            }
        }
        /* decodeSay: gjstr. */
        if( (flag & V5_NPC_SAY) != 0 )
        {
            char* text = tail_string(data, len, &pos);
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SAY);

            if( op )
                op->_say.text = text;
            else
                ToriMem_Free(ToriMem_Net(), text);
        }
        /* decodeTransparency: g2sAlt1 start, g2s end, g1sAlt2 start alpha,
         * g1sAlt3 end alpha, g1 use-start. */
        if( (flag & V5_NPC_TRANSPARENCY) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_TRANSPARENCY);
            int start = tail_g2_alt1(data, len, &pos);
            int end = tail_g2(data, len, &pos);
            int from = tail_g1_alt2(data, len, &pos);
            int to = tail_g1_alt3(data, len, &pos);
            int use_start = tail_g1(data, len, &pos);

            if( op )
            {
                op->_transparency.start_cycle_delta = (int16_t)start;
                op->_transparency.end_cycle_delta = (int16_t)end;
                op->_transparency.from = (uint8_t)from;
                op->_transparency.to = (uint8_t)to;
                op->_transparency.use_start = use_start == 1;
            }
        }
        /* decodeNameChange: gjstr. */
        if( (flag & V5_NPC_NAME_CHANGE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_NAME_CHANGE);
            char* name = tail_string(data, len, &pos);

            if( op )
                op->_name_change.name = name;
            else
                ToriMem_Free(ToriMem_Net(), name);
        }
        /* decodeEnabledOps: g1Alt1. */
        if( (flag & V5_NPC_OPS) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_VISIBLE_OPS);
            int visible = tail_g1_alt1(data, len, &pos);

            if( op )
                op->_bitvalue = (uint64_t)visible;
        }
        /* decodeTransformation: g2Alt2 id (big endian, low byte +128). */
        if( (flag & V5_NPC_TRANSFORMATION) != 0 )
        {
            int type = tail_g2_alt2(data, len, &pos);
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_CHANGE_TYPE);

            if( op )
                op->_change_type.npc_type = type == 65535 ? -1 : type;
        }
        /* decodeHitmarks: g1Alt3 count, four smarts per hit. */
        if( (flag & V5_NPC_HITMARKS) != 0 )
        {
            int hits = tail_g1_alt3(data, len, &pos);

            for( int h = 0; h < hits && pos < len; h++ )
            {
                int type = tail_smart(data, len, &pos);
                int value = tail_smart(data, len, &pos);
                int delay = tail_smart(data, len, &pos);
                int slots = tail_smart(data, len, &pos);
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_DAMAGE);

                if( op )
                {
                    op->_damage.damage_type = type;
                    op->_damage.damage = value;
                    op->_damage.delay = (uint16_t)delay;
                    op->_damage.slots = (uint16_t)slots;
                }
            }
        }
        /* decodeBaseAnimationSet: g4Alt1 mask, then one g2 per set bit in the
         * 241 transforms below. */
        if( (flag & V5_NPC_BAS_CHANGE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_BAS_CHANGE);
            uint32_t bas = tail_g4_alt1(data, len, &pos);
            int values[15];

            for( int v = 0; v < 15; v++ )
                values[v] = -1;
            if( (bas & (1u << 0)) != 0 ) values[0] = tail_g2_alt1(data, len, &pos);
            if( (bas & (1u << 1)) != 0 ) values[1] = tail_g2(data, len, &pos);
            if( (bas & (1u << 2)) != 0 ) values[2] = tail_g2_alt2(data, len, &pos);
            if( (bas & (1u << 3)) != 0 ) values[3] = tail_g2_alt2(data, len, &pos);
            if( (bas & (1u << 4)) != 0 ) values[4] = tail_g2_alt1(data, len, &pos);
            if( (bas & (1u << 5)) != 0 ) values[5] = tail_g2_alt1(data, len, &pos);
            if( (bas & (1u << 6)) != 0 ) values[6] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 7)) != 0 ) values[7] = tail_g2_alt1(data, len, &pos);
            if( (bas & (1u << 8)) != 0 ) values[8] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 9)) != 0 ) values[9] = tail_g2(data, len, &pos);
            if( (bas & (1u << 10)) != 0 ) values[10] = tail_g2_alt2(data, len, &pos);
            if( (bas & (1u << 11)) != 0 ) values[11] = tail_g2(data, len, &pos);
            if( (bas & (1u << 12)) != 0 ) values[12] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 13)) != 0 ) values[13] = tail_g2(data, len, &pos);
            if( (bas & (1u << 14)) != 0 ) values[14] = tail_g2(data, len, &pos);
            for( int v = 0; v < 15; v++ )
                if( values[v] == 65535 )
                    values[v] = -1;
            if( op )
            {
                op->_bas_change.mask = bas;
                op->_bas_change.turnanim = values[0];
                op->_bas_change.turnanim_r = values[1];
                op->_bas_change.walkanim = values[2];
                op->_bas_change.walkanim_b = values[3];
                op->_bas_change.walkanim_l = values[4];
                op->_bas_change.walkanim_r = values[5];
                op->_bas_change.runanim = values[6];
                op->_bas_change.runanim_b = values[7];
                op->_bas_change.runanim_l = values[8];
                op->_bas_change.runanim_r = values[9];
                op->_bas_change.crawlanim = values[10];
                op->_bas_change.crawlanim_b = values[11];
                op->_bas_change.crawlanim_l = values[12];
                op->_bas_change.crawlanim_r = values[13];
                op->_bas_change.readyanim = values[14];
            }
        }
    }
}

int
osrs241_npc_info_read(
    uint8_t const* data,
    int len,
    struct PktNpcInfoOp* ops,
    int cap)
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
    struct V5NpcReader r;
    struct Net_BitBuffer buf;
    int count;
    int list_idx = 0;

    memset(&r, 0, sizeof(r));
    r.ops = ops;
    r.cap = cap;
    Net_BitBufferInit(&buf, data, len);

    count = Net_BitBufferGbits(&buf, 8);
    {
        struct PktNpcInfoOp* op = npc_op(&r, PKT_NPC_INFO_OPBITS_COUNT_RESET);

        if( op )
            op->_bitvalue = (uint64_t)count;
    }

    for( int old_idx = 0; old_idx < count; old_idx++ )
    {
        int info = Net_BitBufferGbits(&buf, 1);
        int move_op;
        struct PktNpcInfoOp* op;

        if( info == 0 )
        {
            op = npc_op(&r, PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX);
            if( op )
                op->_bitvalue = (uint64_t)old_idx;
            npc_op(&r, PKT_NPC_INFO_OPBITS_INFO);
            list_idx++;
            continue;
        }

        move_op = Net_BitBufferGbits(&buf, 2);
        if( move_op == 3 )
        {
            op = npc_op(&r, PKT_NPC_INFO_OP_CLEAR_NPC_OPBITS_IDX);
            if( op )
                op->_bitvalue = (uint64_t)old_idx;
            continue;
        }

        op = npc_op(&r, PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX);
        if( op )
            op->_bitvalue = (uint64_t)old_idx;
        op = npc_op(&r, PKT_NPC_INFO_OPBITS_INFO);
        if( op )
            op->_bitvalue = (uint64_t)info;

        if( move_op == 0 )
        {
            /* Stood still with something to say. The extended bit is implied —
             * an unchanged npc is written with the leading bit clear instead —
             * so there is no bit to read here. */
            npc_queue_extended(&r, list_idx);
        }
        else if( move_op == 1 )
        {
            int dir = Net_BitBufferGbits(&buf, 3);

            op = npc_op(&r, PKT_NPC_INFO_OPBITS_WALKDIR);
            if( op )
                op->_bitvalue = (uint64_t)dir;
            if( Net_BitBufferGbits(&buf, 1) )
                npc_queue_extended(&r, list_idx);
        }
        else if( Net_BitBufferGbits(&buf, 1) )
        {
            /* Type 2 carries its own traversal bit (deob Statics ~47110:
             * gBits(1) == 1 is two run steps, 0 one crawl step; rsprot's
             * pRun / pCrawl write the same). */
            for( int step = 0; step < 2; step++ )
            {
                int dir = Net_BitBufferGbits(&buf, 3);

                op = npc_op(&r, PKT_NPC_INFO_OPBITS_RUNDIR);
                if( op )
                    op->_bitvalue = (uint64_t)dir;
            }
            if( Net_BitBufferGbits(&buf, 1) )
                npc_queue_extended(&r, list_idx);
        }
        else
        {
            int dir = Net_BitBufferGbits(&buf, 3);

            op = npc_op(&r, PKT_NPC_INFO_OPBITS_CRAWLDIR);
            if( op )
                op->_bitvalue = (uint64_t)dir;
            if( Net_BitBufferGbits(&buf, 1) )
                npc_queue_extended(&r, list_idx);
        }
        list_idx++;
    }

    /*
     * Entering-view records, NPC_INFO_SMALL_V6 (RSProx NpcInfoClient
     * processLowResolution, `version == 6`): a 16-bit index terminated by
     * 0xFFFF, then
     *
     *   dz (6 bits, signed), jump (1), facing (3),
     *   hasSpawnCycle (1) [ width selector (2) -> 18/19/20/32 bits of cycle ],
     *   extended (1), type width selector (2) -> 12/14/17/24 bits of type,
     *   dx (6 bits, signed).
     *
     * 239's v5 record was spawn-cycle, extended, dx, facing, dz, jump and a
     * fixed 14-bit type; reading one as the other places an npc that does not
     * exist at a coordinate that is not there. The 28-bit guard (16 index bits
     * plus the client's 12-bit lookahead) is unchanged.
     */
    while( Net_BitBufferBitPos(&buf) + 28 <= len * 8 )
    {
        static uint16_t const k_spawn_facing[8] = {
            768, 1024, 1280, 512, 1536, 256, 0, 1792,
        };
        static int const k_spawn_clock_bits[4] = { 18, 19, 20, 32 };
        static int const k_type_bits[4] = { 12, 14, 17, 24 };
        int slot = Net_BitBufferGbits(&buf, 16);
        int has_spawn_cycle;
        uint32_t spawn_cycle = 0;
        int extended;
        int dx;
        int dz;
        int facing;
        int jump;
        int npc_type;
        struct PktNpcInfoOp* op;

        if( slot == 0xffff )
            break;

        op = npc_op(&r, PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID);
        if( op )
            op->_bitvalue = (uint64_t)slot;

        dz = Net_BitBufferGbits(&buf, 6);
        if( dz > 31 )
            dz -= 64;
        jump = Net_BitBufferGbits(&buf, 1);
        facing = Net_BitBufferGbits(&buf, 3);
        has_spawn_cycle = Net_BitBufferGbits(&buf, 1);
        if( has_spawn_cycle )
            spawn_cycle = Net_BitBufferGbits(&buf, k_spawn_clock_bits[Net_BitBufferGbits(&buf, 2)]);
        extended = Net_BitBufferGbits(&buf, 1);
        npc_type = Net_BitBufferGbits(&buf, k_type_bits[Net_BitBufferGbits(&buf, 2)]);
        dx = Net_BitBufferGbits(&buf, 6);
        if( dx > 31 )
            dx -= 64;

        op = npc_op(&r, PKT_NPC_INFO_OPBITS_NPCTYPE);
        if( op )
            op->_bitvalue = (uint64_t)npc_type;
        op = npc_op(&r, PKT_NPC_INFO_OP_DELTA_XZ);
        if( op )
        {
            op->_delta_xz.dx = (int16_t)dx;
            op->_delta_xz.dz = (int16_t)dz;
            op->_delta_xz.jump = jump != 0;
        }
        op = npc_op(&r, PKT_NPC_INFO_OP_FACE_ANGLE);
        if( op )
        {
            op->_face_angle.angle = k_spawn_facing[facing];
            op->_face_angle.instant = true;
            op->_face_angle.spawn = true;
        }
        if( has_spawn_cycle )
        {
            op = npc_op(&r, PKT_NPC_INFO_OP_SPAWN_CYCLE);
            if( op )
                op->_bitvalue = spawn_cycle;
        }
        if( extended )
            npc_queue_extended(&r, list_idx);
        list_idx++;
    }

    npc_extended(&r, data, len, Net_BitBufferBytePos(&buf));
    return r.count;
}
