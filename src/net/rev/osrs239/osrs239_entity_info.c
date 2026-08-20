/*
 * PLAYER_INFO / NPC_INFO v5 — the revision-239 entity streams, client side.
 *
 * These are a different CODEC from the classic bitstreams, not a different
 * field order, which is why they live here instead of in a `case` of
 * `osrs239_parse`. Both emit the same revision-independent op arrays the
 * classic readers do (`pkt_player_info.h` / `pkt_npc_info.h`), so everything
 * downstream — spawning, appearance builds, path steps, hitsplats — is
 * untouched.
 *
 * PROVENANCE. The player stream is transcribed from RSProt's own reference
 * DECODER (`PlayerInfoClient.kt` in its test tree) rather than from this repo's
 * encoder: a decoder written to match our encoder agrees with it by
 * construction and proves nothing. The extended-info flag values and the block
 * ORDER come from `PlayerAvatarExtendedInfoDesktopWriter` /
 * `NpcAvatarExtendedInfoDesktopWriter` at revision 239, because the 224 test
 * client's flags are a different set (its continuation bits are 0x10/0x1000
 * where 239's are 0x8/0x800) and nothing on the wire says which you are
 * reading.
 *
 * STATE. Unlike the classic stream, v5 is stateful across packets: the client
 * holds a 2048-slot table of who is in high resolution, each slot's coordinate,
 * and a two-bit cycle flag that decides which of the four bit sections a slot
 * is described in this tick. Losing that state does not desynchronise one
 * packet, it desynchronises every packet after it, so the table is seeded by
 * the GPI init block that rides inside the login REBUILD and reset with it.
 */

#include "net/bitbuffer.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_info.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define V5_PLAYER_SLOTS 2048

/* `unmodifiedFlags`: bit 0 is "inactive this cycle", bit 1 "inactive next".
 * The whole array shifts right one place at the end of every packet, which is
 * how a skip run written in one tick is consumed in the next. */
#define V5_CUR_CYCLE_INACTIVE 0x1
#define V5_NEXT_CYCLE_INACTIVE 0x2

/* Player extended-info flags, revision 239 (the desktop writer's own set). */
#define V5_PLAYER_FACE 0x1
#define V5_PLAYER_RESET 0x2
#define V5_PLAYER_SAY 0x4
#define V5_PLAYER_EXT_SHORT 0x8
#define V5_PLAYER_AUX_SHORT 0x10
#define V5_PLAYER_APPEARANCE 0x20
#define V5_PLAYER_SEQUENCE 0x40
#define V5_PLAYER_AUX_MESSAGE 0x80
#define V5_PLAYER_CHAT 0x100
#define V5_PLAYER_TINTING 0x200
#define V5_PLAYER_MOVE_SPEED 0x400
#define V5_PLAYER_EXT_MEDIUM 0x800
#define V5_PLAYER_TEMP_MOVE_SPEED 0x1000
#define V5_PLAYER_MINIMENU 0x2000
#define V5_PLAYER_EXACT_MOVE 0x4000
#define V5_PLAYER_HEADBARS 0x10000
#define V5_PLAYER_SPOTANIM 0x20000
#define V5_PLAYER_HITMARKS 0x40000
#define V5_PLAYER_FREEZE 0x80000
#define V5_PLAYER_TRANSPARENCY 0x100000

/* Npc extended-info flags, revision 239. A different set from the player's,
 * including the continuation bits: an npc's first is 0x40 where a player's is
 * 0x8, and only the second (0x800) is shared. */
#define V5_NPC_TRANSFORMATION 0x1
#define V5_NPC_SAY 0x2
#define V5_NPC_AUX_BYTES 0x4
#define V5_NPC_FACING 0x8
#define V5_NPC_AUX_SHORT 0x10
#define V5_NPC_FREEZE 0x20
#define V5_NPC_EXT_SHORT 0x40
#define V5_NPC_SEQUENCE 0x80
#define V5_NPC_TINTING 0x100
#define V5_NPC_EXACT_MOVE 0x200
#define V5_NPC_TRANSPARENCY 0x400
#define V5_NPC_EXT_MEDIUM 0x800
#define V5_NPC_OPS 0x1000
#define V5_NPC_AUX_BLOCK 0x2000
#define V5_NPC_NAME_CHANGE 0x4000
#define V5_NPC_LEVEL_CHANGE 0x8000
#define V5_NPC_AUX_MEDIUM 0x10000
#define V5_NPC_HEAD_CUSTOMISATION 0x20000
#define V5_NPC_SPOTANIM 0x40000
#define V5_NPC_HITMARKS 0x80000
#define V5_NPC_BAS_CHANGE 0x100000
#define V5_NPC_EXT_INT 0x200000
#define V5_NPC_HEADICON_CUSTOMISATION 0x400000
#define V5_NPC_BODY_CUSTOMISATION_LEGACY 0x800000
#define V5_NPC_HEADBARS 0x1000000
#define V5_NPC_BODY_CUSTOMISATION 0x2000000

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
osrs239_playerinfo_set_local(int local_index)
{
    memset(&g_player, 0, sizeof(g_player));
    g_player.local_index = local_index;
}

void
osrs239_playerinfo_init(
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

    /* The temporary value 127 is class174.field2477 in the official client:
     * it applies the coordinate immediately instead of queuing locomotion. */
    r->ops[op_index]._local_xz_level.has_move_speed = true;
    r->ops[op_index]._local_xz_level.move_speed = (int8_t)speed;
    if( speed == PKT_PLAYER_TRAVERSAL_SNAP )
        r->ops[op_index]._local_xz_level.jump = true;
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
        int32_t rough = g_player.low_res_pos[idx];
        int level;

        /* The recursion is the wire's, not a convenience: a promotion may be
         * preceded by one more low-resolution update for the same slot. */
        if( Net_BitBufferGbits(buf, 1) != 0 )
            player_low_res(r, buf, idx);
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
            fprintf(stderr, "osrs239: PLAYER_INFO dropped the local index to low res\n");
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
        int is_high = g_player.high_res[idx] != 0;
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

/* The appearance block: p1Alt3 length (`128 - n`), then the body with every
 * byte written +128. Both are obfuscation rather than structure, and both are
 * load-bearing — a plain length and a plain body read as a different length of
 * different data. */
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
    block_len = (128 - data[(*pos)++]) & 0xff;
    if( *pos + block_len > len )
        return 0;
    copy = (uint8_t*)malloc((size_t)block_len);
    assert(copy);
    for( int i = 0; i < block_len; i++ )
        copy[i] = (uint8_t)((data[*pos + i] - 128) & 0xff);
    *pos += block_len;
    {
        struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_APPEARANCE);

        if( !op )
        {
            free(copy);
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

static uint32_t
tail_g4_alt2(uint8_t const* data, int len, int* pos)
{
    uint32_t b0 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b1 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b2 = (uint32_t)tail_g1(data, len, pos);
    uint32_t b3 = (uint32_t)tail_g1(data, len, pos);

    return (b0 << 8) | b1 | (b2 << 24) | (b3 << 16);
}

static char*
tail_string(uint8_t const* data, int len, int* pos)
{
    int start = *pos;
    char* text;

    while( *pos < len && data[*pos] != 0 )
        (*pos)++;
    text = (char*)malloc((size_t)(*pos - start) + 1);
    assert(text);
    memcpy(text, data + start, (size_t)(*pos - start));
    text[*pos - start] = '\0';
    if( *pos < len )
        (*pos)++;
    return text;
}

/*
 * The extended-info tail, byte-aligned, in the order the indices were flagged.
 *
 * Blocks are written in the DESKTOP WRITER's order — HITMARKS, then SEQUENCE,
 * then APPEARANCE among what this server sends — which is neither ascending
 * flag order nor the order they are declared in. Nothing on the wire separates
 * them, so a block read out of place is read as the next one.
 *
 * A flag this reader has no decoder for ENDS the tail rather than skipping it:
 * the block's length is not on the wire, so there is no way to step over one
 * and stay aligned, and continuing would decode the next player's flag byte
 * out of the middle of a block.
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

        /* One line per extended block, because "the animation did not play" has
         * three candidate causes — the server never set the mask, the tail
         * decoded at the wrong offset, or the executor dropped the op — and
         * only the middle one is invisible from either end. Pairs with
         * TORIRSSERVER_EXT_DEBUG on the server. */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr, "osrs239: player %d extended flag=0x%x at %d/%d\n", idx, flag,
                    pos, len);

        {
            struct PktPlayerInfoOp* op =
                player_op(r, idx == g_player.local_index
                                 ? PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER
                                 : PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID);

            if( op && idx != g_player.local_index )
                op->_bitvalue = (uint64_t)idx;
        }

        if( (flag & V5_PLAYER_MINIMENU) != 0 )
        {
            /* The three player-option strings are global UI state in the
             * reference. They are still structural here: each is NUL-ended. */
            for( int option = 0; option < 3; option++ )
            {
                char* ignored = tail_string(data, len, &pos);
                free(ignored);
            }
        }

        if( (flag & V5_PLAYER_HITMARKS) != 0 )
        {
            /* PlayerHitEncoder: p1Alt3 count, then per hit pSmart1or2 type,
             * value, delay, limit. The COUNT is p1Alt3 here where the npc
             * encoder writes p1Alt1 — reading the wrong one turns one hitsplat
             * into 255 and runs off the end of the packet. */
            int hits = (128 - data[pos++]) & 0xff;

            for( int h = 0; h < hits && pos < len; h++ )
            {
                int type = tail_smart(data, len, &pos);
                int value = tail_smart(data, len, &pos);

                int delay = tail_smart(data, len, &pos);
                int slots = tail_smart(data, len, &pos);
                {
                    struct PktPlayerInfoOp* op =
                        player_op(r, PKT_PLAYER_INFO_OP_DAMAGE);

                    if( op )
                    {
                        op->_damage.damage_type = type;
                        op->_damage.damage = value;
                        op->_damage.delay = (uint16_t)delay;
                        op->_damage.slots = (uint16_t)slots;
                    }
                }
            }
        }
        if( (flag & V5_PLAYER_RESET) != 0 )
        {
            /* Reset's first half clears cached avatar state before the tail is
             * read; its p1Alt2 byte is nevertheless part of this record. */
            (void)tail_g1_alt2(data, len, &pos);
        }
        if( (flag & V5_PLAYER_FACE) != 0 )
        {
            /* PlayerFacingEncoder: g1Alt2 header.  Preserve the legacy op
             * vocabulary for the common executor while translating Face.Loc's
             * tile/size coordinates back to a half-tile centre. */
            int header = tail_g1_alt2(data, len, &pos);
            int kind = (header >> 3) & 0x7;
            bool instant = ((header >> 6) & 1) != 0;
            int movement_mode = header & 0x7;

            if( kind == 0 ) /* Entity */
            {
                int type = tail_smart(data, len, &pos);
                int index = tail_smart2or4null(data, len, &pos);
                struct PktPlayerInfoOp* op;

                int fallback_angle = tail_smart(data, len, &pos);
                if( type == 3 )
                {
                    op = player_op(r, PKT_PLAYER_INFO_OP_FACE_ANGLE);
                    if( op )
                    {
                        /* The C world has no third arbitrary-world-entity
                         * registry. Match the gamepack's missing-target path
                         * by applying the supplied fallback angle. */
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
                            type == 1 ? index :
                            type == 2 && index >= 0 ? 32768 + index : -1;
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
                struct PktPlayerInfoOp* op =
                    player_op(r, PKT_PLAYER_INFO_OP_FACE_COORD);

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
                struct PktPlayerInfoOp* op =
                    player_op(r, PKT_PLAYER_INFO_OP_FACE_ANGLE);

                if( op )
                {
                    op->_face_angle.angle = (uint16_t)tail_smart(data, len, &pos);
                    op->_face_angle.instant = instant;
                    op->_face_angle.movement_mode = (uint8_t)movement_mode;
                    op->_face_angle.modern = true;
                }
                else
                    (void)tail_smart(data, len, &pos);
            }
            else if( kind == 3 ) /* Reset */
            {
                struct PktPlayerInfoOp* op =
                    player_op(r, PKT_PLAYER_INFO_OP_FACE_ENTITY);

                if( op )
                {
                    op->_face_entity.entity_id = -1;
                    op->_face_entity.movement_mode = (uint8_t)movement_mode;
                    op->_face_entity.modern = true;
                }
            }
        }
        if( (flag & V5_PLAYER_AUX_MESSAGE) != 0 )
        {
            int message_len;

            (void)tail_g2_alt3(data, len, &pos);
            (void)tail_g1(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
            message_len = tail_g1_alt3(data, len, &pos);
            pos += message_len;
            if( pos > len )
                pos = len;
        }
        if( (flag & V5_PLAYER_MOVE_SPEED) != 0 )
        {
            /* PlayerMoveSpeedEncoder is p1Alt2: the signed inverse byte. It is
             * persistent and also becomes this cycle's traversal when the
             * player moved, matching class109.method3804's field1570 update. */
            int speed = pos < len ? (int)(int8_t)(-data[pos++]) :
                                    PKT_PLAYER_TRAVERSAL_WALK;

            player_set_move_speed(r, idx, speed, 1);
        }
        if( (flag & V5_PLAYER_SPOTANIM) != 0 )
        {
            int entries = tail_g1(data, len, &pos);

            for( int e = 0; e < entries; e++ )
            {
                struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_SPOTANIM);
                int slot = tail_g1_alt2(data, len, &pos);
                int spot = tail_g2_alt2(data, len, &pos);
                uint32_t height_delay =
                    ((uint32_t)tail_g1(data, len, &pos) << 24) |
                    ((uint32_t)tail_g1(data, len, &pos) << 16) |
                    ((uint32_t)tail_g1(data, len, &pos) << 8) |
                    (uint32_t)tail_g1(data, len, &pos);

                if( op )
                {
                    op->_spotanim.slot = (uint8_t)slot;
                    op->_spotanim.spotanim_id = spot == 65535 ? -1 : spot;
                    op->_spotanim.height_delay = (int32_t)height_delay;
                }
            }
        }
        if( (flag & V5_PLAYER_AUX_SHORT) != 0 )
        {
            (void)tail_g2_alt1(data, len, &pos);
            (void)tail_g1(data, len, &pos);
        }
        if( (flag & V5_PLAYER_CHAT) != 0 )
        {
            int colour_effect = tail_g2_alt2(data, len, &pos);
            int type = tail_g1_alt2(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos); /* auto-typed */
            int payload_len = tail_g1_alt2(data, len, &pos);
            uint8_t* payload = NULL;
            int pattern_len = 0;

            if( payload_len > 0 && pos + payload_len <= len )
            {
                payload = (uint8_t*)malloc((size_t)payload_len);
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
                (void)tail_g1_alt2(data, len, &pos);
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
                    free(payload);
            }
        }
        if( (flag & V5_PLAYER_TRANSPARENCY) != 0 )
        {
            (void)tail_g2_alt2(data, len, &pos); /* start cycle */
            (void)tail_g2(data, len, &pos);      /* end cycle */
            (void)tail_g1_alt3(data, len, &pos); /* start alpha */
            (void)tail_g1_alt3(data, len, &pos); /* end alpha */
            (void)tail_g1_alt3(data, len, &pos); /* use start */
        }
        if( (flag & V5_PLAYER_TINTING) != 0 )
        {
            (void)tail_g2_alt1(data, len, &pos); /* start cycle */
            (void)tail_g2(data, len, &pos);      /* end cycle */
            (void)tail_g1_alt2(data, len, &pos); /* hue */
            (void)tail_g1(data, len, &pos);      /* saturation */
            (void)tail_g1_alt3(data, len, &pos); /* lightness */
            (void)tail_g1_alt3(data, len, &pos); /* weight */
        }
        if( (flag & V5_PLAYER_HEADBARS) != 0 )
        {
            /*
             * The three byte transforms here are NOT the npc block's, and the
             * two blocks do not even agree on which is which: the player reads
             * the count alt2 and the target fill alt3, the npc reads the count
             * alt1 and the target fill alt2. Both used to be spelled the other
             * way round, which costs nothing against a server that makes the
             * same swap and turns a fill of 12 into 140 against one that does
             * not -- and a target fill of 140 drew a 140-pixel bar.
             */
            int bars = tail_g1_alt2(data, len, &pos);

            for( int b = 0; b < bars; b++ )
            {
                int type = tail_smart(data, len, &pos);
                {
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
                        int start_fill = tail_g1_alt1(data, len, &pos);
                        int end_fill = duration > 0 ? tail_g1_alt3(data, len, &pos) : start_fill;

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
        }
        if( (flag & V5_PLAYER_SAY) != 0 )
        {
            int start = pos;
            char* text;

            while( pos < len && data[pos] != 0 )
                pos++;
            text = (char*)malloc((size_t)(pos - start) + 1);
            assert(text);
            memcpy(text, data + start, (size_t)(pos - start));
            text[pos - start] = '\0';
            pos++;
            {
                struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_SAY);

                if( op )
                    op->_say.text = text;
                else
                    free(text);
            }
        }
        if( (flag & V5_PLAYER_SEQUENCE) != 0 )
        {
            /* PlayerSequenceEncoder: p2Alt2 id, p1 delay — both orders differ
             * from the npc encoder's p2 id, p1Alt2 delay. */
            int hi = pos < len ? data[pos++] : 0;
            int lo = pos < len ? data[pos++] : 0;
            int seq = (hi << 8) | ((lo - 128) & 0xff);
            int delay = pos < len ? data[pos++] : 0;
            struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_SEQUENCE);

            if( op )
            {
                op->_sequence.sequence_id = seq == 65535 ? -1 : seq;
                op->_sequence.delay = (uint8_t)delay;
            }
        }
        if( (flag & V5_PLAYER_TEMP_MOVE_SPEED) != 0 )
        {
            /* Raw signed byte, applied to this cycle only. Value 2 is run. */
            int speed = pos < len ? (int)(int8_t)data[pos++] :
                                    PKT_PLAYER_TRAVERSAL_WALK;

            player_set_move_speed(r, idx, speed, 0);
        }
        if( (flag & V5_PLAYER_APPEARANCE) != 0 )
        {
            if( !player_appearance_block(r, data, len, &pos) )
                return;
        }
        if( (flag & V5_PLAYER_EXACT_MOVE) != 0 )
        {
            struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_EXACT_MOVE);
            int sx = (int8_t)tail_g1(data, len, &pos);
            int sz = (int8_t)tail_g1_alt1(data, len, &pos);
            int ex = (int8_t)tail_g1_alt2(data, len, &pos);
            int ez = (int8_t)tail_g1_alt3(data, len, &pos);
            int start = tail_g2(data, len, &pos);
            int end = tail_g2_alt1(data, len, &pos);
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
        if( (flag & V5_PLAYER_FREEZE) != 0 )
        {
            (void)tail_g2_alt3(data, len, &pos); /* delay */
            (void)tail_g2_alt3(data, len, &pos); /* duration */
            (void)tail_g1_alt1(data, len, &pos); /* cancel sequence */
        }

        known = V5_PLAYER_EXT_SHORT | V5_PLAYER_EXT_MEDIUM | V5_PLAYER_FACE |
                V5_PLAYER_RESET | V5_PLAYER_AUX_SHORT | V5_PLAYER_AUX_MESSAGE |
                V5_PLAYER_MINIMENU | V5_PLAYER_HITMARKS | V5_PLAYER_CHAT |
                V5_PLAYER_TINTING | V5_PLAYER_TRANSPARENCY | V5_PLAYER_HEADBARS |
                V5_PLAYER_SPOTANIM | V5_PLAYER_EXACT_MOVE | V5_PLAYER_FREEZE |
                V5_PLAYER_SAY | V5_PLAYER_SEQUENCE | V5_PLAYER_MOVE_SPEED |
                V5_PLAYER_TEMP_MOVE_SPEED |
                V5_PLAYER_APPEARANCE;
        if( (flag & ~known) != 0 )
        {
            fprintf(
                stderr,
                "osrs239: PLAYER_INFO extended flag 0x%x has undecoded blocks "
                "(0x%x); tail stopped\n",
                flag,
                flag & ~known);
            return;
        }
    }
}

int
osrs239_player_info_read(
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
        fprintf(stderr, "osrs239: PLAYER_INFO before the GPI init block; dropped\n");
        return 0;
    }

    memset(&r, 0, sizeof(r));
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
 * The npc extended tail. Same rule as the player's: blocks in the desktop
 * writer's order, and an undecoded flag ends the tail rather than skipping a
 * block whose length is not stated.
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

        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX);

            if( op )
                op->_bitvalue = (uint64_t)r->extended[i];
        }

        if( (flag & V5_NPC_BODY_CUSTOMISATION_LEGACY) != 0 )
        {
            int body = tail_g1(data, len, &pos);

            if( (body & 1) == 0 )
            {
                if( (body & 2) != 0 )
                {
                    int models = tail_g1(data, len, &pos);
                    for( int m = 0; m < models; m++ )
                        (void)tail_g4_alt1(data, len, &pos);
                }
                if( (body & (4 | 8)) != 0 )
                {
                    fprintf(stderr,
                            "osrs239: NPC legacy body customisation needs NpcType array "
                            "lengths; tail stopped\n");
                    return;
                }
                if( (body & 0x10) != 0 )
                    (void)tail_g1(data, len, &pos);
            }
        }

        if( (flag & V5_NPC_LEVEL_CHANGE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_LEVEL_CHANGE);
            uint32_t level = tail_g4_alt1(data, len, &pos);

            if( op )
                op->_bitvalue = level;
        }
        if( (flag & V5_NPC_HEADICON_CUSTOMISATION) != 0 )
        {
            int slots = tail_g1_alt3(data, len, &pos);

            for( int slot = 0; slot < 8; slot++ )
                if( (slots & (1 << slot)) != 0 )
                {
                    (void)tail_smart2or4null(data, len, &pos);
                    (void)tail_smart1or2null(data, len, &pos);
                }
        }
        if( (flag & V5_NPC_OPS) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_VISIBLE_OPS);
            int visible = tail_g1_alt2(data, len, &pos);

            if( op )
                op->_bitvalue = (uint64_t)visible;
        }

        if( (flag & V5_NPC_HITMARKS) != 0 )
        {
            /* NpcHitmarkEncoder: p1Alt1 count (`n + 128`), NOT the player
             * encoder's p1Alt3. */
            int hits = (data[pos++] - 128) & 0xff;

            for( int h = 0; h < hits && pos < len; h++ )
            {
                int type = tail_smart(data, len, &pos);
                int value = tail_smart(data, len, &pos);

                int delay = tail_smart(data, len, &pos);
                int slots = tail_smart(data, len, &pos);
                {
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
        }
        if( (flag & V5_NPC_EXACT_MOVE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_EXACT_MOVE);
            int sx = (int8_t)tail_g1_alt3(data, len, &pos);
            int sz = (int8_t)tail_g1(data, len, &pos);
            int ex = (int8_t)tail_g1_alt1(data, len, &pos);
            int ez = (int8_t)tail_g1(data, len, &pos);
            int start = tail_g2_alt3(data, len, &pos);
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
        }
        if( (flag & V5_NPC_BODY_CUSTOMISATION) != 0 )
        {
            int body = tail_g1_alt2(data, len, &pos);

            if( (body & 1) == 0 )
            {
                if( (body & 2) != 0 )
                {
                    int models = tail_g1_alt1(data, len, &pos);
                    for( int m = 0; m < models; m++ )
                        (void)tail_g4(data, len, &pos);
                }
                if( (body & 4) != 0 )
                {
                    int colours = tail_g1_alt2(data, len, &pos);
                    for( int c = 0; c < colours; c++ )
                        (void)tail_g2_alt1(data, len, &pos);
                }
                if( (body & 8) != 0 )
                {
                    int textures = tail_g1_alt2(data, len, &pos);
                    for( int t = 0; t < textures; t++ )
                        (void)tail_g2_alt3(data, len, &pos);
                }
                if( (body & 0x20) != 0 )
                {
                    (void)tail_g1_alt1(data, len, &pos); /* gender/body flag */
                    {
                        int equipment = tail_g1_alt1(data, len, &pos);
                        for( int e = 0; e < equipment; e++ )
                            (void)tail_g2(data, len, &pos);
                    }
                }
            }
        }
        if( (flag & V5_NPC_HEADBARS) != 0 )
        {
            /* Count alt1 and target fill alt2 -- see the player block above
             * for why these two are worth spelling out separately. */
            int bars = tail_g1_alt1(data, len, &pos);

            for( int b = 0; b < bars; b++ )
            {
                int type = tail_smart(data, len, &pos);
                {
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
                        int start_fill = tail_g1_alt1(data, len, &pos);
                        int end_fill = duration > 0 ? tail_g1_alt2(data, len, &pos) : start_fill;

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
        }
        if( (flag & V5_NPC_SEQUENCE) != 0 )
        {
            /* NpcSequenceEncoder: p2 id, p1Alt2 delay. */
            int seq = pos + 1 < len ? ((data[pos] << 8) | data[pos + 1]) : 65535;
            int delay;

            pos += 2;
            delay = pos < len ? ((-data[pos]) & 0xff) : 0;
            pos++;
            {
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SEQUENCE);

                if( op )
                {
                    op->_sequence.sequence_id = seq == 65535 ? -1 : seq;
                    op->_sequence.delay = (uint8_t)delay;
                }
            }
        }
        if( (flag & V5_NPC_SAY) != 0 )
        {
            int start = pos;
            char* text;

            while( pos < len && data[pos] != 0 )
                pos++;
            text = (char*)malloc((size_t)(pos - start) + 1);
            assert(text);
            memcpy(text, data + start, (size_t)(pos - start));
            text[pos - start] = '\0';
            pos++;
            {
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SAY);

                if( op )
                    op->_say.text = text;
                else
                    free(text);
            }
        }
        if( (flag & V5_NPC_TINTING) != 0 )
        {
            (void)tail_g2_alt1(data, len, &pos);
            (void)tail_g2_alt2(data, len, &pos);
            (void)tail_g1(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
        }
        if( (flag & V5_NPC_TRANSPARENCY) != 0 )
        {
            (void)tail_g2_alt3(data, len, &pos);
            (void)tail_g2(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
            (void)tail_g1_alt2(data, len, &pos);
            (void)tail_g1_alt2(data, len, &pos);
        }
        if( (flag & V5_NPC_BAS_CHANGE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_BAS_CHANGE);
            uint32_t bas = tail_g4_alt1(data, len, &pos);
            int values[15];

            for( int v = 0; v < 15; v++ )
                values[v] = -1;
            if( (bas & (1u << 0)) != 0 ) values[0] = tail_g2(data, len, &pos);
            if( (bas & (1u << 1)) != 0 ) values[1] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 2)) != 0 ) values[2] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 3)) != 0 ) values[3] = tail_g2_alt1(data, len, &pos);
            if( (bas & (1u << 4)) != 0 ) values[4] = tail_g2(data, len, &pos);
            if( (bas & (1u << 5)) != 0 ) values[5] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 6)) != 0 ) values[6] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 7)) != 0 ) values[7] = tail_g2_alt3(data, len, &pos);
            if( (bas & (1u << 8)) != 0 ) values[8] = tail_g2_alt2(data, len, &pos);
            if( (bas & (1u << 9)) != 0 ) values[9] = tail_g2_alt2(data, len, &pos);
            if( (bas & (1u << 10)) != 0 ) values[10] = tail_g2(data, len, &pos);
            if( (bas & (1u << 11)) != 0 ) values[11] = tail_g2_alt1(data, len, &pos);
            if( (bas & (1u << 12)) != 0 ) values[12] = tail_g2_alt2(data, len, &pos);
            if( (bas & (1u << 13)) != 0 ) values[13] = tail_g2_alt1(data, len, &pos);
            if( (bas & (1u << 14)) != 0 ) values[14] = tail_g2(data, len, &pos);
            for( int v = 0; v < 15; v++ )
                if( values[v] == 65535 )
                    values[v] = -1;
            if( op )
            {
                op->_bas_change.mask = bas;
                op->_bas_change.turnanim = values[0];
                op->_bas_change.walkanim = values[2];
                op->_bas_change.walkanim_b = values[3];
                op->_bas_change.walkanim_l = values[4];
                op->_bas_change.walkanim_r = values[5];
                op->_bas_change.runanim = values[6];
                op->_bas_change.readyanim = values[14];
            }
        }
        if( (flag & V5_NPC_FREEZE) != 0 )
        {
            (void)tail_g2(data, len, &pos);
            (void)tail_g2(data, len, &pos);
            (void)tail_g1_alt2(data, len, &pos);
        }
        if( (flag & V5_NPC_AUX_MEDIUM) != 0 )
        {
            (void)tail_g2_alt1(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
        }
        if( (flag & V5_NPC_AUX_BLOCK) != 0 )
        {
            (void)tail_g1_alt2(data, len, &pos);
            (void)tail_g1(data, len, &pos);
            (void)tail_g2_alt3(data, len, &pos);
            (void)tail_g2_alt1(data, len, &pos);
            (void)tail_g2_alt2(data, len, &pos);
            (void)tail_g1(data, len, &pos);
        }
        if( (flag & V5_NPC_HEAD_CUSTOMISATION) != 0 )
        {
            int head = tail_g1_alt1(data, len, &pos);

            if( (head & 1) == 0 )
            {
                if( (head & 2) != 0 )
                {
                    int models = tail_g1_alt3(data, len, &pos);
                    for( int m = 0; m < models; m++ )
                        (void)tail_g4_alt2(data, len, &pos);
                }
                if( (head & (4 | 8)) != 0 )
                {
                    /* These two arrays deliberately have no wire count: their
                     * lengths come from the current NpcType recolour/retexture
                     * arrays. This pure decoder has no cache access, so lying
                     * about a length would corrupt every later npc block. */
                    fprintf(stderr,
                            "osrs239: NPC head customisation needs NpcType array lengths; "
                            "tail stopped\n");
                    return;
                }
                if( (head & 0x10) != 0 )
                    (void)tail_g1_alt2(data, len, &pos);
            }
        }
        if( (flag & V5_NPC_SPOTANIM) != 0 )
        {
            /* NpcSpotAnimEncoder: p1Alt2 count, then per entry p1 slot, p2 id,
             * p4Alt2 (delay | height << 16). A LIST at this revision where the
             * classic packet carried exactly one and had no slot. */
            int entries = pos < len ? ((-data[pos++]) & 0xff) : 0;

            for( int e = 0; e < entries && pos + 6 < len; e++ )
            {
                int spot;
                int height_delay;

                int slot = tail_g1(data, len, &pos);
                spot = (data[pos] << 8) | data[pos + 1];
                pos += 2;
                height_delay = (data[pos + 2] << 24) | (data[pos + 3] << 16) |
                               (data[pos] << 8) | data[pos + 1];
                pos += 4;
                {
                    struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SPOTANIM);

                    if( op )
                    {
                        op->_spotanim.spotanim_id = spot == 65535 ? -1 : spot;
                        op->_spotanim.height_delay = height_delay;
                        op->_spotanim.slot = (uint8_t)slot;
                    }
                }
            }
        }
        if( (flag & V5_NPC_AUX_SHORT) != 0 )
        {
            (void)tail_g2_alt3(data, len, &pos);
            (void)tail_g1_alt1(data, len, &pos);
        }
        if( (flag & V5_NPC_NAME_CHANGE) != 0 )
        {
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_NAME_CHANGE);
            char* name = tail_string(data, len, &pos);

            if( op )
                op->_name_change.name = name;
            else
                free(name);
        }
        if( (flag & V5_NPC_TRANSFORMATION) != 0 )
        {
            /* NpcTransformationEncoder: p2Alt3 id. */
            int lo = pos < len ? data[pos++] : 0;
            int hi = pos < len ? data[pos++] : 0;
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_CHANGE_TYPE);

            if( op )
            {
                int type = (hi << 8) | ((lo - 128) & 0xff);
                op->_change_type.npc_type = type == 65535 ? -1 : type;
            }
        }
        if( (flag & V5_NPC_FACING) != 0 )
        {
            /* NpcFaceEncoder differs from PlayerFacingEncoder only in its
             * g1Alt1 header transform. */
            int header = tail_g1_alt1(data, len, &pos);
            int kind = (header >> 3) & 0x7;
            bool instant = ((header >> 6) & 1) != 0;
            int movement_mode = header & 0x7;

            if( kind == 0 ) /* Entity */
            {
                int type = tail_smart(data, len, &pos);
                int index = tail_smart2or4null(data, len, &pos);
                struct PktNpcInfoOp* op;

                int fallback_angle = tail_smart(data, len, &pos);
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
                            type == 1 ? index :
                            type == 2 && index >= 0 ? 32768 + index : -1;
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
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_FACE_ANGLE);

                if( op )
                {
                    op->_face_angle.angle = (uint16_t)tail_smart(data, len, &pos);
                    op->_face_angle.instant = instant;
                    op->_face_angle.movement_mode = (uint8_t)movement_mode;
                    op->_face_angle.modern = true;
                }
                else
                    (void)tail_smart(data, len, &pos);
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
        if( (flag & V5_NPC_AUX_BYTES) != 0 )
        {
            (void)tail_g1_alt1(data, len, &pos);
            (void)tail_g1_alt2(data, len, &pos);
            (void)tail_g1_alt2(data, len, &pos);
            (void)tail_g1_alt3(data, len, &pos);
        }

        known = V5_NPC_EXT_SHORT | V5_NPC_EXT_MEDIUM | V5_NPC_EXT_INT | V5_NPC_FACING |
                V5_NPC_AUX_BYTES | V5_NPC_AUX_SHORT | V5_NPC_AUX_BLOCK |
                V5_NPC_AUX_MEDIUM | V5_NPC_HITMARKS | V5_NPC_EXACT_MOVE |
                V5_NPC_BODY_CUSTOMISATION | V5_NPC_HEAD_CUSTOMISATION |
                V5_NPC_BODY_CUSTOMISATION_LEGACY |
                V5_NPC_HEADICON_CUSTOMISATION | V5_NPC_HEADBARS | V5_NPC_SEQUENCE |
                V5_NPC_SAY | V5_NPC_TINTING | V5_NPC_TRANSPARENCY |
                V5_NPC_BAS_CHANGE | V5_NPC_FREEZE | V5_NPC_OPS |
                V5_NPC_LEVEL_CHANGE | V5_NPC_NAME_CHANGE | V5_NPC_SPOTANIM |
                V5_NPC_TRANSFORMATION;
        if( (flag & ~known) != 0 )
        {
            fprintf(
                stderr,
                "osrs239: NPC_INFO extended flag 0x%x has undecoded blocks (0x%x); "
                "tail stopped\n",
                flag,
                flag & ~known);
            return;
        }
    }
}

int
osrs239_npc_info_read(
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
        else
        {
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
        list_idx++;
    }

    /*
     * Entering-view records: a 16-bit index terminated by 0xFFFF, then
     * spawn-cycle, extended, a 6-bit dx, a 3-bit facing, a 6-bit dz, a jump bit
     * and a 14-bit type. The classic record is 5-bit deltas with the type
     * BEFORE them and no facing at all, so reading one as the other places an
     * npc that does not exist at a coordinate that is not there.
     *
     * The 28-bit guard is the client's own — 16 index bits plus its fixed
     * 12-bit low-resolution lookahead, `readableBits() < var20 + 12` in the
     * rev-239 deob. The terminator is only written when
     * extended-info bytes follow, so with nothing after the bit section the
     * loop has to stop on remaining width instead.
     */
    while( Net_BitBufferBitPos(&buf) + 28 <= len * 8 )
    {
        static uint16_t const k_spawn_facing[8] = {
            768, 1024, 1280, 512, 1536, 256, 0, 1792,
        };
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

        has_spawn_cycle = Net_BitBufferGbits(&buf, 1);
        if( has_spawn_cycle )
            spawn_cycle = Net_BitBufferGbits(&buf, 32);
        extended = Net_BitBufferGbits(&buf, 1);
        dx = Net_BitBufferGbits(&buf, 6);
        if( dx > 31 )
            dx -= 64;
        facing = Net_BitBufferGbits(&buf, 3);
        dz = Net_BitBufferGbits(&buf, 6);
        if( dz > 31 )
            dz -= 64;
        jump = Net_BitBufferGbits(&buf, 1);
        npc_type = Net_BitBufferGbits(&buf, 14);

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
