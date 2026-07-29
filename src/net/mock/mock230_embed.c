/*
 * The in-process server. See mock230_embed.h.
 *
 * This file is short on purpose. Everything an embedded server needs already
 * existed once the session stopped blocking; what is here is the plumbing that
 * points a session at two byte queues instead of a socket, plus the pump the
 * host drives in place of a select() loop.
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

struct Mock230Embed
{
    struct Mock230Server srv;
    struct Mock230Session session;
    struct Mock230Transport transport;

    /* From the server's point of view: it reads `to_server`, writes `to_client`. */
    struct Mock230Pipe to_server;
    struct Mock230Pipe to_client;
    struct Mock230MemoryEnds ends;

    struct Mock230BootConfig config;

    int online;
};

/*
 * Static data is process-wide (the cache decoders and the content tree are all
 * file-scope tables), so a second concurrent embed would be sharing them. One
 * handle at a time, and say so rather than corrupting quietly.
 */
static int g_embed_live;

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

    mock230_transport_memory(&embed->transport, &embed->ends, &embed->to_server,
                             &embed->to_client);
    mock230_session_init(&embed->session, &embed->transport, embed->srv.verbose);
    embed->srv.session = &embed->session;

    g_embed_live = 1;
    return embed;
}

void
mock230_embed_stop(struct Mock230Embed* embed)
{
    if( !embed )
        return;

    mock230_bank_shutdown(&embed->srv);
    mock230_scripts_free(&embed->srv);
    mock230_session_free(&embed->session);
    /* The transport closed the pipes; this releases what they held. */
    mock230_pipe_free(&embed->to_server);
    mock230_pipe_free(&embed->to_client);
    mock230_boot_free();

    free(embed);
    g_embed_live = 0;
}

int
mock230_embed_write(
    struct Mock230Embed* embed,
    const uint8_t* data,
    int len)
{
    if( !mock230_session_alive(&embed->session) )
        return -1;
    return mock230_pipe_write(&embed->to_server, data, len);
}

int
mock230_embed_read(
    struct Mock230Embed* embed,
    uint8_t* dst,
    int max)
{
    return mock230_pipe_read(&embed->to_client, dst, max);
}

int
mock230_embed_pending(const struct Mock230Embed* embed)
{
    return mock230_pipe_available(&embed->to_client);
}

struct Mock230Server*
mock230_embed_world(struct Mock230Embed* embed)
{
    return embed->online ? &embed->srv : NULL;
}

int
mock230_embed_online(const struct Mock230Embed* embed)
{
    return embed->online;
}

int
mock230_embed_pump(
    struct Mock230Embed* embed,
    int run_tick)
{
    if( !mock230_session_alive(&embed->session) )
        return 0;

    if( !mock230_session_pump(&embed->session, &embed->srv) )
        return 0;

    /* Identical to the socket server's, deliberately: the world coming up is
     * not transport business, so both callers answer the same signal the same
     * way. */
    if( mock230_session_take_login(&embed->session) )
    {
        mock230_scripts_load(&embed->srv, embed->config.script_dir);
        mock230_world_init(&embed->srv, mock230_boot_zone(embed->config.home_x),
                           mock230_boot_zone(embed->config.home_z));
        mock230_world_set_display_name(&embed->srv, embed->session.display_name);
        mock230_world_login(&embed->srv);
        embed->online = 1;
        /* Anything sent behind the login block is still buffered. */
        if( !mock230_session_pump(&embed->session, &embed->srv) )
            return 0;
    }

    if( run_tick && embed->online )
        mock230_world_tick(&embed->srv);

    return mock230_session_alive(&embed->session);
}
