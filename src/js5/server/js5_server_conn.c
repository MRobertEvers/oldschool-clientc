/*
 * JS5 connection framing. See the header for why this is socket-free.
 */

#include "js5_server_conn.h"
#include <assert.h>

#include "js5_server_session.h"
#include "platform/net_transport_ws_frame.h"
#include "platform/net_transport_ws_handshake.h"

#include <string.h>

bool
Js5ServerConn_Open(
    struct Js5ServerConn* conn,
    struct Js5ServerCache* cache,
    const struct Js5ServerSessionConfig* config,
    uint64_t now_ms)
{
    assert(conn);
    memset(conn, 0, sizeof(*conn));
    conn->session = Js5ServerSessionNew(cache, config, now_ms);
    return conn->session != NULL;
}

void
Js5ServerConn_Close(struct Js5ServerConn* conn)
{
    assert(conn);
    Js5ServerSessionFree(conn->session);
    memset(conn, 0, sizeof(*conn));
}

/* Drop consumed bytes off the front of the inbound buffer. */
static void
js5_conn_in_drop(
    struct Js5ServerConn* conn,
    int count)
{
    if( count <= 0 )
        return;
    if( count >= conn->in_len )
    {
        conn->in_len = 0;
        return;
    }
    memmove(conn->in, conn->in + count, (size_t)(conn->in_len - count));
    conn->in_len -= count;
}

/*
 * Queue a control frame if there is room.
 *
 * A pong is advisory — browsers do not require one to keep a connection alive —
 * so a full output buffer drops it rather than stalling the data the client is
 * actually waiting for.
 */
static void
js5_conn_queue_control(
    struct Js5ServerConn* conn,
    int opcode,
    const uint8_t* payload,
    int payload_len)
{
    int written = ws_frame_encode(
        opcode,
        payload,
        payload_len,
        NULL, /* a server must not mask (RFC 6455 5.1) */
        conn->out + conn->out_len,
        (int)sizeof(conn->out) - conn->out_len);
    if( written > 0 )
        conn->out_len += written;
}

/* Turn whatever whole frames are buffered into session input. */
static int
js5_conn_deframe(
    struct Js5ServerConn* conn,
    uint64_t now_ms)
{
    int pos = 0;

    for( ;; )
    {
        struct WsFrame frame;
        int consumed = 0;
        enum WsDecodeStatus status =
            ws_frame_decode(conn->in + pos, conn->in_len - pos, &frame, &consumed);

        if( status == WS_DECODE_INCOMPLETE )
            break;
        if( status == WS_DECODE_ERROR )
            return -1;

        switch( frame.opcode )
        {
        case WS_OP_BINARY:
        case WS_OP_TEXT:
        case WS_OP_CONT:
            if( frame.payload_len > 0 &&
                Js5ServerSessionFeed(
                    conn->session, frame.payload, (size_t)frame.payload_len, now_ms) != 0 )
                return -1;
            break;
        case WS_OP_PING:
            js5_conn_queue_control(conn, WS_OP_PONG, frame.payload, frame.payload_len);
            break;
        case WS_OP_PONG:
            break;
        case WS_OP_CLOSE:
        default:
            /* JS5 has no orderly shutdown of its own and the session has
             * nothing to finish, so a close is simply a drop. */
            return -1;
        }
        pos += consumed;
    }

    js5_conn_in_drop(conn, pos);
    return 0;
}

int
Js5ServerConn_Feed(
    struct Js5ServerConn* conn,
    const uint8_t* data,
    int size,
    uint64_t now_ms)
{
    if( !conn || !conn->session || (!data && size > 0) || size < 0 )
        return -1;
    if( size > (int)sizeof(conn->in) - conn->in_len )
    {
        /* Nothing a client legitimately sends is this large. Refusing is the
         * honest answer; a bigger buffer would only postpone it. */
        return -1;
    }
    memcpy(conn->in + conn->in_len, data, (size_t)size);
    conn->in_len += size;

    if( conn->framing == JS5_FRAMING_UNKNOWN )
    {
        if( conn->in_len == 0 )
            return 0;
        conn->framing = WsHandshake_LooksLikeHttp(conn->in[0]) ? JS5_FRAMING_WS_HANDSHAKE
                                                               : JS5_FRAMING_RAW;
    }

    if( conn->framing == JS5_FRAMING_RAW )
    {
        int fed = conn->in_len;
        conn->in_len = 0;
        if( fed > 0 && Js5ServerSessionFeed(conn->session, conn->in, (size_t)fed, now_ms) != 0 )
            return -1;
        return 0;
    }

    if( conn->framing == JS5_FRAMING_WS_HANDSHAKE )
    {
        struct WsHandshake handshake;
        enum WsHandshakeStatus status =
            WsHandshake_Consume(conn->in, conn->in_len, &handshake);

        if( status == WS_HANDSHAKE_INCOMPLETE )
            return 0;
        if( status != WS_HANDSHAKE_OK )
            return -1;
        if( handshake.response_len > (int)sizeof(conn->out) - conn->out_len )
            return -1;
        memcpy(conn->out + conn->out_len, handshake.response, (size_t)handshake.response_len);
        conn->out_len += handshake.response_len;
        js5_conn_in_drop(conn, handshake.consumed);
        conn->framing = JS5_FRAMING_WS;
        /* Anything pipelined after the headers is already frame data, and the
         * deframe below picks it up. */
    }

    return js5_conn_deframe(conn, now_ms);
}

bool
Js5ServerConn_HasPendingOutput(const struct Js5ServerConn* conn)
{
    return conn && conn->out_pos < conn->out_len;
}

int
Js5ServerConn_TakeOutput(
    struct Js5ServerConn* conn,
    size_t budget,
    const uint8_t** data,
    int* size,
    uint64_t now_ms)
{
    const uint8_t* view;
    size_t view_size;
    int available;

    assert(conn);
    if( !conn->session )
        return -1;
    assert(data);
    assert(size);
    *data = NULL;
    *size = 0;

    /* Already-framed bytes first: a frame in progress must finish before
     * another header goes out. */
    if( conn->out_pos < conn->out_len )
    {
        *data = conn->out + conn->out_pos;
        *size = conn->out_len - conn->out_pos;
        return 1;
    }
    conn->out_pos = 0;
    conn->out_len = 0;

    available = Js5ServerSessionPeekOutput(conn->session, &view, &view_size);
    if( available < 0 )
        return -1;
    if( available == 0 )
        return 0;
    if( budget > 0u && view_size > budget )
        view_size = budget;

    if( conn->framing != JS5_FRAMING_WS )
    {
        /* Raw: the session's own view goes straight out, and Consumed reports
         * back to it directly. */
        *data = view;
        *size = (int)view_size;
        return 1;
    }

    /*
     * Frame it into the output buffer and tell the session it is gone.
     *
     * The copy is what makes a partial write safe. A frame header announces a
     * length, so the payload has to arrive whole; leaving it in the session's
     * view and re-peeking after a short send would emit a second header in the
     * middle of a message.
     */
    {
        int room = (int)sizeof(conn->out);
        int header_len;

        if( view_size > (size_t)(room - 16) )
            view_size = (size_t)(room - 16);
        header_len = ws_frame_encode_header(WS_OP_BINARY, (int)view_size, NULL, conn->out, room);
        if( header_len < 0 )
            return -1;
        memcpy(conn->out + header_len, view, view_size);
        conn->out_len = header_len + (int)view_size;
        conn->out_pos = 0;
        if( Js5ServerSessionConsumeOutput(conn->session, view_size, now_ms) != 0 )
            return -1;
    }

    *data = conn->out;
    *size = conn->out_len;
    return 1;
}

void
Js5ServerConn_Consumed(
    struct Js5ServerConn* conn,
    int size,
    uint64_t now_ms)
{
    if( size <= 0 )
        return;
    assert(conn);
    if( conn->out_len > 0 )
    {
        /* Framed bytes: the session was already told when the frame was built,
         * so only the buffer position moves. */
        conn->out_pos += size;
        if( conn->out_pos >= conn->out_len )
        {
            conn->out_pos = 0;
            conn->out_len = 0;
        }
        return;
    }
    /* Raw: the session owns the view, so it is what gets told — and it is what
     * carries the output-progress deadline. */
    Js5ServerSessionConsumeOutput(conn->session, (size_t)size, now_ms);
}
