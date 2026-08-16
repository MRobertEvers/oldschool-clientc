/*
 * rsareabuf unit tests.
 *
 * The load-bearing property is that every reader is the exact inverse of its
 * writer, and that the byte-order variants put bytes exactly where the Jagex
 * protocols expect — a transposed alt order is invisible on symmetric values
 * and corrupts everything else, so the byte-pattern assertions matter as much
 * as the round-trips.
 *
 *   make -C src test-rsareabuf
 */
#include "rsareabuf.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                 \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define CHECK_EQ(actual, expected)                                                                 \
    do                                                                                             \
    {                                                                                              \
        long long _a = (long long)(actual);                                                        \
        long long _e = (long long)(expected);                                                      \
        if( _a != _e )                                                                             \
        {                                                                                          \
            printf(                                                                                \
                "FAIL %s:%d  %s = %lld, want %lld\n", __FILE__, __LINE__, #actual, _a, _e);        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static uint8_t g_arena_memory[64 * 1024];
static struct RSArena g_arena;

static void
open_buf(
    struct RSAreaBuf* buf,
    size_t capacity)
{
    CHECK(rsab_open_arena(buf, &g_arena, capacity));
}

/* ------------------------------------------------------------------ */

static void
test_arena(void)
{
    struct RSArena arena;
    uint8_t memory[64];
    rsab_arena_init(&arena, memory, sizeof(memory));

    uint8_t* a = rsab_arena_alloc(&arena, 16);
    uint8_t* b = rsab_arena_alloc(&arena, 16);
    CHECK(a && b && b > a);
    CHECK_EQ(a[0], 0); /* zeroed */

    /* Blocks are pointer-aligned even after an odd-sized request. */
    (void)rsab_arena_alloc(&arena, 1);
    uint8_t* c = rsab_arena_alloc(&arena, 4);
    CHECK(c && ((uintptr_t)c % sizeof(void*)) == 0);

    /* Exhaustion is reported, not fatal, and survives a reset. */
    CHECK(rsab_arena_alloc(&arena, 1024) == NULL);
    CHECK_EQ(arena.exhausted, 1);
    size_t peak = arena.used;
    rsab_arena_reset(&arena);
    CHECK_EQ(arena.used, 0);
    CHECK_EQ(arena.high_water, peak);
    CHECK_EQ(arena.exhausted, 1);
    CHECK(rsab_arena_alloc(&arena, 16) == memory);
}

static void
test_byte_orders(void)
{
    struct RSAreaBuf buf;
    open_buf(&buf, 256);

    rsab_p1(&buf, 0x42);
    rsab_p1_alt1(&buf, 0x42);
    rsab_p1_alt2(&buf, 0x42);
    rsab_p1_alt3(&buf, 0x42);
    CHECK_EQ(buf.data[0], 0x42);
    CHECK_EQ(buf.data[1], (0x42 + 128) & 0xff);
    CHECK_EQ(buf.data[2], (-0x42) & 0xff);
    CHECK_EQ(buf.data[3], (128 - 0x42) & 0xff);

    rsab_rewind(&buf);
    rsab_p2(&buf, 0x1234);
    rsab_p2_alt1(&buf, 0x1234);
    rsab_p2_alt2(&buf, 0x1234);
    rsab_p2_alt3(&buf, 0x1234);
    CHECK_EQ(buf.data[0], 0x12);
    CHECK_EQ(buf.data[1], 0x34);
    CHECK_EQ(buf.data[2], 0x34);
    CHECK_EQ(buf.data[3], 0x12);
    CHECK_EQ(buf.data[4], 0x12);
    CHECK_EQ(buf.data[5], (0x34 + 128) & 0xff);
    CHECK_EQ(buf.data[6], (0x34 + 128) & 0xff);
    CHECK_EQ(buf.data[7], 0x12);

    /* The 24-bit permutations, transcribed from RSProt's JagByteBuf rather
     * than derived — three of the four put b0 somewhere other than the top. */
    rsab_rewind(&buf);
    rsab_p3(&buf, 0x112233);
    rsab_p3_alt1(&buf, 0x112233);
    rsab_p3_alt2(&buf, 0x112233);
    rsab_p3_alt3(&buf, 0x112233);
    CHECK_EQ(buf.data[0], 0x11);
    CHECK_EQ(buf.data[1], 0x22);
    CHECK_EQ(buf.data[2], 0x33);
    CHECK_EQ(buf.data[3], 0x33); /* alt1 = little endian */
    CHECK_EQ(buf.data[4], 0x22);
    CHECK_EQ(buf.data[5], 0x11);
    CHECK_EQ(buf.data[6], 0x11); /* alt2 = [b0,b2,b1] */
    CHECK_EQ(buf.data[7], 0x33);
    CHECK_EQ(buf.data[8], 0x22);
    CHECK_EQ(buf.data[9], 0x22); /* alt3 = [b1,b0,b2] */
    CHECK_EQ(buf.data[10], 0x11);
    CHECK_EQ(buf.data[11], 0x33);

    rsab_rewind(&buf);
    rsab_p4(&buf, 0x11223344);
    rsab_p4_alt1(&buf, 0x11223344);
    rsab_p4_alt2(&buf, 0x11223344);
    rsab_p4_alt3(&buf, 0x11223344);
    CHECK_EQ(buf.data[0], 0x11);
    CHECK_EQ(buf.data[3], 0x44);
    CHECK_EQ(buf.data[4], 0x44); /* alt1 = little endian */
    CHECK_EQ(buf.data[7], 0x11);
    CHECK_EQ(buf.data[8], 0x33); /* alt2 = [b1,b0,b3,b2] */
    CHECK_EQ(buf.data[9], 0x44);
    CHECK_EQ(buf.data[10], 0x11);
    CHECK_EQ(buf.data[11], 0x22);
    CHECK_EQ(buf.data[12], 0x22); /* alt3 = [b2,b3,b0,b1] */
    CHECK_EQ(buf.data[13], 0x11);
    CHECK_EQ(buf.data[14], 0x44);
    CHECK_EQ(buf.data[15], 0x33);
}

/* Asymmetric values so a transposed pair cannot pass by accident. */
static void
test_roundtrip(void)
{
    struct RSAreaBuf buf;
    open_buf(&buf, 512);

    rsab_p1(&buf, 0xA7);
    rsab_p1_alt1(&buf, 0xA7);
    rsab_p1_alt2(&buf, 0xA7);
    rsab_p1_alt3(&buf, 0xA7);
    rsab_p2(&buf, 0xBEEF);
    rsab_p2_alt1(&buf, 0xBEEF);
    rsab_p2_alt2(&buf, 0xBEEF);
    rsab_p2_alt3(&buf, 0xBEEF);
    rsab_p3(&buf, 0xABCDEF);
    rsab_p3_alt1(&buf, 0xABCDEF);
    rsab_p3_alt2(&buf, 0xABCDEF);
    rsab_p3_alt3(&buf, 0xABCDEF);
    rsab_p4(&buf, 0x12345678);
    rsab_p4_alt1(&buf, 0x12345678);
    rsab_p4_alt2(&buf, 0x12345678);
    rsab_p4_alt3(&buf, 0x12345678);
    rsab_p8(&buf, 0x0123456789ABCDEFLL);

    size_t written = rsab_len(&buf);
    CHECK(rsab_ok(&buf));

    rsab_rewind(&buf);
    CHECK_EQ(rsab_g1(&buf), 0xA7);
    CHECK_EQ(rsab_g1_alt1(&buf), 0xA7);
    CHECK_EQ(rsab_g1_alt2(&buf), 0xA7);
    CHECK_EQ(rsab_g1_alt3(&buf), 0xA7);
    CHECK_EQ(rsab_g2(&buf), 0xBEEF);
    CHECK_EQ(rsab_g2_alt1(&buf), 0xBEEF);
    CHECK_EQ(rsab_g2_alt2(&buf), 0xBEEF);
    CHECK_EQ(rsab_g2_alt3(&buf), 0xBEEF);
    CHECK_EQ(rsab_g3(&buf), 0xABCDEF);
    CHECK_EQ(rsab_g3_alt1(&buf), 0xABCDEF);
    CHECK_EQ(rsab_g3_alt2(&buf), 0xABCDEF);
    CHECK_EQ(rsab_g3_alt3(&buf), 0xABCDEF);
    CHECK_EQ(rsab_g4(&buf), 0x12345678);
    CHECK_EQ(rsab_g4_alt1(&buf), 0x12345678);
    CHECK_EQ(rsab_g4_alt2(&buf), 0x12345678);
    CHECK_EQ(rsab_g4_alt3(&buf), 0x12345678);
    CHECK_EQ(rsab_g8(&buf), 0x0123456789ABCDEFLL);
    CHECK_EQ(rsab_len(&buf), written);
    CHECK(rsab_ok(&buf));

    /* Signed readers sign-extend where the unsigned ones do not. */
    rsab_rewind(&buf);
    rsab_p1(&buf, -3);
    rsab_p2(&buf, -300);
    rsab_rewind(&buf);
    CHECK_EQ(rsab_g1(&buf), 0xfd);
    rsab_rewind(&buf);
    CHECK_EQ(rsab_g1s(&buf), -3);
    CHECK_EQ(rsab_g2s(&buf), -300);
}

static void
test_smart(void)
{
    struct RSAreaBuf buf;
    open_buf(&buf, 256);

    /* Unsigned smart: the width boundary is 128. */
    rsab_psmart(&buf, 0);
    rsab_psmart(&buf, 127);
    rsab_psmart(&buf, 128);
    rsab_psmart(&buf, 32767);
    CHECK_EQ(rsab_len(&buf), 1 + 1 + 2 + 2);
    CHECK_EQ(buf.data[2], 0x80); /* 128 + 0x8000 */
    rsab_rewind(&buf);
    CHECK_EQ(rsab_gsmart(&buf), 0);
    CHECK_EQ(rsab_gsmart(&buf), 127);
    CHECK_EQ(rsab_gsmart(&buf), 128);
    CHECK_EQ(rsab_gsmart(&buf), 32767);
    CHECK(rsab_ok(&buf));

    /* Out of range latches rather than truncating. */
    rsab_rewind(&buf);
    rsab_psmart(&buf, 32768);
    CHECK(!rsab_ok(&buf));

    /* Signed smart: boundary at +-64. */
    open_buf(&buf, 256);
    rsab_psmarts(&buf, -64);
    rsab_psmarts(&buf, 63);
    rsab_psmarts(&buf, -16384);
    rsab_psmarts(&buf, 16383);
    CHECK_EQ(rsab_len(&buf), 1 + 1 + 2 + 2);
    rsab_rewind(&buf);
    CHECK_EQ(rsab_gsmarts(&buf), -64);
    CHECK_EQ(rsab_gsmarts(&buf), 63);
    CHECK_EQ(rsab_gsmarts(&buf), -16384);
    CHECK_EQ(rsab_gsmarts(&buf), 16383);
    CHECK(rsab_ok(&buf));
}

static void
test_strings(void)
{
    struct RSAreaBuf buf;
    char out[32];
    open_buf(&buf, 256);

    rsab_pjstr(&buf, "Welcome to the mock", RSAB_JSTR_NUL);
    rsab_pjstr(&buf, "newline form", RSAB_JSTR_NEWLINE);
    CHECK_EQ(buf.data[19], 0x00);
    CHECK_EQ(buf.data[19 + 1 + 12], 0x0A);

    rsab_rewind(&buf);
    CHECK_EQ(rsab_gjstr(&buf, out, sizeof(out), RSAB_JSTR_NUL), 19);
    CHECK(strcmp(out, "Welcome to the mock") == 0);
    CHECK_EQ(rsab_gjstr(&buf, out, sizeof(out), RSAB_JSTR_NEWLINE), 12);
    CHECK(strcmp(out, "newline form") == 0);
    CHECK(rsab_ok(&buf));

    /* An over-long string truncates the copy but still consumes the record,
     * so the next field is not read from the middle of the previous one. */
    rsab_rewind(&buf);
    rsab_pjstr(&buf, "0123456789", RSAB_JSTR_NUL);
    rsab_p1(&buf, 0x5A);
    rsab_rewind(&buf);
    char small[4];
    CHECK_EQ(rsab_gjstr(&buf, small, sizeof(small), RSAB_JSTR_NUL), 3);
    CHECK(strcmp(small, "012") == 0);
    CHECK_EQ(rsab_g1(&buf), 0x5A);
}

static void
test_bits(void)
{
    struct RSAreaBuf buf;
    open_buf(&buf, 64);

    /* A PLAYER_INFO-shaped local-player teleport block: 1+2+2+7+7+1+1 bits. */
    rsab_bits(&buf);
    rsab_pbit(&buf, 1, 1);  /* info */
    rsab_pbit(&buf, 2, 3);  /* moveType = teleport */
    rsab_pbit(&buf, 2, 1);  /* level */
    rsab_pbit(&buf, 7, 52); /* scene x */
    rsab_pbit(&buf, 7, 41); /* scene z */
    rsab_pbit(&buf, 1, 1);  /* jump */
    rsab_pbit(&buf, 1, 1);  /* has extended */
    rsab_pbit(&buf, 8, 0);  /* tracked count */
    rsab_pbit(&buf, 11, 2047);
    rsab_bytes(&buf);
    CHECK_EQ(rsab_len(&buf), (1 + 2 + 2 + 7 + 7 + 1 + 1 + 8 + 11 + 7) / 8);
    CHECK(rsab_ok(&buf));

    /* The byte cursor picks up where the bit cursor left off. */
    rsab_p1(&buf, 0x01);
    size_t total = rsab_len(&buf);

    rsab_rewind(&buf);
    rsab_bits(&buf);
    CHECK_EQ(rsab_gbit(&buf, 1), 1);
    CHECK_EQ(rsab_gbit(&buf, 2), 3);
    CHECK_EQ(rsab_gbit(&buf, 2), 1);
    CHECK_EQ(rsab_gbit(&buf, 7), 52);
    CHECK_EQ(rsab_gbit(&buf, 7), 41);
    CHECK_EQ(rsab_gbit(&buf, 1), 1);
    CHECK_EQ(rsab_gbit(&buf, 1), 1);
    CHECK_EQ(rsab_gbit(&buf, 8), 0);
    CHECK_EQ(rsab_gbit(&buf, 11), 2047);
    rsab_bytes(&buf);
    CHECK_EQ(rsab_g1(&buf), 0x01);
    CHECK_EQ(rsab_len(&buf), total);
    CHECK(rsab_ok(&buf));

    /* Widths that straddle several bytes, and a 32-bit run. */
    open_buf(&buf, 64);
    rsab_bits(&buf);
    rsab_pbit(&buf, 3, 5);
    rsab_pbit(&buf, 14, 9001);
    rsab_pbit(&buf, 32, (int32_t)0xDEADBEEF);
    rsab_pbit(&buf, 5, 17);
    rsab_bytes(&buf);
    rsab_rewind(&buf);
    rsab_bits(&buf);
    CHECK_EQ(rsab_gbit(&buf, 3), 5);
    CHECK_EQ(rsab_gbit(&buf, 14), 9001);
    CHECK_EQ((uint32_t)rsab_gbit(&buf, 32), 0xDEADBEEFu);
    CHECK_EQ(rsab_gbit(&buf, 5), 17);
    CHECK(rsab_ok(&buf));

    /* Writing bits over existing bytes must not disturb neighbours. */
    open_buf(&buf, 8);
    memset(buf.data, 0xff, 8);
    rsab_bits(&buf);
    rsab_pbit(&buf, 4, 0);
    rsab_bytes(&buf);
    CHECK_EQ(buf.data[0], 0x0f);
}

static void
test_psize(void)
{
    struct RSAreaBuf buf;
    open_buf(&buf, 256);

    /* Mark form: the body length is discovered, not tracked by the caller. */
    size_t mark = rsab_psize1_begin(&buf);
    rsab_p1(&buf, 0);
    rsab_pjstr(&buf, "hello", RSAB_JSTR_NUL);
    rsab_psize1_end(&buf, mark);
    CHECK_EQ(buf.data[mark], 1 + 5 + 1);
    CHECK(rsab_ok(&buf));

    open_buf(&buf, 512);
    mark = rsab_psize2_begin(&buf);
    rsab_pfill(&buf, 0xAB, 300);
    rsab_psize2_end(&buf, mark);
    CHECK_EQ((buf.data[mark] << 8) | buf.data[mark + 1], 300);
    CHECK(rsab_ok(&buf));

    /* A body too large for a 1-byte slot is caught, not wrapped. */
    open_buf(&buf, 512);
    mark = rsab_psize1_begin(&buf);
    rsab_pfill(&buf, 0, 256);
    rsab_psize1_end(&buf, mark);
    CHECK(!rsab_ok(&buf));

    /* rsbuf-compatible form: caller already knows the size. */
    open_buf(&buf, 64);
    rsab_p1(&buf, 0); /* slot */
    rsab_pfill(&buf, 0x11, 4);
    rsab_psize1(&buf, 4);
    CHECK_EQ(buf.data[0], 4);
    CHECK(rsab_ok(&buf));
}

static void
test_overflow(void)
{
    struct RSAreaBuf buf;
    open_buf(&buf, 4);

    rsab_p4(&buf, 0x11223344);
    CHECK(rsab_ok(&buf));
    rsab_p1(&buf, 0x55);
    CHECK(!rsab_ok(&buf));
    /* The cursor still reports the length the caller asked for, so the
     * mismatch surfaces as a bad length rather than a silently short packet. */
    CHECK_EQ(rsab_len(&buf), 5);

    /* Reads past the end return 0 and latch instead of walking off. */
    open_buf(&buf, 2);
    buf.pos = 2;
    rsab_rewind(&buf);
    CHECK_EQ(rsab_g4(&buf), 0);
    CHECK(!rsab_ok(&buf));

    /* Bit writes past the end are bounded too. */
    open_buf(&buf, 1);
    rsab_bits(&buf);
    rsab_pbit(&buf, 8, 0xff);
    CHECK(rsab_ok(&buf));
    rsab_pbit(&buf, 1, 1);
    CHECK(!rsab_ok(&buf));

    /* A wrapped buffer never frees its bytes. */
    uint8_t borrowed[4] = { 1, 2, 3, 4 };
    rsab_wrap(&buf, borrowed, sizeof(borrowed));
    CHECK_EQ(rsab_g1(&buf), 1);
    rsab_close(&buf);
    CHECK_EQ(borrowed[0], 1);
}

static void
test_heap(void)
{
    struct RSAreaBuf buf;
    CHECK(rsab_open_heap(&buf, 32));
    rsab_p4(&buf, 0x0BADF00D);
    rsab_rewind(&buf);
    CHECK_EQ(rsab_g4(&buf), 0x0BADF00D);
    rsab_close(&buf);
    CHECK(buf.data == NULL);
}

int
main(void)
{
    rsab_arena_init(&g_arena, g_arena_memory, sizeof(g_arena_memory));

    test_arena();
    test_byte_orders();
    test_roundtrip();
    test_smart();
    test_strings();
    test_bits();
    test_psize();
    test_overflow();
    test_heap();

    if( g_failures )
    {
        printf("rsareabuf: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rsareabuf: all tests passed (arena high water %zu bytes)\n", g_arena.high_water);
    return 0;
}
