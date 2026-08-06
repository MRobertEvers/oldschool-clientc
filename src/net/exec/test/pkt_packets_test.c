/*
 * Packet-file convention tests.
 *
 *   make -C src test-pktpackets
 *
 * Proves, on a real measured packet, the four things every file under
 * 3rd/rsprot/packets/ promises: one message struct across versions, one codec
 * per layout running both directions, greppable per-revision aliases, and a
 * range table that answers "no such packet here" rather than falling back.
 *
 * **These tests live on the server side, not in 3rd/rsprot/test/, and that is a
 * consequence of the split rather than an accident.** rsprot declares the
 * executor vocabulary and never implements it, so a packet codec cannot run
 * without a host. Testing them here is testing them the way they will actually
 * be used.
 *
 * IF_OPENTOP is the right packet to pin this on: it has 13 distinct layouts
 * across revisions 221-239 while keeping ONE class name the whole way, so it
 * demonstrates both why versions are the unit and why they cannot be keyed on
 * RSProt's class names or its `V2` suffix.
 */
#include "packets/if_opentop.h"

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

static uint8_t g_bytes[64];

/*
 * The bytes, pinned per version.
 *
 * A round trip cannot catch a wrong version being selected — both versions are
 * two bytes and both round-trip perfectly through themselves. Only the byte
 * pattern distinguishes them, which is exactly the failure mode
 * docs/RSPROT_OSRS239_PORT.md describes: "a packet like that frames perfectly,
 * passes every length assert, and arrives as a different interface."
 */
static void
test_each_version_writes_its_own_bytes(void)
{
    MsgIfOpenTop m = { .interface_id = 0x1234 };
    RsprotExec x;
    RsprotBuf buf;

    /* v6, rev 227-230: p2Alt1 = little endian. */
    rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
    rsprot_exec_encode(&x, &buf);
    packet_if_opentop_v6_out(&x, &m);
    CHECK(rsprot_exec_ok(&x));
    CHECK_EQ(buf.wpos, 2);
    CHECK_EQ(g_bytes[0], 0x34);
    CHECK_EQ(g_bytes[1], 0x12);

    /* v13, rev 239: p2Alt2 = [hi, lo + 128]. Same two bytes, different two bytes. */
    rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
    rsprot_exec_encode(&x, &buf);
    packet_if_opentop_v13_out(&x, &m);
    CHECK(rsprot_exec_ok(&x));
    CHECK_EQ(buf.wpos, 2);
    CHECK_EQ(g_bytes[0], 0x12);
    CHECK_EQ(g_bytes[1], (0x34 + 128) & 0xff);
}

/** The same function decodes. This is the whole premise. */
static void
test_one_function_runs_both_directions(void)
{
    MsgIfOpenTop sent = { .interface_id = 0x1234 };
    MsgIfOpenTop got;
    RsprotExec x;
    RsprotBuf buf;

    memset(&got, 0, sizeof(got));

    rsprot_buf_wrap(&buf, g_bytes, sizeof(g_bytes));
    rsprot_exec_encode(&x, &buf);
    packet_if_opentop_v13_out(&x, &sent);

    rsprot_buf_wrap_read(&buf, g_bytes, buf.wpos);
    rsprot_exec_decode(&x, &buf);
    packet_if_opentop_v13_out(&x, &got);
    CHECK(rsprot_exec_ok(&x));
    CHECK_EQ(got.interface_id, sent.interface_id);
    CHECK_EQ(buf.rpos, 2); /* consumed exactly the packet */
}

/** The per-revision alias is the version, not a wrapper around it. */
static void
test_aliases_resolve_to_versions(void)
{
    CHECK(packet_if_opentop_rev227_out == packet_if_opentop_v6_out);
    CHECK(packet_if_opentop_rev230_out == packet_if_opentop_v6_out);
    CHECK(packet_if_opentop_rev239_out == packet_if_opentop_v13_out);
}

/**
 * The range table picks by revision — and answers NULL in the gap.
 *
 * Revisions 231-238 have their own layouts (v7..v12) that this build does not
 * carry. NULL is a real answer the caller reports once; falling back to a
 * neighbouring revision's bytes would frame cleanly and mean something else,
 * which is the failure the whole design exists to remove.
 */
static void
test_range_table_picks_and_refuses(void)
{
    const RsprotVersionRange *r = rsprot_if_opentop_out;
    const int n = rsprot_if_opentop_out_count;

    CHECK(rsprot_version_pick(r, n, 227) == (RsprotCodecFn)packet_if_opentop_v6_out);
    CHECK(rsprot_version_pick(r, n, 230) == (RsprotCodecFn)packet_if_opentop_v6_out);
    CHECK(rsprot_version_pick(r, n, 239) == (RsprotCodecFn)packet_if_opentop_v13_out);

    CHECK(rsprot_version_pick(r, n, 235) == NULL); /* in the gap */
    CHECK(rsprot_version_pick(r, n, 221) == NULL); /* before the first range */
    CHECK(rsprot_version_pick(r, n, 240) == NULL); /* after the last */
}

/**
 * DESCRIBE makes each version state its own layout, which is what a
 * cross-version diff is built on and what gives a wrong layout a symptom.
 */
static void
test_describe_distinguishes_the_versions(void)
{
    MsgIfOpenTop m = { .interface_id = 0 };
    RsprotExec x;
    RsprotSchema s;

    rsprot_exec_describe(&x, &s);
    packet_if_opentop_v6_out(&x, &m);
    CHECK(rsprot_exec_ok(&x));
    CHECK_EQ(s.count, 1);
    CHECK_EQ(s.fields[0].prim, RSPROT_PRIM_U2_ALT1);
    CHECK_EQ(s.fields[0].offset, 0);
    /* Cross-check against the revision's {opcode, size} table: IF_OPENTOP is
     * 2 bytes at both 230 and 239, and the codec agrees. */
    CHECK_EQ(rsprot_schema_fixed_size(&s), 2);

    rsprot_exec_describe(&x, &s);
    packet_if_opentop_v13_out(&x, &m);
    CHECK(rsprot_exec_ok(&x));
    CHECK_EQ(s.count, 1);
    CHECK_EQ(s.fields[0].prim, RSPROT_PRIM_U2_ALT2);
    CHECK_EQ(rsprot_schema_fixed_size(&s), 2);

    /* Same size, same field, different primitive — the diff a length check
     * cannot see. */
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    test_each_version_writes_its_own_bytes();
    test_one_function_runs_both_directions();
    test_aliases_resolve_to_versions();
    test_range_table_picks_and_refuses();
    test_describe_distinguishes_the_versions();

    if (g_failures) {
        printf("pkt_packets: %d failure%s\n", g_failures, g_failures == 1 ? "" : "s");
        return 1;
    }
    printf("pkt_packets: all tests passed\n");
    return 0;
}
