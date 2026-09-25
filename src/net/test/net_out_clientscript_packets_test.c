/*
 * The packets clientscript commands send, built by net_out and read back by
 * RSProt's own decoders for revision 239.
 *
 * The builders are hand-written against RSProt's Kotlin; the codecs under
 * 3rd/rsprot/packets are generated from that same Kotlin. Decoding what a
 * builder wrote with the generated codec -- and checking every field and that
 * nothing is left over -- is the check that the two transcriptions agree.
 * Four packets have no generated codec (the generator refuses a `g1() == 1`
 * read): SEND_SNAPSHOT, FRIENDCHAT_JOIN_LEAVE, the set-muted request and
 * OPPLAYER6..8. Those are checked byte for byte against the Kotlin decoder's
 * read order, quoted beside each.
 */

#include "net/isaac.h"
#include "net/net_out.h"
#include "net/rev/gameproto_revisions.h"
#include "net/rev/osrs239/packetout.h"

#include "rsprot_exec.h"
#include "packets/affinedclansettings_addbanned_fromchannel.h"
#include "packets/bug_report.h"
#include "packets/clanchannel_full_request.h"
#include "packets/clanchannel_kickuser.h"
#include "packets/clansettings_full_request.h"
#include "packets/friendchat_kick.h"
#include "packets/friendchat_setrank.h"
#include "packets/resume_p_countdialog_long.h"
#include "packets/resume_p_namedialog.h"
#include "packets/resume_p_objdialog.h"
#include "packets/resume_p_stringdialog.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("  FAIL ");                                                                     \
            printf(__VA_ARGS__);                                                                   \
            printf("  (%s:%d)\n", __FILE__, __LINE__);                                             \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/* Decode `len` payload bytes with the rev-239 codec from `ranges`, requiring it
 * to consume every byte. */
static int
decode_exact(
    RsprotVersionRange const* ranges,
    int count,
    uint8_t const* payload,
    int len,
    void* message)
{
    RsprotCodecFn fn = rsprot_version_pick(ranges, count, 239);
    RsprotExec x;
    RsprotBuf buf;
    if( !fn )
        return 0;
    rsprot_buf_wrap_read(&buf, payload, len);
    rsprot_exec_decode(&x, &buf);
    fn(&x, message);
    return rsprot_exec_ok(&x) && rsprot_buf_ok(&buf) && buf.rpos == buf.wpos;
}

/* The payload of a var-u8 packet: its length byte, then the body. */
static uint8_t const*
var_u8_body(uint8_t const* packet, int length, int* out_len)
{
    *out_len = packet[1];
    CHECK(*out_len == length - 2, "var-u8 length byte %d disagrees with the packet length %d", *out_len, length);
    return packet + 2;
}

int
main(void)
{
    struct GameProtoRevTable const* rev = GameProtoRev_OSRS239();
    struct Isaac* random = isaac_new(NULL, 0);
    uint8_t packet[512];
    int n;
    int body_len;
    uint8_t const* body;

    n = net_out_resume_namedialog(rev, random, packet, sizeof(packet), "Zezima");
    body = var_u8_body(packet, n, &body_len);
    {
        MsgResumePNameDialog m = { 0 };
        CHECK(decode_exact(rsprot_resume_p_namedialog_in, rsprot_resume_p_namedialog_in_count, body, body_len, &m) &&
                  m.name && strcmp(m.name, "Zezima") == 0,
            "RESUME_P_NAMEDIALOG");
    }

    n = net_out_resume_stringdialog(rev, random, packet, sizeof(packet), "hello there");
    body = var_u8_body(packet, n, &body_len);
    {
        MsgResumePStringDialog m = { 0 };
        CHECK(decode_exact(rsprot_resume_p_stringdialog_in, rsprot_resume_p_stringdialog_in_count, body, body_len, &m) &&
                  m.string && strcmp(m.string, "hello there") == 0,
            "RESUME_P_STRINGDIALOG");
    }

    n = net_out_resume_objdialog(rev, random, packet, sizeof(packet), 4151);
    {
        MsgResumePObjDialog m = { 0 };
        CHECK(n == 3 && decode_exact(rsprot_resume_p_objdialog_in, rsprot_resume_p_objdialog_in_count, packet + 1, 2, &m) &&
                  m.obj == 4151,
            "RESUME_P_OBJDIALOG");
    }

    n = net_out_resume_countdialog_long(rev, random, packet, sizeof(packet), 5000000000LL);
    {
        MsgResumePCountDialogLong m = { 0 };
        CHECK(n == 9 && decode_exact(rsprot_resume_p_countdialog_long_in, rsprot_resume_p_countdialog_long_in_count, packet + 1, 8, &m) &&
                  m.count == 5000000000LL,
            "RESUME_P_COUNTDIALOG_LONG");
    }

    n = net_out_bug_report(rev, random, packet, sizeof(packet), "it broke", "click the thing", 3);
    {
        MsgBugReport m = { 0 };
        int const len = (packet[1] << 8) | packet[2];
        CHECK(len == n - 3, "BUG_REPORT var-u16 length");
        CHECK(decode_exact(rsprot_bug_report_in, rsprot_bug_report_in_count, packet + 3, len, &m) && m.type == 3 &&
                  m.description && strcmp(m.description, "it broke") == 0 && m.instructions &&
                  strcmp(m.instructions, "click the thing") == 0,
            "BUG_REPORT");
    }

    n = net_out_friendchat_kick(rev, random, packet, sizeof(packet), "Bob");
    body = var_u8_body(packet, n, &body_len);
    {
        MsgFriendChatKick m = { 0 };
        CHECK(decode_exact(rsprot_friendchat_kick_in, rsprot_friendchat_kick_in_count, body, body_len, &m) && m.name &&
                  strcmp(m.name, "Bob") == 0,
            "FRIENDCHAT_KICK");
    }

    n = net_out_friendchat_setrank(rev, random, packet, sizeof(packet), "Bob", 4);
    body = var_u8_body(packet, n, &body_len);
    {
        MsgFriendChatSetRank m = { 0 };
        CHECK(decode_exact(rsprot_friendchat_setrank_in, rsprot_friendchat_setrank_in_count, body, body_len, &m) &&
                  m.rank == 4 && m.name && strcmp(m.name, "Bob") == 0,
            "FRIENDCHAT_SETRANK");
    }

    n = net_out_clanchannel_full_request(rev, random, packet, sizeof(packet), 1);
    {
        MsgClanChannelFullRequest m = { 0 };
        CHECK(n == 2 && decode_exact(rsprot_clanchannel_full_request_in, rsprot_clanchannel_full_request_in_count, packet + 1, 1, &m) &&
                  m.clan_id == 1,
            "CLANCHANNEL_FULL_REQUEST");
    }

    n = net_out_clansettings_full_request(rev, random, packet, sizeof(packet), 0);
    {
        MsgClanSettingsFullRequest m = { 1 };
        CHECK(n == 2 && decode_exact(rsprot_clansettings_full_request_in, rsprot_clansettings_full_request_in_count, packet + 1, 1, &m) &&
                  m.clan_id == 0,
            "CLANSETTINGS_FULL_REQUEST");
    }

    n = net_out_clanchannel_kickuser(rev, random, packet, sizeof(packet), 1, 513, "Guest");
    body = var_u8_body(packet, n, &body_len);
    {
        MsgClanChannelKickUser m = { 0 };
        CHECK(decode_exact(rsprot_clanchannel_kickuser_in, rsprot_clanchannel_kickuser_in_count, body, body_len, &m) &&
                  m.clan_id == 1 && m.member_index == 513 && m.name && strcmp(m.name, "Guest") == 0,
            "CLANCHANNEL_KICKUSER");
    }

    n = net_out_affinedclansettings_addbanned_fromchannel(rev, random, packet, sizeof(packet), 0, 7, "Guest");
    body = var_u8_body(packet, n, &body_len);
    {
        MsgAffinedClanSettingsAddBannedFromChannel m = { 0 };
        CHECK(decode_exact(rsprot_affinedclansettings_addbanned_fromchannel_in,
                  rsprot_affinedclansettings_addbanned_fromchannel_in_count, body, body_len, &m) &&
                  m.clan_id == 0 && m.member_index == 7 && m.name && strcmp(m.name, "Guest") == 0,
            "AFFINEDCLANSETTINGS_ADDBANNED_FROMCHANNEL");
    }

    /* SendSnapshotDecoder: gjstr name, g1 ruleId, g1 mute. */
    n = net_out_send_snapshot(rev, random, packet, sizeof(packet), "Bob", 11, 1);
    body = var_u8_body(packet, n, &body_len);
    CHECK(body_len == 6 && memcmp(body, "Bob\0", 4) == 0 && body[4] == 11 && body[5] == 1, "SEND_SNAPSHOT");

    /* FriendChatJoinLeaveDecoder: gjstr name when readable, else a leave. */
    n = net_out_friendchat_join_leave(rev, random, packet, sizeof(packet), "Chan");
    body = var_u8_body(packet, n, &body_len);
    CHECK(body_len == 5 && memcmp(body, "Chan\0", 5) == 0, "FRIENDCHAT_JOIN_LEAVE join");
    n = net_out_friendchat_join_leave(rev, random, packet, sizeof(packet), NULL);
    CHECK(n == 2 && packet[1] == 0, "FRIENDCHAT_JOIN_LEAVE leave is an empty body");

    /* AffinedClanSettingsSetMutedFromChannelDecoder: g1 clan, g2 member,
     * g1 muted, gjstr name. */
    n = net_out_affinedclansettings_setmuted_fromchannel(rev, random, packet, sizeof(packet), 1, 0x0102, 1, "Bob");
    body = var_u8_body(packet, n, &body_len);
    CHECK(body_len == 8 && body[0] == 1 && body[1] == 0x01 && body[2] == 0x02 && body[3] == 1 &&
              memcmp(body + 4, "Bob\0", 4) == 0,
        "AFFINEDCLANSETTINGS_SETMUTED_FROMCHANNEL");

    /* OpPlayer6: g2Alt3 index, g1Alt3 ctrl. OpPlayer7: g1 ctrl, g2 index.
     * OpPlayer8: g2 index, g1Alt2 ctrl. Alt3 on a u2 is little-endian with the
     * low byte +128; Alt3 on a u1 is 128-v; Alt2 on a u1 is -v. */
    n = net_out_opplayer(rev, random, packet, sizeof(packet), 6, 0x0304);
    CHECK(n == 4 && packet[1] == (uint8_t)(0x04 + 128) && packet[2] == 0x03 && packet[3] == (uint8_t)(128 - 0), "OPPLAYER6");
    n = net_out_opplayer(rev, random, packet, sizeof(packet), 7, 0x0304);
    CHECK(n == 4 && packet[1] == 0 && packet[2] == 0x03 && packet[3] == 0x04, "OPPLAYER7");
    n = net_out_opplayer(rev, random, packet, sizeof(packet), 8, 0x0304);
    CHECK(n == 4 && packet[1] == 0x03 && packet[2] == 0x04 && packet[3] == 0, "OPPLAYER8");

    isaac_free(random);
    if( g_fail )
    {
        printf("net_out_clientscript_packets_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("net_out_clientscript_packets_test: all packets agree with RSProt\n");
    return 0;
}
