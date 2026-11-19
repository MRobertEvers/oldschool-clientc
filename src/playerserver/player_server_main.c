/*
 * player_server: the player account server as its own process, for worlds
 * that share one saves directory. See player_server.h.
 *
 *   TORIRSSERVER_SAVES=saves player_server [--port 8092]
 *
 * Every world then names it: TORIRS_PLAYER_SERVER=127.0.0.1:8092. A world
 * that names none hosts one itself on this port, so a deployment with one
 * world needs no separate process -- and a second world started without the
 * variable finds the port taken and refuses to start, rather than lock
 * characters in a table of its own.
 */
#include "mem/torirs_mem.h"
#include "player_proto.h"
#include "player_server.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
on_signal(int sig)
{
    (void)sig;
    HttpServer_RequestStop();
}

int
main(
    int argc,
    char** argv)
{
    int port = PLAYER_SERVER_PORT;
    int status;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
            port = atoi(argv[++i]);
        else
        {
            fprintf(stderr, "usage: %s [--port %d]   (TORIRSSERVER_SAVES names the saves)\n",
                    argv[0], PLAYER_SERVER_PORT);
            return 2;
        }
    }

    ToriMem_Init();
    if( !PlayerServer_Open() )
    {
        ToriMem_Shutdown();
        return 1;
    }
    fprintf(stderr, "player_server: player accounts and leases, on 127.0.0.1:%d\n", port);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    status = HttpServer_RunOn(1, port, PlayerServer_Handle, NULL);
    PlayerServer_Close();
    ToriMem_Shutdown();
    return status;
}
