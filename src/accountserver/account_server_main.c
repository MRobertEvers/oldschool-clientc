/*
 * account_server: the account server -- the login server -- as its own
 * process. See account_server.h.
 *
 *   TORIRSSERVER_SAVES=saves account_server [--port 8091] [--bind-all]
 *
 * Every world names it: TORIRS_ACCOUNT_SERVER=127.0.0.1:8091. Its routes are
 * under /svc/account and every request is signed by one of the servers
 * (service/service_auth.h), so `--bind-all` -- worlds on other machines -- is
 * as closed as 127.0.0.1. The saves directory is locked while it runs: a
 * second account server on the same saves refuses to start rather than keep
 * a table of its own.
 */
#include "account_proto.h"
#include "account_server.h"
#include "mem/torirs_mem.h"
#include "service/service_auth.h"
#include "service/service_host.h"

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
    int port = ACCOUNT_SERVER_PORT;
    int loopback_only = 1;
    int status;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
            port = atoi(argv[++i]);
        else if( strcmp(argv[i], "--bind-all") == 0 )
            loopback_only = 0;
        else
        {
            fprintf(stderr,
                    "usage: %s [--port %d] [--bind-all]   (TORIRSSERVER_SAVES names the saves)\n",
                    argv[0], ACCOUNT_SERVER_PORT);
            return 2;
        }
    }

    ToriMem_Init();
    ServiceAuth_Load();
    if( !AccountServer_Open() )
    {
        ToriMem_Shutdown();
        return 1;
    }
    ServiceHost_Add("/svc/account", AccountServer_Handle, NULL);
    fprintf(stderr, "account_server: player accounts, saves and the lock, /svc/account on %s:%d\n",
            loopback_only ? "127.0.0.1" : "0.0.0.0", port);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGBREAK
    /* Windows: torirsd's stop (CTRL_BREAK to the server's process group). */
    signal(SIGBREAK, on_signal);
#endif
    status = ServiceHost_Run(loopback_only, port);
    AccountServer_Close();
    ToriMem_Shutdown();
    return status;
}
