#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_WS_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_WS_H

/*
 * The mock server's connection layer: one byte stream, reached either over raw
 * TCP (the native client) or wrapped in RFC 6455 frames (the browser build,
 * whose sockets are WebSockets and cannot be anything else).
 *
 * Which one it is is decided by looking at the first byte the client sends and
 * never by configuration: the 230 login stream opens with opcode 14, an HTTP
 * upgrade opens with 'G'. So one listening port serves both, and nothing above
 * this file knows the difference — ToriRSServer_Conn_* carries application bytes
 * either way, and the framing (if any) is gone by the time a packet is decoded.
 */

#include "torirs_server_transport.h"

#include <stdint.h>

#define TORIRSSERVER_CONN_RAW_MAX 65536
#define TORIRSSERVER_CONN_APP_MAX 65536

/*
 * How much a connection may owe its peer before the host gives up on it.
 *
 * Output is queued rather than written straight through (see `out` below), so
 * a peer that has stopped reading no longer stalls the process -- it grows
 * this queue instead. Something still has to bound it, and a client that is
 * eight megabytes behind is not slow, it is gone: the JS5 service applies its
 * own backpressure an order of magnitude below this, so anything that reaches
 * here is a peer that stopped draining altogether.
 */
#define TORIRSSERVER_CONN_OUT_MAX (8 * 1024 * 1024)

/** What one step of the open sequence found. */
enum ToriRSServerConnOpen
{
    /** Not enough has arrived yet -- select() again and re-enter. */
    TORIRSSERVER_CONN_OPENING = 0,
    /** The stream carries application bytes from here on. */
    TORIRSSERVER_CONN_OPEN_READY = 1,
    /** The peer left, or said something that is not a connection. */
    TORIRSSERVER_CONN_OPEN_FAILED = -1,
};

struct ToriRSServerConn
{
    int fd;
    int ws;     /* 1 once the WebSocket handshake completed */
    int closed; /* peer hung up, or the stream went unrecoverable */

    /**
     * 1 while the open sequence is still running -- the first byte has not
     * arrived, or the upgrade request is incomplete. `raw` accumulates the
     * request headers meanwhile, which is safe because nothing framed can
     * arrive until the handshake this is waiting on has been answered.
     */
    int opening;
    /** 1 once the first byte said 'G': this is an HTTP upgrade, not raw TCP. */
    int upgrade;

    /* Bytes off the socket that are not yet whole frames (raw mode leaves this
     * empty — there is nothing to reassemble). */
    uint8_t raw[TORIRSSERVER_CONN_RAW_MAX];
    int raw_len;

    /* Application bytes, deframed, waiting to be taken. */
    uint8_t app[TORIRSSERVER_CONN_APP_MAX];
    int app_len;

    /**
     * Bytes owed to the peer that the kernel would not take yet.
     *
     * The socket is non-blocking, so a write can place part of a packet and
     * refuse the rest, and there is no thread to park while it drains: one
     * host thread carries every connection, so a blocking write is not this
     * client stalling, it is all of them. So a send always succeeds at this
     * layer, into here, and the host flushes whatever is left whenever
     * select() says the descriptor will take more.
     */
    struct ToriRSServerPipe out;
};

/*
 * Take over an accepted socket, without reading it.
 *
 * The descriptor is put into non-blocking mode here, which is the property the
 * rest of this file is written against: every read and every write below can
 * come back having done nothing, and none of them may park the caller. That is
 * a decision about this connection layer rather than about the host, so it
 * belongs here and not in whoever called accept().
 */
void
ToriRSServer_ConnBegin(
    struct ToriRSServerConn* conn,
    int fd);

/*
 * Advance the open sequence: sniff the first byte and, if it is an HTTP
 * upgrade, get as far through the WebSocket handshake as the bytes allow.
 *
 * Re-enter it whenever the descriptor is readable until it stops answering
 * TORIRSSERVER_CONN_OPENING. Nothing above may recv or send before it answers
 * TORIRSSERVER_CONN_OPEN_READY -- a WebSocket client's application bytes are
 * still frame payloads until the upgrade has been answered.
 *
 * This used to be `ToriRSServer_ConnOpen`, which blocked for the first byte and
 * then blocked again for each read of the header block. One connection to a
 * process could afford that. A host that holds every connection on one thread
 * cannot: a client that opens a socket and then says nothing would hold up the
 * world tick and every other player with it, which is indistinguishable from a
 * hung server and is a thing any peer can do on purpose.
 */
enum ToriRSServerConnOpen
ToriRSServer_ConnOpenStep(struct ToriRSServerConn* conn);

/*
 * Bytes queued for the peer that the kernel has not taken.
 *
 * Nonzero is the host's cue to select() for writability on this descriptor,
 * and it is also backpressure: a service that generates output faster than the
 * peer drains it (JS5, whose answers are two orders of magnitude larger than
 * its requests) reads this and stops rather than queueing without limit.
 */
int
ToriRSServer_ConnPending(const struct ToriRSServerConn* conn);

/*
 * Hand as much of the queue to the kernel as it will take. Returns 0 once the
 * connection is dead -- a peer that is gone, or one so far behind that it has
 * passed TORIRSSERVER_CONN_OUT_MAX.
 */
int
ToriRSServer_ConnFlush(struct ToriRSServerConn* conn);

/** Release what the connection holds. The descriptor is the caller's. */
void
ToriRSServer_ConnFree(struct ToriRSServerConn* conn);

/*
 * Application bytes already deframed and waiting to be taken.
 *
 * Nonzero means a recv answers from this buffer without touching the socket,
 * which is what makes it wrong to select() on the fd: the bytes are in hand and
 * the peer, having sent them, may well be waiting for the reply.
 */
int
ToriRSServer_ConnBuffered(const struct ToriRSServerConn* conn);

/*
 * Send application bytes. One call is one WebSocket message (the client
 * concatenates them, so message boundaries carry no meaning). Returns the byte
 * count, or -1 once the connection is dead.
 */
int
ToriRSServer_ConnSend(
    struct ToriRSServerConn* conn,
    uint8_t const* data,
    int len);

/*
 * Take up to `max` buffered application bytes, reading the socket once if none
 * are buffered. Returns the count (possibly 0 when the read yielded only part
 * of a frame), or -1 on a closed/failed connection.
 */
int
ToriRSServer_ConnRecv(
    struct ToriRSServerConn* conn,
    uint8_t* out,
    int max);

/*
 * Nothing in this file blocks, and that is now true of the open sequence too.
 *
 * `ToriRSServer_ConnRecvFull` used to exist, and the login handshake was written
 * against it. It went first, because the server has a transport that is not
 * always a socket (torirs_server_transport.h): an in-process session shares a thread
 * with the client feeding it, so a blocking read is not a stall, it is a
 * deadlock. `ToriRSServer_ConnOpen` and `ToriRSServer_ConnPeekFirst` went second,
 * for the same reason one host thread now holds every connection rather than
 * one. Everything above this file waits by re-entering with more bytes, never
 * by asking for them.
 */

#endif
