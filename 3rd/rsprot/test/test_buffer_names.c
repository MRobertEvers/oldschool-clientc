/*
 * RSProt_Buffer — every name checked against the bytes it claims.
 *
 *     make -C 3rd/rsprot test
 *
 * The rename from `Alt1`/`Alt2`/`Alt3` to `_be`/`_le`/`_3412`/`_add128` is only
 * worth anything if the names are true. An opaque `p4Alt2` is merely unhelpful;
 * a `p4_3412` that actually writes 1,2,3,4 is a lie that a reader will act on,
 * which is strictly worse than the ordinal it replaced.
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
#include "rsprot_buffer.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

static void
expect(const char *name, RSProt_Buffer *b, const uint8_t *want, int n)
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
        RSProt_Buffer b;                                                                           \
        RSProt_BufferWrap(&b, g_mem, (int32_t)sizeof(g_mem));                                      \
        fn(&b, (value));                                                                           \
        expect(#fn, &b, want, (int)sizeof(want));                                                  \
    } while (0)

/*
 * Width 1. No byte order exists, so every variant differs only in the
 * transform — which is why naming these `_be` would give four encodings one
 * name. 0x12 is 18.
 */
static void
test_width1(void)
{
    CASE(RSProt_BufferP1, 0x12, 0x12);
    CASE(RSProt_BufferP1_add128, 0x12, 0x92);              /* 18 + 128 = 146 */
    CASE(RSProt_BufferP1_neg, 0x12, 0xEE);                 /* -18 & 0xFF     */
    CASE(RSProt_BufferP1_sub128, 0x12, 0x6E);              /* 128 - 18 = 110 */
}

/*
 * Width 2 — the case that forced the transform into the name. `_be` and
 * `_be_add128` write the SAME byte order and different bytes; a positional-only
 * scheme would call them the same thing.
 */
static void
test_width2(void)
{
    CASE(RSProt_BufferP2Be, 0x1234, 0x12, 0x34);
    CASE(RSProt_BufferP2Le, 0x1234, 0x34, 0x12);
    CASE(RSProt_BufferP2Be_add128, 0x1234, 0x12, 0xB4);    /* 0x34 + 128 */
    CASE(RSProt_BufferP2Le_add128, 0x1234, 0xB4, 0x12);
}

/* Width 3 — pure permutation of bytes 1,2,3 = 0x12,0x34,0x56. */
static void
test_width3(void)
{
    CASE(RSProt_BufferP3Be, 0x123456, 0x12, 0x34, 0x56);
    CASE(RSProt_BufferP3Le, 0x123456, 0x56, 0x34, 0x12);
    CASE(RSProt_BufferP3_132, 0x123456, 0x12, 0x56, 0x34);
    CASE(RSProt_BufferP3_213, 0x123456, 0x34, 0x12, 0x56);
}

/* Width 4 — pure permutation of bytes 1,2,3,4 = 0x12,0x34,0x56,0x78. */
static void
test_width4(void)
{
    CASE(RSProt_BufferP4Be, 0x12345678, 0x12, 0x34, 0x56, 0x78);
    CASE(RSProt_BufferP4Le, 0x12345678, 0x78, 0x56, 0x34, 0x12);
    CASE(RSProt_BufferP4_3412, 0x12345678, 0x56, 0x78, 0x12, 0x34);
    CASE(RSProt_BufferP4_2143, 0x12345678, 0x34, 0x12, 0x78, 0x56);
}

/* A combined id is a 4-byte field in the same four orders. Named apart from
 * p4 so a call site says which it means, but it must agree byte for byte. */
static void
test_combined_id_matches_p4(void)
{
    CASE(RSProt_BufferPCombinedIdBe, 0x12345678, 0x12, 0x34, 0x56, 0x78);
    CASE(RSProt_BufferPCombinedIdLe, 0x12345678, 0x78, 0x56, 0x34, 0x12);
    CASE(RSProt_BufferPCombinedId_3412, 0x12345678, 0x56, 0x78, 0x12, 0x34);
    CASE(RSProt_BufferPCombinedId_2143, 0x12345678, 0x34, 0x12, 0x78, 0x56);
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
        RSProt_Buffer b;                                                                           \
        int32_t got;                                                                               \
        RSProt_BufferWrap(&b, g_mem, (int32_t)sizeof(g_mem));                                      \
        pfn(&b, (value));                                                                          \
        RSProt_BufferWrapRead(&b, g_mem, b.wpos);                                                  \
        got = gfn(&b);                                                                             \
        if (got != (value)) {                                                                      \
            printf("FAIL %-28s read back 0x%X, wrote 0x%X\n", #gfn, got, (value));                 \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

    ROUND(RSProt_BufferP1, RSProt_BufferG1, 0x12);
    ROUND(RSProt_BufferP1_add128, RSProt_BufferG1_add128, 0x12);
    ROUND(RSProt_BufferP1_neg, RSProt_BufferG1_neg, 0x12);
    ROUND(RSProt_BufferP1_sub128, RSProt_BufferG1_sub128, 0x12);
    ROUND(RSProt_BufferP2Be, RSProt_BufferG2Be, 0x1234);
    ROUND(RSProt_BufferP2Le, RSProt_BufferG2Le, 0x1234);
    ROUND(RSProt_BufferP2Be_add128, RSProt_BufferG2Be_add128, 0x1234);
    ROUND(RSProt_BufferP2Le_add128, RSProt_BufferG2Le_add128, 0x1234);
    ROUND(RSProt_BufferP3Be, RSProt_BufferG3Be, 0x123456);
    ROUND(RSProt_BufferP3Le, RSProt_BufferG3Le, 0x123456);
    ROUND(RSProt_BufferP3_132, RSProt_BufferG3_132, 0x123456);
    ROUND(RSProt_BufferP3_213, RSProt_BufferG3_213, 0x123456);
    ROUND(RSProt_BufferP4Be, RSProt_BufferG4Be, 0x12345678);
    ROUND(RSProt_BufferP4Le, RSProt_BufferG4Le, 0x12345678);
    ROUND(RSProt_BufferP4_3412, RSProt_BufferG4_3412, 0x12345678);
    ROUND(RSProt_BufferP4_2143, RSProt_BufferG4_2143, 0x12345678);
    ROUND(RSProt_BufferPSmart1or2, RSProt_BufferGSmart1or2, 100);
    ROUND(RSProt_BufferPSmart1or2, RSProt_BufferGSmart1or2, 20000);
    ROUND(RSProt_BufferPSmart2or4, RSProt_BufferGSmart2or4, 30000);
    ROUND(RSProt_BufferPVarInt2, RSProt_BufferGVarInt2, 123456);
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
    RSProt_Buffer b;

    RSProt_BufferWrap(&b, g_mem, (int32_t)sizeof(g_mem));
    RSProt_BufferPSmart1or2(&b, 127);
    if (b.wpos != 1) { printf("FAIL smart1or2(127) wrote %d bytes, want 1\n", b.wpos); g_failures++; }

    RSProt_BufferWrap(&b, g_mem, (int32_t)sizeof(g_mem));
    RSProt_BufferPSmart1or2(&b, 128);
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
    test_readers_invert_writers();
    test_smart_boundary();

    if (g_failures) {
        printf("rsprot_buffer names: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rsprot_buffer names: every name matches the bytes it claims\n");
    return 0;
}
