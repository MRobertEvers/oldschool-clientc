/*
 * A process's listener for the servers it hosts. See service_host.h.
 */
#include "service_host.h"

#include "mem/torirs_mem.h"
#include "service_auth.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

static struct
{
    struct
    {
        char prefix[64];
        HttpHandler handler;
        void* user;
    } routes[SERVICE_HOST_MAX];
    int count;
    HttpHandler fallback;
    void* fallback_user;
    int fallback_signed;
} g_host;

void
ServiceHost_Add(
    char const* prefix,
    HttpHandler handler,
    void* user)
{
    assert(prefix);
    assert(prefix[0] == '/');
    assert(handler);
    assert(g_host.count < SERVICE_HOST_MAX);
    snprintf(g_host.routes[g_host.count].prefix, sizeof(g_host.routes[0].prefix), "%s", prefix);
    g_host.routes[g_host.count].handler = handler;
    g_host.routes[g_host.count].user = user;
    g_host.count++;
}

void
ServiceHost_SetFallback(
    HttpHandler handler,
    void* user,
    int signed_only)
{
    assert(handler);
    g_host.fallback = handler;
    g_host.fallback_user = user;
    g_host.fallback_signed = signed_only;
}

static void
refuse(
    struct HttpResponse* res,
    int status,
    char const* why)
{
    size_t len = strlen(why);
    char* body = ToriMem_Alloc(ToriMem_Net(), len + 8);

    assert(body);
    res->body_len = snprintf(body, len + 8, "error\t%s\n", why);
    res->status = status;
    snprintf(res->content_type, sizeof(res->content_type), "text/plain; charset=utf-8");
    res->body = body;
    res->owns_body = 1;
}

void
ServiceHost_Handle(
    void* user,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    (void)user;
    assert(req);
    assert(res);
    for( int i = 0; i < g_host.count; i++ )
    {
        size_t len = strlen(g_host.routes[i].prefix);
        struct HttpRequest inner;

        if( strncmp(req->path, g_host.routes[i].prefix, len) != 0 ||
            (req->path[len] != '/' && req->path[len] != '?') )
            continue;
        if( !ServiceAuth_Check(req) )
        {
            refuse(res, 401, "not signed by a server (service_auth.h)");
            return;
        }
        /* The handler's own routes: the prefix is how it was reached. */
        inner = *req;
        memmove(inner.path, req->path + len, strlen(req->path + len) + 1);
        g_host.routes[i].handler(g_host.routes[i].user, &inner, res);
        return;
    }
    if( !g_host.fallback )
    {
        refuse(res, 404, "no such route");
        return;
    }
    if( g_host.fallback_signed && !ServiceAuth_Check(req) )
    {
        refuse(res, 401, "not signed by a server (service_auth.h)");
        return;
    }
    g_host.fallback(g_host.fallback_user, req, res);
}

int
ServiceHost_Run(
    int loopback_only,
    int port)
{
    return HttpServer_RunOn(loopback_only, port, ServiceHost_Handle, NULL);
}

/* ------------------------------------------------------------------ */
/* On a thread                                                         */
/* ------------------------------------------------------------------ */

static struct
{
    int started;
    int port;
    int loopback_only;
    volatile int finished;
    volatile int status;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
} g_thread;

#ifdef _WIN32
static DWORD WINAPI
thread_main(LPVOID arg)
#else
static void*
thread_main(void* arg)
#endif
{
    (void)arg;
    g_thread.status = ServiceHost_Run(g_thread.loopback_only, g_thread.port);
    g_thread.finished = 1;
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

int
ServiceHost_StartThread(
    int loopback_only,
    int port)
{
    assert(!g_thread.started);
    assert(port > 0);
    g_thread.port = port;
    g_thread.loopback_only = loopback_only;
    g_thread.finished = 0;
#ifdef _WIN32
    g_thread.thread = CreateThread(NULL, 0, thread_main, NULL, 0, NULL);
    assert(g_thread.thread);
#else
    {
        int const created = pthread_create(&g_thread.thread, NULL, thread_main, NULL);

        assert(created == 0);
        (void)created;
    }
#endif
    g_thread.started = 1;
    /* Up, or out with its reason: a bind on a taken port fails at once. */
    for( int waited = 0; waited < 2000 && !HttpServer_Running() && !g_thread.finished;
         waited += 10 )
    {
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10 * 1000);
#endif
    }
    if( HttpServer_Running() )
        return 1;
    ServiceHost_StopThread();
    return 0;
}

void
ServiceHost_StopThread(void)
{
    if( !g_thread.started )
        return;
    HttpServer_RequestStop();
#ifdef _WIN32
    WaitForSingleObject(g_thread.thread, INFINITE);
    CloseHandle(g_thread.thread);
#else
    pthread_join(g_thread.thread, NULL);
#endif
    g_thread.started = 0;
}
