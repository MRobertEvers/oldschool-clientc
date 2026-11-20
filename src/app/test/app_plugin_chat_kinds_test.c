/*
 * Every chat line the client adds reaches a plugin's on_chat_message, the four
 * kinds the packet-kinds step added included: a private echo, a friend-channel
 * line, a clan-channel line and a clan-channel system line (RuneLite's
 * ChatMessage fires for every line the client adds, whichever channel).
 *
 * The packets go through the client's own executor (App_GameProto_Exec) into
 * a PluginHost built over the client's own engine (app_plugin_engine): the
 * path a live line takes, App_NotifyChatMessage to PluginHost_ChatMessage to
 * the plugin, with nothing stubbed between the packet and the callback.
 */
#include "app.h"
#include "app/app_internal.h"
#include "app/app_gameproto_exec.h"
#include "game/rs_chat.h"
#include "game/rs_clan.h"
#include "game/rs_server_state.h"
#include "mem/torirs_mem.h"
#include "net/rev/revpacket.h"
#include "plugin/torirs_plugin_api.h"
#include "plugin/torirs_plugin_host.h"
#include "ui/uitree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                      \
            exit(1);                                                                               \
        }                                                                                          \
    } while( 0 )

enum
{
    HEARD_MAX = 8
};

static struct ToriRS_ChatMessageEvent g_heard[HEARD_MAX];
static int g_heard_count;

static void
chat_probe(
    struct ToriRS_Api* api,
    void* state,
    struct ToriRS_ChatMessageEvent const* event)
{
    (void)api;
    (void)state;
    assert(event);
    assert(g_heard_count < HEARD_MAX);
    g_heard[g_heard_count++] = *event;
}

static struct ToriRS_PluginDef const CHAT_PROBE = {
    .struct_size = sizeof(CHAT_PROBE),
    .id = "chat-kinds-probe",
    .title = "Chat Kinds Probe",
    .version = "1.0.0",
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_chat_message = chat_probe,
    },
};

/* The one line the last packet should have delivered. */
static void
expect_heard(
    int type,
    char const* sender,
    char const* text,
    char const* what)
{
    CHECK(g_heard_count == 1, what);
    CHECK(g_heard[0].type == type, what);
    CHECK(strcmp(g_heard[0].sender, sender) == 0, what);
    CHECK(strcmp(g_heard[0].text, text) == 0, what);
    g_heard_count = 0;
    printf("ok - %s\n", what);
}

int
main(void)
{
    struct App* app = calloc(1, sizeof(*app));
    struct RS_ServerState state;
    struct ToriRS_PluginEngine engine;
    struct RS_ClanChannel* channel;
    struct RevPacket p;
    char echo_text[] = "on my way";
    char fc_text[] = "hello channel";
    char clan_text[] = "hello clan";
    char clan_system_text[] = "Bob has joined.";

    assert(app);
    app->tree = UITree_New(2);
    RS_Chat_Init(&app->chat, "Player");
    RS_CS2Host_SetChat(&app->host, &app->chat);
    App_ServerStateBind(app, &state);
    engine = app_plugin_engine(app);
    app->plugins = PluginHost_New(&engine);
    CHECK(PluginHost_Register(app->plugins, &CHAT_PROBE) >= 0, "the probe registers");
    PluginHost_Start(app->plugins);

    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MESSAGE_PRIVATE_ECHO;
    snprintf(p._message_private_echo.recipient, sizeof(p._message_private_echo.recipient), "%s", "bob");
    p._message_private_echo.text = echo_text;
    App_GameProto_Exec(app, &state, &p);
    expect_heard(RS_CHAT_TYPE_PRIVATE_TO, "bob", "on my way", "a private echo reaches on_chat_message");

    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MESSAGE_FRIENDCHANNEL;
    snprintf(p._message_friendchannel.sender, sizeof(p._message_friendchannel.sender), "%s", "carol");
    p._message_friendchannel.channel_name = 12345;
    p._message_friendchannel.world = 301;
    p._message_friendchannel.counter = 9;
    p._message_friendchannel.text = fc_text;
    App_GameProto_Exec(app, &state, &p);
    expect_heard(
        RS_CHAT_TYPE_FRIENDS_CHAT, "carol", "hello channel", "a friend-channel line reaches on_chat_message");

    /* A clan line needs the channel it is for: the reference drops one for a
     * channel it has not been sent. The listened (guest) channel here. */
    channel = calloc(1, sizeof(*channel));
    assert(channel);
    snprintf(channel->name, sizeof(channel->name), "%s", "Guests");
    app->host.clan.listened_channel = channel;

    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MESSAGE_CLANCHANNEL;
    p._message_clanchannel.clan_type = -1;
    snprintf(p._message_clanchannel.sender, sizeof(p._message_clanchannel.sender), "%s", "dave");
    p._message_clanchannel.world = 302;
    p._message_clanchannel.counter = 1;
    p._message_clanchannel.crown = 0;
    p._message_clanchannel.text = clan_text;
    App_GameProto_Exec(app, &state, &p);
    expect_heard(
        RS_CHAT_TYPE_CLAN_GUEST_CHAT, "dave", "hello clan", "a clan-channel line reaches on_chat_message");

    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM;
    p._message_clanchannel.clan_type = -1;
    p._message_clanchannel.world = 302;
    p._message_clanchannel.counter = 2;
    p._message_clanchannel.crown = -1;
    p._message_clanchannel.text = clan_system_text;
    App_GameProto_Exec(app, &state, &p);
    expect_heard(
        RS_CHAT_TYPE_CLAN_GUEST_MESSAGE, "Guests", "Bob has joined.",
        "a clan-channel system line reaches on_chat_message, named for its channel");

    /* The same clan line again is a line already seen: nothing new is heard. */
    memset(&p, 0, sizeof(p));
    p.packet_type = PKT_NAME_MESSAGE_CLANCHANNEL;
    p._message_clanchannel.clan_type = -1;
    snprintf(p._message_clanchannel.sender, sizeof(p._message_clanchannel.sender), "%s", "dave");
    p._message_clanchannel.world = 302;
    p._message_clanchannel.counter = 1;
    p._message_clanchannel.text = clan_text;
    App_GameProto_Exec(app, &state, &p);
    CHECK(g_heard_count == 0, "a repeated clan line is not heard twice");
    printf("ok - a repeated clan line is not heard twice\n");

    app->host.clan.listened_channel = NULL;
    free(channel);
    PluginHost_Free(app->plugins);
    printf("plugin-chat-kinds: all tests passed\n");
    return 0;
}
