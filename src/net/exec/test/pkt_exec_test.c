/*
 * Packet executor tests.
 *
 *   make -C src test-pktexec
 *
 * The load-bearing property is the one the subsystem exists for: **a codec
 * written once is correct in both directions.** So the central test is not
 * "does u2_alt2 work" but fill -> encode -> decode -> compare over codecs that
 * exercise every primitive and every structure form real packets are made of —
 * a `switch` on a transferred discriminator, a count-driven loop, a string, a
 * packed bit byte.
 *
 * Two things are asserted that a round trip alone cannot catch:
 *
 *  - **byte patterns**, because a symmetric value round-trips through a
 *    transposed pair perfectly. A codec that writes two fields in the wrong
 *    order and reads them back in the same wrong order passes every round trip
 *    ever written. `test_layout_is_not_just_symmetric` pins real bytes.
 *  - **the branch rule**, because a codec that reads a field before
 *    transferring it encodes correctly and decodes garbage — the exact failure
 *    with no frame-level symptom. It is proved by *mutating a correct codec
 *    into a wrong one*, not by asserting on a codec written to fail.
 */
#include "rsprot_exec.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                 \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
    do {                                                                                           \
        long long _a = (long long)(actual);                                                        \
        long long _e = (long long)(expected);                                                      \
        if (_a != _e) {                                                                            \
            printf("FAIL %s:%d  %s = %lld, want %lld\n", __FILE__, __LINE__, #actual, _a, _e);     \
            g_failures++;                                                                          \
        }                                                                                          \
    } while (0)

static uint8_t g_bytes[4096];

/* ------------------------------------------------------------------ */
/* Messages and codecs under test                                      */
/* ------------------------------------------------------------------ */

/** Every scalar primitive, once. */
typedef struct MsgAllPrims
{
    int32_t u1, u1a1, u1a2, u1a3, i1;
    int32_t u2, u2a1, u2a2, u2a3, i2;
    int32_t u3, u3a1, u3a2, u3a3;
    int32_t u4, u4a1, u4a2, u4a3;
    int32_t com, coma1, coma2, coma3;
    int32_t smart, smartnull, smart24null, varint2, varint2s, boolean;
    int64_t u8;
} MsgAllPrims;

static void
codec_all_prims(RsprotExec *x, MsgAllPrims *m)
{
    RSPROT_U1(x, m->u1);
    RSPROT_U1_ALT1(x, m->u1a1);
    RSPROT_U1_ALT2(x, m->u1a2);
    RSPROT_U1_ALT3(x, m->u1a3);
    RSPROT_I1(x, m->i1);
    RSPROT_U2(x, m->u2);
    RSPROT_U2_ALT1(x, m->u2a1);
    RSPROT_U2_ALT2(x, m->u2a2);
    RSPROT_U2_ALT3(x, m->u2a3);
    RSPROT_I2(x, m->i2);
    RSPROT_U3(x, m->u3);
    RSPROT_U3_ALT1(x, m->u3a1);
    RSPROT_U3_ALT2(x, m->u3a2);
    RSPROT_U3_ALT3(x, m->u3a3);
    RSPROT_U4(x, m->u4);
    RSPROT_U4_ALT1(x, m->u4a1);
    RSPROT_U4_ALT2(x, m->u4a2);
    RSPROT_U4_ALT3(x, m->u4a3);
    RSPROT_COM(x, m->com);
    RSPROT_COM_ALT1(x, m->coma1);
    RSPROT_COM_ALT2(x, m->coma2);
    RSPROT_COM_ALT3(x, m->coma3);
    RSPROT_SMART(x, m->smart);
    RSPROT_SMARTNULL(x, m->smartnull);
    RSPROT_SMART2OR4NULL(x, m->smart24null);
    RSPROT_VARINT2(x, m->varint2);
    RSPROT_VARINT2S(x, m->varint2s);
    RSPROT_BOOL(x, m->boolean);
    RSPROT_U8(x, m->u8);
}

/**
 * The real shape of a modern packet: a discriminator, a `switch` on it, a
 * bounded count-driven loop, a string, and a packed bit byte.
 *
 * This is the codec that would have needed four bespoke bytecode instructions.
 * Here it is a switch and a for loop.
 */
#define MSG_MAX_SLOTS 8

typedef struct MsgShapes
{
    int32_t kind;
    int32_t a, b, c;
    int32_t count;
    int32_t slot_id[MSG_MAX_SLOTS];
    int32_t slot_qty[MSG_MAX_SLOTS];
    const char *text;
    int32_t bits_hi, bits_lo;
} MsgShapes;

static void
codec_shapes(RsprotExec *x, MsgShapes *m)
{
    RSPROT_U1(x, m->kind);

    /* Branching on `kind`, transferred one line above — the rule in the form
     * real codecs use it. A tagged-union payload is exactly this. */
    switch (RSPROT_BRANCH(x, m->kind)) {
    case 0:
        RSPROT_U2_ALT2(x, m->a);
        break;
    case 1:
        RSPROT_U4_ALT3(x, m->b);
        RSPROT_U1_ALT1(x, m->c);
        break;
    default:
        RSPROT_U2(x, m->a);
        RSPROT_U2_ALT1(x, m->b);
        break;
    }

    int32_t n = RSPROT_COUNT(x, m->count, MSG_MAX_SLOTS);
    for (int32_t i = 0; i < n; i++) {
        rsprot_x_u2_alt1(x, &m->slot_id[i], "slot_id");
        rsprot_x_smart(x, &m->slot_qty[i], "slot_qty");
    }

    RSPROT_JSTR(x, m->text);

    rsprot_xbits_begin(x);
    RSPROT_BITS(x, 5, m->bits_hi);
    RSPROT_BITS(x, 3, m->bits_lo);
    rsprot_xbits_end(x);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void
test_primitive_table_is_complete(void)
{
    for (int i = 0; i < RSPROT_PRIM_COUNT; i++) {
        const char *name = rsprot_prim_name((RsprotPrim)i);
        if (!name) {
            printf("FAIL primitive %d has no table row\n", i);
            g_failures++;
            continue;
        }

        RsprotExec x;
        RsprotBuf buf;
        int32_t v = 1;
        int32_t back = 0;

        rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
        rsprot_exec_encode(&x, &buf);
        rsprot_x(&x, (RsprotPrim)i, &v, name);
        CHECK(rsprot_exec_ok(&x));
        if (buf.wpos == 0) {
            printf("FAIL primitive %s wrote no bytes\n", name);
            g_failures++;
            continue;
        }

        rsprot_buf_wrap_read(&buf, g_bytes, buf.wpos);
        rsprot_exec_decode(&x, &buf);
        rsprot_x(&x, (RsprotPrim)i, &back, name);
        if (back != v) {
            printf("FAIL primitive %s round trip: got %d want %d\n", name, back, v);
            g_failures++;
        }
    }
}

static void
test_roundtrip_all_prims(void)
{
    /* Many seeds: one seed is one sample of the value space, and the masking in
     * fill_value is exactly what a single sample would not exercise. */
    for (uint32_t seed = 1; seed <= 128; seed++) {
        MsgAllPrims sent;
        MsgAllPrims got;
        RsprotExec x;
        RsprotBuf buf;

        memset(&sent, 0, sizeof(sent));
        memset(&got, 0, sizeof(got));

        rsprot_exec_fill(&x, seed);
        codec_all_prims(&x, &sent);
        CHECK(rsprot_exec_ok(&x));

        rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
        rsprot_exec_encode(&x, &buf);
        codec_all_prims(&x, &sent);
        CHECK(rsprot_exec_ok(&x));
        if (!rsprot_buf_ok(&buf)) {
            printf("FAIL seed %u: encode set buf err 0x%x\n", seed, buf.err);
            g_failures++;
            continue;
        }

        int32_t written = buf.wpos;
        rsprot_buf_wrap_read(&buf, g_bytes, written);
        rsprot_exec_decode(&x, &buf);
        codec_all_prims(&x, &got);
        CHECK(rsprot_exec_ok(&x));
        CHECK(rsprot_buf_ok(&buf));
        CHECK_EQ(buf.rpos, written); /* consumed exactly what was produced */

        if (memcmp(&sent, &got, sizeof(sent)) != 0) {
            printf("FAIL seed %u: round trip differs\n", seed);
            g_failures++;
        }
    }
}

static void
test_roundtrip_shapes(void)
{
    for (uint32_t seed = 1; seed <= 128; seed++) {
        MsgShapes sent;
        MsgShapes got;
        RsprotExec x;
        RsprotBuf buf;

        memset(&sent, 0, sizeof(sent));
        memset(&got, 0, sizeof(got));

        rsprot_exec_fill(&x, seed);
        codec_shapes(&x, &sent);
        CHECK(rsprot_exec_ok(&x));
        CHECK(sent.count >= 0 && sent.count <= MSG_MAX_SLOTS);

        rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
        rsprot_exec_encode(&x, &buf);
        codec_shapes(&x, &sent);
        CHECK(rsprot_exec_ok(&x));
        CHECK(rsprot_buf_ok(&buf));

        int32_t written = buf.wpos;
        rsprot_buf_wrap_read(&buf, g_bytes, written);
        rsprot_exec_decode(&x, &buf);
        codec_shapes(&x, &got);
        CHECK(rsprot_exec_ok(&x));
        CHECK(rsprot_buf_ok(&buf));

        CHECK_EQ(got.kind, sent.kind);
        CHECK_EQ(got.count, sent.count);
        CHECK_EQ(got.bits_hi, sent.bits_hi);
        CHECK_EQ(got.bits_lo, sent.bits_lo);
        for (int32_t i = 0; i < sent.count; i++) {
            CHECK_EQ(got.slot_id[i], sent.slot_id[i]);
            CHECK_EQ(got.slot_qty[i], sent.slot_qty[i]);
        }
        CHECK(got.text && strcmp(got.text, sent.text) == 0);

        /* The `switch` must have taken the same arm in both directions —
         * otherwise the two sides move different field counts and the compare
         * above would only pass by luck. */
        switch (sent.kind) {
        case 0:
            CHECK_EQ(got.a, sent.a);
            break;
        case 1:
            CHECK_EQ(got.b, sent.b);
            CHECK_EQ(got.c, sent.c);
            break;
        default:
            CHECK_EQ(got.a, sent.a);
            CHECK_EQ(got.b, sent.b);
            break;
        }
    }
}

/*
 * Pin real bytes for a layout whose fields cannot be confused. Expectations
 * come from RSProt's documented transforms, which 3rd/rsprot's own test_buf
 * pins independently.
 */
static void
test_layout_is_not_just_symmetric(void)
{
    MsgShapes m;
    RsprotExec x;
    RsprotBuf buf;

    memset(&m, 0, sizeof(m));
    m.kind = 1;
    m.b = 0x11223344;
    m.c = 0x42;
    m.count = 0;
    m.text = "hi";
    m.bits_hi = 0x15; /* 10101 */
    m.bits_lo = 0x03; /*       011 */

    rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
    rsprot_exec_encode(&x, &buf);
    codec_shapes(&x, &m);
    CHECK(rsprot_exec_ok(&x));
    CHECK(rsprot_buf_ok(&buf));

    CHECK_EQ(g_bytes[0], 1); /* kind */
    /* b as u4_alt3 = [b2,b3,b0,b1] */
    CHECK_EQ(g_bytes[1], 0x22);
    CHECK_EQ(g_bytes[2], 0x11);
    CHECK_EQ(g_bytes[3], 0x44);
    CHECK_EQ(g_bytes[4], 0x33);
    /* c as u1_alt1 = v + 128 */
    CHECK_EQ(g_bytes[5], (0x42 + 128) & 0xff);
    CHECK_EQ(g_bytes[6], 0); /* count */
    CHECK_EQ(g_bytes[7], 'h');
    CHECK_EQ(g_bytes[8], 'i');
    CHECK_EQ(g_bytes[9], 0);
    CHECK_EQ(g_bytes[10], 0xAB); /* 10101 011, high bits first */
    CHECK_EQ(buf.wpos, 11);
}

/*
 * DESCRIBE reports the codec's own field list — what makes a layout bug visible
 * and what a schema diff between two versions is built on.
 */
static void
test_describe_reports_the_layout(void)
{
    MsgShapes m;
    RsprotExec x;
    RsprotSchema schema;

    memset(&m, 0, sizeof(m));
    rsprot_exec_describe(&x, &schema);
    codec_shapes(&x, &m);
    CHECK(rsprot_exec_ok(&x));
    CHECK(!schema.truncated);

    /* kind, then the `case 0` arm (BRANCH returns 0 in DESCRIBE), then count,
     * the string, and the two bit fields. */
    CHECK_EQ(schema.count, 6);
    CHECK_EQ(schema.fields[0].kind, RSPROT_FIELD_PRIM);
    CHECK_EQ(schema.fields[0].prim, RSPROT_PRIM_U1);
    CHECK_EQ(schema.fields[0].offset, 0);
    CHECK(strcmp(schema.fields[0].name, "m->kind") == 0);
    CHECK_EQ(schema.fields[1].prim, RSPROT_PRIM_U2_ALT2);
    CHECK_EQ(schema.fields[1].offset, 1);
    CHECK_EQ(schema.fields[2].offset, 3); /* count */
    CHECK_EQ(schema.fields[3].kind, RSPROT_FIELD_STR);
    CHECK_EQ(schema.fields[3].offset, -1);
    /* Everything after a variable-width field has an unknown offset. */
    CHECK_EQ(schema.fields[4].kind, RSPROT_FIELD_BITS);
    CHECK_EQ(schema.fields[4].bit_width, 5);
    CHECK_EQ(schema.fields[4].offset, -1);

    /* A packet with a string has no fixed size, and says so. */
    CHECK_EQ(rsprot_schema_fixed_size(&schema), -1);

    /* A fixed-width one reports its real size. This is the cross-check against
     * a revision's {opcode, size} table. */
    typedef struct MsgFixed
    {
        int32_t a, b, c;
    } MsgFixed;
    MsgFixed f;
    memset(&f, 0, sizeof(f));
    rsprot_exec_describe(&x, &schema);
    rsprot_x_u1(&x, &f.a, "a");
    rsprot_x_u2_alt2(&x, &f.b, "b");
    rsprot_x_com_alt3(&x, &f.c, "c");
    CHECK_EQ(rsprot_schema_fixed_size(&schema), 1 + 2 + 4);
}

/*
 * The branch rule, proved by breaking a correct codec.
 *
 * A test that only asserts the good path proves nothing about the guard — it
 * would pass with the guard deleted. So mutate the codec into the wrong shape
 * and assert the executor refuses it.
 */
static void
codec_branches_too_early(RsprotExec *x, MsgShapes *m)
{
    /* WRONG ON PURPOSE: `kind` has not been transferred, so decoding would
     * branch on uninitialised memory while encoding branches on a real value. */
    if (RSPROT_BRANCH(x, m->kind) == 1)
        RSPROT_U4(x, m->b);
    else
        RSPROT_U2(x, m->a);
    RSPROT_U1(x, m->kind);
}

static void
test_branch_rule_is_enforced(void)
{
    MsgShapes m;
    RsprotExec x;
    RsprotSchema schema;

    memset(&m, 0, sizeof(m));

    rsprot_exec_describe(&x, &schema);
    codec_shapes(&x, &m);
    CHECK(rsprot_exec_ok(&x)); /* the correct codec passes */

    rsprot_exec_describe(&x, &schema);
    codec_branches_too_early(&x, &m);
    CHECK(!rsprot_exec_ok(&x)); /* the mutated one does not */
    CHECK(x.error_reason != NULL);
}

/*
 * The count bound, checked directly — no loop, no array.
 *
 * This exists separately from the codec-level test below because the codec-level
 * one detects a missing bound by *crashing*: the loop runs 255 times into an
 * 8-slot array and the stack protector fires. That is a detection, but a bad
 * one — exit 134 with no message, and (since stdout is block-buffered) every
 * earlier result discarded with it. This test names the failure instead.
 */
static void
test_count_bound_is_checked_directly(void)
{
    RsprotExec x;
    RsprotBuf buf;
    uint8_t payload[] = { 200 };
    int32_t n = 0;

    rsprot_buf_wrap_read(&buf, payload, sizeof(payload));
    rsprot_exec_decode(&x, &buf);
    CHECK_EQ(rsprot_xcount(&x, RSPROT_PRIM_U1, &n, 8, "n"), 0);
    CHECK(!rsprot_exec_ok(&x));
    CHECK_EQ(n, 0); /* zeroed, so a caller that ignores the error still cannot loop */

    /* An in-range count is returned untouched. */
    payload[0] = 5;
    rsprot_buf_wrap_read(&buf, payload, sizeof(payload));
    rsprot_exec_decode(&x, &buf);
    CHECK_EQ(rsprot_xcount(&x, RSPROT_PRIM_U1, &n, 8, "n"), 5);
    CHECK(rsprot_exec_ok(&x));
}

/*
 * A decoded count driven out of range by a corrupt packet must be an error, not
 * a loop bound. Today every inbound handler hand-checks this and the ones that
 * forget are the interesting ones.
 */
static void
test_count_out_of_range_fails(void)
{
    MsgShapes m;
    RsprotExec x;
    RsprotBuf buf;
    uint8_t payload[16];

    memset(&m, 0, sizeof(m));
    memset(payload, 0, sizeof(payload));
    payload[0] = 0;    /* kind = 0 */
    payload[1] = 0x00; /* a, u2_alt2 */
    payload[2] = 0x80;
    payload[3] = 0xff; /* count = 255, far above MSG_MAX_SLOTS */

    rsprot_buf_wrap_read(&buf, payload, sizeof(payload));
    rsprot_exec_decode(&x, &buf);
    codec_shapes(&x, &m);

    CHECK(!rsprot_exec_ok(&x));
    CHECK(x.error_reason != NULL);
}

/** An unterminated string is a malformed packet, not an empty one. */
static void
test_unterminated_string_fails(void)
{
    RsprotExec x;
    RsprotBuf buf;
    uint8_t payload[] = { 'a', 'b', 'c' };
    const char *s = NULL;

    rsprot_buf_wrap_read(&buf, payload, sizeof(payload));
    rsprot_exec_decode(&x, &buf);
    rsprot_xstr(&x, &s, RSPROT_STR_PLAIN, "s");
    CHECK(!rsprot_exec_ok(&x));
    CHECK(s == NULL);
}

/**
 * A nullable string distinguishes "absent" from "empty", and neither is an
 * error. Conflating them is a real bug shape — an absent name is not a blank
 * name.
 */
static void
test_nullable_string_absent_is_not_an_error(void)
{
    RsprotExec x;
    RsprotBuf buf;
    const char *s = "unset";

    rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
    rsprot_exec_encode(&x, &buf);
    s = NULL;
    rsprot_xstr(&x, &s, RSPROT_STR_NULLABLE, "s");
    CHECK(rsprot_exec_ok(&x));

    int32_t written = buf.wpos;
    rsprot_buf_wrap_read(&buf, g_bytes, written);
    rsprot_exec_decode(&x, &buf);
    s = "unset";
    rsprot_xstr(&x, &s, RSPROT_STR_NULLABLE, "s");
    CHECK(rsprot_exec_ok(&x));
    CHECK(s == NULL);
}

/**
 * The version table answers "which layout does this revision speak", and
 * answers "none" for a revision that lacks the packet — a real answer, not an
 * error.
 */
static void
codec_v6(RsprotExec *x, void *m)
{
    (void)x;
    (void)m;
}
static void
codec_v13(RsprotExec *x, void *m)
{
    (void)x;
    (void)m;
}

static void
test_version_pick(void)
{
    static const RsprotVersionRange ranges[] = {
        { 227, 230, codec_v6 },
        { 239, 239, codec_v13 },
    };
    const int n = (int)(sizeof(ranges) / sizeof(ranges[0]));

    CHECK(rsprot_version_pick(ranges, n, 227) == codec_v6);
    CHECK(rsprot_version_pick(ranges, n, 230) == codec_v6);
    CHECK(rsprot_version_pick(ranges, n, 239) == codec_v13);
    /* Inside the gap and outside both ends: no such packet here. */
    CHECK(rsprot_version_pick(ranges, n, 235) == NULL);
    CHECK(rsprot_version_pick(ranges, n, 221) == NULL);
    CHECK(rsprot_version_pick(ranges, n, 240) == NULL);
}

int
main(void)
{
    /* Unbuffered: a test further down can abort the process (an unbounded count
     * smashes the stack, by design of what it is testing), and block-buffered
     * stdout would discard every result printed before it. A crash should cost
     * you the crashing test's output, not the whole run's. */
    setvbuf(stdout, NULL, _IONBF, 0);

    test_primitive_table_is_complete();
    test_roundtrip_all_prims();
    test_roundtrip_shapes();
    test_layout_is_not_just_symmetric();
    test_describe_reports_the_layout();
    test_branch_rule_is_enforced();
    test_count_bound_is_checked_directly();
    test_count_out_of_range_fails();
    test_unterminated_string_fails();
    test_nullable_string_absent_is_not_an_error();
    test_version_pick();

    if (g_failures) {
        printf("pkt_exec: %d failure%s\n", g_failures, g_failures == 1 ? "" : "s");
        return 1;
    }
    printf("pkt_exec: all tests passed\n");
    return 0;
}
