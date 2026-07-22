#include "platform_socket.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "sockstream.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct PlatformSocket
{
    struct SockStream* stream;
    int default_port;
    int connecting;
    int last_status;
    /* Outbound bytes queued while the non-blocking connect is still in
     * flight (ConnectLogin pushes CONNECT + the first login bytes in the
     * same drain) or while send() would block; flushed FIFO once writable. */
    uint8_t* out_pending;
    int out_pending_len;
    int out_pending_cap;
};

static void
out_pending_append(
    struct PlatformSocket* sock,
    uint8_t const* data,
    int len)
{
    if( len <= 0 )
        return;
    if( sock->out_pending_len + len > sock->out_pending_cap )
    {
        int cap = sock->out_pending_cap ? sock->out_pending_cap : 1024;
        while( cap < sock->out_pending_len + len )
            cap *= 2;
        sock->out_pending = realloc(sock->out_pending, cap);
        assert(sock->out_pending);
        sock->out_pending_cap = cap;
    }
    memcpy(sock->out_pending + sock->out_pending_len, data, len);
    sock->out_pending_len += len;
}

/* Send as much of the pending buffer as the socket accepts. */
static void
out_pending_flush(struct PlatformSocket* sock)
{
    while( sock->out_pending_len > 0 && sock->stream && !sock->connecting )
    {
        int sent = sockstream_send(sock->stream, sock->out_pending, sock->out_pending_len);
        if( sent <= 0 )
            break;
        if( sent < sock->out_pending_len )
            memmove(sock->out_pending, sock->out_pending + sent, sock->out_pending_len - sent);
        sock->out_pending_len -= sent;
    }
}

struct PlatformSocket*
PlatformSocket_New(int default_port)
{
    struct PlatformSocket* sock = calloc(1, sizeof(*sock));
    assert(sock);
    sock->default_port = default_port > 0 ? default_port : 43594;
    sock->last_status = TORIRS_NET_STATUS_DISCONNECTED;
    return sock;
}

void
PlatformSocket_Free(struct PlatformSocket* sock)
{
    if( !sock )
        return;
    if( sock->stream )
        sockstream_close(sock->stream);
    free(sock->out_pending);
    free(sock);
}

/* "host:port" -> host + port (default_port when no colon). */
static void
parse_host(
    struct PlatformSocket* sock,
    char const* spec,
    int spec_len,
    char* out_host,
    int host_cap,
    int* out_port)
{
    int colon = -1;
    int copy;
    for( int i = 0; i < spec_len; i++ )
    {
        if( spec[i] == ':' )
        {
            colon = i;
            break;
        }
    }
    copy = (colon >= 0 ? colon : spec_len);
    if( copy >= host_cap )
        copy = host_cap - 1;
    memcpy(out_host, spec, copy);
    out_host[copy] = '\0';
    *out_port = sock->default_port;
    if( colon >= 0 )
    {
        char portbuf[16] = { 0 };
        int plen = spec_len - colon - 1;
        if( plen > 0 && plen < (int)sizeof(portbuf) )
        {
            memcpy(portbuf, spec + colon + 1, plen);
            *out_port = atoi(portbuf);
        }
    }
}

static void
emit_status(
    struct PlatformSocket* sock,
    struct ToriRS_CmdBus* bus,
    int status)
{
    if( sock->last_status == status )
        return;
    sock->last_status = status;
    CmdBus_PushNetStatus(bus, status);
}

void
PlatformSocket_Poll(
    struct PlatformSocket* sock,
    struct ToriRS_Network* net,
    struct ToriRS_CmdBus* bus)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    assert(sock && net && bus);

    /* 1. Drain the subsystem's outbound ring. */
    while( ToriRS_Network_PopOut(net, &header, payload) )
    {
        if( header.type == TORIRS_NET_OUT_CONNECT )
        {
            char host[128];
            int port;
            parse_host(sock, (char const*)payload, header.length, host, sizeof(host), &port);
            if( sock->stream )
                sockstream_close(sock->stream);
            sock->out_pending_len = 0;
            sock->stream = sockstream_new();
            if( sock->stream )
            {
                sockstream_connect(sock->stream, host, port, 0);
                sock->connecting = 1;
                emit_status(sock, bus, TORIRS_NET_STATUS_CONNECTING);
            }
            else
            {
                emit_status(sock, bus, TORIRS_NET_STATUS_FAILED);
            }
        }
        else if( header.type == TORIRS_NET_OUT_SEND_DATA )
        {
            /* Queue-then-flush keeps wire order: bytes pushed while the
             * connect is in flight (the login hello) must not be dropped,
             * and later sends must not overtake buffered ones. */
            out_pending_append(sock, payload, header.length);
            out_pending_flush(sock);
        }
    }

    if( !sock->stream )
        return;

    /* 2. Pump the pending non-blocking connect. */
    if( sock->connecting )
    {
        int rc = sockstream_poll_connect(sock->stream);
        if( rc == SOCKSTREAM_CONNECT_SUCCESS )
        {
            sock->connecting = 0;
            emit_status(sock, bus, TORIRS_NET_STATUS_CONNECTED);
            out_pending_flush(sock);
        }
        else if( rc == SOCKSTREAM_CONNECT_FAILED )
        {
            sock->connecting = 0;
            sockstream_close(sock->stream);
            sock->stream = NULL;
            emit_status(sock, bus, TORIRS_NET_STATUS_FAILED);
            return;
        }
        else
        {
            return; /* still in-flight */
        }
    }

    /* Retry any bytes a previous poll could not push (would-block). */
    out_pending_flush(sock);

    /* 3. Read available bytes -> NET_RECV (chunked to the command cap). */
    for( ;; )
    {
        int n = sockstream_recv(sock->stream, payload, TORIRS_CMD_MAX_PAYLOAD);
        if( n > 0 )
        {
            CmdBus_Push(bus, TORIRS_CMD_NET_RECV, payload, (uint16_t)n);
            if( n < TORIRS_CMD_MAX_PAYLOAD )
                break;
        }
        else if( n == 0 )
        {
            /* Peer closed the connection. */
            sockstream_close(sock->stream);
            sock->stream = NULL;
            emit_status(sock, bus, TORIRS_NET_STATUS_DISCONNECTED);
            break;
        }
        else
        {
            break; /* -1: would-block or error; try next poll */
        }
    }
}
