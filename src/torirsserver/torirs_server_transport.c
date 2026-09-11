/*
 * Transport implementations: a socket, and a pair of in-process byte queues.
 *
 * See torirs_server_transport.h for the contract. Everything here is deliberately
 * dull — the interesting consequence of the seam lives in torirs_server_session.c,
 * which had to stop blocking to use it.
 */

#include "torirs_server_transport.h"
#include <assert.h>

#include "torirs_server_ws.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Byte FIFO                                                           */
/* ------------------------------------------------------------------ */

int
ToriRSServer_PipeAvailable(const struct ToriRSServerPipe* pipe)
{
    return pipe->tail - pipe->head;
}

const uint8_t*
ToriRSServer_PipePeek(
    const struct ToriRSServerPipe* pipe,
    int* len)
{
    int live = pipe->tail - pipe->head;

    assert(len);
    *len = live;
    return live > 0 ? pipe->data + pipe->head : NULL;
}

void
ToriRSServer_PipeDrop(
    struct ToriRSServerPipe* pipe,
    int len)
{
    assert(len >= 0);
    assert(len <= pipe->tail - pipe->head);
    pipe->head += len;
    if( pipe->head == pipe->tail )
    {
        pipe->head = 0;
        pipe->tail = 0;
    }
}

void
ToriRSServer_PipeClose(struct ToriRSServerPipe* pipe)
{
    pipe->closed = 1;
}

void
ToriRSServer_PipeFree(struct ToriRSServerPipe* pipe)
{
    free(pipe->data);
    pipe->data = NULL;
    pipe->cap = 0;
    pipe->head = 0;
    pipe->tail = 0;
}

int
ToriRSServer_PipeWrite(
    struct ToriRSServerPipe* pipe,
    const uint8_t* src,
    int len)
{
    int live;

    if( pipe->closed )
        return -1;
    if( len <= 0 )
        return 0;

    /* Compact before growing: a long session writes and drains continuously, so
     * `head` marches forward and the tail hits `cap` while most of the buffer
     * is dead space. Without this the pipe grows without bound. */
    live = pipe->tail - pipe->head;
    if( pipe->head > 0 && pipe->tail + len > pipe->cap )
    {
        memmove(pipe->data, pipe->data + pipe->head, (size_t)live);
        pipe->head = 0;
        pipe->tail = live;
    }

    if( pipe->tail + len > pipe->cap )
    {
        int want = pipe->cap ? pipe->cap : 8192;
        uint8_t* grown;

        while( want < pipe->tail + len )
            want *= 2;
        grown = (uint8_t*)realloc(pipe->data, (size_t)want);
        assert(grown);
        pipe->data = grown;
        pipe->cap = want;
    }

    memcpy(pipe->data + pipe->tail, src, (size_t)len);
    pipe->tail += len;
    return len;
}

int
ToriRSServer_PipeRead(
    struct ToriRSServerPipe* pipe,
    uint8_t* dst,
    int max)
{
    int live = pipe->tail - pipe->head;

    if( live <= 0 )
    {
        /* Closed only reports as closed once drained, so a reader never loses
         * bytes the writer sent before hanging up. */
        return pipe->closed ? -1 : 0;
    }
    if( max <= 0 )
        return 0;
    if( live > max )
        live = max;
    memcpy(dst, pipe->data + pipe->head, (size_t)live);
    pipe->head += live;
    if( pipe->head == pipe->tail )
    {
        pipe->head = 0;
        pipe->tail = 0;
    }
    return live;
}

/* ------------------------------------------------------------------ */
/* Socket                                                              */
/* ------------------------------------------------------------------ */

static int
socket_recv(void* ctx, uint8_t* dst, int max)
{
    return ToriRSServer_ConnRecv((struct ToriRSServerConn*)ctx, dst, max);
}

static int
socket_send(void* ctx, const uint8_t* src, int len)
{
    return ToriRSServer_ConnSend((struct ToriRSServerConn*)ctx, src, len);
}

static int
socket_pollfd(void* ctx)
{
    return ((struct ToriRSServerConn*)ctx)->fd;
}

static void
socket_close(void* ctx)
{
    ((struct ToriRSServerConn*)ctx)->closed = 1;
}

static int
socket_buffered(void* ctx)
{
    return ToriRSServer_ConnBuffered((struct ToriRSServerConn*)ctx);
}

static int
socket_pending(void* ctx)
{
    return ToriRSServer_ConnPending((struct ToriRSServerConn*)ctx);
}

void
ToriRSServer_TransportSocket(
    struct ToriRSServerTransport* transport,
    struct ToriRSServerConn* conn)
{
    transport->ctx = conn;
    transport->recv = socket_recv;
    transport->send = socket_send;
    transport->pollfd = socket_pollfd;
    transport->buffered = socket_buffered;
    transport->pending = socket_pending;
    transport->close = socket_close;
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

static int
memory_recv(void* ctx, uint8_t* dst, int max)
{
    return ToriRSServer_PipeRead(((struct ToriRSServerMemoryEnds*)ctx)->to_server, dst, max);
}

static int
memory_send(void* ctx, const uint8_t* src, int len)
{
    return ToriRSServer_PipeWrite(((struct ToriRSServerMemoryEnds*)ctx)->to_client, src, len);
}

static int
memory_pollfd(void* ctx)
{
    (void)ctx;
    /* Nothing to select() on. An embedded host drives the pump itself. */
    return -1;
}

static void
memory_close(void* ctx)
{
    struct ToriRSServerMemoryEnds* ends = (struct ToriRSServerMemoryEnds*)ctx;

    /* Close, do not free: the host still has to drain whatever the server said
     * on its way out — a logout message, or the tail of the last tick. */
    ToriRSServer_PipeClose(ends->to_client);
    ToriRSServer_PipeClose(ends->to_server);
}

static int
memory_buffered(void* ctx)
{
    return ToriRSServer_PipeAvailable(((struct ToriRSServerMemoryEnds*)ctx)->to_server);
}

/*
 * An embedded host drains `to_client` on its own schedule and cannot be
 * outrun by anything that would help: the queue grows, the host takes it, and
 * there is no kernel in between to refuse. Reporting the depth anyway is what
 * keeps the JS5 service's backpressure honest in-process too -- a host that
 * pumps once per frame while a cache download runs is exactly the case where
 * an unbounded answer stream would show up as memory rather than as latency.
 */
static int
memory_pending(void* ctx)
{
    return ToriRSServer_PipeAvailable(((struct ToriRSServerMemoryEnds*)ctx)->to_client);
}

void
ToriRSServer_TransportMemory(
    struct ToriRSServerTransport* transport,
    struct ToriRSServerMemoryEnds* ends,
    struct ToriRSServerPipe* to_server,
    struct ToriRSServerPipe* to_client)
{
    ends->to_server = to_server;
    ends->to_client = to_client;

    transport->ctx = ends;
    transport->recv = memory_recv;
    transport->send = memory_send;
    transport->pollfd = memory_pollfd;
    transport->buffered = memory_buffered;
    transport->pending = memory_pending;
    transport->close = memory_close;
}
