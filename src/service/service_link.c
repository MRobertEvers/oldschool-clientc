/*
 * A client's link to a server. See service_link.h.
 */
#include "service_link.h"

#include "mem/torirs_mem.h"
#include "platform/platform_x_http.h"
#include "service_auth.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A server on this machine or the next: a page or a login waits this long. */
#define SERVICE_TIMEOUT_MS 8000
/* A cache batch through the website's forward can be large and slow. */
#define SERVICE_FORWARD_TIMEOUT_MS 30000

static ServiceExchange g_exchange;

void
ServiceLink_SetLocal(
    struct ServiceLink* link,
    HttpHandler handler,
    void* user)
{
    assert(link);
    link->local = handler;
    link->local_user = user;
}

int
ServiceLink_Named(struct ServiceLink const* link)
{
    char const* env;

    assert(link);
    env = getenv(link->env);
    return env && env[0];
}

void
ServiceLink_Address(
    struct ServiceLink const* link,
    char* host,
    size_t cap,
    int* port)
{
    char const* env;
    char const* colon;

    assert(link);
    assert(host);
    assert(port);
    env = getenv(link->env);
    colon = env ? strrchr(env, ':') : NULL;
    if( env && colon && colon > env )
    {
        snprintf(host, cap, "%.*s", (int)(colon - env), env);
        *port = atoi(colon + 1);
        return;
    }
    snprintf(host, cap, "127.0.0.1");
    *port = link->default_port;
}

void
ServiceLink_SetExchange(ServiceExchange exchange)
{
    g_exchange = exchange;
}

/* The handler, called as its ServiceHost would call it. */
static char*
ask_local(
    struct ServiceLink* link,
    char const* method,
    char const* route,
    char const* body,
    int body_len,
    int* out_status,
    int* out_size)
{
    struct HttpRequest req;
    struct HttpResponse res;
    char* copy;

    memset(&req, 0, sizeof(req));
    memset(&res, 0, sizeof(res));
    snprintf(req.method, sizeof(req.method), "%s", method);
    snprintf(req.path, sizeof(req.path), "%s", route);
    req.body = (uint8_t const*)body;
    req.body_len = body_len;
    req.trusted = 1;
    link->local(link->local_user, &req, &res);
    *out_status = res.status ? res.status : 404;
    *out_size = res.body_len;
    copy = ToriMem_Alloc(ToriMem_Net(), (size_t)res.body_len + 1);
    assert(copy);
    if( res.body_len > 0 )
        memcpy(copy, res.body, (size_t)res.body_len);
    copy[res.body_len] = '\0';
    if( res.owns_body )
        ToriMem_Free(ToriMem_Net(), res.body);
    return copy;
}

char*
ServiceLink_Ask(
    struct ServiceLink* link,
    char const* method,
    char const* route,
    char const* body,
    int body_len,
    int* out_status,
    int* out_size)
{
    char host[128];
    char path[HTTP_MAX_PATH];
    char sign[SERVICE_AUTH_LINE_MAX];
    char const* type = body ? "application/x-www-form-urlencoded" : NULL;
    int port;
    int size = 0;
    int status = 0;
    char* raw;
    char* text;

    assert(link);
    assert(method);
    assert(route);
    assert(body || body_len == 0);
    assert(out_status);
    assert(out_size);
    *out_status = 0;
    *out_size = 0;
    if( link->local )
        return ask_local(link, method, route, body, body_len, out_status, out_size);

    ServiceLink_Address(link, host, sizeof(host), &port);
    snprintf(path, sizeof(path), "%s%s", link->prefix, route);
    ServiceAuth_HeaderLine(method, path, body, body_len, sign);
    if( g_exchange )
    {
        struct HttpExchangeAnswer answer;

        g_exchange(host, port, method, path, sign, type, body, body_len, SERVICE_TIMEOUT_MS,
                   &answer);
        raw = answer.body;
        size = answer.body_len;
        status = answer.status;
    }
    else
        raw = PlatformX_HttpRequest(host, port, method, path, sign, type, body, body_len, &size,
                                    &status);
    *out_status = status;
    if( !raw && status == 0 )
    {
        fprintf(stderr, "service: the %s server at %s:%d did not answer %s (%s)\n", link->name,
                host, port, route, link->env);
        return NULL;
    }
    text = ToriMem_Alloc(ToriMem_Net(), (size_t)size + 1);
    assert(text);
    if( raw && size > 0 )
        memcpy(text, raw, (size_t)size);
    text[size] = '\0';
    ToriMem_Free(ToriMem_Net(), raw);
    *out_size = size;
    return text;
}

void
ServiceLink_Forward(
    struct ServiceLink* link,
    struct HttpRequest const* req,
    struct HttpResponse* res)
{
    char host[128];
    char path[HTTP_MAX_PATH];
    char headers[SERVICE_AUTH_LINE_MAX + 128];
    char const* match;
    int match_len = 0;
    int port;
    struct HttpExchangeAnswer answer;
    int body_len;

    assert(link);
    assert(req);
    assert(res);
    if( link->local )
    {
        struct HttpRequest copy = *req;

        copy.trusted = 1;
        link->local(link->local_user, &copy, res);
        return;
    }
    /* A forward is made by the server's loop; a process forwarding has one. */
    assert(g_exchange);
    body_len = req->body ? req->body_len : 0;
    ServiceLink_Address(link, host, sizeof(host), &port);
    snprintf(path, sizeof(path), "%s%s", link->prefix, req->path);
    ServiceAuth_HeaderLine(req->method, path, req->body, body_len, headers);
    match = HttpRequest_Header(req, "If-None-Match", &match_len);
    if( match && match_len > 0 && match_len < 100 )
        snprintf(headers + strlen(headers), sizeof(headers) - strlen(headers),
                 "If-None-Match: %.*s\r\n", match_len, match);
    g_exchange(host, port, req->method, path, headers,
               body_len > 0 ? "application/octet-stream" : NULL, req->body, body_len,
               SERVICE_FORWARD_TIMEOUT_MS, &answer);
    if( answer.status == 0 )
    {
        fprintf(stderr, "service: the %s server at %s:%d did not answer %s %s\n", link->name,
                host, port, req->method, req->path);
        res->status = 502;
        return;
    }
    res->status = answer.status;
    res->body = answer.body;
    res->body_len = answer.body_len;
    res->owns_body = 1;
    snprintf(res->content_type, sizeof(res->content_type), "%s",
             answer.content_type[0] ? answer.content_type : "application/octet-stream");
    snprintf(res->etag, sizeof(res->etag), "%s", answer.etag);
    snprintf(res->location, sizeof(res->location), "%s", answer.location);
}
