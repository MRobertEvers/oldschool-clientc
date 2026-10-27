/*
 * account_server: the account database's process. See account_server.h.
 *
 *   TORIRSSERVER_SAVES=saves account_server [--port 8091]
 *
 * Listens on 127.0.0.1 only. The database is `<saves>/accounts.sqlite`; the
 * first start imports the files it replaced (account_db.h).
 */
#include "account_db.h"
#include "account_server.h"
#include "mem/torirs_mem.h"

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
    int status;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
            port = atoi(argv[++i]);
        else
        {
            fprintf(stderr, "usage: %s [--port %d]   (TORIRSSERVER_SAVES names the saves)\n",
                    argv[0], ACCOUNT_SERVER_PORT);
            return 2;
        }
    }

    ToriMem_Init();
    /* Boot opens the database, so a file that cannot be opened -- or the
     * one-time import -- shows here, not at the first request. */
    AccountDb_Open();
    fprintf(stderr, "account_server: %s, on 127.0.0.1:%d\n", AccountDb_Path(), port);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    status = HttpServer_RunOn(1, port, AccountServer_Handle, NULL);
    AccountDb_Close();
    ToriMem_Shutdown();
    return status;
}
