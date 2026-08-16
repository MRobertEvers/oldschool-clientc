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
#include <assert.h>

#include "mock230.h"
#include "mock230_bank.h"
#include "mock230_container.h"
#include "mock230_shop.h"
#include "mock230_boot.h"
#include "mock230_session.h"
#include "mock230_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    if( client < 0 || client >= MOCK230_EMBED_CLIENT_MAX )
        return NULL;
    assert(embed);
    return embed->clients[client].open ? &embed->clients[client] : NULL;
}

struct Mock230Embed*
mock230_embed_start(char const* rev_name)
{
    struct Mock230Embed* embed;

    if( g_embed_live )
    {
        fprintf(stderr, "mock230: an embedded server is already running\n");
        return NULL;
    }

    embed = (struct Mock230Embed*)calloc(1, sizeof(*embed));
    assert(embed);

    mock230_boot_defaults(&embed->config);
    mock230_boot_load(&embed->config);
    /* Shop definitions are global (mock230_content_load populated them);
     * seeding a container is per-server-instance, so it happens once `srv`
     * itself exists. Calloc above already zeroed world_containers. */
    mock230_shop_seed(&embed->srv);

    embed->srv.verbose = getenv("MOCK230_VERBOSE") != NULL;
    embed->srv.members_world = mock230_flag_default_on("MOCK230_MEMBERS_WORLD");
    /*
     * Which bytes this world writes (and which login block it expects). The
     * caller's revision wins — the embed serves exactly one client, in this
     * process, so its wire is a fact about that client and not a preference
     * (a mismatch surfaces as "rsa decrypt failed" at login). MOCK230_REV and
     * the osrs230 default remain for hosts that pass NULL (embed_test).
     */
    {
        if( !rev_name )
            rev_name = getenv("MOCK230_REV");
        const struct Mock230Wire* wire =
            rev_name ? mock230_wire_by_name(rev_name) : mock230_wire_default();

        if( !wire )
        {
            fprintf(stderr, "mock230: unknown embed revision '%s' (osrs230, osrs239)\n",
                    rev_name);
            mock230_boot_free();
            free(embed);
            return NULL;
        }
        embed->srv.wire = wire;
        fprintf(stderr, "mock230: embedded wire %s\n", wire->name);
    }

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

int
mock230_embed_disconnect(
    struct Mock230Embed* embed,
    int client_id)
{
    struct Mock230EmbedClient* client = client_at(embed, client_id);

    if( !client )
        return 0;

    /* Order matters and is the socket server's: the world lets go of the player
     * while the session is still addressable, because that is what makes the
     * packets a logout generates reach everyone *else* before the queues go. */
    mock230_world_remove_player(&embed->srv, client->session.player);
    client->session.player = NULL;
    client->online = 0;
    mock230_session_free(&client->session);
    mock230_pipe_free(&client->to_server);
    mock230_pipe_free(&client->to_client);
    client->open = 0;
    return 1;
}

void
mock230_embed_stop(struct Mock230Embed* embed)
{
    assert(embed);

    for( int i = 0; i < MOCK230_EMBED_CLIENT_MAX; i++ )
    {
        struct Mock230EmbedClient* client = &embed->clients[i];

        if( !client->open )
            continue;
        /*
         * Log the player out before freeing anything, exactly as
         * `mock230_embed_disconnect` does.
         *
         * Shutting the host down IS a logout for whoever is still on it, and
         * this loop was skipping it — it freed the session and the pipes and
         * left `mock230_world_remove_player` uncalled. That was invisible while
         * `remove_player` only released a slot the process was about to drop
         * anyway; it stopped being invisible when the save moved there, because
         * closing the embedded client (which is how anyone actually plays this)
         * threw the session's progress away while a socket logout kept it.
         */
        if( client->session.player )
        {
            mock230_world_remove_player(&embed->srv, client->session.player);
            client->session.player = NULL;
        }
        mock230_session_free(&client->session);
        /* The transport closed the pipes; this releases what they held. */
        mock230_pipe_free(&client->to_server);
        mock230_pipe_free(&client->to_client);
    }
    mock230_bank_shutdown(&embed->srv);
    mock230_container_shutdown(&embed->srv);
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

/*
 * TORIRS_SERVER_BREAKDOWN=<ms>: when one pump exceeds <ms>, say how much of it
 * was draining client input versus running the world tick. mock230_world_tick
 * splits its own half by phase under the same switch. A host that shares its
 * frame thread with this pump -- the client does -- drops a frame for every
 * millisecond spent here, and the two halves have nothing in common, so
 * knowing which one ran long is the first question.
 */
static int g_pump_bd_ms = -1;

/*
 * Script cost inside the *client* half, from mock230_scripts.c.
 *
 * mock230_world_tick zeroes these at its own start, so anything a packet
 * handler ran before the tick was overwritten before the tick reported. That
 * mattered: a click arrives as a packet, its `[opnpc]`/`[oploc]` trigger runs
 * here rather than in a phase, and a pump that spent 300 ms decoding input
 * looked like it spent it on nothing at all.
 */
extern uint64_t g_mock230_script_us;
extern int g_mock230_script_runs;
extern uint64_t g_mock230_script_slow_us;
extern char g_mock230_script_slow_name[96];

static int
pump_bd_on(void)
{
    if( g_pump_bd_ms < 0 )
    {
        char const* v = getenv("TORIRS_SERVER_BREAKDOWN");
        g_pump_bd_ms = (v && v[0]) ? atoi(v) : 0;
    }
    return g_pump_bd_ms > 0;
}

static uint64_t
pump_bd_now_us(void)
{
    struct timespec ts;

    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        return 0;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

int
mock230_embed_pump(
    struct Mock230Embed* embed,
    int run_tick)
{
    int any_alive = 0;
    int any_online = 0;
    int bd_on = pump_bd_on();
    uint64_t bd_t0 = bd_on ? pump_bd_now_us() : 0;
    uint64_t bd_clients = 0;
    uint64_t bd_tick = 0;
    uint64_t bd_script_us = 0;
    int bd_script_runs = 0;
    uint64_t bd_script_slow_us = 0;
    char bd_script_slow[96];

    bd_script_slow[0] = '\0';
    if( bd_on )
    {
        g_mock230_script_us = 0;
        g_mock230_script_runs = 0;
        g_mock230_script_slow_us = 0;
        g_mock230_script_slow_name[0] = '\0';
    }

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

    if( bd_on )
    {
        bd_clients = pump_bd_now_us() - bd_t0;
        /* Snapshot before the tick, which zeroes these for its own accounting. */
        bd_script_us = g_mock230_script_us;
        bd_script_runs = g_mock230_script_runs;
        bd_script_slow_us = g_mock230_script_slow_us;
        snprintf(bd_script_slow, sizeof(bd_script_slow), "%s", g_mock230_script_slow_name);
    }

    if( run_tick && any_online )
        mock230_world_tick(&embed->srv);

    if( bd_on )
    {
        uint64_t total = pump_bd_now_us() - bd_t0;

        bd_tick = total - bd_clients;
        if( total >= (uint64_t)g_pump_bd_ms * 1000u )
        {
            fprintf(stderr,
                    "server_pump: total %.2f ms clients %.2f tick %.2f (tick_ran %d)"
                    " | client scripts %.2f x%d",
                    total / 1000.0, bd_clients / 1000.0, bd_tick / 1000.0,
                    run_tick && any_online, bd_script_us / 1000.0, bd_script_runs);
            if( bd_script_slow[0] )
                fprintf(stderr, " slowest %s %.2f", bd_script_slow,
                        bd_script_slow_us / 1000.0);
            fprintf(stderr, "\n");
        }
    }

    return any_alive;
}
