#ifndef SRC_NET_MOCK_MOCK230_TRANSPORT_H
#define SRC_NET_MOCK_MOCK230_TRANSPORT_H

/*
 * The byte-stream seam: where the server's bytes come from and go to, with no
 * assumption that either end is a socket.
 *
 * There are two implementations and the server cannot tell them apart:
 *
 *   - **socket** wraps `struct Mock230Conn`, which is itself already two things
 *     (raw TCP and RFC 6455) sniffed per client. That layering stays.
 *   - **memory** is a pair of byte FIFOs, for a server hosted *inside* the
 *     client's process. Nothing is framed, nothing blocks and no fd exists.
 *
 * The reason this is a vtable rather than an `if( embedded )` is the second
 * implementation's real constraint: in-process, client and server run on one
 * thread, so any blocking read in the server is a deadlock rather than a stall.
 * Making the transport non-blocking is what forced the login handshake to
 * become a state machine (mock230_session.c) — which is the change that
 * actually buys the embedding, the vtable just names the seam.
 *
 * Contract, and all three implementations must honour it exactly:
 *
 *   recv   > 0  bytes taken
 *          = 0  nothing available *yet* — not an error, and specifically not
 *               end-of-stream, because a WebSocket read can deliver less than
 *               one whole frame
 *          < 0  the peer is gone
 *   send   len  on success (buffering internally if it must), -1 once dead
 *   pollfd      a descriptor to select() on, or -1 when there is nothing to
 *               wait on — an embedded host polls on its own schedule
 */

#include <stdint.h>

struct Mock230Conn;

struct Mock230Transport
{
    void* ctx;
    int (*recv)(void* ctx, uint8_t* dst, int max);
    int (*send)(void* ctx, const uint8_t* src, int len);
    int (*pollfd)(void* ctx);
    void (*close)(void* ctx);
};

/* ------------------------------------------------------------------ */
/* Byte FIFO                                                           */
/* ------------------------------------------------------------------ */

/*
 * A growable one-way byte queue.
 *
 * Contiguous with a read offset rather than a ring: an embedded session moves
 * whole login bursts through this (a REBUILD_NORMAL plus every container is
 * tens of kilobytes in one tick), and a fixed ring would have to either block
 * — impossible on one thread — or drop, which corrupts the stream silently.
 * Growing and compacting is the only behaviour with no failure mode.
 */
struct Mock230Pipe
{
    uint8_t* data;
    int cap;
    /** Read offset into `data`; bytes before it are consumed. */
    int head;
    /** Write offset. Live bytes are [head, tail). */
    int tail;
    int closed;
};

/** Append. Returns `len`, or -1 once closed. Grows as needed; only an
 *  allocation failure can short-write, and that returns -1 too. */
int
mock230_pipe_write(
    struct Mock230Pipe* pipe,
    const uint8_t* src,
    int len);

/** Take up to `max`. Returns the count, 0 when empty, or -1 when the writer
 *  closed *and* nothing is left — so a reader drains before it sees the end. */
int
mock230_pipe_read(
    struct Mock230Pipe* pipe,
    uint8_t* dst,
    int max);

/** Live bytes. */
int
mock230_pipe_available(const struct Mock230Pipe* pipe);

void
mock230_pipe_close(struct Mock230Pipe* pipe);

void
mock230_pipe_free(struct Mock230Pipe* pipe);

/* ------------------------------------------------------------------ */
/* Implementations                                                     */
/* ------------------------------------------------------------------ */

/** Over an accepted (and already sniffed) connection. */
void
mock230_transport_socket(
    struct Mock230Transport* transport,
    struct Mock230Conn* conn);

/**
 * The two ends of an in-process connection.
 *
 * Owned by the caller rather than by the transport, which is what keeps a
 * second embedded session possible: a `static` here would have made "one at a
 * time" a property of the transport layer instead of a property of whoever is
 * sharing the process-wide config tables.
 */
struct Mock230MemoryEnds
{
    struct Mock230Pipe* to_server;
    struct Mock230Pipe* to_client;
};

/**
 * Over a FIFO pair, from the *server's* point of view: it reads `to_server` and
 * writes `to_client`. The host owns both and does the mirror image.
 *
 * `ends` must outlive the transport. Neither pipe is owned by the transport —
 * closing it closes them but does not free them, because the host still has to
 * drain whatever the server said on its way out.
 */
void
mock230_transport_memory(
    struct Mock230Transport* transport,
    struct Mock230MemoryEnds* ends,
    struct Mock230Pipe* to_server,
    struct Mock230Pipe* to_client);

#endif
