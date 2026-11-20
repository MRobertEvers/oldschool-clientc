/*
 * website_server: the website -- the public front door.
 *
 *   TORIRSSERVER_SAVES=saves website_server --gate LANDING [--port 8088]
 *       [--io HOST:PORT | --host io [IO flags]] [--host hiscores] [--host account]
 *
 * What it answers itself: the sign-in gate and the USER accounts behind it
 * (web logins, invites, the links to player accounts: website_gate.h,
 * website_users_db.h), OAuth for the game's launcher, the account site and its
 * staff pages, the player editor (website_site.h). Everything else a
 * signed-in visitor asks for -- the client page, its assets, the cache, the
 * black box -- is the IO server's (ioserver/io_server.h), and goes to it:
 *
 *   --io HOST:PORT   (or TORIRS_IO_SERVER) an io_server --behind-website, each
 *                    request signed (service/service_auth.h); the default is
 *                    127.0.0.1:IO_SERVER_BEHIND_PORT
 *   --host io        the IO server in this process, called directly; the IO
 *                    server's own flags then apply here too
 *
 * The servers it asks: the account server (player accounts, links, saves:
 * TORIRS_ACCOUNT_SERVER) and the hiscores server (the site's pages:
 * TORIRS_HISCORES_SERVER), each over signed HTTP, or in this process with
 * `--host account` / `--host hiscores`. Its own user accounts are answered to
 * the processes outside it under /svc/users (website_users.h), signed.
 *
 * Without --gate there is no sign-in: every request goes to the IO server, as
 * io_server alone serves a local web client.
 */
#include "accountserver/account_client.h"
#include "accountserver/account_proto.h"
#include "accountserver/account_server.h"
#include "hiscoresserver/hiscores_client.h"
#include "hiscoresserver/hiscores_db.h"
#include "hiscoresserver/hiscores_server.h"
#include "ioserver/io_server.h"
#include "mem/torirs_mem.h"
#include "mem/torirs_mem_libs.h"
#include "service/service_auth.h"
#include "service/service_host.h"
#include "service/service_link.h"
#include "torirsserver/torirs_server_oauth.h"
#include "website_gate.h"
#include "website_users.h"
#include "website_users_db.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct WebsiteGate g_gate;
static int g_gate_on;
static struct ServiceLink g_io_link = {
    "io", "TORIRS_IO_SERVER", IO_SERVER_BEHIND_PORT, "", NULL, NULL,
};

static void
on_signal(int sig)
{
    (void)sig;
    HttpServer_RequestStop();
}

/* The public site: the gate's pages, else the IO server's. */
static void
website_handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    (void)user;
    if( g_gate_on && WebsiteGate_Handle(&g_gate, req, res) )
    {
        /* A HEAD is a GET whose body is dropped. */
        if( strcmp(req->method, "HEAD") == 0 )
        {
            if( res->owns_body )
                ToriMem_Free(ToriMem_Net(), res->body);
            res->body = NULL;
            res->body_len = 0;
            res->owns_body = 0;
        }
        return;
    }
    ServiceLink_Forward(&g_io_link, req, res);
}

static void
usage(char const* argv0)
{
    fprintf(stderr,
            "usage: %s --gate LANDING [--port %d] [--io HOST:PORT | --host io [IO flags]]\n"
            "          [--host hiscores] [--host account]\n"
            "  --gate     serve only the sign-in pages until the visitor signs in with a\n"
            "             web login, then send them to LANDING, e.g.\n"
            "             '/?args=--manifest,manifests/manifest_latest.ini&ws=ws'.\n"
            "             Run torirsserver with --gate too: it checks the same ticket on\n"
            "             the game socket\n"
            "  --io       the IO server to forward to (default TORIRS_IO_SERVER, else\n"
            "             127.0.0.1:%d)\n"
            "  --host     run that server inside this process: io, hiscores, account\n\n",
            argv0, WEBSITE_SERVER_PORT, IO_SERVER_BEHIND_PORT);
    IoServer_Usage(stderr);
}

int
main(
    int argc,
    char** argv)
{
    static const struct ToriRSServerOAuthAccounts accounts = {
        AccountClient_Auth,
        AccountClient_ManagedBy,
        AccountClient_LinkVerify,
    };
    char const* landing = NULL;
    int port = WEBSITE_SERVER_PORT;
    int host_io = 0;
    int host_hiscores = 0;
    int host_account = 0;
    int status;

    ToriMem_Init();
    ToriMem_InstallRSCache();
    ToriMem_InstallBZip();

    for( int i = 1; i < argc; i++ )
    {
        int taken;

        if( strcmp(argv[i], "--gate") == 0 && i + 1 < argc )
            landing = argv[++i];
        else if( strcmp(argv[i], "--port") == 0 && i + 1 < argc )
            port = atoi(argv[++i]);
        else if( strcmp(argv[i], "--io") == 0 && i + 1 < argc )
            setenv("TORIRS_IO_SERVER", argv[++i], 1);
        else if( strcmp(argv[i], "--host") == 0 && i + 1 < argc )
        {
            char const* name = argv[++i];

            if( strcmp(name, "io") == 0 )
                host_io = 1;
            else if( strcmp(name, "hiscores") == 0 )
                host_hiscores = 1;
            else if( strcmp(name, "account") == 0 )
                host_account = 1;
            else
            {
                fprintf(stderr, "website_server: no server called '%s' to host\n", name);
                return 2;
            }
        }
        else if( (taken = IoServer_ParseArg(argc, argv, &i)) != 0 )
        {
            if( taken < 0 )
                return 1;
            if( !host_io )
                fprintf(stderr, "website_server: %s is the IO server's; it applies with "
                                "--host io\n",
                        argv[i]);
        }
        else
        {
            usage(argv[0]);
            return 2;
        }
    }

    ServiceAuth_Load();
    /* Its handlers run on the server's worker, which must not block on a
     * socket: every request to another server goes through the loop. */
    ServiceLink_SetExchange(HttpServer_ExchangeEx);
    ToriRSServer_OAuthUseAccounts(&accounts);

    if( host_io )
    {
        if( !IoServer_Start(port) )
            return 1;
        ServiceLink_SetLocal(&g_io_link, IoServer_Handle, NULL);
    }
    if( host_hiscores )
    {
        HiscoresDb_Open();
        ServiceHost_Add("/svc/hiscores", HiscoresServer_Handle, NULL);
        HiscoresClient_SetLocal(HiscoresServer_Handle);
    }
    if( host_account )
    {
        if( !AccountServer_Open() )
            return 1;
        ServiceHost_Add("/svc/account", AccountServer_Handle, NULL);
        AccountClient_SetLocal(AccountServer_Handle);
    }
    if( landing )
    {
        WebsiteUsersDb_Open();
        WebsiteGate_Init(&g_gate, landing);
        g_gate_on = 1;
    }
    ServiceHost_Add("/svc/users", WebsiteUsers_Handle, NULL);
    ServiceHost_SetFallback(website_handle, NULL, 0);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifdef SIGBREAK
    /* Windows: torirsd's stop (CTRL_BREAK to the server's process group). */
    signal(SIGBREAK, on_signal);
#endif
    if( host_io )
        printf("website_server: http://localhost:%d/  %s; the IO server in this process\n", port,
               g_gate_on ? "behind the sign-in gate" : "WITHOUT a gate");
    else
    {
        char io_host[128];
        int io_port;

        ServiceLink_Address(&g_io_link, io_host, sizeof(io_host), &io_port);
        printf("website_server: http://localhost:%d/  %s; the IO server at %s:%d (signed)\n", port,
               g_gate_on ? "behind the sign-in gate" : "WITHOUT a gate", io_host, io_port);
    }
    fflush(stdout);
    /* The public port: browsers come here, through the proxy in front. */
    status = ServiceHost_Run(0, port);
    if( host_io )
        IoServer_Stop();
    if( host_account )
        AccountServer_Close();
    if( host_hiscores )
        HiscoresDb_Close();
    if( g_gate_on )
    {
        WebsiteGate_Close(&g_gate);
        WebsiteUsersDb_Close();
    }
    ToriMem_Shutdown();
    return status;
}
