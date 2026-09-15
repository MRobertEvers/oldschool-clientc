#include "net_transport.h"
#include "torirs_env.h"

#include "cmd/cmdbus.h"
#include "net/net.h"
#include "net_transport_ws_frame.h"
#include "sockstream.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * Minimal RFC 6455 WebSocket client transport (xrsps). Flow:
 *   CONNECT -> TCP dial -> HTTP/1.1 Upgrade -> await 101 -> OPEN.
 * Once OPEN, outbound app bytes are wrapped in masked binary frames and inbound
 * frames are de-framed so NET_RECV carries plain application bytes (the framer
 * above never sees WebSocket headers). We never offer permessage-deflate (no
 * Sec-WebSocket-Extensions header), so the server sends uncompressed frames.
 */

enum ws_state
{
    WS_STATE_IDLE = 0,   /* no socket */
    WS_STATE_CONNECTING, /* TCP connect in flight */
    WS_STATE_HANDSHAKE,  /* upgrade request sent, awaiting 101 */
    WS_STATE_OPEN,       /* frames flow */
};

struct ByteBuf
{
    uint8_t* data;
    int len;
    int cap;
};

struct NetTransportWs
{
    struct NetTransport base;
    struct SockStream* stream;
    int default_port;
    enum ws_state state;
    int last_status;
    char host[128];
    int port;

    struct ByteBuf wire; /* raw bytes queued for the socket (request + frames) */
    struct ByteBuf app;  /* app bytes queued before OPEN, framed on open */
    /* Per-message boundaries within `app`: each pre-OPEN SEND_DATA is one
     * message and must become its own WS frame (the server decodes one packet
     * per message). */
    int app_msg_len[16];
    int app_msg_count;
    struct ByteBuf in; /* raw received bytes awaiting handshake/frame parse */
};

/* How much un-de-framed inbound the transport will hold before it stops
 * reading. Comfortably above one large frame, far below a minutes-long
 * backlog. */
#define WS_IN_MAX (512 * 1024)

/* Fixed masking key: correctness only needs *a* key (the server unmasks with
 * whatever we send); a constant keeps record/replay deterministic. */
static uint8_t const k_mask[4] = { 0x37, 0x9a, 0xc2, 0x51 };

static int
ws_debug(void)
{
    static int cached = -1;
    if( cached < 0 )
        cached = torirs_env_net_debug() ? 1 : 0;
    return cached;
}
#define WSLOG(...)                                                                                  \
    do                                                                                             \
    {                                                                                              \
        if( ws_debug() )                                                                           \
            TORIRS_LOG(__VA_ARGS__);                                                          \
    } while( 0 )

static void
buf_append(struct ByteBuf* buf, uint8_t const* data, int len)
{
    if( len <= 0 )
        return;
    if( buf->len + len > buf->cap )
    {
        int cap = buf->cap ? buf->cap : 1024;
        while( cap < buf->len + len )
            cap *= 2;
        buf->data = realloc(buf->data, cap);
        assert(buf->data);
        buf->cap = cap;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
}

static void
buf_consume(struct ByteBuf* buf, int n)
{
    if( n <= 0 )
        return;
    if( n >= buf->len )
    {
        buf->len = 0;
        return;
    }
    memmove(buf->data, buf->data + n, buf->len - n);
    buf->len -= n;
}

static void
buf_free(struct ByteBuf* buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->len = buf->cap = 0;
}

static void
ws_emit_status(struct NetTransportWs* self, struct ToriRS_CmdBus* bus, int status)
{
    if( self->last_status == status )
        return;
    self->last_status = status;
    CmdBus_PushNetStatus(bus, status);
}

/* Send as much of the wire buffer as the socket accepts. */
static void
ws_wire_flush(struct NetTransportWs* self)
{
    while( self->wire.len > 0 && self->stream && self->state != WS_STATE_CONNECTING )
    {
        int sent = sockstream_send(self->stream, self->wire.data, self->wire.len);
        if( sent <= 0 )
            break;
        buf_consume(&self->wire, sent);
    }
}

/* Frame `payload` as a client binary frame and queue it on the wire. */
static void
ws_queue_frame(struct NetTransportWs* self, int opcode, uint8_t const* payload, int len)
{
    /* Worst-case header (10) + mask (4). */
    uint8_t stackbuf[2048 + 14];
    uint8_t* framebuf = stackbuf;
    int cap = len + 14;
    if( cap > (int)sizeof(stackbuf) )
        framebuf = malloc((size_t)cap);
    assert(framebuf);

    int n = ws_frame_encode(opcode, payload, len, k_mask, framebuf, cap);
    if( n > 0 )
        buf_append(&self->wire, framebuf, n);
    if( framebuf != stackbuf )
        free(framebuf);
}

static void
ws_parse_host(struct NetTransportWs* self, char const* spec, int spec_len)
{
    int colon = -1;
    for( int i = 0; i < spec_len; i++ )
        if( spec[i] == ':' )
        {
            colon = i;
            break;
        }
    int copy = (colon >= 0 ? colon : spec_len);
    if( copy >= (int)sizeof(self->host) )
        copy = (int)sizeof(self->host) - 1;
    memcpy(self->host, spec, copy);
    self->host[copy] = '\0';
    self->port = self->default_port;
    if( colon >= 0 )
    {
        char portbuf[16] = { 0 };
        int plen = spec_len - colon - 1;
        if( plen > 0 && plen < (int)sizeof(portbuf) )
        {
            memcpy(portbuf, spec + colon + 1, plen);
            self->port = atoi(portbuf);
        }
    }
}

static void
ws_send_upgrade_request(struct NetTransportWs* self)
{
    char req[512];
    /* No Sec-WebSocket-Extensions -> permessage-deflate is never negotiated.
     * Sec-WebSocket-Key is the RFC sample value; we don't verify the server's
     * Accept response, so a constant key is fine for local dev. */
    int n = snprintf(
        req,
        sizeof(req),
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        self->host,
        self->port);
    buf_append(&self->wire, (uint8_t const*)req, n);
    ws_wire_flush(self);
}

static void
ws_close(struct NetTransportWs* self, struct ToriRS_CmdBus* bus, int status)
{
    if( self->stream )
    {
        sockstream_close(self->stream);
        self->stream = NULL;
    }
    self->state = WS_STATE_IDLE;
    self->wire.len = 0;
    self->app.len = 0;
    self->in.len = 0;
    ws_emit_status(self, bus, status);
}

/* Drain net->out: open connections and queue outbound app bytes. */
static void
ws_drain_out(struct NetTransportWs* self, struct ToriRS_Network* net, struct ToriRS_CmdBus* bus)
{
    struct ToriRS_CmdHeader header;
    static uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    while( ToriRS_Network_PopOut(net, &header, payload) )
    {
        if( header.type == TORIRS_NET_OUT_CONNECT )
        {
            ws_parse_host(self, (char const*)payload, header.length);
            if( self->stream )
                sockstream_close(self->stream);
            self->wire.len = self->app.len = self->in.len = 0;
            self->app_msg_count = 0;
            self->stream = sockstream_new();
            if( self->stream )
            {
                sockstream_connect(self->stream, self->host, self->port, 0);
                self->state = WS_STATE_CONNECTING;
                ws_emit_status(self, bus, TORIRS_NET_STATUS_CONNECTING);
            }
            else
            {
                ws_emit_status(self, bus, TORIRS_NET_STATUS_FAILED);
            }
        }
        else if( header.type == TORIRS_NET_OUT_DISCONNECT )
        {
            ws_close(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
            self->app_msg_count = 0;
        }
        else if( header.type == TORIRS_NET_OUT_SEND_DATA )
        {
            if( self->state == WS_STATE_OPEN )
            {
                ws_queue_frame(self, WS_OP_BINARY, payload, header.length);
                ws_wire_flush(self);
            }
            else
            {
                /* Not open yet: hold app bytes AND remember the message
                 * boundary so each becomes its own WS frame on OPEN. */
                buf_append(&self->app, payload, header.length);
                int cap = (int)(sizeof(self->app_msg_len) / sizeof(self->app_msg_len[0]));
                if( self->app_msg_count < cap )
                    self->app_msg_len[self->app_msg_count++] = header.length;
                else
                    self->app_msg_len[cap - 1] += header.length; /* rare: coalesce tail */
            }
        }
    }
}

/* Returns 1 if still connected, 0 if the socket closed/failed. */
static int
ws_read_socket(struct NetTransportWs* self, struct ToriRS_CmdBus* bus)
{
    uint8_t chunk[TORIRS_CMD_MAX_PAYLOAD];
    for( ;; )
    {
        int n;

        /* Paired with the bus back-pressure in ws_parse_frames: once frames
         * stop being handed over, reading more only grows `in`. Stop at a
         * bound and let the socket hold the rest. */
        if( self->in.len >= WS_IN_MAX )
            break;
        n = sockstream_recv(self->stream, chunk, (int)sizeof(chunk));
        if( n > 0 )
        {
            buf_append(&self->in, chunk, n);
            if( n < (int)sizeof(chunk) )
                break;
        }
        else if( n == 0 || n == SOCKSTREAM_ERROR_CLOSED )
        {
            /* Both, for the reason platform_socket.c's read loop spells out:
             * sockstream_recv signals a dead connection with the code, never
             * with a zero-length read. */
            ws_close(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
            return 0;
        }
        else
        {
            break; /* would-block */
        }
    }
    return 1;
}

/* Consume the HTTP 101 response header. Returns 1 when the handshake completed
 * (state advanced to OPEN), 0 while still waiting, -1 on a non-101 response. */
static int
ws_try_finish_handshake(struct NetTransportWs* self, struct ToriRS_CmdBus* bus)
{
    /* Find the end-of-headers marker. */
    int end = -1;
    for( int i = 0; i + 3 < self->in.len; i++ )
    {
        if( self->in.data[i] == '\r' && self->in.data[i + 1] == '\n' &&
            self->in.data[i + 2] == '\r' && self->in.data[i + 3] == '\n' )
        {
            end = i + 4;
            break;
        }
    }
    if( end < 0 )
        return 0; /* headers not fully received yet */

    /* Status line must be a 101 switch. */
    int is_101 = 0;
    for( int i = 0; i + 2 < end; i++ )
    {
        if( self->in.data[i] == '1' && self->in.data[i + 1] == '0' &&
            self->in.data[i + 2] == '1' )
        {
            is_101 = 1;
            break;
        }
    }
    if( !is_101 )
    {
        TORIRS_ERR("ws: upgrade rejected (no 101 status)\n");
        ws_close(self, bus, TORIRS_NET_STATUS_FAILED);
        return -1;
    }

    buf_consume(&self->in, end);
    self->state = WS_STATE_OPEN;
    WSLOG("ws: handshake OK, OPEN (%d queued msgs)\n", self->app_msg_count);
    ws_emit_status(self, bus, TORIRS_NET_STATUS_CONNECTED);

    /* Frame each app message queued during the handshake as its own WS frame. */
    int off = 0;
    for( int i = 0; i < self->app_msg_count; i++ )
    {
        ws_queue_frame(self, WS_OP_BINARY, self->app.data + off, self->app_msg_len[i]);
        off += self->app_msg_len[i];
    }
    self->app.len = 0;
    self->app_msg_count = 0;
    ws_wire_flush(self);
    return 1;
}

/* Can the bus take every chunk this payload will be split into?
 *
 * Summed, not asked chunk by chunk: each chunk costs a header too, and asking
 * per chunk against an unchanging free count would admit a payload whose
 * chunks collectively do not fit. */
static int
ws_bus_has_room(struct ToriRS_CmdBus const* bus, int payload_len)
{
    uint32_t header = (uint32_t)sizeof(struct ToriRS_CmdHeader);
    uint32_t chunks =
        payload_len <= 0 ? 1u
                         : (uint32_t)((payload_len + TORIRS_CMD_MAX_PAYLOAD - 1) /
                                      TORIRS_CMD_MAX_PAYLOAD);

    return CmdBus_FreeBytes(bus) >= chunks * header + (uint32_t)(payload_len > 0 ? payload_len : 0);
}

/* Parse complete frames out of the inbound buffer. */
static void
ws_parse_frames(struct NetTransportWs* self, struct ToriRS_CmdBus* bus)
{
    for( ;; )
    {
        struct WsFrame frame;
        int consumed = 0;
        enum WsDecodeStatus st = ws_frame_decode(self->in.data, self->in.len, &frame, &consumed);
        if( st == WS_DECODE_INCOMPLETE )
            break;
        if( st == WS_DECODE_ERROR )
        {
            TORIRS_LOG("ws: malformed frame, closing\n");
            ws_close(self, bus, TORIRS_NET_STATUS_FAILED);
            return;
        }

        /* Back-pressure: a frame is only consumed when all of its chunks can
         * reach the bus. CmdBus_Push refuses silently when the ring is full,
         * and a half-pushed frame is a hole in the byte stream — see
         * platform_socket.c's read loop for what that looks like downstream.
         * Leaving the bytes in `in` retries them on the next poll. */
        if( (frame.opcode == WS_OP_BINARY || frame.opcode == WS_OP_CONT ||
             frame.opcode == WS_OP_TEXT) &&
            !ws_bus_has_room(bus, frame.payload_len) )
            break;

        switch( frame.opcode )
        {
        case WS_OP_BINARY:
        case WS_OP_CONT:
        case WS_OP_TEXT:
            /* De-framed application bytes; chunk to the command payload cap. */
            {
                int off = 0;
                while( off < frame.payload_len )
                {
                    int chunk = frame.payload_len - off;
                    if( chunk > TORIRS_CMD_MAX_PAYLOAD )
                        chunk = TORIRS_CMD_MAX_PAYLOAD;
                    CmdBus_Push(bus, TORIRS_CMD_NET_RECV, frame.payload + off, (uint16_t)chunk);
                    off += chunk;
                }
            }
            break;
        case WS_OP_PING:
            ws_queue_frame(self, WS_OP_PONG, frame.payload, frame.payload_len);
            ws_wire_flush(self);
            break;
        case WS_OP_PONG:
            break;
        case WS_OP_CLOSE:
            ws_close(self, bus, TORIRS_NET_STATUS_DISCONNECTED);
            return;
        default:
            break;
        }

        buf_consume(&self->in, consumed);
        if( self->in.len == 0 )
            break;
    }
}

static void
ws_poll(struct NetTransport* t, struct ToriRS_Network* net, struct ToriRS_CmdBus* bus)
{
    struct NetTransportWs* self = (struct NetTransportWs*)t;
    assert(self && net && bus);

    ws_drain_out(self, net, bus);
    if( !self->stream )
        return;

    if( self->state == WS_STATE_CONNECTING )
    {
        int rc = sockstream_poll_connect(self->stream);
        if( rc == SOCKSTREAM_CONNECT_SUCCESS )
        {
            self->state = WS_STATE_HANDSHAKE;
            ws_send_upgrade_request(self);
        }
        else if( rc == SOCKSTREAM_CONNECT_FAILED )
        {
            ws_close(self, bus, TORIRS_NET_STATUS_FAILED);
            return;
        }
        else
        {
            return; /* still connecting */
        }
    }

    ws_wire_flush(self);

    if( !ws_read_socket(self, bus) )
        return;

    if( self->state == WS_STATE_HANDSHAKE )
    {
        if( ws_try_finish_handshake(self, bus) != 1 )
            return;
    }

    if( self->state == WS_STATE_OPEN )
        ws_parse_frames(self, bus);
}

static void
ws_free(struct NetTransport* t)
{
    struct NetTransportWs* self = (struct NetTransportWs*)t;
    if( self->stream )
        sockstream_close(self->stream);
    buf_free(&self->wire);
    buf_free(&self->app);
    buf_free(&self->in);
    free(self);
}

static struct NetTransportVTable const k_ws_vtable = {
    .poll = ws_poll,
    .free_ = ws_free,
};

struct NetTransport*
NetTransport_NewWs(int default_port)
{
    struct NetTransportWs* self = calloc(1, sizeof(*self));
    assert(self);
    self->base.vtable = &k_ws_vtable;
    self->default_port = default_port > 0 ? default_port : 43594;
    self->last_status = TORIRS_NET_STATUS_DISCONNECTED;
    return &self->base;
}
