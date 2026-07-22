/*
 * PLAYER_INFO / NPC_INFO bit-stream decode unit tests. A tiny bit-writer
 * (mirroring Net_BitBufferGbits' MSB-first packing) builds command streams;
 * the decoders must emit the expected op arrays. Also exercises the
 * appearance blob decode and the extended-info byte order.
 */
#include "net/bitbuffer.h"
#include "net/rev/pkt_npc_info.h"
#include "net/rev/pkt_player_appearance.h"
#include "net/rev/pkt_player_info.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MSB-first bit writer matching the reader's packing. */
struct BitWriter
{
    uint8_t buf[4096];
    int bit_pos;
};

static void
bw_bits(struct BitWriter* w, int value, int count)
{
    for( int i = count - 1; i >= 0; i-- )
    {
        int bit = (value >> i) & 1;
        int byte = w->bit_pos >> 3;
        int off = 7 - (w->bit_pos & 7);
        if( bit )
            w->buf[byte] |= (uint8_t)(1 << off);
        w->bit_pos++;
    }
}

static void
bw_align(struct BitWriter* w)
{
    w->bit_pos = ((w->bit_pos + 7) / 8) * 8;
}

static void
bw_byte(struct BitWriter* w, int value)
{
    bw_align(w);
    w->buf[w->bit_pos >> 3] = (uint8_t)value;
    w->bit_pos += 8;
}

static int
bw_len(struct BitWriter const* w)
{
    return (w->bit_pos + 7) / 8;
}

static struct PktPlayerInfoOp const*
find_player_op(
    struct PktPlayerInfoOp const* ops,
    int count,
    enum PktPlayerInfoOpKind kind)
{
    for( int i = 0; i < count; i++ )
        if( ops[i].kind == kind )
            return &ops[i];
    return NULL;
}

static void
test_player_local_teleport(void)
{
    struct BitWriter w = { 0 };
    /* Local: has-update 1, op 3 (teleport), level 0, sx 40, sz 52, jump 0,
     * no extended. */
    bw_bits(&w, 1, 1);
    bw_bits(&w, 3, 2);
    bw_bits(&w, 0, 2);
    bw_bits(&w, 40, 7);
    bw_bits(&w, 52, 7);
    bw_bits(&w, 0, 1);
    bw_bits(&w, 0, 1); /* no extended info */
    /* Tracked count 0. */
    bw_bits(&w, 0, 8);
    /* New players: terminator 2047. */
    bw_bits(&w, 2047, 11);

    struct PktPlayerInfoReader reader;
    struct PktPlayerInfoOp ops[64];
    int n = pkt_player_info_reader_read(&reader, w.buf, bw_len(&w), ops, 64);

    struct PktPlayerInfoOp const* tp =
        find_player_op(ops, n, PKT_PLAYER_INFO_OP_LOCAL_XZLEVEL);
    assert(tp);
    assert(tp->_local_xz_level.x == 40);
    assert(tp->_local_xz_level.z == 52);
    assert(tp->_local_xz_level.level == 0);
    pkt_player_info_ops_free(ops, n);
    printf("ok - player local teleport block\n");
}

static void
test_player_new_walk_with_seq(void)
{
    struct BitWriter w = { 0 };
    /* Local: no update. */
    bw_bits(&w, 0, 1);
    /* Tracked count 0. */
    bw_bits(&w, 0, 8);
    /* New player pid 5, dx 1, dz -1 (31), jump 0, has-ext 1. */
    bw_bits(&w, 5, 11);
    bw_bits(&w, 1, 5);
    bw_bits(&w, 31, 5); /* -1 */
    bw_bits(&w, 0, 1);
    bw_bits(&w, 1, 1); /* extended */
    /* Terminator. */
    bw_bits(&w, 2047, 11);
    /* Extended: SEQUENCE mask (0x02): seq 42, delay 3. */
    bw_byte(&w, 0x02);
    bw_byte(&w, 0x00); /* seq high */
    bw_byte(&w, 42);   /* seq low */
    bw_byte(&w, 3);    /* delay */

    struct PktPlayerInfoReader reader;
    struct PktPlayerInfoOp ops[64];
    int n = pkt_player_info_reader_read(&reader, w.buf, bw_len(&w), ops, 64);

    struct PktPlayerInfoOp const* add =
        find_player_op(ops, n, PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID);
    struct PktPlayerInfoOp const* delta = find_player_op(ops, n, PKT_PLAYER_INFO_OP_DELTA_XZ);
    struct PktPlayerInfoOp const* seq = find_player_op(ops, n, PKT_PLAYER_INFO_OP_SEQUENCE);
    assert(add && add->_bitvalue == 5);
    assert(delta && delta->_delta_xz.dx == 1 && delta->_delta_xz.dz == -1);
    assert(seq && seq->_sequence.sequence_id == 42 && seq->_sequence.delay == 3);
    pkt_player_info_ops_free(ops, n);
    printf("ok - player new-add + walk-delta + sequence mask\n");
}

static void
test_player_exact_move_and_chat_skip(void)
{
    struct BitWriter w = { 0 };
    bw_bits(&w, 0, 1); /* local no update */
    bw_bits(&w, 0, 8); /* tracked 0 */
    bw_bits(&w, 9, 11); /* new pid 9 */
    bw_bits(&w, 0, 5);
    bw_bits(&w, 0, 5);
    bw_bits(&w, 0, 1);
    bw_bits(&w, 1, 1); /* extended */
    bw_bits(&w, 2047, 11);
    /* Extended: CHAT (0x40) then EXACT_MOVE (0x200) -> needs BIG (0x80). */
    bw_byte(&w, 0x40 | 0x80); /* low byte: CHAT + BIG */
    bw_byte(&w, 0x02);        /* high byte: 0x200 EXACT_MOVE */
    /* CHAT: colourEffect g2, type g1, length g1, then length bytes. */
    bw_byte(&w, 0x00);
    bw_byte(&w, 0x00);
    bw_byte(&w, 0); /* type */
    bw_byte(&w, 3); /* length */
    bw_byte(&w, 0xAA);
    bw_byte(&w, 0xBB);
    bw_byte(&w, 0xCC);
    /* EXACT_MOVE: sx,sz,ex,ez, endDelta(g2), startDelta(g2), facing. */
    bw_byte(&w, 10); /* sx */
    bw_byte(&w, 11); /* sz */
    bw_byte(&w, 12); /* ex */
    bw_byte(&w, 13); /* ez */
    bw_byte(&w, 0);
    bw_byte(&w, 5); /* endDelta = 5 */
    bw_byte(&w, 0);
    bw_byte(&w, 9); /* startDelta = 9 */
    bw_byte(&w, 2); /* facing */

    struct PktPlayerInfoReader reader;
    struct PktPlayerInfoOp ops[64];
    int n = pkt_player_info_reader_read(&reader, w.buf, bw_len(&w), ops, 64);

    struct PktPlayerInfoOp const* chat = find_player_op(ops, n, PKT_PLAYER_INFO_OP_CHAT);
    struct PktPlayerInfoOp const* em = find_player_op(ops, n, PKT_PLAYER_INFO_OP_EXACT_MOVE);
    assert(chat && chat->_chat.length == 3);
    /* The exact-move must have decoded — proving the CHAT payload was
     * consumed and did not desync the stream (the v0 bug this fixes). */
    assert(em);
    assert(em->_exactmove.start_x == 10 && em->_exactmove.end_z == 13);
    assert(em->_exactmove.end_cycle_delta == 5 && em->_exactmove.start_cycle_delta == 9);
    assert(em->_exactmove.facing == 2);
    pkt_player_info_ops_free(ops, n);
    printf("ok - player CHAT-skip fix + EXACT_MOVE byte order\n");
}

static struct PktNpcInfoOp const*
find_npc_op(
    struct PktNpcInfoOp const* ops,
    int count,
    enum PktNpcInfoOpKind kind)
{
    for( int i = 0; i < count; i++ )
        if( ops[i].kind == kind )
            return &ops[i];
    return NULL;
}

static void
test_npc_add_change_type_spotanim(void)
{
    struct BitWriter w = { 0 };
    /* Tracked count 0. */
    bw_bits(&w, 0, 8);
    /* New npc: slot 3, type 100, dx 2, dz 0, has-ext 1. */
    bw_bits(&w, 3, 14);
    bw_bits(&w, 100, 11);
    bw_bits(&w, 2, 5);
    bw_bits(&w, 0, 5);
    bw_bits(&w, 1, 1);
    /* Terminator 16383. */
    bw_bits(&w, 16383, 14);
    /* Extended: CHANGE_TYPE (0x20) + SPOTANIM (0x40). */
    bw_byte(&w, 0x20 | 0x40);
    bw_byte(&w, 0x00);
    bw_byte(&w, 200); /* change type -> npc 200 */
    bw_byte(&w, 0x00);
    bw_byte(&w, 55); /* spotanim id 55 */
    bw_byte(&w, 0x00);
    bw_byte(&w, 0x01);
    bw_byte(&w, 0x00);
    bw_byte(&w, 0x40); /* heightDelay g4 = 0x00010040 */

    struct PktNpcInfoReader reader;
    struct PktNpcInfoOp ops[64];
    int n = pkt_npc_info_reader_read(&reader, w.buf, bw_len(&w), ops, 64);

    struct PktNpcInfoOp const* add = find_npc_op(ops, n, PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID);
    struct PktNpcInfoOp const* type = find_npc_op(ops, n, PKT_NPC_INFO_OPBITS_NPCTYPE);
    struct PktNpcInfoOp const* change = find_npc_op(ops, n, PKT_NPC_INFO_OP_CHANGE_TYPE);
    struct PktNpcInfoOp const* spot = find_npc_op(ops, n, PKT_NPC_INFO_OP_SPOTANIM);
    assert(add && add->_bitvalue == 3);
    assert(type && type->_bitvalue == 100);
    assert(change && change->_change_type.npc_type == 200);
    assert(spot && spot->_spotanim.spotanim_id == 55);
    pkt_npc_info_ops_free(ops, n);
    printf("ok - npc add + CHANGE_TYPE + SPOTANIM\n");
}

static void
test_appearance_decode(void)
{
    struct BitWriter w = { 0 };
    /* Appearance blob (byte layout). */
    bw_byte(&w, 0);   /* gender male */
    bw_byte(&w, 0);   /* headicon */
    /* 12 slots: slot 2 (torso) = idk 0x100+18; rest empty. */
    for( int i = 0; i < 12; i++ )
    {
        if( i == 2 )
        {
            bw_byte(&w, 0x01); /* msb -> 0x100 */
            bw_byte(&w, 18);
        }
        else
        {
            bw_byte(&w, 0);
        }
    }
    for( int i = 0; i < 5; i++ )
        bw_byte(&w, i); /* colours */
    /* 7 anim g2: readyanim 808, rest 65535 (-1). */
    bw_byte(&w, 808 >> 8);
    bw_byte(&w, 808 & 0xff);
    for( int i = 0; i < 6; i++ )
    {
        bw_byte(&w, 0xff);
        bw_byte(&w, 0xff);
    }
    /* name g8 (base37 for "test"). */
    for( int i = 0; i < 8; i++ )
        bw_byte(&w, 0);
    bw_byte(&w, 42); /* combat level */

    struct PktPlayerAppearance app;
    int ok = PktPlayerAppearance_Decode(&app, w.buf, bw_len(&w));
    assert(ok);
    assert(app.gender == 0);
    assert(app.slots[2] == 0x100 + 18);
    assert(app.colors[3] == 3);
    assert(app.readyanim == 808);
    assert(app.turnanim == -1);
    assert(app.combat_level == 42);
    printf("ok - appearance blob decode\n");
}

int
main(void)
{
    test_player_local_teleport();
    test_player_new_walk_with_seq();
    test_player_exact_move_and_chat_skip();
    test_npc_add_change_type_spotanim();
    test_appearance_decode();
    printf("entity-decode: all tests passed\n");
    return 0;
}
