/*
 * The in-process server. See torirs_server_embed.h.
 *
 * This file is short on purpose. Everything an embedded server needs already
 * existed once the session stopped blocking; what is here is the plumbing that
 * points a session at two byte queues instead of a socket, plus the pump the
 * host drives in place of a select() loop.
 *
 * One world, N connections. The connection array is what a socket server gets
 * from accept(); here the host asks for one with ToriRSServer_EmbedConnect, and the
 * two differ only in where the bytes come from — the login sequence below is
 * character for character the socket server's.
 */

#include "torirs_server_embed.h"
#include <assert.h>

#include "torirs_server.h"
#include "torirs_server_bank.h"
#include "torirs_server_container.h"
#include "torirs_server_shop.h"
#include "torirs_server_boot.h"
#include "torirs_server_session.h"
#include "torirs_server_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct ToriRSServerEmbedClient
{
    int open;
    struct ToriRSServerSession session;
    struct ToriRSServerTransport transport;

    /* From the server's point of view: it reads `to_server`, writes `to_client`. */
    struct ToriRSServerPipe to_server;
    struct ToriRSServerPipe to_client;
    struct ToriRSServerMemoryEnds ends;

    int online;
};

struct ToriRSServerEmbed
{
    struct ToriRSServer srv;
    struct ToriRSServerEmbedClient clients[TORIRSSERVER_EMBED_CLIENT_MAX];
    struct ToriRSServerBootConfig config;
};

/*
 * Static data is process-wide (the cache decoders and the content tree are all
 * file-scope tables), so a second concurrent embed would be sharing them. One
 * handle at a time, and say so rather than corrupting quietly. Note this is a
 * limit on *worlds*, not on clients: several clients in one embed is the point.
 */
static int g_embed_live;

static struct ToriRSServerEmbedClient*
client_at(
    struct ToriRSServerEmbed* embed,
    int client)
{
    if( client < 0 || client >= TORIRSSERVER_EMBED_CLIENT_MAX )
        return NULL;
    assert(embed);
    return embed->clients[client].open ? &embed->clients[client] : NULL;
}

struct ToriRSServerEmbed*
ToriRSServer_EmbedStart(char const* rev_name)
{
    struct ToriRSServerEmbed* embed;

    if( g_embed_live )
    {
        fprintf(stderr, "torirsserver: an embedded server is already running\n");
        return NULL;
    }

    embed = (struct ToriRSServerEmbed*)calloc(1, sizeof(*embed));
    assert(embed);

    ToriRSServer_BootDefaults(&embed->config);
    ToriRSServer_BootLoad(&embed->config);
    /* Shop definitions are global (ToriRSServer_ContentLoad populated them);
     * seeding a container is per-server-instance, so it happens once `srv`
     * itself exists. Calloc above already zeroed world_containers. */
    ToriRSServer_ShopSeed(&embed->srv);

    embed->srv.verbose = getenv("TORIRSSERVER_VERBOSE") != NULL;
    embed->srv.members_world = ToriRSServer_FlagDefaultOn("TORIRSSERVER_MEMBERS_WORLD");
    /*
     * Which bytes this world writes (and which login block it expects). The
     * caller's revision wins — the embed serves exactly one client, in this
     * process, so its wire is a fact about that client and not a preference
     * (a mismatch surfaces as "rsa decrypt failed" at login). TORIRSSERVER_REV and
     * the osrs230 default remain for hosts that pass NULL (embed_test).
     */
    {
        if( !rev_name )
            rev_name = getenv("TORIRSSERVER_REV");
        const struct ToriRSServerWire* wire =
            rev_name ? ToriRSServer_WireByName(rev_name) : ToriRSServer_WireDefault();

        if( !wire )
        {
            fprintf(stderr, "torirsserver: unknown embed revision '%s' (osrs230, osrs239)\n",
                    rev_name);
            ToriRSServer_BootFree();
            free(embed);
            return NULL;
        }
        embed->srv.wire = wire;
        fprintf(stderr, "torirsserver: embedded wire %s\n", wire->name);
    }

    g_embed_live = 1;

    /* Client 0 opens with the world, so a single-client host — which is every
     * host that existed before this — never has to ask for one. */
    if( ToriRSServer_EmbedConnect(embed) != 0 )
    {
        ToriRSServer_EmbedStop(embed);
        return NULL;
    }
    return embed;
}

int
ToriRSServer_EmbedConnect(struct ToriRSServerEmbed* embed)
{
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        struct ToriRSServerEmbedClient* client = &embed->clients[i];

        if( client->open )
            continue;
        memset(client, 0, sizeof(*client));
        client->open = 1;
        ToriRSServer_TransportMemory(&client->transport, &client->ends, &client->to_server,
                                 &client->to_client);
        ToriRSServer_SessionInit(&client->session, &client->transport, embed->srv.verbose);
        return i;
    }
    fprintf(stderr, "torirsserver: the embedded server holds %d clients already\n",
            TORIRSSERVER_EMBED_CLIENT_MAX);
    return -1;
}

int
ToriRSServer_EmbedDisconnect(
    struct ToriRSServerEmbed* embed,
    int client_id)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    if( !client )
        return 0;

    /* Order matters and is the socket server's: the world lets go of the player
     * while the session is still addressable, because that is what makes the
     * packets a logout generates reach everyone *else* before the queues go. */
    ToriRSServer_WorldRemovePlayer(&embed->srv, client->session.player);
    client->session.player = NULL;
    client->online = 0;
    ToriRSServer_SessionFree(&client->session);
    ToriRSServer_PipeFree(&client->to_server);
    ToriRSServer_PipeFree(&client->to_client);
    client->open = 0;
    return 1;
}

void
ToriRSServer_EmbedStop(struct ToriRSServerEmbed* embed)
{
    assert(embed);

    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        struct ToriRSServerEmbedClient* client = &embed->clients[i];

        if( !client->open )
            continue;
        /*
         * Log the player out before freeing anything, exactly as
         * `ToriRSServer_EmbedDisconnect` does.
         *
         * Shutting the host down IS a logout for whoever is still on it, and
         * this loop was skipping it — it freed the session and the pipes and
         * left `ToriRSServer_WorldRemovePlayer` uncalled. That was invisible while
         * `remove_player` only released a slot the process was about to drop
         * anyway; it stopped being invisible when the save moved there, because
         * closing the embedded client (which is how anyone actually plays this)
         * threw the session's progress away while a socket logout kept it.
         */
        if( client->session.player )
        {
            ToriRSServer_WorldRemovePlayer(&embed->srv, client->session.player);
            client->session.player = NULL;
        }
        ToriRSServer_SessionFree(&client->session);
        /* The transport closed the pipes; this releases what they held. */
        ToriRSServer_PipeFree(&client->to_server);
        ToriRSServer_PipeFree(&client->to_client);
    }
    ToriRSServer_BankShutdown(&embed->srv);
    ToriRSServer_ContainerShutdown(&embed->srv);
    ToriRSServer_ScriptsFree(&embed->srv);
    ToriRSServer_BootFree();

    free(embed);
    g_embed_live = 0;
}

int
ToriRSServer_EmbedWrite(
    struct ToriRSServerEmbed* embed,
    int client_id,
    const uint8_t* data,
    int len)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    if( !client || !ToriRSServer_SessionAlive(&client->session) )
        return -1;
    return ToriRSServer_PipeWrite(&client->to_server, data, len);
}

int
ToriRSServer_EmbedRead(
    struct ToriRSServerEmbed* embed,
    int client_id,
    uint8_t* dst,
    int max)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    if( !client )
        return -1;
    return ToriRSServer_PipeRead(&client->to_client, dst, max);
}

int
ToriRSServer_EmbedPending(
    const struct ToriRSServerEmbed* embed,
    int client_id)
{
    const struct ToriRSServerEmbedClient* client =
        client_at((struct ToriRSServerEmbed*)embed, client_id);

    return client ? ToriRSServer_PipeAvailable(&client->to_client) : 0;
}

struct ToriRSServer*
ToriRSServer_EmbedWorld(struct ToriRSServerEmbed* embed)
{
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
        if( embed->clients[i].open && embed->clients[i].online )
            return &embed->srv;
    return NULL;
}

int
ToriRSServer_EmbedOnline(
    const struct ToriRSServerEmbed* embed,
    int client_id)
{
    const struct ToriRSServerEmbedClient* client =
        client_at((struct ToriRSServerEmbed*)embed, client_id);

    return client ? client->online : 0;
}

struct ToriRSServerPlayer*
ToriRSServer_EmbedPlayer(
    struct ToriRSServerEmbed* embed,
    int client_id)
{
    struct ToriRSServerEmbedClient* client = client_at(embed, client_id);

    return client ? client->session.player : NULL;
}

/* Decode whatever this client sent and bring its world up if the handshake just
 * completed. Identical to the socket server's, deliberately: the world coming up
 * is not transport business, so both callers answer the same signal the same
 * way. */
static int
pump_client(
    struct ToriRSServerEmbed* embed,
    struct ToriRSServerEmbedClient* client)
{
    if( !ToriRSServer_SessionAlive(&client->session) )
        return 0;

    if( !ToriRSServer_SessionPump(&client->session, &embed->srv) )
        return 0;

    if( ToriRSServer_SessionTakeLogin(&client->session) )
    {
        struct ToriRSServerPlayer* player;

        ToriRSServer_ScriptsLoad(&embed->srv, embed->config.script_dir);
        ToriRSServer_WorldInit(&embed->srv, ToriRSServer_BootZone(embed->config.home_x),
                           ToriRSServer_BootZone(embed->config.home_z));
        player = ToriRSServer_WorldAddPlayer(&embed->srv, &client->session);
        if( !player )
            return 0;
        client->session.player = player;
        ToriRSServer_WorldPlayerInit(player);
        ToriRSServer_WorldSetDisplayName(player, client->session.display_name);
        ToriRSServer_WorldLogin(player);
        client->online = 1;
        /* Anything sent behind the login block is still buffered. */
        if( !ToriRSServer_SessionPump(&client->session, &embed->srv) )
            return 0;
    }

    return ToriRSServer_SessionAlive(&client->session);
}

/*
 * TORIRS_SERVER_BREAKDOWN=<ms>: when one pump exceeds <ms>, say how much of it
 * was draining client input versus running the world tick. ToriRSServer_WorldTick
 * splits its own half by phase under the same switch. A host that shares its
 * frame thread with this pump -- the client does -- drops a frame for every
 * millisecond spent here, and the two halves have nothing in common, so
 * knowing which one ran long is the first question.
 */
static int g_pump_bd_ms = -1;

/*
 * Script cost inside the *client* half, from torirs_server_scripts.c.
 *
 * ToriRSServer_WorldTick zeroes these at its own start, so anything a packet
 * handler ran before the tick was overwritten before the tick reported. That
 * mattered: a click arrives as a packet, its `[opnpc]`/`[oploc]` trigger runs
 * here rather than in a phase, and a pump that spent 300 ms decoding input
 * looked like it spent it on nothing at all.
 */
extern uint64_t g_ToriRSServer_ScriptUs;
extern int g_ToriRSServer_ScriptRuns;
extern uint64_t g_ToriRSServer_ScriptSlowUs;
extern char g_ToriRSServer_ScriptSlowName[96];

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
ToriRSServer_EmbedPump(
    struct ToriRSServerEmbed* embed,
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
        g_ToriRSServer_ScriptUs = 0;
        g_ToriRSServer_ScriptRuns = 0;
        g_ToriRSServer_ScriptSlowUs = 0;
        g_ToriRSServer_ScriptSlowName[0] = '\0';
    }

    /*
     * Every client's input first, then *one* tick. The tick is the world's, not
     * a client's: running it per client would advance the world N times per
     * 600 ms and give whoever is pumped first N moves to everyone else's one.
     */
    for( int i = 0; i < TORIRSSERVER_EMBED_CLIENT_MAX; i++ )
    {
        struct ToriRSServerEmbedClient* client = &embed->clients[i];

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
        bd_script_us = g_ToriRSServer_ScriptUs;
        bd_script_runs = g_ToriRSServer_ScriptRuns;
        bd_script_slow_us = g_ToriRSServer_ScriptSlowUs;
        snprintf(bd_script_slow, sizeof(bd_script_slow), "%s", g_ToriRSServer_ScriptSlowName);
    }

    if( run_tick && any_online )
        ToriRSServer_WorldTick(&embed->srv);

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
