/*
 * RUNCLIENTSCRIPT (op 84) on the wire — the mixed int/string payload.
 *
 * This is the client-side half of Blocker A. The server-side half (compiler ->
 * bytecode -> VM -> host callback) is `serverscript/test/ssc_test.c`'s
 * `test_runclientscript_vararg`; this file starts where that one stops, at the
 * bytes, and ends where `App_RunClientScript` picks the struct up.
 *
 * Layout, from `mock230_send_run_clientscript_mixed` (`net/mock/mock230_encode.c`):
 *
 *     p1 * argc      one type character per argument, in DECLARATION order
 *     p1             '\n'
 *     args           in REVERSE order: 's' -> jstr (newline-terminated),
 *                    anything else -> p4
 *     p4             the clientscript id
 *
 * The reversal is not a quirk of this port: the reference's writer pushes the
 * CS2 operand stack, which unwinds last-argument-first, so the type string is
 * forward and the values are backward. That asymmetry is the single most
 * likely thing to get wrong, and an all-int payload of symmetric values hides
 * it completely — which is why every case below is asymmetric and at least one
 * is mixed.
 *
 * The writer here mirrors the encoder rather than linking it: `mock230_encode.c`
 * writes through a file-static arena and a `struct Mock230Player`, so linking it
 * pulls in the whole server. The encoder's own output is asserted, byte by byte,
 * by mock230's `--selftest` (the skill guide's four-int payload); what is NOT
 * asserted anywhere else, and is asserted here, is that `osrs230_parse` reads
 * that layout back — including what it does when the payload does not fit.
 */

#include "net/rev/gameproto_revisions.h"
#include "net/rev/pktnames.h"
#include "net/rev/revpacket.h"
#include "rsareabuf.h"

#include <stdio.h>
#include <string.h>

/* net/rev/osrs230/osrs230_parse.c */
int
osrs230_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do                                                                         \
    {                                                                          \
        if( cond )                                                             \
        {                                                                      \
            printf("  ok   %s\n", (msg));                                      \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__);          \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

#define CHECK_EQ(got, want, msg)                                               \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), wv = (long long)(want);               \
        if( gv == wv )                                                         \
        {                                                                      \
            printf("  ok   %s == %lld\n", (msg), gv);                          \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want %lld (%s:%d)\n", (msg), gv, wv,   \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

/** The encoder's layout, written into `out` (caller-owned). Returns the length. */
static int
write_runclientscript(
    uint8_t* out,
    size_t capacity,
    int script_id,
    const char* types,
    int const* intv,
    const char* const* strv,
    int argc)
{
    struct RSAreaBuf buf;

    rsab_wrap(&buf, out, capacity);
    for( int i = 0; i < argc; i++ )
        rsab_p1(&buf, (uint8_t)types[i]);
    rsab_p1(&buf, '\n');
    for( int i = argc - 1; i >= 0; i-- )
    {
        if( types[i] == 's' )
            rsab_pjstr(&buf, strv && strv[i] ? strv[i] : "", RSAB_JSTR_NEWLINE);
        else
            rsab_p4(&buf, intv ? intv[i] : 0);
    }
    rsab_p4(&buf, script_id);
    return rsab_ok(&buf) ? (int)rsab_len(&buf) : -1;
}

static int
parse(uint8_t const* data, int len, struct RevPacket* out)
{
    memset(out, 0, sizeof(*out));
    return osrs230_parse(NULL, PKT_NAME_RUNCLIENTSCRIPT, data, len, out);
}

/*
 * The shape Blocker A exists for: several ints and a string in one call.
 *
 * Every int is distinct and none is a small index, so a reversed read produces
 * different numbers rather than a plausible permutation, and the string sits in
 * the middle so a fencepost error in `str_mask` moves it rather than losing it.
 */
static void
test_mixed_roundtrip(void)
{
    uint8_t bytes[512];
    struct RevPacket pkt;
    const int intv[5] = { 11, 22, 0, 44, 0 };
    const char* strv[5] = { NULL, NULL, "herb patch", NULL, "second" };
    int len;

    printf("mixed int/string payload\n");

    len = write_runclientscript(bytes, sizeof(bytes), 1074, "iisis", intv, strv, 5);
    CHECK(len > 0, "the payload encodes");
    if( len <= 0 )
        return;

    CHECK_EQ(parse(bytes, len, &pkt), 1, "and parses");
    CHECK_EQ(pkt._runclientscript.script_id, 1074, "the script id survives");
    CHECK_EQ(pkt._runclientscript.argc, 5, "five arguments");
    CHECK_EQ(pkt._runclientscript.intv[0], 11, "int 0");
    CHECK_EQ(pkt._runclientscript.intv[1], 22, "int 1");
    CHECK_EQ(pkt._runclientscript.intv[3], 44, "int 3");
    CHECK(strcmp(pkt._runclientscript.strv[2], "herb patch") == 0, "string 2");
    CHECK(strcmp(pkt._runclientscript.strv[4], "second") == 0, "string 4");
    /* `str_mask` is what `App_RunClientScript` hands to the CS2 VM to decide
     * which stack each argument goes on. A bit in the wrong place puts a heap
     * pointer where an int belongs. */
    CHECK_EQ(pkt._runclientscript.str_mask, (1u << 2) | (1u << 4), "str_mask names 2 and 4");

    /*
     * And the hand-off itself, which is where this shape actually broke.
     *
     * `strv` is indexed by argument; `RS_CS2_RunScript` wants the compacted
     * list. Passing the sparse array is silently correct for the two shapes
     * content used to send and wrong for every mixed one — see
     * `pkt_runclientscript_compact_strings`.
     */
    {
        char const* strp[PKT_RUNCLIENTSCRIPT_ARG_MAX] = { 0 };
        int n = pkt_runclientscript_compact_strings(&pkt._runclientscript, strp,
                                                    PKT_RUNCLIENTSCRIPT_ARG_MAX);

        CHECK_EQ(n, 2, "two strings reach the CS2 dispatch");
        CHECK(strp[0] && strcmp(strp[0], "herb patch") == 0,
              "string local 0 is argument 2's string, not argument 0's");
        CHECK(strp[1] && strcmp(strp[1], "second") == 0, "string local 1 is argument 4's");
    }
}

/*
 * The single-trailing-string shape, on its own, because it is the one that was
 * broken and the one every measured consumer has:
 * `shop_main_init(inv, int, int, int, string)`,
 * `farming_view_setpanel(int, obj, coord, int, string)`,
 * `deathkeep_init(9 ints, string)`.
 *
 * Sparse and compact disagree here by four slots, and `strv[0]` is "", so the
 * failure is a *blank* string rather than a wrong one — which renders as a
 * sentence with a hole in it and reads exactly like a content bug.
 */
static void
test_trailing_string_compaction(void)
{
    uint8_t bytes[512];
    struct RevPacket pkt;
    const int intv[5] = { 8, 249, 49155, 4, 0 };
    const char* strv[5] = { NULL, NULL, NULL, NULL, "herb patch" };
    char const* strp[PKT_RUNCLIENTSCRIPT_ARG_MAX] = { 0 };
    int len;
    int n;

    printf("four ints then a string\n");

    len = write_runclientscript(bytes, sizeof(bytes), 1119, "iiiis", intv, strv, 5);
    CHECK_EQ(parse(bytes, len, &pkt), 1, "the payload round-trips");
    CHECK_EQ(pkt._runclientscript.str_mask, 1u << 4, "only the last argument is a string");
    CHECK_EQ(pkt._runclientscript.intv[2], 49155, "the packed state word survives");

    n = pkt_runclientscript_compact_strings(&pkt._runclientscript, strp,
                                            PKT_RUNCLIENTSCRIPT_ARG_MAX);
    CHECK_EQ(n, 1, "one string reaches the CS2 dispatch");
    CHECK(strp[0] && strcmp(strp[0], "herb patch") == 0,
          "and it is the argument's string, not the empty strv[0]");
}

/* The two shapes content ships today, so a change that only fixes the mixed
 * case does not pass. */
static void
test_homogeneous_roundtrip(void)
{
    uint8_t bytes[512];
    struct RevPacket pkt;
    int len;

    printf("all-int and all-string payloads\n");

    {
        const int intv[4] = { 3, 100, 7, 9 };

        len = write_runclientscript(bytes, sizeof(bytes), 1902, "iiii", intv, NULL, 4);
        CHECK_EQ(parse(bytes, len, &pkt), 1, "the skill guide's four ints parse");
        CHECK_EQ(pkt._runclientscript.intv[0], 3, "argument 0 is first, not last");
        CHECK_EQ(pkt._runclientscript.intv[3], 9, "argument 3 is last");
        CHECK_EQ(pkt._runclientscript.str_mask, 0u, "and no argument is a string");
    }
    {
        const char* strv[2] = { "Choose an option", "Yes|No" };

        len = write_runclientscript(bytes, sizeof(bytes), 58, "ss", NULL, strv, 2);
        CHECK_EQ(parse(bytes, len, &pkt), 1, "the chatmenu's two strings parse");
        CHECK(strcmp(pkt._runclientscript.strv[0], "Choose an option") == 0, "header first");
        CHECK(strcmp(pkt._runclientscript.strv[1], "Yes|No") == 0, "options second");
    }
    {
        len = write_runclientscript(bytes, sizeof(bytes), 1749, "", NULL, NULL, 0);
        CHECK_EQ(parse(bytes, len, &pkt), 1, "an empty argument list parses");
        CHECK_EQ(pkt._runclientscript.argc, 0, "as zero arguments");
        CHECK_EQ(pkt._runclientscript.script_id, 1749, "still carrying the script id");
    }
}

/*
 * The overflow guard, which is the reason this file exists.
 *
 * `PKT_RUNCLIENTSCRIPT_ARG_MAX` bounds the struct, not the protocol: a server
 * that is not this repo's can legally send more. The parser used to stop
 * reading type characters at the cap and carry on — which left the remaining
 * type bytes AND the '\n' unread, so the first value was decoded from those
 * stray bytes, every argument after it was wrong, the cursor never ran off the
 * end, and the packet was reported as parsed. A wrong panel, silently.
 *
 * The fix is to fail the packet. This asserts the fail, and — the half that
 * makes it a real test — asserts that exactly-at-the-cap still succeeds, so
 * "reject everything" cannot pass either.
 */
static void
test_argument_overflow(void)
{
    uint8_t bytes[2048];
    struct RevPacket pkt;
    char types[64];
    int intv[64];
    int len;
    int i;

    printf("argument-count overflow\n");

    for( i = 0; i < (int)sizeof(intv) / (int)sizeof(intv[0]); i++ )
    {
        types[i] = 'i';
        intv[i] = 1000 + i;
    }

    len = write_runclientscript(bytes, sizeof(bytes), 785, types, intv, NULL,
                                PKT_RUNCLIENTSCRIPT_ARG_MAX);
    CHECK_EQ(parse(bytes, len, &pkt), 1, "exactly PKT_RUNCLIENTSCRIPT_ARG_MAX arguments parse");
    CHECK_EQ(pkt._runclientscript.argc, PKT_RUNCLIENTSCRIPT_ARG_MAX, "with every argument");
    CHECK_EQ(pkt._runclientscript.intv[PKT_RUNCLIENTSCRIPT_ARG_MAX - 1],
             1000 + PKT_RUNCLIENTSCRIPT_ARG_MAX - 1, "and the last one intact");
    CHECK_EQ(pkt._runclientscript.script_id, 785, "and the script id after them");

    /* `ge_pricechecker_prices` (script 785) really does take 28 ints; this is
     * the packet a server that implemented it would send. */
    len = write_runclientscript(bytes, sizeof(bytes), 785, types, intv, NULL, 28);
    CHECK_EQ(parse(bytes, len, &pkt), 0,
             "one more than the cap is REJECTED, not truncated to garbage");

    /* A type string with no terminator at all — a truncated or hostile packet —
     * must also fail rather than read arguments out of whatever follows. */
    for( i = 0; i < 8; i++ )
        bytes[i] = 'i';
    CHECK_EQ(parse(bytes, 8, &pkt), 0, "an unterminated type string is rejected");
}

int
main(void)
{
    test_mixed_roundtrip();
    test_trailing_string_compaction();
    test_homogeneous_roundtrip();
    test_argument_overflow();

    if( g_fail )
    {
        printf("runclientscript test: %d FAILED\n", g_fail);
        return 1;
    }
    printf("runclientscript test: all passed\n");
    return 0;
}
