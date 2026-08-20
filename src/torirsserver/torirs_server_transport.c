/*
 * Transport implementations: a socket, and a pair of in-process byte queues.
 *
 * See mock230_transport.h for the contract. Everything here is deliberately
 * dull — the interesting consequence of the seam lives in mock230_session.c,
 * which had to stop blocking to use it.
 */

#include "mock230_transport.h"
#include <assert.h>

#include "mock230_ws.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Byte FIFO                                                           */
/* ------------------------------------------------------------------ */

int
mock230_pipe_available(const struct Mock230Pipe* pipe)
{
    return pipe->tail - pipe->head;
}

void
mock230_pipe_close(struct Mock230Pipe* pipe)
{
    pipe->closed = 1;
}

void
mock230_pipe_free(struct Mock230Pipe* pipe)
{
    free(pipe->data);
    pipe->data = NULL;
    pipe->cap = 0;
    pipe->head = 0;
    pipe->tail = 0;
}

int
mock230_pipe_write(
    struct Mock230Pipe* pipe,
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
mock230_pipe_read(
    struct Mock230Pipe* pipe,
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
    return mock230_conn_recv((struct Mock230Conn*)ctx, dst, max);
}

static int
socket_send(void* ctx, const uint8_t* src, int len)
{
    return mock230_conn_send((struct Mock230Conn*)ctx, src, len);
}

static int
socket_pollfd(void* ctx)
{
    return ((struct Mock230Conn*)ctx)->fd;
}

static void
socket_close(void* ctx)
{
    ((struct Mock230Conn*)ctx)->closed = 1;
}

void
mock230_transport_socket(
    struct Mock230Transport* transport,
    struct Mock230Conn* conn)
{
    transport->ctx = conn;
    transport->recv = socket_recv;
    transport->send = socket_send;
    transport->pollfd = socket_pollfd;
    transport->close = socket_close;
}

/* ------------------------------------------------------------------ */
/* Memory                                                              */
/* ------------------------------------------------------------------ */

static int
memory_recv(void* ctx, uint8_t* dst, int max)
{
    return mock230_pipe_read(((struct Mock230MemoryEnds*)ctx)->to_server, dst, max);
}

static int
memory_send(void* ctx, const uint8_t* src, int len)
{
    return mock230_pipe_write(((struct Mock230MemoryEnds*)ctx)->to_client, src, len);
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
    struct Mock230MemoryEnds* ends = (struct Mock230MemoryEnds*)ctx;

    /* Close, do not free: the host still has to drain whatever the server said
     * on its way out — a logout message, or the tail of the last tick. */
    mock230_pipe_close(ends->to_client);
    mock230_pipe_close(ends->to_server);
}

void
mock230_transport_memory(
    struct Mock230Transport* transport,
    struct Mock230MemoryEnds* ends,
    struct Mock230Pipe* to_server,
    struct Mock230Pipe* to_client)
{
    ends->to_server = to_server;
    ends->to_client = to_client;

    transport->ctx = ends;
    transport->recv = memory_recv;
    transport->send = memory_send;
    transport->pollfd = memory_pollfd;
    transport->close = memory_close;
}
