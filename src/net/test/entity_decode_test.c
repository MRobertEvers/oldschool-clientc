/*
 * PLAYER_INFO / NPC_INFO bit-stream decode unit tests. A tiny bit-writer
 * (mirroring rsprot_gbits' MSB-first packing) builds command streams;
 * the decoders must emit the expected op arrays. Also exercises the
 * appearance blob decode and the extended-info byte order.
 */
#include "net/rev/packets/pkt_player_appearance.h"
#include "streams/info_rs.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The readers write a list; these hand a test its ops, the strings in one
 * arena the next read reuses, and whether the list overflowed. */
static uint8_t g_info_bytes[1 << 16];
static int g_overflow;

/* 254's npc record widths (info_rs.c). */
static NpcInfoRs const k_classic = { .slot_bits = 14, .type_bits = 11 };

static int
read_into(
    int (*read)(uint8_t const*, int, RsprotInfoList*),
    uint8_t const* data, int len, struct RsprotInfoOp* ops, int cap)
{
    RsprotInfoList list;
    int n;

    rsprot_info_list_init(&list, ops, cap, g_info_bytes, (int)sizeof(g_info_bytes));
    n = read(data, len, &list);
    g_overflow = list.overflow;
    return n;
}

static int
player_read(uint8_t const* data, int len, struct RsprotInfoOp* ops, int cap)
{
    return read_into(player_info_rs254, data, len, ops, cap);
}

static int
lc289_read(uint8_t const* data, int len, struct RsprotInfoOp* ops, int cap)
{
    return read_into(player_info_rs289, data, len, ops, cap);
}

static int
npc_read(
    NpcInfoRs const* k, uint8_t const* data, int len, struct RsprotInfoOp* ops, int cap)
{
    RsprotInfoList list;
    int n;

    rsprot_info_list_init(&list, ops, cap, g_info_bytes, (int)sizeof(g_info_bytes));
    n = npc_info_rs_v1(k, data, len, &list);
    g_overflow = list.overflow;
    return n;
}

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

static struct RsprotInfoOp const*
find_player_op(
    struct RsprotInfoOp const* ops,
    int count,
    RsprotInfoOpKind kind)
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

    struct RsprotInfoOp ops[64];
    int n = player_read(w.buf, bw_len(&w), ops, 64);

    struct RsprotInfoOp const* tp =
        find_player_op(ops, n, RSPROT_OP_LOCAL_XZLEVEL);
    TEST_CHECK(tp);
    TEST_CHECK(tp->xz_level.x == 40);
    TEST_CHECK(tp->xz_level.z == 52);
    TEST_CHECK(tp->xz_level.level == 0);
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

    struct RsprotInfoOp ops[64];
    int n = player_read(w.buf, bw_len(&w), ops, 64);

    struct RsprotInfoOp const* add =
        find_player_op(ops, n, RSPROT_OP_ADD_NEW);
    struct RsprotInfoOp const* delta = find_player_op(ops, n, RSPROT_OP_DELTA_XZ);
    struct RsprotInfoOp const* seq = find_player_op(ops, n, RSPROT_OP_SEQUENCE);
    TEST_CHECK(add && add->value == 5);
    TEST_CHECK(delta && delta->delta_xz.dx == 1 && delta->delta_xz.dz == -1);
    TEST_CHECK(seq && seq->sequence.sequence_id == 42 && seq->sequence.delay == 3);
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

    struct RsprotInfoOp ops[64];
    int n = player_read(w.buf, bw_len(&w), ops, 64);

    struct RsprotInfoOp const* chat = find_player_op(ops, n, RSPROT_OP_CHAT);
    struct RsprotInfoOp const* em = find_player_op(ops, n, RSPROT_OP_EXACT_MOVE);
    TEST_CHECK(chat && chat->chat.length == 3);
    /* The exact-move must have decoded — proving the CHAT payload was
     * consumed and did not desync the stream (the v0 bug this fixes). */
    TEST_CHECK(em);
    TEST_CHECK(em->exact_move.start_x == 10 && em->exact_move.end_z == 13);
    TEST_CHECK(em->exact_move.end_cycle_delta == 5 && em->exact_move.start_cycle_delta == 9);
    TEST_CHECK(em->exact_move.facing == 2);
    printf("ok - player CHAT-skip fix + EXACT_MOVE byte order\n");
}

static struct RsprotInfoOp const*
find_npc_op(
    struct RsprotInfoOp const* ops,
    int count,
    RsprotInfoOpKind kind)
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

    struct RsprotInfoOp ops[64];
    int n;
    /* 0, 0 = the classic widths this stream was written with. */
    n = npc_read(&k_classic, w.buf, bw_len(&w), ops, 64);

    struct RsprotInfoOp const* add = find_npc_op(ops, n, RSPROT_OP_ADD_NEW);
    struct RsprotInfoOp const* type = find_npc_op(ops, n, RSPROT_OP_NPCTYPE);
    struct RsprotInfoOp const* change = find_npc_op(ops, n, RSPROT_OP_CHANGE_TYPE);
    struct RsprotInfoOp const* spot = find_npc_op(ops, n, RSPROT_OP_SPOTANIM);
    TEST_CHECK(add && add->value == 3);
    TEST_CHECK(type && type->value == 100);
    TEST_CHECK(change && change->change_type.npc_type == 200);
    TEST_CHECK(spot && spot->spotanim.spotanim_id == 55);
    printf("ok - npc add + CHANGE_TYPE + SPOTANIM\n");
}

/*
 * The npc-type field width is revision state, and getting it wrong does not
 * fail — it truncates into a different, usually valid, npc id. 3106 ("Man" in
 * the OSRS caches) read as 11 bits is 388. Both widths are asserted here so a
 * regression cannot hide behind an id that still resolves to something.
 */
/*
 * 289's new-npc record carries a jump bit between (dx, dz) and the
 * extended-info bit (npc_info_rs_v2). Read through the 289 entry point, the
 * record's fields land where they were written and the jump survives.
 */
static void
test_npc_rs289_new_npc_jump(void)
{
    struct RsprotInfoOp ops[64];
    struct BitWriter w = { 0 };
    struct RsprotInfoOp const* type;
    struct RsprotInfoOp const* delta;
    int n;

    bw_bits(&w, 0, 8);    /* tracked count 0 */
    bw_bits(&w, 5, 14);   /* slot 5 */
    bw_bits(&w, 100, 11); /* type 100 */
    bw_bits(&w, 1, 5);    /* dx */
    bw_bits(&w, 2, 5);    /* dz */
    bw_bits(&w, 1, 1);    /* jump */
    bw_bits(&w, 0, 1);    /* no extended info */
    bw_bits(&w, 16383, 14);
    bw_bits(&w, 0, 32);   /* past the 21-bit loop guard */

    n = read_into(npc_info_rs289, w.buf, bw_len(&w), ops, 64);
    type = find_npc_op(ops, n, RSPROT_OP_NPCTYPE);
    delta = find_npc_op(ops, n, RSPROT_OP_DELTA_XZ);
    TEST_CHECK(type && type->value == 100);
    TEST_CHECK(delta && delta->delta_xz.dx == 1 && delta->delta_xz.dz == 2);
    TEST_CHECK(delta && delta->delta_xz.jump);
    printf("ok - rs289 new-npc record carries the jump bit\n");
}

static void
test_npc_type_width(void)
{
    struct RsprotInfoOp const* type;
    struct RsprotInfoOp ops[64];
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

    {
        NpcInfoRs const wide = { .slot_bits = 14, .type_bits = 14 };

        n = npc_read(&wide, w.buf, bw_len(&w), ops, 64);
    }
    type = find_npc_op(ops, n, RSPROT_OP_NPCTYPE);
    TEST_CHECK(type && type->value == 3106);

    /* The same bytes through the classic 11-bit reader take the top 11 bits of
     * the type field and read every later field off by three. That is what
     * made this silent: the decode still succeeds, it just describes a
     * different npc. */
    n = npc_read(&k_classic, w.buf, bw_len(&w), ops, 64);
    type = find_npc_op(ops, n, RSPROT_OP_NPCTYPE);
    TEST_CHECK(type && type->value == (3106 >> 3));

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
static void build_v5_blob_with(
    struct BitWriter* w, int obj_tag, int customisation_flag, int model_bytes);

static void
build_v5_blob(struct BitWriter* w, int obj_tag, int customisation_flag)
{
    build_v5_blob_with(w, obj_tag, customisation_flag, 0);
}

/* `model_bytes` > 0 writes one obj customisation block per set flag bit, in
 * RSProx's 227-240 shape: slot flag 0x1 | 0x4, a recolour index pair with one
 * replacement (index 2; the second index is 15, so none), and two worn model
 * ids `model_bytes` wide. */
static void
build_v5_blob_with(struct BitWriter* w, int obj_tag, int customisation_flag, int model_bytes)
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
    for( int i = 0; model_bytes > 0 && i < 12; i++ )
    {
        if( !((customisation_flag >> (12 - i)) & 1) )
            continue;
        bw_byte(w, 0x1 | 0x4);
        bw_byte(w, 0xF2);
        bw_word(w, 1234);
        for( int m = 0; m < 2; m++ )
        {
            if( model_bytes == 4 )
                bw_word(w, 0);
            bw_word(w, 4321);
        }
    }
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

/* 222-223 wear objs at the classic 0x200 in the 239-shaped block: the word a
 * 223 server writes for obj 1153 is an obj there, and a kit to v3 (224+),
 * whose kit range runs to 0x800 -- kit 1409, past any table, drawing nothing. */
static void
test_appearance_v2_obj_tag(void)
{
    struct BitWriter w = { 0 };
    struct PktPlayerAppearance v2;
    struct PktPlayerAppearance v3;

    build_v5_blob(&w, APPEARANCE_WIRE_OBJ_TAG_CLASSIC, 0);
    TEST_CHECK(PktPlayerAppearance_DecodeAs(&v2, APPEARANCE_ENC_V2, w.buf, bw_len(&w)));
    TEST_CHECK(v2.slots[FIXTURE_OBJ_SLOT] == Appearance_PackObj(FIXTURE_OBJ_ID));
    TEST_CHECK(v2.slots[FIXTURE_KIT_SLOT] == Appearance_PackKit(FIXTURE_KIT_ID));
    TEST_CHECK(v2.identkit[FIXTURE_KIT_SLOT] == Appearance_PackKit(FIXTURE_KIT_ID));
    TEST_CHECK(PktPlayerAppearance_DecodeAs(&v3, APPEARANCE_ENC_V3, w.buf, bw_len(&w)));
    TEST_CHECK(v3.slots[FIXTURE_OBJ_SLOT] ==
               Appearance_PackKit(APPEARANCE_WIRE_OBJ_TAG_CLASSIC + FIXTURE_OBJ_ID -
                                  APPEARANCE_WIRE_KIT_TAG));
    printf("ok - 222-223 worn objs are 0x200 + obj\n");
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

    /* A per-obj customisation block follows a set flag bit. It delimits
     * itself (RSProx decodeAppearance, 227-240), so a block that IS there is
     * stepped over -- and one the flag promises but the blob lacks reads the
     * name extras out of place and is refused. Models are four bytes at 237+
     * (v5) and two below it (v4). */
    build_v5_blob(&w, APPEARANCE_WIRE_OBJ_TAG_V5, 1 << 12);
    TEST_CHECK(!PktPlayerAppearance_DecodeAs(&app, APPEARANCE_ENC_V5, w.buf, bw_len(&w)));
    {
        struct BitWriter v5 = { 0 };
        struct BitWriter v4 = { 0 };

        build_v5_blob_with(&v5, APPEARANCE_WIRE_OBJ_TAG_V5, (1 << 12) | (1 << 9), 4);
        TEST_CHECK(PktPlayerAppearance_DecodeAs(&app, APPEARANCE_ENC_V5, v5.buf, bw_len(&v5)));
        TEST_CHECK(app.combat_level == FIXTURE_COMBAT_LEVEL);
        build_v5_blob_with(&v4, APPEARANCE_WIRE_OBJ_TAG_V5, (1 << 12) | (1 << 9), 2);
        TEST_CHECK(PktPlayerAppearance_DecodeAs(&app, APPEARANCE_ENC_V4, v4.buf, bw_len(&v4)));
        TEST_CHECK(app.combat_level == FIXTURE_COMBAT_LEVEL);
    }

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
        struct RsprotInfoOp ops[OPS_CAP];
        unsigned char canary[64];
    } probe;
    int n;

    memset(&probe, 0, sizeof(probe));
    memset(probe.canary, 0xA5, sizeof(probe.canary));

    n = npc_read(&k_classic, w.buf, bw_len(&w), probe.ops, OPS_CAP);

    TEST_CHECK(n <= OPS_CAP);
    TEST_CHECK(g_overflow);
    for( size_t i = 0; i < sizeof(probe.canary); i++ )
        TEST_CHECK(probe.canary[i] == 0xA5);

    printf("ok - npc op overflow stays inside the caller's array\n");
}

/*
 * Same shape one layer down: the bit cursor is walked by counts the packet
 * supplies, so a short packet runs it off the end of the buffer. The rs-family
 * readers (3rd/rsprot/streams/info_rs.h) rely on RSProt's bit reader refusing
 * that read -- whole, zero, and marked -- rather than serving what follows.
 */
static void
test_bitbuffer_refuses_reads_past_the_end(void)
{
    uint8_t data[2] = { 0xAB, 0xCD };
    RsprotBuf buf;

    rsprot_buf_wrap_read(&buf, data, (int32_t)sizeof(data));
    rsprot_buf_bit_begin(&buf);
    TEST_CHECK(rsprot_gbits(&buf, 16) == 0xABCD);
    TEST_CHECK(buf.err == RSPROT_BUF_OK);

    /* One bit past the end: refused, reported, and zero rather than whatever
     * follows the buffer. */
    TEST_CHECK(rsprot_gbits(&buf, 1) == 0);
    TEST_CHECK(buf.err != RSPROT_BUF_OK);

    /* A read that straddles the end is refused whole, not partially served. */
    rsprot_buf_wrap_read(&buf, data, (int32_t)sizeof(data));
    rsprot_buf_bit_begin(&buf);
    TEST_CHECK(rsprot_gbits(&buf, 12) == 0xABC);
    TEST_CHECK(rsprot_gbits(&buf, 8) == 0);
    TEST_CHECK(buf.err != RSPROT_BUF_OK);

    printf("ok - bit reads past the packet end are refused\n");
}

/*
 * A dropped tracked player (old-vis op 3 -- what the server sends when a player
 * dies out of view, logs out, or walks off the edge of the scene) must NOT be
 * announced as an entry of the list this packet is rebuilding.
 *
 * Extended-info blocks address players by POSITION in that rebuilt list, and
 * the same list becomes the next packet's `old_list`, which every old_idx is
 * resolved through. Emitting ADD_PLAYER_OLD for a dropped entry makes the
 * consumer append a player the decoder's own position counter never counted,
 * so from the drop onward:
 *   - every appearance/chat/hit/anim block lands one player early, and
 *   - the next packet reads every old_idx after the drop off by one, walking
 *     players to each other's tiles and count-shrinking a live one off the end.
 *
 * Tracked list is [0, 1, 2]; index 1 is dropped; index 2 carries extended info.
 * The block must therefore address list position 1, not 2, and no ADD may be
 * emitted for index 1.
 */
static void
check_dropped_player_leaves_no_list_entry(
    struct RsprotInfoOp const* ops,
    int n,
    char const* who)
{
    int adds[8];
    int add_count = 0;
    int clears = 0;
    int set_idx = -1;

    for( int i = 0; i < n; i++ )
    {
        if( ops[i].kind == RSPROT_OP_ADD_OLD &&
            add_count < (int)(sizeof(adds) / sizeof(adds[0])) )
            adds[add_count++] = (int)ops[i].value;
        else if( ops[i].kind == RSPROT_OP_CLEAR_OLD )
        {
            clears++;
            TEST_CHECK(ops[i].value == 1);
        }
        else if( ops[i].kind == RSPROT_OP_SET_EXT )
            set_idx = (int)ops[i].value;
    }

    TEST_CHECK(clears == 1);
    /* Kept entries only: old indices 0 and 2, never the dropped 1. */
    TEST_CHECK(add_count == 2);
    TEST_CHECK(adds[0] == 0);
    TEST_CHECK(adds[1] == 2);
    /* Old index 2 is list position 1 once the drop is not counted. */
    TEST_CHECK(set_idx == 1);
    printf("ok - %s: dropped player takes no list position\n", who);
}

static void
write_dropped_player_packet(struct BitWriter* w)
{
    bw_bits(w, 0, 1); /* local player: no update */
    bw_bits(w, 3, 8); /* tracked count 3 */

    bw_bits(w, 0, 1); /* old 0: info clear, kept unchanged */

    bw_bits(w, 1, 1); /* old 1: info set... */
    bw_bits(w, 3, 2); /* ...op 3 = REMOVED */

    bw_bits(w, 1, 1); /* old 2: info set... */
    bw_bits(w, 0, 2); /* ...op 0 = stood still, extended info follows */

    bw_bits(w, 2047, 11); /* new-vis terminator */
    bw_byte(w, 0x02);     /* extended: SEQUENCE mask */
    bw_byte(w, 0x00);
    bw_byte(w, 7); /* seq 7 */
    bw_byte(w, 1); /* delay 1 */
}

static void
test_player_drop_does_not_shift_list_positions(void)
{
    struct BitWriter w = { 0 };
    struct RsprotInfoOp ops[64];
    int n;

    write_dropped_player_packet(&w);
    n = player_read(w.buf, bw_len(&w), ops, 64);
    check_dropped_player_leaves_no_list_entry(ops, n, "classic");
    {
        struct RsprotInfoOp const* seq =
            find_player_op(ops, n, RSPROT_OP_SEQUENCE);
        TEST_CHECK(seq && seq->sequence.sequence_id == 7);
    }
}

static void
test_lc289_player_drop_does_not_shift_list_positions(void)
{
    struct BitWriter w = { 0 };
    struct RsprotInfoOp ops[64];
    int n;

    write_dropped_player_packet(&w);
    n = lc289_read(w.buf, bw_len(&w), ops, 64);
    check_dropped_player_leaves_no_list_entry(ops, n, "lc289");
    {
        struct RsprotInfoOp const* seq =
            find_player_op(ops, n, RSPROT_OP_SEQUENCE);
        TEST_CHECK(seq && seq->sequence.sequence_id == 7);
    }
}

int
main(void)
{
    test_npc_ops_capacity_is_a_guard_not_an_assert();
    test_bitbuffer_refuses_reads_past_the_end();
    test_player_local_teleport();
    test_player_new_walk_with_seq();
    test_player_exact_move_and_chat_skip();
    test_player_drop_does_not_shift_list_positions();
    test_lc289_player_drop_does_not_shift_list_positions();
    test_npc_add_change_type_spotanim();
    test_npc_type_width();
    test_npc_rs289_new_npc_jump();
    test_appearance_decode();
    test_appearance_v5_decode();
    test_appearance_encoding_independent();
    test_appearance_tag_is_per_encoding();
    test_appearance_v2_obj_tag();
    test_appearance_rejects_undecodable();
    test_appearance_transmog();
    printf("entity-decode: all tests passed\n");
    return 0;
}
