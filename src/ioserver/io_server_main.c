/*
 * io_server: the IO server as its own process. See io_server.h.
 *
 *   io_server [--port 8088] [IO flags]                a local web client's
 *   io_server --behind-website [--port 8089] [flags]  behind the website:
 *                                                     signed requests only
 */
#include "io_server.h"
#include "mem/torirs_mem.h"
#include "mem/torirs_mem_libs.h"
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

static void
usage(char const* argv0)
{
    fprintf(stderr,
            "usage: %s [--port N] [--behind-website] [--bind-all] [IO flags]\n"
            "  --port            listen port (default %d alone, %d behind the website)\n"
            "  --behind-website  answer only requests signed by the servers -- the\n"
            "                    website's forwards (service/service_auth.h)\n"
            "  --bind-all        listen on every address, not only 127.0.0.1 (behind\n"
            "                    the website only: alone it already listens on all)\n\n",
            argv0, IO_SERVER_DEFAULT_PORT, IO_SERVER_BEHIND_PORT);
    IoServer_Usage(stderr);
}

int
main(
    int argc,
    char** argv)
{
    int port = 0;
    int behind = 0;
    int bind_all = 0;
    int status;

    /* Before anything allocates: this binary links the ondemand and io_wire
     * code the client links, which hands bytes to rscache; it runs under the
     * same allocator regime (src/mem/torirs_mem_libs.h). */
    ToriMem_Init();
    ToriMem_InstallRSCache();
    ToriMem_InstallBZip();

    for( int i = 1; i < argc; i++ )
    {
        int taken;

        if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
        {
            port = atoi(argv[++i]);
            continue;
        }
        if( strcmp(argv[i], "--behind-website") == 0 )
        {
            behind = 1;
            continue;
        }
        if( strcmp(argv[i], "--bind-all") == 0 )
        {
            bind_all = 1;
            continue;
        }
        taken = IoServer_ParseArg(argc, argv, &i);
        if( taken < 0 )
            return 1;
        if( taken == 0 )
        {
            usage(argv[0]);
            return 1;
        }
    }
    if( port == 0 )
        port = behind ? IO_SERVER_BEHIND_PORT : IO_SERVER_DEFAULT_PORT;
    if( !IoServer_Start(port) )
        return 1;
    if( behind )
        ServiceAuth_Load();
    ServiceHost_SetFallback(IoServer_Handle, NULL, behind);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGBREAK
    /* Windows: torirsd's stop (CTRL_BREAK to the server's process group). */
    signal(SIGBREAK, on_signal);
#endif
    printf("io_server: http://localhost:%d/  %s\n"
           "io_server: http://localhost:%d/status  (status page)\n",
           port, behind ? "(behind the website: signed requests only)" : "", port);
    fflush(stdout);
    /* Alone it is a browser's server, as it always was; behind the website,
     * only the website reaches it, on this machine unless --bind-all. */
    status = ServiceHost_Run(behind && !bind_all, port);
    IoServer_Stop();
    ToriMem_Shutdown();
    return status;
}
