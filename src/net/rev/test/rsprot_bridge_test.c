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
#include "packets/if_clearinv.h"
#include "packets/if_movesub.h"
#include "packets/if_setangle.h"
#include "packets/if_setcolour.h"
#include "packets/if_setevents_v2.h"
#include "packets/if_setmodel_v2.h"
#include "packets/if_setnpchead.h"
#include "packets/if_setnpchead_active.h"
#include "packets/if_setobject.h"
#include "packets/if_setplayerhead.h"
#include "packets/if_setposition.h"
#include "packets/if_setrotatespeed.h"
#include "packets/if_setscrollpos.h"
#include "packets/midi_jingle.h"
#include "packets/midi_song_v2.h"
#include "packets/synth_sound.h"
#include "packets/update_stat_v2.h"
#include "packets/update_zone_full_follows.h"
#include "packets/update_zone_partial_follows.h"
#include "packets/cam_lookat_v2.h"
#include "packets/cam_moveto_v2.h"
#include "packets/cam_shake.h"
#include "packets/chat_filter_settings.h"
#include "packets/if_sethide.h"
#include "packets/if_setplayermodel_basecolour.h"
#include "packets/if_setplayermodel_bodytype.h"
#include "packets/if_setplayermodel_obj.h"
#include "packets/if_setplayermodel_self.h"
#include "packets/friendlist_loaded.h"
#include "packets/server_tick_end.h"
#include "packets/set_map_flag_v2.h"
#include "packets/set_npc_update_origin.h"
#include "packets/update_inv_stoptransmit.h"

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

    /*
     * Both get `packet_type` up front, exactly as net.c sets it before calling
     * either parser. Without this the hand-written side leaves it zero while
     * the bridge sets it, and a whole-struct comparison reports every packet as
     * disagreeing — which is what it did, on all eighteen at once. Eighteen
     * identical failures are a harness bug, not eighteen mapping bugs.
     */
    via_rsprot->packet_type = (enum GameProtoPktName)pkt_name;
    via_hand->packet_type = (enum GameProtoPktName)pkt_name;

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

/* MAP_PROJANIM_V2 and PLAYER_INFO use the same rev-239 client-player index.
 * The last three bytes are signed target index -2, which names client player
 * index 1. This is a literal wire fixture because this packet has no top-level
 * opcode in rev 239 and therefore is not part of the generated bridge set. */
static void
test_projectile_target_index(void)
{
    static const uint8_t payload[24] = {
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0xff, 0xff, 0xfe,
    };
    struct RevPacket packet;

    g_case = "MAP_PROJANIM target index";
    memset(&packet, 0, sizeof(packet));
    CHECK(
        osrs239_parse(
            GameProtoRev_OSRS239(),
            PKT_NAME_MAP_PROJANIM,
            payload,
            (int)sizeof(payload),
            &packet) == 1);
    CHECK_EQ(packet._map_projanim.target, -2);
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


/*
 * MIDI_JINGLE has NO hand-written osrs239_parse.c arm to diff against, by
 * design: routing it through the generated codec here is what fixes the
 * actual bug. gameproto_parse.c's 4-byte `g2 id / g2 delay` reader is the
 * LostCity/2004 layout (kept for lc254/lc245_2, which really are 4 bytes);
 * rev 239 is a 5-byte `p3 length_in_millis / p2Alt3 id` shape, and running the
 * LC reader over a real 239 packet trips `assert(position == data_size)` in a
 * build with no `-DNDEBUG` -- it would abort the client, not just mis-decode.
 *
 * So this checks the bridge alone rather than differentially: `encode_at_239`
 * FILLs `m` with seed-derived ground truth and leaves it there (ENCODE only
 * reads), so the decoded packet is compared against what was filled, not
 * against a second parser that was never meant to see this packet.
 */
static void
test_midi_jingle(uint32_t seed)
{
    MsgMidiJingle m;
    struct RevPacket r;
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    int len, ok;

    g_case = "MIDI_JINGLE";
    memset(&m, 0, sizeof(m));
    len = encode_at_239(rsprot_midi_jingle_out, rsprot_midi_jingle_out_count, &m, seed);
    CHECK(len > 0);
    if( len <= 0 )
        return;

    memset(&r, 0, sizeof(r));
    r.packet_type = (enum GameProtoPktName)PKT_NAME_MIDI_JINGLE;
    ok = rsprot_bridge_parse(rev, PKT_NAME_MIDI_JINGLE, g_bytes, len, &r);
    CHECK(ok == 1);
    if( ok != 1 )
        return;

    CHECK_EQ(r._midi_jingle.id, m.id);
    CHECK_EQ(r._midi_jingle.delay, m.length_in_millis);
    gameproto_free(&r);
}

/*
 * The rest of the migrated packets, checked the same way but generically.
 *
 * Every packet whose canonical struct holds no pointers can be compared with a
 * whole-RevPacket memcmp: fill, encode at 239, then run BOTH parsers over the
 * bytes and require the union to come out identical. That covers the field
 * mapping, the byte order, and the payload length in one assertion, and it
 * scales to a packet per line — which matters, because the alternative is a
 * hand-written comparison per packet and the ones nobody writes are the ones
 * that break.
 *
 * String-carrying packets (IF_SETTEXT, MESSAGE_GAME) are NOT here: their arms
 * hold heap pointers that differ by construction. Those have their own cases
 * above, which compare with strcmp.
 */
#define GENERIC_CASE(pkt, table, msgtype, label)                                          \
    do {                                                                                  \
        msgtype m;                                                                        \
        struct RevPacket r, h;                                                            \
        int len;                                                                          \
        g_case = (label);                                                                  \
        memset(&m, 0, sizeof(m));                                                         \
        len = encode_at_239((table), (table##_count), &m, seed);                          \
        /* len == 0 is a zero-payload packet, not a failure. */                           \
        if (len < 0) {                                                                   \
            printf("FAIL [%s] no rev239 codec\n", (label));                               \
            g_failures++;                                                                  \
            break;                                                                        \
        }                                                                                 \
        if (!parse_both((pkt), len, &r, &h))                                              \
            break;                                                                        \
        if (memcmp(&r, &h, sizeof(r)) != 0) {                                             \
            printf("FAIL [%s] rsprot and osrs239_parse disagree on the packet\n",         \
                   (label));                                                              \
            g_failures++;                                                                  \
        }                                                                                 \
        gameproto_free(&r);                                                               \
        gameproto_free(&h);                                                               \
    } while (0)

static void
test_generic_packets(uint32_t seed)
{
GENERIC_CASE(PKT_NAME_IF_CLEARINV, rsprot_if_clearinv_out, MsgIfClearInv, "IF_CLEARINV");
    GENERIC_CASE(PKT_NAME_IF_MOVESUB, rsprot_if_movesub_out, MsgIfMoveSub, "IF_MOVESUB");
    GENERIC_CASE(PKT_NAME_IF_SETANGLE, rsprot_if_setangle_out, MsgIfSetAngle, "IF_SETANGLE");
    GENERIC_CASE(PKT_NAME_IF_SETCOLOUR, rsprot_if_setcolour_out, MsgIfSetColour, "IF_SETCOLOUR");
    GENERIC_CASE(PKT_NAME_IF_SETEVENTS, rsprot_if_setevents_v2_out, MsgIfSetEventsV2, "IF_SETEVENTS");
    GENERIC_CASE(PKT_NAME_IF_SETMODEL, rsprot_if_setmodel_v2_out, MsgIfSetModelV2, "IF_SETMODEL");
    GENERIC_CASE(PKT_NAME_IF_SETNPCHEAD, rsprot_if_setnpchead_out, MsgIfSetNpcHead, "IF_SETNPCHEAD");
    GENERIC_CASE(PKT_NAME_IF_SETNPCHEAD_ACTIVE, rsprot_if_setnpchead_active_out, MsgIfSetNpcHeadActive, "IF_SETNPCHEAD_ACTIVE");
    GENERIC_CASE(PKT_NAME_IF_SETOBJECT, rsprot_if_setobject_out, MsgIfSetObject, "IF_SETOBJECT");
    GENERIC_CASE(PKT_NAME_IF_SETPLAYERHEAD, rsprot_if_setplayerhead_out, MsgIfSetPlayerHead, "IF_SETPLAYERHEAD");
    GENERIC_CASE(PKT_NAME_IF_SETPOSITION, rsprot_if_setposition_out, MsgIfSetPosition, "IF_SETPOSITION");
    GENERIC_CASE(PKT_NAME_IF_SETROTATESPEED, rsprot_if_setrotatespeed_out, MsgIfSetRotateSpeed, "IF_SETROTATESPEED");
    GENERIC_CASE(PKT_NAME_IF_SETSCROLLPOS, rsprot_if_setscrollpos_out, MsgIfSetScrollPos, "IF_SETSCROLLPOS");
    GENERIC_CASE(PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, rsprot_update_zone_full_follows_out, MsgUpdateZoneFullFollows, "UPDATE_ZONE_FULL_FOLLOWS");
    GENERIC_CASE(PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS, rsprot_update_zone_partial_follows_out, MsgUpdateZonePartialFollows, "UPDATE_ZONE_PARTIAL_FOLLOWS");
    GENERIC_CASE(PKT_NAME_UPDATE_STAT, rsprot_update_stat_v2_out, MsgUpdateStatV2, "UPDATE_STAT");
    GENERIC_CASE(PKT_NAME_SYNTH_SOUND, rsprot_synth_sound_out, MsgSynthSound, "SYNTH_SOUND");
    GENERIC_CASE(PKT_NAME_MIDI_SONG, rsprot_midi_song_v2_out, MsgMidiSongV2, "MIDI_SONG");
    GENERIC_CASE(PKT_NAME_CAM_SHAKE, rsprot_cam_shake_out, MsgCamShake, "CAM_SHAKE");
    GENERIC_CASE(PKT_NAME_CAM_LOOKAT, rsprot_cam_lookat_v2_out,
                 MsgCamLookAtV2, "CAM_LOOKAT");
    GENERIC_CASE(PKT_NAME_CAM_MOVETO, rsprot_cam_moveto_v2_out,
                 MsgCamMoveToV2, "CAM_MOVETO");
    GENERIC_CASE(PKT_NAME_CHAT_FILTER_SETTINGS, rsprot_chat_filter_settings_out,
                 MsgChatFilterSettings, "CHAT_FILTER_SETTINGS");
    GENERIC_CASE(PKT_NAME_IF_SETHIDE, rsprot_if_sethide_out, MsgIfSetHide, "IF_SETHIDE");
    GENERIC_CASE(PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR,
                 rsprot_if_setplayermodel_basecolour_out,
                 MsgIfSetPlayerModelBaseColour, "IF_SETPLAYERMODEL_BASECOLOUR");
    GENERIC_CASE(PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE,
                 rsprot_if_setplayermodel_bodytype_out,
                 MsgIfSetPlayerModelBodyType, "IF_SETPLAYERMODEL_BODYTYPE");
    GENERIC_CASE(PKT_NAME_IF_SETPLAYERMODEL_OBJ, rsprot_if_setplayermodel_obj_out,
                 MsgIfSetPlayerModelObj, "IF_SETPLAYERMODEL_OBJ");
    GENERIC_CASE(PKT_NAME_IF_SETPLAYERMODEL_SELF, rsprot_if_setplayermodel_self_out,
                 MsgIfSetPlayerModelSelf, "IF_SETPLAYERMODEL_SELF");
    GENERIC_CASE(PKT_NAME_SET_NPC_UPDATE_ORIGIN, rsprot_set_npc_update_origin_out,
                 MsgSetNpcUpdateOrigin, "SET_NPC_UPDATE_ORIGIN");
    GENERIC_CASE(PKT_NAME_UNSET_MAP_FLAG, rsprot_set_map_flag_v2_out,
                 MsgSetMapFlagV2, "UNSET_MAP_FLAG");
    GENERIC_CASE(PKT_NAME_UPDATE_INV_STOP_TRANSMIT, rsprot_update_inv_stoptransmit_out,
                 MsgUpdateInvStopTransmit, "UPDATE_INV_STOP_TRANSMIT");
    /*
     * Zero-payload packets, which is exactly why they are here. They carry no
     * fields to get wrong, so they look untestable -- but the ADAPTER still
     * sets canonical fields from nothing, and one of them (FRIENDLIST_LOADED)
     * shipped with status=2 where the hand-written arm says 1, purely because
     * nothing compared them.
     */
    GENERIC_CASE(PKT_NAME_FRIENDLIST_LOADED, rsprot_friendlist_loaded_out,
                 MsgFriendListLoaded, "FRIENDLIST_LOADED");
    GENERIC_CASE(PKT_NAME_SERVER_TICK_END, rsprot_server_tick_end_out,
                 MsgServerTickEnd, "SERVER_TICK_END");
}

int
main(void)
{
    uint32_t seed;

    setvbuf(stdout, NULL, _IONBF, 0);
    test_projectile_target_index();

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
        test_midi_jingle(seed);
        test_generic_packets(seed);

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
