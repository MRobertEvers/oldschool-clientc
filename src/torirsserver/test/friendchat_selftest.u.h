/*
 * Friends chat (torirs_server_friendchat.h), two players through the real
 * packet path and the real content procs. Every expectation is a recorded
 * exchange: the RSProx index sessions are cited at each step.
 *
 * Included into torirs_server_world_selftest.c (a unity part, like the other
 * *.u.h). Run alone with TORIRSSERVER_SELFTEST_FRIENDCHAT_ONLY=1. The packets
 * this server sends for friends chat exist only on the 239+ wires, so it runs
 * under TORIRSSERVER_REV=osrs242 (or 239/241); on 230 it skips.
 */

#include "mem/torirs_mem.h"
#include "net/chat_huffman.h"
#include "torirsserver/torirs_server_friendchat.h"
#include "torirsserver/wire/core/wire_in.h"
#include "packets/message_friendchannel.h"
#include "packets/update_friendchat_channel_full_v2.h"
#include "packets/update_friendchat_channel_singleuser.h"

enum
{
    FC_SELFTEST_LINES = 64,
};

struct FcSelftestLine
{
    const struct ToriRSServerPlayer* to;
    char text[160];
};

static struct FcSelftestLine g_fc_lines[FC_SELFTEST_LINES];
static int g_fc_line_count;

static void
fc_selftest_sink(
    const struct ToriRSServerPlayer* player,
    const char* text,
    void* ctx)
{
    (void)ctx;
    if( g_fc_line_count >= FC_SELFTEST_LINES )
        return;
    g_fc_lines[g_fc_line_count].to = player;
    snprintf(g_fc_lines[g_fc_line_count].text, sizeof(g_fc_lines[0].text), "%s", text);
    g_fc_line_count++;
}

/** Did `to` get exactly this line since the last reset? */
static int
fc_selftest_heard(
    const struct ToriRSServerPlayer* to,
    const char* text)
{
    for( int i = 0; i < g_fc_line_count; i++ )
        if( g_fc_lines[i].to == to && strcmp(g_fc_lines[i].text, text) == 0 )
            return 1;
    return 0;
}

static int
fc_selftest_heard_any(const struct ToriRSServerPlayer* to)
{
    for( int i = 0; i < g_fc_line_count; i++ )
        if( g_fc_lines[i].to == to )
            return 1;
    return 0;
}

static void
fc_selftest_name_packet(
    struct ToriRSServerPlayer* player,
    int name,
    const char* text)
{
    uint8_t body[32];
    size_t len = strlen(text);

    assert(len + 1 < sizeof(body));
    memcpy(body, text, len + 1);
    selftest_handle(player, name, body, (int)len + 1);
}

/** MESSAGE_PUBLIC as wire_in.c hands it on: type, colour, effect, Huffman. */
static void
fc_selftest_say(
    struct ToriRSServerPlayer* player,
    int type,
    const char* text)
{
    uint8_t body[3 + 256];
    int packed;

    body[0] = (uint8_t)type;
    body[1] = 0;
    body[2] = 0;
    packed = chat_huffman_pack(body + 3, (int)sizeof(body) - 3, text);
    selftest_handle(player, PKTOUT_NAME_MESSAGE_PUBLIC, body, 3 + packed);
}

/** The text of the n-th captured MESSAGE_FRIENDCHANNEL from `from`, "" if none:
 *  jstr sender, p8 channel, p2 world, p3 counter, p1 crown, Huffman. */
static const char*
fc_selftest_channel_text(
    const struct ToriRSServerCapture* cap,
    int from,
    int* out_index)
{
    static char text[256];
    int index = ToriRSServer_CaptureFindNamed(cap, PKT_NAME_MESSAGE_FRIENDCHANNEL, from);
    const struct ToriRSServerCapturedPacket* packet;
    int at = 0;
    char* unpacked;

    text[0] = '\0';
    *out_index = index;
    if( index < 0 )
        return text;
    packet = &cap->packets[index];
    while( at < packet->len && packet->data[at] != 0 )
        at++;
    at += 1 + 8 + 2 + 3 + 1;
    if( at > packet->len )
        return text;
    unpacked = chat_huffman_unpack(packet->data + at, packet->len - at);
    snprintf(text, sizeof(text), "%s", unpacked ? unpacked : "");
    ToriMem_Free(ToriMem_Net(), unpacked);
    return text;
}


/*
 * The client's side of each packet: RSProt's reader (the one row it has for
 * 222..242, which the 242 client's executor runs), over the captured bytes,
 * consuming every one of them. A field out of place here is a frame the
 * client cannot read.
 */
static MsgUpdateFriendchatChannelFullV2 g_fc_full;

static int
fc_selftest_read_full(const struct ToriRSServerCapturedPacket* packet)
{
    struct WireInReader r;

    memset(&g_fc_full, 0, sizeof(g_fc_full));
    packet_update_friendchat_channel_full_v2_v1_out(wire_in_reader(&r, packet->data, packet->len),
                                                    &g_fc_full);
    return wire_in_read_all(&r);
}

static int
fc_selftest_read_user(
    const struct ToriRSServerCapturedPacket* packet,
    MsgUpdateFriendchatChannelSingleuser* out)
{
    struct WireInReader r;

    memset(out, 0, sizeof(*out));
    packet_update_friendchat_channel_singleuser_v1_out(wire_in_reader(&r, packet->data, packet->len),
                                                       out);
    return wire_in_read_all(&r);
}

static int
fc_selftest_read_message(
    const struct ToriRSServerCapturedPacket* packet,
    MsgMessageFriendchannel* out)
{
    struct WireInReader r;

    memset(out, 0, sizeof(*out));
    packet_message_friendchannel_v1_out(wire_in_reader(&r, packet->data, packet->len), out);
    return wire_in_read_all(&r);
}

static void
fc_selftest_ticks(
    struct ToriRSServer* srv,
    int ticks)
{
    for( int i = 0; i < ticks; i++ )
        ToriRSServer_WorldTick(srv);
}

static void
selftest_friendchat(struct ToriRSServer* srv)
{
    static struct ToriRSServerCapture cap;
    struct ToriRSServerPlayer* owner;
    struct ToriRSServerPlayer* guest;
    int loaded;
    int owned;
    int index;
    const char* said;

    fprintf(stderr, "ToriRSServer selftest: friends chat\n");
    if( !srv->wire || srv->wire->revision < 239 )
    {
        fprintf(stderr, "  SKIP  friends chat is a 239+ wire's (TORIRSSERVER_REV=osrs242)\n");
        return;
    }
    /* The suite's own pack when one is loaded; a pack of its own otherwise,
     * freed again at the end (the run-energy section's shape). */
    owned = !srv->scripts_ok;
    loaded = srv->scripts_ok;
    if( owned )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir());
    if( !loaded )
        loaded = ToriRSServer_ScriptsLoad(srv, selftest_scripts_dir_from_src());
    if( !loaded )
    {
        fprintf(stderr, "  SKIP  no compiled script pack\n");
        return;
    }

    owner = srv->active_player;
    assert(owner);
    guest = ToriRSServer_WorldAddPlayer(srv, NULL);
    SELFTEST_CHECK(guest != NULL, "a second player for the channel");
    if( !guest )
        return;
    ToriRSServer_WorldPlayerInit(guest);
    ToriRSServer_WorldSetDisplayName(owner, "Fc Owner");
    ToriRSServer_WorldSetDisplayName(guest, "Fc Guest");
    ToriRSServer_FriendsReset();
    ToriRSServer_FriendchatReset();
    ToriRSServer_FriendsLogin(owner->name37, 0, TORIRSSERVER_CHAT_PRIVATE_ON, 0, 0);
    ToriRSServer_FriendsLogin(guest->name37, 0, TORIRSSERVER_CHAT_PRIVATE_ON, 0, 0);
    owner->friendchat_owner37 = 0;
    guest->friendchat_owner37 = 0;
    ToriRSServer_MessageSink(fc_selftest_sink, NULL);

    /* A channel nobody set up does not exist, and a name nobody has heard of
     * gets the refusal alone (RSProx index 753 [1049..1050]). */
    g_fc_line_count = 0;
    fc_selftest_name_packet(guest, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, "Nobody Here");
    SELFTEST_CHECK(fc_selftest_heard(guest, "The channel you tried to join does not exist.") &&
                       !fc_selftest_heard(guest, "Attempting to join chat-channel..."),
                   "an unknown channel is refused without the attempt line");
    g_fc_line_count = 0;
    fc_selftest_name_packet(guest, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, "Fc Owner");
    fc_selftest_ticks(srv, 1);
    SELFTEST_CHECK(fc_selftest_heard(guest, "Attempting to join chat-channel...") &&
                       fc_selftest_heard(guest, "The channel you tried to join does not exist."),
                   "a known player's disabled channel is attempted, then refused (753 [1246..1247])");

    /* The owner sets a prefix: base 37 keeps twelve characters and the panel
     * shows each word capitalised (RSProx index 745 [68]). Enabling is now. */
    SELFTEST_CHECK(ToriRSServer_FriendchatSetPrefix(owner, "Test FC Name Weeeeeeeee") ==
                       TORIRSSERVER_FRIENDCHAT_CHANGE_ENABLED,
                   "setting a prefix on a disabled channel enables it at once");
    SELFTEST_CHECK(strcmp(ToriRSServer_FriendchatPrefix(owner), "Test Fc Name") == 0,
                   "the prefix reads \"Test Fc Name\", got \"%s\"",
                   ToriRSServer_FriendchatPrefix(owner));

    /* Join: the attempt line on the packet's tick, the answer and the channel
     * a tick later (RSProx index 762 [7..8]). */
    g_fc_line_count = 0;
    ToriRSServer_CaptureBegin(srv, &cap);
    fc_selftest_name_packet(guest, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, "Fc Owner");
    SELFTEST_CHECK(fc_selftest_heard(guest, "Attempting to join chat-channel...") &&
                       !fc_selftest_heard(guest, "Now talking in chat-channel Test Fc Name"),
                   "the join's first tick says only that it is attempting");
    fc_selftest_ticks(srv, 1);
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(fc_selftest_heard(guest, "Now talking in chat-channel Test Fc Name") &&
                       fc_selftest_heard(guest, "To talk, start each line of chat with the / symbol."),
                   "the answer names the channel and how to talk in it (753 [9])");
    index = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL, 0);
    SELFTEST_CHECK(index >= 0, "the joiner gets the whole channel");
    if( index >= 0 )
        SELFTEST_CHECK(fc_selftest_read_full(&cap.packets[index]) && g_fc_full.joined &&
                           strcmp(g_fc_full.channel_owner, "Fc Owner") == 0 &&
                           g_fc_full.channel_name == (int64_t)strtobase37("test fc name") &&
                           g_fc_full.kick_rank == TORIRSSERVER_FRIENDCHAT_RANK_OWNER &&
                           g_fc_full.entry_count == 1 &&
                           strcmp(g_fc_full.entries[0].name, "Fc Guest") == 0 &&
                           g_fc_full.entries[0].rank == TORIRSSERVER_FRIENDCHAT_RANK_ANYONE &&
                           g_fc_full.entries[0].world_id == ToriRSServer_FriendsWorld(guest->name37),
                       "the client reads the channel: owner, name, kick rank, the one member");
    SELFTEST_CHECK(strcmp(ToriRSServer_FriendchatCurrent(guest), "Fc Owner") == 0,
                   "the guest is in Fc Owner's channel");

    /* The owner joins their own: the guest hears the new member's row. */
    ToriRSServer_CaptureBegin(srv, &cap);
    fc_selftest_name_packet(owner, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, "Fc Owner");
    fc_selftest_ticks(srv, 1);
    ToriRSServer_CaptureEnd(srv);
    index = ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER, 0);
    SELFTEST_CHECK(index >= 0, "a member joining sends the others their row (757 [19])");
    if( index >= 0 )
    {
        MsgUpdateFriendchatChannelSingleuser user;

        SELFTEST_CHECK(fc_selftest_read_user(&cap.packets[index], &user) &&
                           strcmp(user.name, "Fc Owner") == 0 &&
                           user.rank == TORIRSSERVER_FRIENDCHAT_RANK_OWNER,
                       "the client reads the owner's row at rank 7 (761 [11])");
    }

    /* A "/" line goes to every member, the slash gone and the first letter
     * capitalised (RSProx index 236 [131155..131217]), and never aloud. */
    ToriRSServer_CaptureBegin(srv, &cap);
    fc_selftest_say(guest, 2, "/sup fellas");
    ToriRSServer_CaptureEnd(srv);
    said = fc_selftest_channel_text(&cap, 0, &index);
    SELFTEST_CHECK(strcmp(said, "Sup fellas") == 0, "the channel line reads \"Sup fellas\", got \"%s\"",
                   said);
    if( index >= 0 )
    {
        MsgMessageFriendchannel message;

        SELFTEST_CHECK(fc_selftest_read_message(&cap.packets[index], &message) &&
                           strcmp(message.sender, "Fc Guest") == 0 &&
                           message.channel_name == (int64_t)strtobase37("test fc name") &&
                           message.world_id == ToriRSServer_FriendsWorld(guest->name37) &&
                           message.huffman_length > 0,
                       "the client reads the line's sender, channel and world");
    }
    SELFTEST_CHECK(index >= 0 && ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_MESSAGE_FRIENDCHANNEL,
                                                               index + 1) >= 0,
                   "both members get the line");
    SELFTEST_CHECK(!(guest->masks & TORIRSSERVER_PMASK_CHAT), "a friends-chat line is not said aloud");

    /* Settings ride a 100-tick window: the first change says so, a second one
     * inside it says nothing (RSProx index 753 [25], [75]). */
    SELFTEST_CHECK(ToriRSServer_FriendchatSetTalkRank(owner, 1) == TORIRSSERVER_FRIENDCHAT_CHANGE_PENDING,
                   "the first change opens the window");
    SELFTEST_CHECK(ToriRSServer_FriendchatSetEnterRank(owner, -1) == TORIRSSERVER_FRIENDCHAT_CHANGE_QUIET,
                   "a change inside the window says nothing");
    g_fc_line_count = 0;
    fc_selftest_say(guest, 2, "/still here");
    SELFTEST_CHECK(!fc_selftest_heard(guest, "You are not allowed to talk in this friendschat channel."),
                   "the talk rank is not live before the flush");
    /* 100 ticks of two players' output would outgrow the capture: tick to
     * just short of the flush, then capture it. */
    fc_selftest_ticks(srv, 96);
    ToriRSServer_CaptureBegin(srv, &cap);
    fc_selftest_ticks(srv, 4);
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(!cap.overflow, "the flush's capture is whole");
    SELFTEST_CHECK(ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL, 0) >= 0,
                   "the flush sends every member the channel again (758 [126])");
    g_fc_line_count = 0;
    fc_selftest_say(guest, 2, "/x");
    SELFTEST_CHECK(fc_selftest_heard(guest, "You are not allowed to talk in this friendschat channel."),
                   "below the talk rank, the line is refused (757 [149])");
    ToriRSServer_FriendchatSetTalkRank(owner, -1);
    fc_selftest_ticks(srv, 101);

    /* A kick: both sides' lines, the kicked player out, and banned on a
     * rejoin (RSProx index 761 [96..98], 762 [44..57]). */
    g_fc_line_count = 0;
    fc_selftest_name_packet(owner, PKTOUT_NAME_FRIENDCHAT_KICK, "Fc Guest");
    SELFTEST_CHECK(fc_selftest_heard(guest, "You have been kicked from the channel.") &&
                       fc_selftest_heard(owner, "Your request to kick/ban this user was successful."),
                   "a kick tells the kicked and the kicker");
    SELFTEST_CHECK(ToriRSServer_FriendchatCurrent(guest)[0] == '\0', "the kicked player is out");
    g_fc_line_count = 0;
    fc_selftest_name_packet(guest, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, "Fc Owner");
    fc_selftest_ticks(srv, 1);
    SELFTEST_CHECK(fc_selftest_heard(guest, "You are temporarily banned from this chat-channel."),
                   "a kicked player rejoining is banned");

    /* The channel empties and its bans go with it (761 [248..258]). */
    g_fc_line_count = 0;
    selftest_handle(owner, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, NULL, 0);
    SELFTEST_CHECK(fc_selftest_heard(owner, "You have left the channel."),
                   "the leave line (753 [954])");
    g_fc_line_count = 0;
    fc_selftest_name_packet(guest, PKTOUT_NAME_FRIENDCHAT_JOIN_LEAVE, "Fc Owner");
    fc_selftest_ticks(srv, 1);
    SELFTEST_CHECK(fc_selftest_heard(guest, "Now talking in chat-channel Test Fc Name"),
                   "an emptied channel forgets its bans");

    /* A friend's rank: the friend list's row straight back, the window's line
     * (RSProx index 759 [3957..3979]); the channel hears it at the flush. */
    ToriRSServer_FriendsAdd(owner->name37, guest->name37);
    {
        uint8_t body[16];

        body[0] = 3;
        memcpy(body + 1, "Fc Guest", 9);
        g_fc_line_count = 0;
        ToriRSServer_CaptureBegin(srv, &cap);
        selftest_handle(owner, PKTOUT_NAME_FRIENDCHAT_SETRANK, body, 10);
        ToriRSServer_CaptureEnd(srv);
    }
    SELFTEST_CHECK(ToriRSServer_FriendchatFriendRank(owner->name37, guest->name37) == 3,
                   "the friend is a Sergeant");
    SELFTEST_CHECK(fc_selftest_heard(owner,
                                     "Changes will take effect on your friends channel in the next 60 seconds."),
                   "a rank change opens the window");
    SELFTEST_CHECK(ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_UPDATE_FRIENDLIST, 0) >= 0,
                   "the friend list's row carries the new rank");
    fc_selftest_ticks(srv, 96);
    ToriRSServer_CaptureBegin(srv, &cap);
    fc_selftest_ticks(srv, 4);
    ToriRSServer_CaptureEnd(srv);
    SELFTEST_CHECK(ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER, 0) >= 0,
                   "the flush sends the member's row with the new rank (754 [186])");

    /* Disabling lands at the flush: the owner's line, then every member
     * removed a tick later (RSProx index 753 [475..476]). */
    SELFTEST_CHECK(ToriRSServer_FriendchatDisable(owner) == TORIRSSERVER_FRIENDCHAT_CHANGE_PENDING,
                   "a disable opens the window");
    g_fc_line_count = 0;
    fc_selftest_ticks(srv, 100);
    SELFTEST_CHECK(fc_selftest_heard(owner, "Your chat-channel has now been disabled!") &&
                       !fc_selftest_heard_any(guest),
                   "the owner hears the disable first");
    fc_selftest_ticks(srv, 1);
    SELFTEST_CHECK(fc_selftest_heard(guest, "You have been removed from this channel.") &&
                       ToriRSServer_FriendchatCurrent(guest)[0] == '\0',
                   "and a tick later the members are removed");

    /* The save keeps the owner's channel. */
    {
        int enabled = -1, enter = 0, talk = 0, kick = 0;
        int64_t prefix37 = 0;
        char channel[16];

        SELFTEST_CHECK(ToriRSServer_FriendchatSaveFields(owner, &enabled, &prefix37, &enter, &talk,
                                                         &kick, channel, (int)sizeof(channel)) &&
                           enabled == 0 && prefix37 == (int64_t)strtobase37("test fc name") &&
                           kick == TORIRSSERVER_FRIENDCHAT_RANK_OWNER,
                       "the save holds the disabled channel and its prefix");
    }

    /* The setup panel (content): the four rows, then the panel (RSProx index
     * 753 [20]). */
    {
        int uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatchannel_current:setup");
        int prefix_uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatchannel_setup:opt_prefix");
        int kick_uid = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, "chatchannel_setup:opt_kick");
        uint8_t button[6];
        int saw_prefix = 0;
        int saw_kick = 0;

        SELFTEST_CHECK(uid >= 0 && prefix_uid >= 0 && kick_uid >= 0, "the setup components resolve");
        if( uid >= 0 )
        {
            button[0] = (uint8_t)(uid >> 24);
            button[1] = (uint8_t)(uid >> 16);
            button[2] = (uint8_t)(uid >> 8);
            button[3] = (uint8_t)uid;
            button[4] = 0xff;
            button[5] = 0xff;
            ToriRSServer_WorldSetActive(srv, owner);
            ToriRSServer_CaptureBegin(srv, &cap);
            selftest_handle(owner, PKTOUT_NAME_IF_BUTTON1, button, sizeof(button));
            ToriRSServer_CaptureEnd(srv);
            /* IF_SETTEXT at 239..242 is the NUL-terminated text, then the
             * component, or the other way round (RSProt IfSetText): the four rows in the recorded
             * order, enter / talk / kick / prefix. */
            {
                static const char* const k_rows[4] = { "Anyone", "Anyone", "Only me",
                                                        "Chat disabled" };
                int row = 0;

                for( int i = 0; i < cap.count && row < 4; i++ )
                {
                    const struct ToriRSServerCapturedPacket* packet = &cap.packets[i];

                    if( packet->name != PKT_NAME_IF_SETTEXT )
                        continue;
                    size_t want = strlen(k_rows[row]) + 1;

                    /* 239 puts the text first; 241 and 242 the component. */
                    if( packet->len == (int)want + 4 &&
                        (memcmp(packet->data, k_rows[row], want) == 0 ||
                         memcmp(packet->data + 4, k_rows[row], want) == 0) )
                    {
                        if( row == 2 )
                            saw_kick = 1;
                        if( row == 3 )
                            saw_prefix = 1;
                    }
                    row++;
                }
            }
            SELFTEST_CHECK(saw_prefix, "the disabled channel's prefix row reads \"Chat disabled\" (753 [25])");
            SELFTEST_CHECK(saw_kick, "the kick row reads \"Only me\"");
            SELFTEST_CHECK(ToriRSServer_CaptureFindNamed(&cap, PKT_NAME_IF_OPENSUB, 0) >= 0,
                           "the setup panel opens");
        }
    }

    ToriRSServer_MessageSink(NULL, NULL);
    ToriRSServer_FriendchatLogout(guest);
    ToriRSServer_WorldPlayerFree(srv, guest->pid);
    ToriRSServer_WorldSetActive(srv, owner);
    ToriRSServer_FriendchatReset();
    ToriRSServer_FriendsReset();
    if( owned )
        ToriRSServer_ScriptsFree(srv);
}
