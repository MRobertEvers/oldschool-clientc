/*
 * Differential test: the rsprot codecs against the hand-written osrs239 parser.
 *
 *     make -C src test-rsprot-bridge
 *
 * Every packet migrated onto `3rd/rsprot/packets/` had an arm in
 * `osrs239_parse.c` that decoded the same bytes. Those two are independent
 * transcriptions of the same RSProt Kotlin -- one made by hand against the
 * source read in an editor, one generated mechanically -- so running both over
 * the same payload and requiring the same answer is the strongest check
 * available without a live client, and it is the check that licenses deleting
 * the hand-written arm.
 *
 * The payloads are not hand-authored hex. Each one is produced by FILLing the
 * message struct with deterministic values and ENCODEing it with the *same*
 * rev-239 codec, which means:
 *
 *   - the bytes are real rev-239 bytes by construction, and
 *   - the test also covers the encode direction, since a wrong encoder would
 *     have to be wrong in exactly the way that makes the independent
 *     hand-written decoder agree with it.
 *
 * That second property is why FILL/ENCODE is better here than a fixed byte
 * fixture: a fixture only ever tests decode, and this design's whole claim is
 * that the two directions are one function.
 *
 * A seed is printed on failure so a failing case is reproducible from it alone.
 */
#include "rev/gameproto_revisions.h"
#include "rev/revpacket.h"
#include "rev/rsprot_bridge.h"
#include "rsprot_exec.h"

#include "packets/if_closesub.h"
#include "packets/if_opensub.h"
#include "packets/if_setanim.h"
#include "packets/if_settext.h"
#include "packets/message_game.h"
#include "packets/update_runenergy.h"
#include "packets/varp_large.h"
#include "packets/varp_small.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* osrs239_parse.c — the hand-written parser this is measured against. */
int
osrs239_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out);

void
gameproto_free(struct RevPacket* packet);

static int g_failures;
static const char* g_case = "?";

#define CHECK(cond)                                                                       \
    do                                                                                    \
    {                                                                                     \
        if( !(cond) )                                                                     \
        {                                                                                 \
            printf("FAIL [%s] %s:%d  %s\n", g_case, __FILE__, __LINE__, #cond);           \
            g_failures++;                                                                 \
        }                                                                                 \
    } while( 0 )

#define CHECK_EQ(a, b)                                                                    \
    do                                                                                    \
    {                                                                                     \
        long long _a = (long long)(a), _b = (long long)(b);                               \
        if( _a != _b )                                                                    \
        {                                                                                 \
            printf(                                                                       \
                "FAIL [%s] %s:%d  %s = %lld, hand-written parser says %lld\n",            \
                g_case, __FILE__, __LINE__, #a, _a, _b);                                  \
            g_failures++;                                                                 \
        }                                                                                 \
    } while( 0 )

static uint8_t g_bytes[512];

/*
 * FILL a message, encode it at rev 239, and hand the bytes to both parsers.
 *
 * Returns the encoded length, or -1 when this revision has no codec (which is
 * a real answer, not a failure — UPDATE_STAT's 239 layout is not transcribed).
 */
static int
encode_at_239(const RsprotVersionRange* ranges, int count, void* msg, uint32_t seed)
{
    RsprotExec x;
    RsprotBuf buf;
    RsprotCodecFn fn = rsprot_version_pick(ranges, count, 239);

    if( !fn )
        return -1;

    rsprot_exec_fill(&x, seed);
    fn(&x, msg);
    if( !rsprot_exec_ok(&x) )
        return -1;

    rsprot_buf_wrap(&buf, g_bytes, (int32_t)sizeof(g_bytes));
    rsprot_exec_encode(&x, &buf);
    fn(&x, msg);
    if( !rsprot_exec_ok(&x) || buf.err )
        return -1;
    return buf.wpos;
}

/* Run both parsers over the same bytes. Returns 0 if either refused. */
static int
parse_both(int pkt_name, int len, struct RevPacket* via_rsprot, struct RevPacket* via_hand)
{
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    int a, b;

    memset(via_rsprot, 0, sizeof(*via_rsprot));
    memset(via_hand, 0, sizeof(*via_hand));

    a = rsprot_bridge_parse(rev, pkt_name, g_bytes, len, via_rsprot);
    b = osrs239_parse(rev, pkt_name, g_bytes, len, via_hand);

    if( a != 1 )
    {
        printf("FAIL [%s] rsprot bridge returned %d (want 1)\n", g_case, a);
        g_failures++;
        return 0;
    }
    if( b != 1 )
    {
        printf("FAIL [%s] hand-written parser returned %d (want 1)\n", g_case, b);
        g_failures++;
        return 0;
    }
    return 1;
}

static void
test_varp_small(uint32_t seed)
{
    MsgVarpSmall m;
    struct RevPacket r, h;
    int len;

    g_case = "VARP_SMALL";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(rsprot_varp_small_out, rsprot_varp_small_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 || !parse_both(PKT_NAME_VARP_SMALL, len, &r, &h) )
        return;

    CHECK_EQ(r._varp_small.variable, h._varp_small.variable);
    /* The interesting half: one byte, and both have to sign-extend it. A
     * varp of -1 read unsigned is 255 — a valid different setting, with
     * nothing to notice it. */
    CHECK_EQ(r._varp_small.value, h._varp_small.value);
    gameproto_free(&r);
    gameproto_free(&h);
}

static void
test_varp_large(uint32_t seed)
{
    MsgVarpLarge m;
    struct RevPacket r, h;
    int len;

    g_case = "VARP_LARGE";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(rsprot_varp_large_out, rsprot_varp_large_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 || !parse_both(PKT_NAME_VARP_LARGE, len, &r, &h) )
        return;

    CHECK_EQ(r._varp_large.variable, h._varp_large.variable);
    CHECK_EQ(r._varp_large.value, h._varp_large.value);
    gameproto_free(&r);
    gameproto_free(&h);
}

static void
test_if_opensub(uint32_t seed)
{
    MsgIfOpenSub m;
    struct RevPacket r, h;
    int len;

    g_case = "IF_OPENSUB";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(rsprot_if_opensub_out, rsprot_if_opensub_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 || !parse_both(PKT_NAME_IF_OPENSUB, len, &r, &h) )
        return;

    CHECK_EQ(r._if_opensub.target_uid, h._if_opensub.target_uid);
    CHECK_EQ(r._if_opensub.interface_id, h._if_opensub.interface_id);
    CHECK_EQ(r._if_opensub.type, h._if_opensub.type);
    gameproto_free(&r);
    gameproto_free(&h);
}

static void
test_if_closesub(uint32_t seed)
{
    MsgIfCloseSub m;
    struct RevPacket r, h;
    int len;

    g_case = "IF_CLOSESUB";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(rsprot_if_closesub_out, rsprot_if_closesub_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 || !parse_both(PKT_NAME_IF_CLOSESUB, len, &r, &h) )
        return;

    CHECK_EQ(r._if_closesub.target_uid, h._if_closesub.target_uid);
    gameproto_free(&r);
    gameproto_free(&h);
}

static void
test_if_setanim(uint32_t seed)
{
    MsgIfSetAnim m;
    struct RevPacket r, h;
    int len;

    g_case = "IF_SETANIM";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(rsprot_if_setanim_out, rsprot_if_setanim_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 || !parse_both(PKT_NAME_IF_SETANIM, len, &r, &h) )
        return;

    CHECK_EQ(r._if_setanim.component_id, h._if_setanim.component_id);
    CHECK_EQ(r._if_setanim.anim_id, h._if_setanim.anim_id);
    gameproto_free(&r);
    gameproto_free(&h);
}

static void
test_if_settext(uint32_t seed)
{
    MsgIfSetText m;
    struct RevPacket r, h;
    int len;

    g_case = "IF_SETTEXT";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(rsprot_if_settext_out, rsprot_if_settext_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 || !parse_both(PKT_NAME_IF_SETTEXT, len, &r, &h) )
        return;

    CHECK_EQ(r._if_settext.component_id, h._if_settext.component_id);
    CHECK(r._if_settext.text != NULL && h._if_settext.text != NULL);
    if( r._if_settext.text && h._if_settext.text )
        CHECK(strcmp(r._if_settext.text, h._if_settext.text) == 0);
    gameproto_free(&r);
    gameproto_free(&h);
}

static void
test_update_runenergy(uint32_t seed)
{
    MsgUpdateRunEnergy m;
    struct RevPacket r, h;
    int len;

    g_case = "UPDATE_RUNENERGY";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(
        rsprot_update_runenergy_out, rsprot_update_runenergy_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 || !parse_both(PKT_NAME_UPDATE_RUNENERGY, len, &r, &h) )
        return;

    CHECK_EQ(r._update_run_energy.run_energy, h._update_run_energy.run_energy);
    gameproto_free(&r);
    gameproto_free(&h);
}

/*
 * MESSAGE_GAME with the optional name ABSENT and PRESENT.
 *
 * FILL cannot choose between them — `name_present` is just a filled int — so
 * both are driven explicitly. The present case is the one that matters: it is
 * the branch the generator had to rewrite, because RSProt writes the flag by
 * testing the name field, which only an encoder can do.
 */
static void
test_message_game(uint32_t seed, int with_name)
{
    MsgMessageGame m;
    struct RevPacket r, h;
    RsprotExec x;
    RsprotBuf buf;
    RsprotCodecFn fn =
        rsprot_version_pick(rsprot_message_game_out, rsprot_message_game_out_count, 239);
    int len;

    g_case = with_name ? "MESSAGE_GAME (with name)" : "MESSAGE_GAME (no name)";
    CHECK(fn != NULL);
    if( !fn )
        return;

    memset(&m, 0, sizeof(m));
    rsprot_exec_fill(&x, seed);
    fn(&x, &m);
    m.name_present = with_name ? 1 : 0;
    m.type = 0;

    rsprot_buf_wrap(&buf, g_bytes, (int32_t)sizeof(g_bytes));
    rsprot_exec_encode(&x, &buf);
    fn(&x, &m);
    CHECK(rsprot_exec_ok(&x) && !buf.err);
    len = buf.wpos;
    if( !rsprot_exec_ok(&x) || buf.err )
        return;

    if( !parse_both(PKT_NAME_MESSAGE_GAME, len, &r, &h) )
        return;

    CHECK(r._message_game.text != NULL && h._message_game.text != NULL);
    if( r._message_game.text && h._message_game.text )
        CHECK(strcmp(r._message_game.text, h._message_game.text) == 0);
    gameproto_free(&r);
    gameproto_free(&h);
}

int
main(void)
{
    uint32_t seed;

    setvbuf(stdout, NULL, _IONBF, 0);

    /* Several seeds: one fill can miss a sign bit or a zero that a
     * transposition would otherwise survive. */
    for( seed = 1; seed <= 32; seed++ )
    {
        test_varp_small(seed);
        test_varp_large(seed);
        test_if_opensub(seed);
        test_if_closesub(seed);
        test_if_setanim(seed);
        test_if_settext(seed);
        test_update_runenergy(seed);
        test_message_game(seed, 0);
        test_message_game(seed, 1);

        if( g_failures )
        {
            printf("rsprot-bridge: first failure at seed %u\n", seed);
            break;
        }
    }

    if( g_failures )
    {
        printf("rsprot-bridge: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rsprot-bridge: rsprot and osrs239_parse agree on every migrated packet\n");
    return 0;
}
