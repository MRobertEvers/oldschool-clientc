/*
 * RsprotBuf -- every name checked against the bytes it claims.
 *
 *     make -C 3rd/rsprot test
 *
 * The names state the byte order (rsprot_buf.h), and that is only worth
 * anything if they are true: a `p4_3412` that actually writes 1,2,3,4 is a lie
 * a reader will act on.
 *
 * So this file does not round-trip. A round trip passes for any encoding that
 * is self-consistent, including one whose name is wrong. Every case here pins
 * the literal bytes, derived from the name alone:
 *
 *   - digits are byte significance in write order, 1 = most significant, so
 *     `_3412` on 0x12345678 must emit 56 78 12 34;
 *   - `_add128` biases the least-significant byte by +128;
 *   - `_neg` writes -v, `_sub128` writes 128-v.
 *
 * If a name and its implementation ever disagree, this fails and says which.
 */
#include "../src/rsprot_buf.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

static void
expect(const char *name, RsprotBuf *b, const uint8_t *want, int n)
{
    if (b->wpos != n) {
        printf("FAIL %-28s wrote %d bytes, name implies %d\n", name, b->wpos, n);
        g_failures++;
        return;
    }
    if (memcmp(b->data, want, (size_t)n) != 0) {
        printf("FAIL %-28s got ", name);
        for (int i = 0; i < n; i++) printf("%02X ", b->data[i]);
        printf("  want ");
        for (int i = 0; i < n; i++) printf("%02X ", want[i]);
        printf("\n");
        g_failures++;
        return;
    }
    if (b->err) {
        printf("FAIL %-28s latched err 0x%X\n", name, b->err);
        g_failures++;
    }
}

static uint8_t g_mem[32];

#define CASE(fn, value, ...)                                                                       \
    do {                                                                                           \
        static const uint8_t want[] = { __VA_ARGS__ };                                             \
        RsprotBuf b;                                                                           \
        rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));                                      \
        fn(&b, (value));                                                                           \
        expect(#fn, &b, want, (int)sizeof(want));                                                  \
    } while (0)

/*
 * Width 1. No byte order exists, so every variant differs only in the
 * transform. 0x12 is 18.
 */
static void
test_width1(void)
{
    CASE(rsprot_p1, 0x12, 0x12);
    CASE(rsprot_p1_add128, 0x12, 0x92);              /* 18 + 128 = 146 */
    CASE(rsprot_p1_neg, 0x12, 0xEE);                 /* -18 & 0xFF     */
    CASE(rsprot_p1_sub128, 0x12, 0x6E);              /* 128 - 18 = 110 */
}

/*
 * Width 2 -- the case that forced the transform into the name. `_12` and
 * `_12_add128` write the SAME byte order and different bytes; a positional-only
 * scheme would call them the same thing.
 */
static void
test_width2(void)
{
    CASE(rsprot_p2_12, 0x1234, 0x12, 0x34);
    CASE(rsprot_p2_21, 0x1234, 0x34, 0x12);
    CASE(rsprot_p2_12_add128, 0x1234, 0x12, 0xB4);    /* 0x34 + 128 */
    CASE(rsprot_p2_21_add128, 0x1234, 0xB4, 0x12);
}

/* Width 3 — pure permutation of bytes 1,2,3 = 0x12,0x34,0x56. */
static void
test_width3(void)
{
    CASE(rsprot_p3_123, 0x123456, 0x12, 0x34, 0x56);
    CASE(rsprot_p3_321, 0x123456, 0x56, 0x34, 0x12);
    CASE(rsprot_p3_132, 0x123456, 0x12, 0x56, 0x34);
    CASE(rsprot_p3_213, 0x123456, 0x34, 0x12, 0x56);
}

/* Width 4 — pure permutation of bytes 1,2,3,4 = 0x12,0x34,0x56,0x78. */
static void
test_width4(void)
{
    CASE(rsprot_p4_1234, 0x12345678, 0x12, 0x34, 0x56, 0x78);
    CASE(rsprot_p4_4321, 0x12345678, 0x78, 0x56, 0x34, 0x12);
    CASE(rsprot_p4_3412, 0x12345678, 0x56, 0x78, 0x12, 0x34);
    CASE(rsprot_p4_2143, 0x12345678, 0x34, 0x12, 0x78, 0x56);
}

/* A combined id is a 4-byte field in the same four orders. Named apart from
 * p4 so a call site says which it means, but it must agree byte for byte. */
static void
test_combined_id_matches_p4(void)
{
    CASE(rsprot_pcombined_id_1234, 0x12345678, 0x12, 0x34, 0x56, 0x78);
    CASE(rsprot_pcombined_id_4321, 0x12345678, 0x78, 0x56, 0x34, 0x12);
    CASE(rsprot_pcombined_id_3412, 0x12345678, 0x56, 0x78, 0x12, 0x34);
    CASE(rsprot_pcombined_id_2143, 0x12345678, 0x34, 0x12, 0x78, 0x56);
}

/* Width 8: bytes 1..8 = 0x01..0x08. */
static void
test_width8(void)
{
    static const uint8_t want[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    RsprotBuf b;

    rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));
    rsprot_p8_12345678(&b, 0x0102030405060708LL);
    expect("rsprot_p8_12345678", &b, want, (int)sizeof(want));
}

/* Blobs: 01 02 03 forward, reversed, each + 128, and both. */
static void
test_data(void)
{
    static const uint8_t src[] = { 0x01, 0x02, 0x03 };
    static const uint8_t fwd[] = { 0x01, 0x02, 0x03 };
    static const uint8_t rev[] = { 0x03, 0x02, 0x01 };
    static const uint8_t add[] = { 0x81, 0x82, 0x83 };
    static const uint8_t rev_add[] = { 0x83, 0x82, 0x81 };
    RsprotBuf b;

    rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));
    rsprot_pdata(&b, src, 3);
    expect("rsprot_pdata", &b, fwd, 3);
    rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));
    rsprot_pdata_rev(&b, src, 3);
    expect("rsprot_pdata_rev", &b, rev, 3);
    rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));
    rsprot_pdata_add128(&b, src, 3);
    expect("rsprot_pdata_add128", &b, add, 3);
    rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));
    rsprot_pdata_rev_add128(&b, src, 3);
    expect("rsprot_pdata_rev_add128", &b, rev_add, 3);
}

/*
 * Every reader is its writer's inverse.
 *
 * Pinning the bytes above proves the writers; this proves the `g` half is the
 * matching permutation and not, say, the same order applied twice. Both halves
 * matter because the codec layer runs one function in both directions.
 */
static void
test_readers_invert_writers(void)
{
#define ROUND(pfn, gfn, value)                                                                     \
    do {                                                                                           \
        RsprotBuf b;                                                                           \
        int32_t got;                                                                               \
        rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));                                      \
        pfn(&b, (value));                                                                          \
        rsprot_buf_wrap_read(&b, g_mem, b.wpos);                                                  \
        got = gfn(&b);                                                                             \
        if (got != (value)) {                                                                      \
            printf("FAIL %-28s read back 0x%X, wrote 0x%X\n", #gfn, got, (value));                 \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

    ROUND(rsprot_p1, rsprot_g1, 0x12);
    ROUND(rsprot_p1_add128, rsprot_g1_add128, 0x12);
    ROUND(rsprot_p1_neg, rsprot_g1_neg, 0x12);
    ROUND(rsprot_p1_sub128, rsprot_g1_sub128, 0x12);
    ROUND(rsprot_p2_12, rsprot_g2_12, 0x1234);
    ROUND(rsprot_p2_21, rsprot_g2_21, 0x1234);
    ROUND(rsprot_p2_12_add128, rsprot_g2_12_add128, 0x1234);
    ROUND(rsprot_p2_21_add128, rsprot_g2_21_add128, 0x1234);
    ROUND(rsprot_p3_123, rsprot_g3_123, 0x123456);
    ROUND(rsprot_p3_321, rsprot_g3_321, 0x123456);
    ROUND(rsprot_p3_132, rsprot_g3_132, 0x123456);
    ROUND(rsprot_p3_213, rsprot_g3_213, 0x123456);
    ROUND(rsprot_p4_1234, rsprot_g4_1234, 0x12345678);
    ROUND(rsprot_p4_4321, rsprot_g4_4321, 0x12345678);
    ROUND(rsprot_p4_3412, rsprot_g4_3412, 0x12345678);
    ROUND(rsprot_p4_2143, rsprot_g4_2143, 0x12345678);
    ROUND(rsprot_psmart1or2, rsprot_gsmart1or2, 100);
    ROUND(rsprot_psmart1or2, rsprot_gsmart1or2, 20000);
    ROUND(rsprot_psmart2or4, rsprot_gsmart2or4, 30000);
    ROUND(rsprot_pvarint2, rsprot_gvarint2, 123456);
#undef ROUND
}

/*
 * The variable-width boundary, pinned.
 *
 * Smart1or2 is the encoding `src/net` had five private copies of, and its
 * whole content is where the 1-byte form stops. 127 must be one byte and 128
 * must be two; a copy that got this off by one would round-trip against itself
 * perfectly and disagree with the other four.
 */
static void
test_smart_boundary(void)
{
    RsprotBuf b;

    rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));
    rsprot_psmart1or2(&b, 127);
    if (b.wpos != 1) { printf("FAIL smart1or2(127) wrote %d bytes, want 1\n", b.wpos); g_failures++; }

    rsprot_buf_wrap(&b, g_mem, (int32_t)sizeof(g_mem));
    rsprot_psmart1or2(&b, 128);
    if (b.wpos != 2) { printf("FAIL smart1or2(128) wrote %d bytes, want 2\n", b.wpos); g_failures++; }
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    test_width1();
    test_width2();
    test_width3();
    test_width4();
    test_combined_id_matches_p4();
    test_width8();
    test_data();
    test_readers_invert_writers();
    test_smart_boundary();

    if (g_failures) {
        printf("rsprot_buf names: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rsprot_buf names: every name matches the bytes it claims\n");
    return 0;
}
