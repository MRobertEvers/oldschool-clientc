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
#define V5_PLAYER_APPEARANCE 0x20
#define V5_PLAYER_SEQUENCE 0x40
#define V5_PLAYER_CHAT 0x100
#define V5_PLAYER_TINTING 0x200
#define V5_PLAYER_MOVE_SPEED 0x400
#define V5_PLAYER_EXT_MEDIUM 0x800
#define V5_PLAYER_TEMP_MOVE_SPEED 0x1000
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
#define V5_NPC_FACING 0x8
#define V5_NPC_FREEZE 0x20
#define V5_NPC_EXT_SHORT 0x40
#define V5_NPC_SEQUENCE 0x80
#define V5_NPC_TINTING 0x100
#define V5_NPC_EXACT_MOVE 0x200
#define V5_NPC_TRANSPARENCY 0x400
#define V5_NPC_EXT_MEDIUM 0x800
#define V5_NPC_OPS 0x1000
#define V5_NPC_NAME_CHANGE 0x4000
#define V5_NPC_LEVEL_CHANGE 0x8000
#define V5_NPC_HEAD_CUSTOMISATION 0x20000
#define V5_NPC_SPOTANIM 0x40000
#define V5_NPC_HITMARKS 0x80000
#define V5_NPC_BAS_CHANGE 0x100000
#define V5_NPC_EXT_INT 0x200000
#define V5_NPC_HEADICON_CUSTOMISATION 0x400000
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
    struct PktPlayerInfoOp* op = player_op(r, PKT_PLAYER_INFO_OP_ABS_XZLEVEL);
    int32_t coord = g_player.coord[idx];

    if( !op )
        return;
    op->_local_xz_level.x = (int16_t)((coord >> 14) & 0x3fff);
    op->_local_xz_level.z = (int16_t)(coord & 0x3fff);
    op->_local_xz_level.level = (uint8_t)((coord >> 28) & 0x3);
    op->_local_xz_level.jump = jump ? true : false;
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
    if( !copy )
        return 0;
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
         * MOCK230_EXT_DEBUG on the server. */
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

                (void)tail_smart(data, len, &pos); /* delay */
                (void)tail_smart(data, len, &pos); /* limit */
                {
                    struct PktPlayerInfoOp* op =
                        player_op(r, PKT_PLAYER_INFO_OP_DAMAGE);

                    if( op )
                    {
                        op->_damage.damage_type = (uint8_t)type;
                        op->_damage.damage = (uint8_t)value;
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
            if( text )
            {
                memcpy(text, data + start, (size_t)(pos - start));
                text[pos - start] = '\0';
            }
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
        if( (flag & V5_PLAYER_APPEARANCE) != 0 )
        {
            if( !player_appearance_block(r, data, len, &pos) )
                return;
        }

        known = V5_PLAYER_EXT_SHORT | V5_PLAYER_EXT_MEDIUM | V5_PLAYER_HITMARKS |
                V5_PLAYER_SAY | V5_PLAYER_SEQUENCE | V5_PLAYER_APPEARANCE;
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

        if( (flag & V5_NPC_HITMARKS) != 0 )
        {
            /* NpcHitmarkEncoder: p1Alt1 count (`n + 128`), NOT the player
             * encoder's p1Alt3. */
            int hits = (data[pos++] - 128) & 0xff;

            for( int h = 0; h < hits && pos < len; h++ )
            {
                int type = tail_smart(data, len, &pos);
                int value = tail_smart(data, len, &pos);

                (void)tail_smart(data, len, &pos); /* delay */
                (void)tail_smart(data, len, &pos); /* limit */
                {
                    struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_DAMAGE);

                    if( op )
                    {
                        op->_damage.damage_type = (uint8_t)type;
                        op->_damage.damage = (uint8_t)value;
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
            if( text )
            {
                memcpy(text, data + start, (size_t)(pos - start));
                text[pos - start] = '\0';
            }
            pos++;
            {
                struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_SAY);

                if( op )
                    op->_say.text = text;
                else
                    free(text);
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

                pos++; /* slot */
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
                    }
                }
            }
        }
        if( (flag & V5_NPC_TRANSFORMATION) != 0 )
        {
            /* NpcTransformationEncoder: p2Alt3 id. */
            int lo = pos < len ? data[pos++] : 0;
            int hi = pos < len ? data[pos++] : 0;
            struct PktNpcInfoOp* op = npc_op(r, PKT_NPC_INFO_OP_CHANGE_TYPE);

            if( op )
                op->_change_type.npc_type = (hi << 8) | ((lo - 128) & 0xff);
        }

        known = V5_NPC_EXT_SHORT | V5_NPC_EXT_MEDIUM | V5_NPC_EXT_INT | V5_NPC_HITMARKS |
                V5_NPC_SEQUENCE | V5_NPC_SAY | V5_NPC_SPOTANIM | V5_NPC_TRANSFORMATION;
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
     * The 21-bit guard is the client's own: the terminator is only written when
     * extended-info bytes follow, so with nothing after the bit section the
     * loop has to stop on remaining width instead.
     */
    while( Net_BitBufferBitPos(&buf) + 21 < len * 8 )
    {
        int slot = Net_BitBufferGbits(&buf, 16);
        int extended;
        int dx;
        int dz;
        int npc_type;
        struct PktNpcInfoOp* op;

        if( slot == 0xffff )
            break;

        op = npc_op(&r, PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID);
        if( op )
            op->_bitvalue = (uint64_t)slot;

        (void)Net_BitBufferGbits(&buf, 1); /* spawn cycle */
        extended = Net_BitBufferGbits(&buf, 1);
        dx = Net_BitBufferGbits(&buf, 6);
        if( dx > 31 )
            dx -= 64;
        (void)Net_BitBufferGbits(&buf, 3); /* facing, applied on enter-view only */
        dz = Net_BitBufferGbits(&buf, 6);
        if( dz > 31 )
            dz -= 64;
        (void)Net_BitBufferGbits(&buf, 1); /* jump */
        npc_type = Net_BitBufferGbits(&buf, 14);

        op = npc_op(&r, PKT_NPC_INFO_OPBITS_NPCTYPE);
        if( op )
            op->_bitvalue = (uint64_t)npc_type;
        op = npc_op(&r, PKT_NPC_INFO_OP_DELTA_XZ);
        if( op )
        {
            op->_delta_xz.dx = (int16_t)dx;
            op->_delta_xz.dz = (int16_t)dz;
            op->_delta_xz.jump = false;
        }
        if( extended )
            npc_queue_extended(&r, list_idx);
        list_idx++;
    }

    npc_extended(&r, data, len, Net_BitBufferBytePos(&buf));
    return r.count;
}
