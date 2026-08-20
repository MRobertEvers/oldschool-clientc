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

#include <stdint.h>

#define TORIRSSERVER_CONN_RAW_MAX 65536
#define TORIRSSERVER_CONN_APP_MAX 65536

struct ToriRSServerConn
{
    int fd;
    int ws;     /* 1 once the WebSocket handshake completed */
    int closed; /* peer hung up, or the stream went unrecoverable */

    /* Bytes off the socket that are not yet whole frames (raw mode leaves this
     * empty — there is nothing to reassemble). */
    uint8_t raw[TORIRSSERVER_CONN_RAW_MAX];
    int raw_len;

    /* Application bytes, deframed, waiting to be taken. */
    uint8_t app[TORIRSSERVER_CONN_APP_MAX];
    int app_len;
};

/*
 * Take over an accepted socket: sniff the first byte and, if it is an HTTP
 * upgrade, complete the WebSocket handshake. Returns 1 when the connection is
 * ready to carry application bytes, 0 if it should be dropped.
 */
int
ToriRSServer_ConnOpen(
    struct ToriRSServerConn* conn,
    int fd);

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
 * There is deliberately no blocking read here.
 *
 * `ToriRSServer_ConnRecvFull` used to exist, and the login handshake was written
 * against it. It is gone because the server now has a transport that is not
 * always a socket (torirs_server_transport.h): an in-process session shares a thread
 * with the client feeding it, so a blocking read is not a stall, it is a
 * deadlock. Everything above this file waits by re-entering with more bytes,
 * never by asking for them.
 */

#endif
