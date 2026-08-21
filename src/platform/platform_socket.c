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
        else if( header.type == TORIRS_NET_OUT_DISCONNECT )
        {
            /* Anything still queued belongs to the session being abandoned. */
            sock->out_pending_len = 0;
            if( sock->stream )
            {
                sockstream_close(sock->stream);
                sock->stream = NULL;
            }
            sock->connecting = 0;
            emit_status(sock, bus, TORIRS_NET_STATUS_DISCONNECTED);
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

    /*
     * 3. Read available bytes -> NET_RECV (chunked to the command cap).
     *
     * Bounded by what the ring can take, not by what the socket holds. The
     * loop used to read until would-block and push regardless, and CmdBus_Push
     * refuses silently when the ring is full — so a backlog larger than the
     * ring (a browser tab that was hidden for minutes, a resumed laptop) was
     * read out of the socket and then dropped on the floor mid-packet. The
     * framer above sees a stream with a hole in it and decodes noise from
     * there on, which presents as a client that "went weird after being
     * backgrounded" rather than as anything network-shaped.
     *
     * Stopping instead leaves the remainder in the socket for the next poll,
     * which is the only back-pressure available with a fixed ring.
     */
    for( ;; )
    {
        int n;

        if( !CmdBus_CanPush(bus, TORIRS_CMD_MAX_PAYLOAD) )
            break;
        n = sockstream_recv(sock->stream, payload, TORIRS_CMD_MAX_PAYLOAD);
        if( n > 0 )
        {
            CmdBus_Push(bus, TORIRS_CMD_NET_RECV, payload, (uint16_t)n);
            /* A short read is NOT "the socket is drained" — keep reading
             * until would-block says so. Emscripten's socket shim returns at
             * most one WebSocket message per recv(), so every read of the
             * server's small game packets is short; breaking here fed the
             * client ONE message per frame while a server tick's burst is a
             * dozen, and SERVER_TICK_END — the fence the UI-transaction
             * latch waits for — arrived last. Measured: the whole burst in
             * the browser inside 1ms, the client popping it over ~90ms of
             * withheld frames, a world freeze every server cycle (worst in
             * combat, where the burst is longest). On native the extra
             * recv() costs one EWOULDBLOCK syscall per poll. */
        }
        else if( n == 0 || n == SOCKSTREAM_ERROR_CLOSED )
        {
            /*
             * Peer closed the connection.
             *
             * SOCKSTREAM_ERROR_CLOSED as well as 0: sockstream_recv reports a
             * graceful close and a fatal error with that code and never
             * returns 0 at all, so a transport testing only for 0 treats
             * every dead socket as a quiet one and polls it forever. That is
             * what made a killed server invisible to this client until a
             * fifteen-second silence timer noticed the absence of packets.
             */
            sockstream_close(sock->stream);
            sock->stream = NULL;
            emit_status(sock, bus, TORIRS_NET_STATUS_DISCONNECTED);
            break;
        }
        else
        {
            break; /* would-block; try next poll */
        }
    }
}
