/*
 * hiscores_server: the hiscores server as its own process. See
 * hiscores_server.h.
 *
 *   TORIRSSERVER_SAVES=saves hiscores_server [--port 8093] [--bind-all]
 *
 * Its routes are under /svc/hiscores and every request is signed by one of
 * the servers (service/service_auth.h), so `--bind-all` -- worlds on other
 * machines -- is as closed as 127.0.0.1. The database is
 * `<saves>/hiscores.sqlite`; the first start copies the records out of
 * `<saves>/accounts.sqlite` (hiscores_db.h).
 */
#include "hiscores_db.h"
#include "hiscores_server.h"
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
    int port = HISCORES_SERVER_PORT;
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
                    argv[0], HISCORES_SERVER_PORT);
            return 2;
        }
    }

    ToriMem_Init();
    ServiceAuth_Load();
    /* Boot opens the database, so a file that cannot be opened -- or the
     * one-time import -- shows here, not at the first request. */
    HiscoresDb_Open();
    ServiceHost_Add("/svc/hiscores", HiscoresServer_Handle, NULL);
    fprintf(stderr, "hiscores_server: %s, /svc/hiscores on %s:%d\n", HiscoresDb_Path(),
            loopback_only ? "127.0.0.1" : "0.0.0.0", port);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGBREAK
    /* Windows: torirsd's stop (CTRL_BREAK to the server's process group). */
    signal(SIGBREAK, on_signal);
#endif
    status = ServiceHost_Run(loopback_only, port);
    HiscoresDb_Close();
    ToriMem_Shutdown();
    return status;
}
