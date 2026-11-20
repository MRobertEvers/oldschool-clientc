/*
 * The readers whose arithmetic the reference does in Java ints.
 *
 * Java's int `<<` and `+` wrap; C's are undefined once a signed value is
 * shifted into or past the sign bit or a sum leaves int. Each case below feeds
 * a reader a value that takes it there and checks the result is the
 * two's-complement wrap the reference computes. Built plain, every case
 * passes on the old code too (the compilers happen to wrap); built with
 * -fsanitize=undefined -fno-sanitize-recover (check-ubsan-units runs these
 * tests that way) the old code stops at the first one.
 *
 *   - rsprot_gbits(32) of a value with bit 31 set: NPC_INFO's spawn cycle
 *     (npc_info_osrs2xx.c) is read this way.
 *   - rsprot_gmidivarlen of a five-byte quantity.
 *   - PLAYER_INFO: a low-resolution walk that steps west of rough x 0, and a
 *     promotion to high resolution from a rough x of 200, whose
 *     ((rough << 13) + fine) << 14 is past 2^31.
 *   - FACING: a player target index whose 32768 + index is past INT_MAX.
 */

#include "rsprot.h"
#include "streams/player_info.h"
#include "streams/rsprot_info.h"
#include "streams/rsprot_info_blocks.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK_EQ(a, b, msg)                                                                        \
    do                                                                                             \
    {                                                                                              \
        long long a_ = (long long)(a);                                                             \
        long long b_ = (long long)(b);                                                             \
        g_checks++;                                                                                \
        if( a_ != b_ )                                                                             \
        {                                                                                          \
            fprintf(stderr, "  FAIL %s: got %lld want %lld\n", msg, a_, b_);                     \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static uint8_t g_mem[1 << 14];

static void
test_gbits_32_top_bit(void)
{
    RsprotBuf b;
    int32_t len;

    /* Byte-aligned, then three bits in, so both the first and a middle chunk
     * of the read carry bit 31. */
    memset(&b, 0, sizeof(b));
    rsprot_buf_alloc(&b, 64);
    rsprot_buf_bit_begin(&b);
    rsprot_pbits(&b, 32, (int32_t)0x9abcdef0);
    rsprot_pbits(&b, 3, 5);
    rsprot_pbits(&b, 32, (int32_t)0xfedcba98);
    rsprot_pbits(&b, 5, 0);
    rsprot_buf_bit_end(&b);
    len = b.wpos;
    memcpy(g_mem, b.data, (size_t)len);
    rsprot_buf_free(&b);

    rsprot_buf_wrap_read(&b, g_mem, len);
    rsprot_buf_bit_begin(&b);
    CHECK_EQ(rsprot_gbits(&b, 32), (int32_t)0x9abcdef0, "gbits 32, aligned, bit 31 set");
    CHECK_EQ(rsprot_gbits(&b, 3), 5, "gbits 3 between");
    CHECK_EQ(rsprot_gbits(&b, 32), (int32_t)0xfedcba98, "gbits 32, three bits in, bit 31 set");
    rsprot_buf_bit_end(&b);
    CHECK_EQ(rsprot_buf_ok(&b), 1, "bit reads ok");
}

static void
test_midivarlen_five_bytes(void)
{
    /* 0x0f, then four 7-bit groups of ones: 35 bits, of which Java keeps the
     * low 32 -- all ones. */
    static const uint8_t bytes[] = { 0x8f, 0xff, 0xff, 0xff, 0x7f };
    RsprotBuf b;

    rsprot_buf_wrap_read(&b, bytes, (int32_t)sizeof(bytes));
    CHECK_EQ(rsprot_gmidivarlen(&b), -1, "midivarlen of five bytes wraps as Java's int does");
}

/* One PLAYER_INFO section stream for a table whose local player is slot 1. */
static int32_t
player_sections(
    uint8_t* out,
    int32_t cap)
{
    RsprotBuf b;
    int32_t len;

    memset(&b, 0, sizeof(b));
    rsprot_buf_alloc(&b, cap);

    /* High-res active: the local player, no update, nothing skipped. */
    rsprot_buf_bit_begin(&b);
    rsprot_pbits(&b, 1, 0);
    rsprot_pbits(&b, 2, 0);
    rsprot_buf_bit_end(&b);
    /* High-res inactive and low-res inactive: nobody. */
    rsprot_buf_bit_begin(&b);
    rsprot_buf_bit_end(&b);
    rsprot_buf_bit_begin(&b);
    rsprot_buf_bit_end(&b);
    /* Low-res active, slots 2..2047. */
    rsprot_buf_bit_begin(&b);
    /* slot 2: a walk, direction 0 (dx -1, dz -1). */
    rsprot_pbits(&b, 1, 1);
    rsprot_pbits(&b, 2, 2);
    rsprot_pbits(&b, 5, 0);
    /* slot 3: into high resolution, no low-res update first, fine x 100,
     * fine z 200, no extended info. */
    rsprot_pbits(&b, 1, 1);
    rsprot_pbits(&b, 2, 0);
    rsprot_pbits(&b, 1, 0);
    rsprot_pbits(&b, 13, 100);
    rsprot_pbits(&b, 13, 200);
    rsprot_pbits(&b, 1, 0);
    /* slot 4 stays, and skips the 2043 after it. */
    rsprot_pbits(&b, 1, 0);
    rsprot_pbits(&b, 2, 3);
    rsprot_pbits(&b, 11, 2043);
    rsprot_buf_bit_end(&b);

    len = b.wpos;
    memcpy(out, b.data, (size_t)len);
    rsprot_buf_free(&b);
    return len;
}

static void
test_player_info_coordinate_packs(void)
{
    static RsprotPlayerInfo pi;
    static int32_t positions[RSPROT_INFO_PLAYER_SLOTS];
    static struct RsprotInfoOp ops[256];
    static uint8_t strings[1 << 12];
    RsprotInfoList list;
    int32_t len;
    uint32_t expect_low;
    uint32_t expect_coord;

    rsprot_player_info_reset(&pi, 1);
    memset(positions, 0, sizeof(positions));
    /* rough positions are level << 16 | x << 8 | z */
    positions[2] = (0 << 16) | (0 << 8) | 5;
    positions[3] = (1 << 16) | (200 << 8) | 7;
    rsprot_player_info_seed(&pi, (0 << 28) | (3200 << 14) | 3200, positions);

    len = player_sections(g_mem, (int32_t)sizeof(g_mem));
    rsprot_info_list_init(&list, ops, 256, strings, (int)sizeof(strings));
    CHECK_EQ(player_info_osrs239(&pi, g_mem, len, &list) >= 0, 1, "player info sections read");

    /* Java: (x << 14) + z + (level << 28) with x = 0 + -1, z = 5 + -1. */
    expect_low = ((uint32_t)-1 << 14) + 4u + (0u << 28);
    CHECK_EQ(pi.low_res_pos[2], (int32_t)expect_low, "a walk west of rough x 0 packs as Java's int does");

    /* Java: (level << 28) | (((rough_x << 13) + fine_x) << 14)
     *       | (((rough_z << 13) + fine_z) & 0x3fff) */
    expect_coord = (1u << 28) | ((((uint32_t)200 << 13) + 100u) << 14)
                   | ((((uint32_t)7 << 13) + 200u) & 0x3fffu);
    CHECK_EQ(pi.coord[3], (int32_t)expect_coord, "a promotion from rough x 200 packs as Java's int does");
    CHECK_EQ(pi.high_res[3], 1, "slot 3 is high resolution");
}

static void
test_facing_player_index(void)
{
    static const RsprotInfoFacingEnc enc = {
        .flag = rsprot_g1,
        .entity_type = rsprot_g1,
        .index = rsprot_g4_1234,
        .fallback_angle = rsprot_g2_12,
        .x = rsprot_g1,
        .z = rsprot_g1,
        .size = rsprot_g1,
        .angle = rsprot_g2_12,
    };
    static struct RsprotInfoOp ops[16];
    static uint8_t strings[256];
    RsprotInfoList list;
    RsprotInfoBlocks t;
    RsprotBuf b;
    uint8_t bytes[16];
    int found = 0;

    memset(&b, 0, sizeof(b));
    rsprot_buf_wrap(&b, bytes, (int32_t)sizeof(bytes));
    rsprot_p1(&b, 0); /* kind 0: an entity */
    rsprot_p1(&b, RSPROT_FACE_TARGET_PLAYER);
    rsprot_p4_1234(&b, 0x7ffffff0);
    rsprot_p2_12(&b, 512);

    rsprot_buf_wrap_read(&b, bytes, 8);
    rsprot_info_list_init(&list, ops, 16, strings, (int)sizeof(strings));
    memset(&t, 0, sizeof(t));
    t.b = &b;
    t.out = &list;
    CHECK_EQ(rsprot_info_facing(&t, 1, &enc) != 0, 1, "facing block read");
    for( int i = 0; i < list.count; i++ )
    {
        if( ops[i].kind != RSPROT_OP_FACE_ENTITY )
            continue;
        found = 1;
        CHECK_EQ(
            ops[i].face_entity.entity_id,
            (int32_t)(32768u + 0x7ffffff0u),
            "32768 + a player index past INT_MAX - 32768 wraps as Java's int does");
    }
    CHECK_EQ(found, 1, "a FACE_ENTITY op");
}

int
main(void)
{
    test_gbits_32_top_bit();
    test_midivarlen_five_bytes();
    test_player_info_coordinate_packs();
    test_facing_player_index();
    printf("test_int_wrap: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
