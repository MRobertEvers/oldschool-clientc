/*
 * The in-process server. See mock230_embed.h.
 *
 * This file is short on purpose. Everything an embedded server needs already
 * existed once the session stopped blocking; what is here is the plumbing that
 * points a session at two byte queues instead of a socket, plus the pump the
 * host drives in place of a select() loop.
 *
 * One world, N connections. The connection array is what a socket server gets
 * from accept(); here the host asks for one with mock230_embed_connect, and the
 * two differ only in where the bytes come from — the login sequence below is
 * character for character the socket server's.
 */

#include "mock230_embed.h"

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_boot.h"
#include "mock230_session.h"
#include "mock230_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Mock230EmbedClient
{
    int open;
    struct Mock230Session session;
    struct Mock230Transport transport;

    /* From the server's point of view: it reads `to_server`, writes `to_client`. */
    struct Mock230Pipe to_server;
    struct Mock230Pipe to_client;
    struct Mock230MemoryEnds ends;

    int online;
};

struct Mock230Embed
{
    struct Mock230Server srv;
    struct Mock230EmbedClient clients[MOCK230_EMBED_CLIENT_MAX];
    struct Mock230BootConfig config;
};

/*
 * Static data is process-wide (the cache decoders and the content tree are all
 * file-scope tables), so a second concurrent embed would be sharing them. One
 * handle at a time, and say so rather than corrupting quietly. Note this is a
 * limit on *worlds*, not on clients: several clients in one embed is the point.
 */
static int g_embed_live;

static struct Mock230EmbedClient*
client_at(
    struct Mock230Embed* embed,
    int client)
{
    if( !embed || client < 0 || client >= MOCK230_EMBED_CLIENT_MAX )
        return NULL;
    return embed->clients[client].open ? &embed->clients[client] : NULL;
}

struct Mock230Embed*
mock230_embed_start(void)
{
    struct Mock230Embed* embed;

    if( g_embed_live )
    {
        fprintf(stderr, "mock230: an embedded server is already running\n");
        return NULL;
    }

    embed = (struct Mock230Embed*)calloc(1, sizeof(*embed));
    if( !embed )
        return NULL;

    mock230_boot_defaults(&embed->config);
    mock230_boot_load(&embed->config);

    embed->srv.verbose = getenv("MOCK230_VERBOSE") != NULL;

    g_embed_live = 1;

    /* Client 0 opens with the world, so a single-client host — which is every
     * host that existed before this — never has to ask for one. */
    if( mock230_embed_connect(embed) != 0 )
    {
        mock230_embed_stop(embed);
        return NULL;
    }
    return embed;
}

int
mock230_embed_connect(struct Mock230Embed* embed)
{
    for( int i = 0; i < MOCK230_EMBED_CLIENT_MAX; i++ )
    {
        struct Mock230EmbedClient* client = &embed->clients[i];

        if( client->open )
            continue;
        memset(client, 0, sizeof(*client));
        client->open = 1;
        mock230_transport_memory(&client->transport, &client->ends, &client->to_server,
                                 &client->to_client);
        mock230_session_init(&client->session, &client->transport, embed->srv.verbose);
        return i;
    }
    fprintf(stderr, "mock230: the embedded server holds %d clients already\n",
            MOCK230_EMBED_CLIENT_MAX);
    return -1;
}

void
mock230_embed_stop(struct Mock230Embed* embed)
{
    if( !embed )
        return;

    for( int i = 0; i < MOCK230_EMBED_CLIENT_MAX; i++ )
    {
        struct Mock230EmbedClient* client = &embed->clients[i];

        if( !client->open )
            continue;
        mock230_session_free(&client->session);
        /* The transport closed the pipes; this releases what they held. */
        mock230_pipe_free(&client->to_server);
        mock230_pipe_free(&client->to_client);
    }
    mock230_bank_shutdown(&embed->srv);
    mock230_scripts_free(&embed->srv);
    mock230_boot_free();

    free(embed);
    g_embed_live = 0;
}

int
mock230_embed_write(
    struct Mock230Embed* embed,
    int client_id,
    const uint8_t* data,
    int len)
{
    struct Mock230EmbedClient* client = client_at(embed, client_id);

    if( !client || !mock230_session_alive(&client->session) )
        return -1;
    return mock230_pipe_write(&client->to_server, data, len);
}

int
mock230_embed_read(
    struct Mock230Embed* embed,
    int client_id,
    uint8_t* dst,
    int max)
{
    struct Mock230EmbedClient* client = client_at(embed, client_id);

    if( !client )
        return -1;
    return mock230_pipe_read(&client->to_client, dst, max);
}

int
mock230_embed_pending(
    const struct Mock230Embed* embed,
    int client_id)
{
    const struct Mock230EmbedClient* client =
        client_at((struct Mock230Embed*)embed, client_id);

    return client ? mock230_pipe_available(&client->to_client) : 0;
}

struct Mock230Server*
mock230_embed_world(struct Mock230Embed* embed)
{
    for( int i = 0; i < MOCK230_EMBED_CLIENT_MAX; i++ )
        if( embed->clients[i].open && embed->clients[i].online )
            return &embed->srv;
    return NULL;
}

int
mock230_embed_online(
    const struct Mock230Embed* embed,
    int client_id)
{
    const struct Mock230EmbedClient* client =
        client_at((struct Mock230Embed*)embed, client_id);

    return client ? client->online : 0;
}

struct Mock230Player*
mock230_embed_player(
    struct Mock230Embed* embed,
    int client_id)
{
    struct Mock230EmbedClient* client = client_at(embed, client_id);

    return client ? client->session.player : NULL;
}

/* Decode whatever this client sent and bring its world up if the handshake just
 * completed. Identical to the socket server's, deliberately: the world coming up
 * is not transport business, so both callers answer the same signal the same
 * way. */
static int
pump_client(
    struct Mock230Embed* embed,
    struct Mock230EmbedClient* client)
{
    if( !mock230_session_alive(&client->session) )
        return 0;

    if( !mock230_session_pump(&client->session, &embed->srv) )
        return 0;

    if( mock230_session_take_login(&client->session) )
    {
        struct Mock230Player* player;

        mock230_scripts_load(&embed->srv, embed->config.script_dir);
        mock230_world_init(&embed->srv, mock230_boot_zone(embed->config.home_x),
                           mock230_boot_zone(embed->config.home_z));
        player = mock230_world_add_player(&embed->srv, &client->session);
        if( !player )
            return 0;
        client->session.player = player;
        mock230_world_player_init(player);
        mock230_world_set_display_name(player, client->session.display_name);
        mock230_world_login(player);
        client->online = 1;
        /* Anything sent behind the login block is still buffered. */
        if( !mock230_session_pump(&client->session, &embed->srv) )
            return 0;
    }

    return mock230_session_alive(&client->session);
}

int
mock230_embed_pump(
    struct Mock230Embed* embed,
    int run_tick)
{
    int any_alive = 0;
    int any_online = 0;

    /*
     * Every client's input first, then *one* tick. The tick is the world's, not
     * a client's: running it per client would advance the world N times per
     * 600 ms and give whoever is pumped first N moves to everyone else's one.
     */
    for( int i = 0; i < MOCK230_EMBED_CLIENT_MAX; i++ )
    {
        struct Mock230EmbedClient* client = &embed->clients[i];

        if( !client->open )
            continue;
        if( pump_client(embed, client) )
            any_alive = 1;
        if( client->online )
            any_online = 1;
    }

    if( run_tick && any_online )
        mock230_world_tick(&embed->srv);

    return any_alive;
}
