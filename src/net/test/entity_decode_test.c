/*
 * PLAYER_INFO / NPC_INFO bit-stream decode unit tests. A tiny bit-writer
 * (mirroring Net_BitBufferGbits' MSB-first packing) builds command streams;
 * the decoders must emit the expected op arrays. Also exercises the
 * appearance blob decode and the extended-info byte order.
 */
#include "net/bitbuffer.h"
#include "net/rev/packets/pkt_npc_info.h"
#include "net/rev/packets/pkt_player_appearance.h"
#include "net/rev/packets/pkt_player_info.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

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
    TEST_CHECK(tp);
    TEST_CHECK(tp->_local_xz_level.x == 40);
    TEST_CHECK(tp->_local_xz_level.z == 52);
    TEST_CHECK(tp->_local_xz_level.level == 0);
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
    TEST_CHECK(add && add->_bitvalue == 5);
    TEST_CHECK(delta && delta->_delta_xz.dx == 1 && delta->_delta_xz.dz == -1);
    TEST_CHECK(seq && seq->_sequence.sequence_id == 42 && seq->_sequence.delay == 3);
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
    TEST_CHECK(chat && chat->_chat.length == 3);
    /* The exact-move must have decoded — proving the CHAT payload was
     * consumed and did not desync the stream (the v0 bug this fixes). */
    TEST_CHECK(em);
    TEST_CHECK(em->_exactmove.start_x == 10 && em->_exactmove.end_z == 13);
    TEST_CHECK(em->_exactmove.end_cycle_delta == 5 && em->_exactmove.start_cycle_delta == 9);
    TEST_CHECK(em->_exactmove.facing == 2);
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
    int n;
    /* 0, 0 = the classic widths this stream was written with. */
    pkt_npc_info_reader_init(&reader, 0, 0);
    n = pkt_npc_info_reader_read(&reader, w.buf, bw_len(&w), ops, 64);

    struct PktNpcInfoOp const* add = find_npc_op(ops, n, PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID);
    struct PktNpcInfoOp const* type = find_npc_op(ops, n, PKT_NPC_INFO_OPBITS_NPCTYPE);
    struct PktNpcInfoOp const* change = find_npc_op(ops, n, PKT_NPC_INFO_OP_CHANGE_TYPE);
    struct PktNpcInfoOp const* spot = find_npc_op(ops, n, PKT_NPC_INFO_OP_SPOTANIM);
    TEST_CHECK(add && add->_bitvalue == 3);
    TEST_CHECK(type && type->_bitvalue == 100);
    TEST_CHECK(change && change->_change_type.npc_type == 200);
    TEST_CHECK(spot && spot->_spotanim.spotanim_id == 55);
    pkt_npc_info_ops_free(ops, n);
    printf("ok - npc add + CHANGE_TYPE + SPOTANIM\n");
}

/*
 * The npc-type field width is revision state, and getting it wrong does not
 * fail — it truncates into a different, usually valid, npc id. 3106 ("Man" in
 * the OSRS caches) read as 11 bits is 388. Both widths are asserted here so a
 * regression cannot hide behind an id that still resolves to something.
 */
static void
test_npc_type_width(void)
{
    struct PktNpcInfoOp const* type;
    struct PktNpcInfoReader reader;
    struct PktNpcInfoOp ops[64];
    struct BitWriter w = { 0 };
    int n;

    bw_bits(&w, 0, 8);     /* tracked count 0 */
    bw_bits(&w, 3, 14);    /* slot 3 */
    bw_bits(&w, 3106, 14); /* type — 14 bits wide */
    bw_bits(&w, 2, 5);     /* dx */
    bw_bits(&w, 0, 5);     /* dz */
    bw_bits(&w, 0, 1);     /* no extended info */
    bw_bits(&w, 16383, 14);
    /* Pad past the reader's 21-bit loop-guard margin so the terminator is
     * consumed rather than skipped. */
    bw_bits(&w, 0, 32);

    pkt_npc_info_reader_init(&reader, 14, 14);
    TEST_CHECK(reader.slot_bits == 14 && reader.type_bits == 14);
    n = pkt_npc_info_reader_read(&reader, w.buf, bw_len(&w), ops, 64);
    type = find_npc_op(ops, n, PKT_NPC_INFO_OPBITS_NPCTYPE);
    TEST_CHECK(type && type->_bitvalue == 3106);
    pkt_npc_info_ops_free(ops, n);

    /* The same bytes through the classic 11-bit reader take the top 11 bits of
     * the type field and read every later field off by three. That is what
     * made this silent: the decode still succeeds, it just describes a
     * different npc. */
    pkt_npc_info_reader_init(&reader, 14, 11);
    n = pkt_npc_info_reader_read(&reader, w.buf, bw_len(&w), ops, 64);
    type = find_npc_op(ops, n, PKT_NPC_INFO_OPBITS_NPCTYPE);
    TEST_CHECK(type && type->_bitvalue == (3106 >> 3));
    pkt_npc_info_ops_free(ops, n);

    /* An unstated width falls back to classic, never to zero bits. */
    pkt_npc_info_reader_init(&reader, 0, 0);
    TEST_CHECK(reader.slot_bits == PKT_NPC_INFO_SLOT_BITS_CLASSIC);
    TEST_CHECK(reader.type_bits == PKT_NPC_INFO_TYPE_BITS_CLASSIC);

    printf("ok - npc type width is per-revision (3106 at 14 bits, %d at 11)\n", 3106 >> 3);
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
    TEST_CHECK(ok);
    TEST_CHECK(app.gender == 0);
    /* The wire's tag becomes the canonical slot, not the number on the wire. */
    TEST_CHECK(app.slots[2] == Appearance_PackKit(18));
    TEST_CHECK(app.colors[3] == 3);
    TEST_CHECK(app.readyanim == 808);
    TEST_CHECK(app.turnanim == -1);
    TEST_CHECK(app.combat_level == 42);
    printf("ok - appearance blob decode\n");
}

/* ------------------------------------------------------------------ */
/* The 239 appearance block, and the encoding-independence claim        */
/* ------------------------------------------------------------------ */

static void
bw_word(struct BitWriter* w, int value)
{
    bw_byte(w, (value >> 8) & 0xff);
    bw_byte(w, value & 0xff);
}

static void
bw_jstr(struct BitWriter* w, char const* s)
{
    for( ; *s; s++ )
        bw_byte(w, (uint8_t)*s);
    bw_byte(w, 0);
}

/* One kit in slot 2 and one worn obj in slot 4 — enough to tell the two tags
 * apart, which is the whole risk in this block. */
enum
{
    FIXTURE_KIT_SLOT = 2,
    FIXTURE_KIT_ID = 18,
    FIXTURE_OBJ_SLOT = 4,
    FIXTURE_OBJ_ID = 1153,
    FIXTURE_READY_ANIM = 808,
    FIXTURE_COMBAT_LEVEL = 42,
};

/* The block the mock server's put_appearance_v5 writes, built independently
 * here so the decoder is tested against the layout rather than against the
 * writer's helper functions. `obj_tag` is a parameter so the test can hand the
 * v5 reader a classic-tagged word and watch what it becomes. */
static void
build_v5_blob(struct BitWriter* w, int obj_tag, int customisation_flag)
{
    bw_byte(w, 0);   /* gender */
    bw_byte(w, 255); /* skull icon: none */
    bw_byte(w, 255); /* head icon: none */
    for( int i = 0; i < 12; i++ ) /* equipment */
    {
        if( i == FIXTURE_KIT_SLOT )
            bw_word(w, 0x100 + FIXTURE_KIT_ID);
        else if( i == FIXTURE_OBJ_SLOT )
            bw_word(w, obj_tag + FIXTURE_OBJ_ID);
        else
            bw_byte(w, 0);
    }
    for( int i = 0; i < 12; i++ ) /* identKit */
    {
        if( i == FIXTURE_KIT_SLOT )
            bw_word(w, 0x100 + FIXTURE_KIT_ID);
        else
            bw_byte(w, 0);
    }
    for( int i = 0; i < 5; i++ )
        bw_byte(w, i); /* colours */
    bw_word(w, FIXTURE_READY_ANIM);
    for( int i = 0; i < 6; i++ )
        bw_word(w, 65535);
    bw_jstr(w, "test");
    bw_byte(w, FIXTURE_COMBAT_LEVEL);
    bw_word(w, 99); /* skill level */
    bw_byte(w, 0);  /* hidden */
    bw_word(w, customisation_flag);
    bw_jstr(w, "");
    bw_jstr(w, "");
    bw_jstr(w, "");
    bw_byte(w, 0); /* pronoun */
}

/* The same appearance in the classic block. */
static void
build_classic_blob(struct BitWriter* w)
{
    bw_byte(w, 0); /* gender */
    bw_byte(w, 0); /* head icons */
    for( int i = 0; i < 12; i++ )
    {
        if( i == FIXTURE_KIT_SLOT )
            bw_word(w, 0x100 + FIXTURE_KIT_ID);
        else if( i == FIXTURE_OBJ_SLOT )
            bw_word(w, 0x200 + FIXTURE_OBJ_ID);
        else
            bw_byte(w, 0);
    }
    for( int i = 0; i < 5; i++ )
        bw_byte(w, i);
    bw_word(w, FIXTURE_READY_ANIM);
    for( int i = 0; i < 6; i++ )
        bw_word(w, 65535);
    for( int i = 0; i < 8; i++ )
        bw_byte(w, 0); /* name37 */
    bw_byte(w, FIXTURE_COMBAT_LEVEL);
}

static void
test_appearance_v5_decode(void)
{
    struct BitWriter w = { 0 };
    struct PktPlayerAppearance app;

    build_v5_blob(&w, APPEARANCE_WIRE_OBJ_TAG_V5, 0);
    TEST_CHECK(PktPlayerAppearance_DecodeAs(&app, APPEARANCE_ENC_V5, w.buf, bw_len(&w)));

    /* Both tags land in the canonical vocabulary, not on the wire's numbers. */
    TEST_CHECK(app.slots[FIXTURE_KIT_SLOT] == Appearance_PackKit(FIXTURE_KIT_ID));
    TEST_CHECK(app.slots[FIXTURE_OBJ_SLOT] == Appearance_PackObj(FIXTURE_OBJ_ID));
    TEST_CHECK(Appearance_SlotObj(app.slots[FIXTURE_OBJ_SLOT]) == FIXTURE_OBJ_ID);
    TEST_CHECK(app.identkit[FIXTURE_KIT_SLOT] == Appearance_PackKit(FIXTURE_KIT_ID));
    TEST_CHECK(app.identkit[FIXTURE_OBJ_SLOT] == 0);
    TEST_CHECK(app.skull_icon == -1);
    TEST_CHECK(app.headicon == 0);
    TEST_CHECK(app.colors[3] == 3);
    TEST_CHECK(app.readyanim == FIXTURE_READY_ANIM);
    TEST_CHECK(app.turnanim == -1);
    TEST_CHECK(strcmp(app.name, "test") == 0);
    TEST_CHECK(app.combat_level == FIXTURE_COMBAT_LEVEL);
    TEST_CHECK(app.skill_level == 99);
    TEST_CHECK(app.hidden == 0);
    TEST_CHECK(app.npc_id == -1);
    printf("ok - 239 appearance block decode\n");
}

static void
test_appearance_encoding_independent(void)
{
    struct BitWriter classic = { 0 };
    struct BitWriter v5 = { 0 };
    struct PktPlayerAppearance a_classic;
    struct PktPlayerAppearance a_v5;

    build_classic_blob(&classic);
    build_v5_blob(&v5, APPEARANCE_WIRE_OBJ_TAG_V5, 0);

    TEST_CHECK(PktPlayerAppearance_Decode(&a_classic, classic.buf, bw_len(&classic)));
    TEST_CHECK(PktPlayerAppearance_DecodeAs(&a_v5, APPEARANCE_ENC_V5, v5.buf, bw_len(&v5)));

    /* Two blocks of different length, different field order and different tags
     * describing one player: everything the client renders from must match. */
    TEST_CHECK(bw_len(&classic) != bw_len(&v5));
    TEST_CHECK(memcmp(a_classic.slots, a_v5.slots, sizeof(a_classic.slots)) == 0);
    TEST_CHECK(memcmp(a_classic.colors, a_v5.colors, sizeof(a_classic.colors)) == 0);
    TEST_CHECK(a_classic.gender == a_v5.gender);
    TEST_CHECK(a_classic.readyanim == a_v5.readyanim);
    TEST_CHECK(a_classic.turnanim == a_v5.turnanim);
    TEST_CHECK(a_classic.combat_level == a_v5.combat_level);
    TEST_CHECK(a_classic.headicon == a_v5.headicon);
    printf("ok - classic and 239 blocks decode to the same appearance\n");
}

static void
test_appearance_tag_is_per_encoding(void)
{
    struct BitWriter w = { 0 };
    struct PktPlayerAppearance app;

    /*
     * The silent failure this module exists to prevent: the classic worn-obj
     * tag read under the 239 encoding is not an error — 0x200 + obj sits inside
     * the 239 kit range, so it decodes as a perfectly valid body part. The tag
     * is therefore never spelled at a call site, only chosen by encoding.
     */
    build_v5_blob(&w, APPEARANCE_WIRE_OBJ_TAG_CLASSIC, 0);
    TEST_CHECK(PktPlayerAppearance_DecodeAs(&app, APPEARANCE_ENC_V5, w.buf, bw_len(&w)));
    TEST_CHECK(Appearance_SlotKind(app.slots[FIXTURE_OBJ_SLOT]) == APPEARANCE_SLOT_KIT);
    TEST_CHECK(Appearance_SlotObj(app.slots[FIXTURE_OBJ_SLOT]) == -1);
    printf("ok - the worn-obj tag is per-encoding (classic tag reads as a kit at 239)\n");
}

static void
test_appearance_rejects_undecodable(void)
{
    struct BitWriter w = { 0 };
    struct PktPlayerAppearance app;

    /* A per-obj customisation block follows a set flag bit, and it is variable
     * length — guessing its size reads every later field at the wrong offset,
     * so the block is refused whole. */
    build_v5_blob(&w, APPEARANCE_WIRE_OBJ_TAG_V5, 1 << 12);
    TEST_CHECK(!PktPlayerAppearance_DecodeAs(&app, APPEARANCE_ENC_V5, w.buf, bw_len(&w)));

    /* Truncation is rejected rather than silently decoding a naked player:
     * rsbuffer's getters return 0 past the end instead of failing. */
    {
        struct BitWriter full = { 0 };
        build_v5_blob(&full, APPEARANCE_WIRE_OBJ_TAG_V5, 0);
        TEST_CHECK(!PktPlayerAppearance_DecodeAs(&app, APPEARANCE_ENC_V5, full.buf, 10));
    }
    {
        struct BitWriter full = { 0 };
        build_classic_blob(&full);
        TEST_CHECK(!PktPlayerAppearance_Decode(&app, full.buf, bw_len(&full) - 1));
    }
    printf("ok - undecodable and truncated appearance blocks are refused\n");
}

static void
test_appearance_transmog(void)
{
    struct BitWriter w = { 0 };
    struct PktAppearanceOp ops[PKT_APPEARANCE_OPS_MAX];
    int n;
    int saw_transmog = 0;
    int visible_slots = 0;

    bw_byte(&w, 0);
    bw_byte(&w, 255);
    bw_byte(&w, 255);
    /* equipment[0] == 65535 is not a slot: an npc id follows and the array ENDS
     * there — the other eleven entries are never written. */
    bw_word(&w, APPEARANCE_WIRE_TRANSMOG);
    bw_word(&w, 3106);
    for( int i = 0; i < 12; i++ )
        bw_byte(&w, 0); /* identKit */
    for( int i = 0; i < 5; i++ )
        bw_byte(&w, 0);
    for( int i = 0; i < 7; i++ )
        bw_word(&w, 65535);
    bw_jstr(&w, "npc");
    bw_byte(&w, 0);
    bw_word(&w, 0);
    bw_byte(&w, 0);
    bw_word(&w, 0);
    bw_jstr(&w, "");
    bw_jstr(&w, "");
    bw_jstr(&w, "");
    bw_byte(&w, 0);

    n = pkt_appearance_read(APPEARANCE_ENC_V5, w.buf, bw_len(&w), ops, PKT_APPEARANCE_OPS_MAX);
    TEST_CHECK(n > 0);
    for( int i = 0; i < n; i++ )
    {
        if( ops[i].kind == PKT_APPEARANCE_OP_TRANSMOG )
        {
            saw_transmog = 1;
            TEST_CHECK(ops[i]._value == 3106);
        }
        if( ops[i].kind == PKT_APPEARANCE_OP_SLOT &&
            ops[i]._slot.layer == APPEARANCE_LAYER_VISIBLE )
            visible_slots++;
    }
    TEST_CHECK(saw_transmog);
    TEST_CHECK(visible_slots == 0);
    printf("ok - 239 transmog ends the equipment array\n");
}

/*
 * The op array is sized by the CALLER; how many ops a packet asks for is chosen
 * by the SERVER. next_op used to bound the two with an assert, which the
 * shipping lane compiles out (-DNDEBUG), and the extended-info loop tests
 * capacity once per entry and then emits one op per bit of a server-supplied
 * mask -- so a full mask wrote past the end of the array by the width of the
 * mask. Runs the same stream as the test above into an array too small for it,
 * with a canary behind it.
 */
static void
test_npc_ops_capacity_is_a_guard_not_an_assert(void)
{
    struct BitWriter w = { 0 };
    bw_bits(&w, 0, 8);
    bw_bits(&w, 3, 14);
    bw_bits(&w, 100, 11);
    bw_bits(&w, 2, 5);
    bw_bits(&w, 0, 5);
    bw_bits(&w, 1, 1);
    bw_bits(&w, 16383, 14);
    /* Every mask bit this revision has, so the entry emits as many ops as it
     * possibly can from the single capacity test at the top of the loop. */
    bw_byte(&w, 0xff);
    bw_byte(&w, 0xff);
    for( int i = 0; i < 64; i++ )
        bw_byte(&w, 0x00);

    enum
    {
        OPS_CAP = 3
    };
    struct
    {
        struct PktNpcInfoOp ops[OPS_CAP];
        unsigned char canary[64];
    } probe;
    struct PktNpcInfoReader reader;
    int n;

    memset(&probe, 0, sizeof(probe));
    memset(probe.canary, 0xA5, sizeof(probe.canary));

    pkt_npc_info_reader_init(&reader, 0, 0);
    n = pkt_npc_info_reader_read(&reader, w.buf, bw_len(&w), probe.ops, OPS_CAP);

    TEST_CHECK(n <= OPS_CAP);
    TEST_CHECK(reader.overflowed);
    for( size_t i = 0; i < sizeof(probe.canary); i++ )
        TEST_CHECK(probe.canary[i] == 0xA5);

    pkt_npc_info_ops_free(probe.ops, n);
    printf("ok - npc op overflow stays inside the caller's array\n");
}

/*
 * Same shape one layer down: the bit cursor is walked by counts the packet
 * supplies, so a short packet ran it off the end of the buffer. That bound was
 * an assert too, i.e. absent from the shipping build.
 */
static void
test_bitbuffer_refuses_reads_past_the_end(void)
{
    uint8_t data[2] = { 0xAB, 0xCD };
    struct Net_BitBuffer buf;

    Net_BitBufferInit(&buf, data, (int)sizeof(data));
    TEST_CHECK(!buf.overrun);
    TEST_CHECK(Net_BitBufferGbits(&buf, 16) == 0xABCD);
    TEST_CHECK(!buf.overrun);

    /* One bit past the end: refused, reported, and zero rather than whatever
     * follows the buffer. */
    TEST_CHECK(Net_BitBufferGbits(&buf, 1) == 0);
    TEST_CHECK(buf.overrun);

    /* A read that straddles the end is refused whole, not partially served. */
    Net_BitBufferInit(&buf, data, (int)sizeof(data));
    TEST_CHECK(Net_BitBufferGbits(&buf, 12) == 0xABC);
    TEST_CHECK(Net_BitBufferGbits(&buf, 8) == 0);
    TEST_CHECK(buf.overrun);

    printf("ok - bit reads past the packet end are refused\n");
}

int
main(void)
{
    test_npc_ops_capacity_is_a_guard_not_an_assert();
    test_bitbuffer_refuses_reads_past_the_end();
    test_player_local_teleport();
    test_player_new_walk_with_seq();
    test_player_exact_move_and_chat_skip();
    test_npc_add_change_type_spotanim();
    test_npc_type_width();
    test_appearance_decode();
    test_appearance_v5_decode();
    test_appearance_encoding_independent();
    test_appearance_tag_is_per_encoding();
    test_appearance_rejects_undecodable();
    test_appearance_transmog();
    printf("entity-decode: all tests passed\n");
    return 0;
}
