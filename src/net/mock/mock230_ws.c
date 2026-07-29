/*
 * WebSocket (and raw TCP) transport for the mock 230 server. See mock230_ws.h
 * for why one port serves both.
 *
 * The browser build has no choice about this: emscripten implements BSD
 * sockets over WebSockets, so a page's connect() to 127.0.0.1:43595 arrives
 * here as an HTTP/1.1 upgrade and every later byte arrives inside a frame. The
 * native client still speaks raw TCP. Both are the same 230 byte stream once
 * the framing is peeled off, which is the whole point of putting the seam here
 * rather than teaching the protocol code about transports.
 */
#include "mock230_ws.h"

#include "platform/net_transport_ws_frame.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* SHA-1 + base64, for the handshake's Sec-WebSocket-Accept            */
/* ------------------------------------------------------------------ */

struct Sha1
{
    uint32_t h[5];
    uint64_t bits;
    uint8_t block[64];
    int block_len;
};

static uint32_t
sha1_rol(
    uint32_t value,
    int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static void
sha1_compress(
    struct Sha1* ctx,
    uint8_t const* block)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int i;

    for( i = 0; i < 16; i++ )
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    for( i = 16; i < 80; i++ )
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = ctx->h[0];
    b = ctx->h[1];
    c = ctx->h[2];
    d = ctx->h[3];
    e = ctx->h[4];

    for( i = 0; i < 80; i++ )
    {
        uint32_t f;
        uint32_t k;
        uint32_t tmp;

        if( i < 20 )
        {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999u;
        }
        else if( i < 40 )
        {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        }
        else if( i < 60 )
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }

        tmp = sha1_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1_rol(b, 30);
        b = a;
        a = tmp;
    }

    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
    ctx->h[4] += e;
}

static void
sha1_init(struct Sha1* ctx)
{
    ctx->h[0] = 0x67452301u;
    ctx->h[1] = 0xefcdab89u;
    ctx->h[2] = 0x98badcfeu;
    ctx->h[3] = 0x10325476u;
    ctx->h[4] = 0xc3d2e1f0u;
    ctx->bits = 0;
    ctx->block_len = 0;
}

static void
sha1_update(
    struct Sha1* ctx,
    uint8_t const* data,
    int len)
{
    int i;
    for( i = 0; i < len; i++ )
    {
        ctx->block[ctx->block_len++] = data[i];
        ctx->bits += 8;
        if( ctx->block_len == 64 )
        {
            sha1_compress(ctx, ctx->block);
            ctx->block_len = 0;
        }
    }
}

static void
sha1_final(
    struct Sha1* ctx,
    uint8_t out[20])
{
    uint64_t bits = ctx->bits;
    uint8_t pad = 0x80;
    uint8_t zero = 0;
    uint8_t tail[8];
    int i;

    sha1_update(ctx, &pad, 1);
    while( ctx->block_len != 56 )
        sha1_update(ctx, &zero, 1);
    for( i = 0; i < 8; i++ )
        tail[i] = (uint8_t)((bits >> (56 - i * 8)) & 0xff);
    sha1_update(ctx, tail, 8);

    for( i = 0; i < 5; i++ )
    {
        out[i * 4] = (uint8_t)((ctx->h[i] >> 24) & 0xff);
        out[i * 4 + 1] = (uint8_t)((ctx->h[i] >> 16) & 0xff);
        out[i * 4 + 2] = (uint8_t)((ctx->h[i] >> 8) & 0xff);
        out[i * 4 + 3] = (uint8_t)(ctx->h[i] & 0xff);
    }
}

static void
base64_encode(
    uint8_t const* data,
    int len,
    char* out,
    int out_cap)
{
    static char const* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0;
    int o = 0;

    while( i < len && o + 4 < out_cap )
    {
        uint32_t triple = (uint32_t)data[i] << 16;
        int have = 1;

        if( i + 1 < len )
        {
            triple |= (uint32_t)data[i + 1] << 8;
            have = 2;
        }
        if( i + 2 < len )
        {
            triple |= (uint32_t)data[i + 2];
            have = 3;
        }

        out[o++] = alphabet[(triple >> 18) & 0x3f];
        out[o++] = alphabet[(triple >> 12) & 0x3f];
        out[o++] = have > 1 ? alphabet[(triple >> 6) & 0x3f] : '=';
        out[o++] = have > 2 ? alphabet[triple & 0x3f] : '=';
        i += 3;
    }
    out[o] = '\0';
}

/* ------------------------------------------------------------------ */
/* Socket helpers                                                      */
/* ------------------------------------------------------------------ */

static int
write_all(
    int fd,
    uint8_t const* data,
    int len)
{
    int sent = 0;
    while( sent < len )
    {
        ssize_t step = write(fd, data + sent, (size_t)(len - sent));
        if( step < 0 )
        {
            if( errno == EINTR )
                continue;
            return -1;
        }
        if( step == 0 )
            return -1;
        sent += (int)step;
    }
    return sent;
}

/* Append to the deframed application buffer, refusing rather than truncating:
 * a dropped byte is a desynced ISAAC stream, which fails far from here. */
static int
app_append(
    struct Mock230Conn* conn,
    uint8_t const* data,
    int len)
{
    if( len <= 0 )
        return 1;
    if( conn->app_len + len > MOCK230_CONN_APP_MAX )
    {
        fprintf(stderr, "mock230: inbound overflow (%d + %d bytes)\n", conn->app_len, len);
        return 0;
    }
    memcpy(conn->app + conn->app_len, data, (size_t)len);
    conn->app_len += len;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Handshake                                                           */
/* ------------------------------------------------------------------ */

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* Case-insensitive search for a header, returning its value trimmed of spaces.
 * Header names are case-insensitive per RFC 7230 and browsers do vary. */
static int
header_value(
    char const* request,
    char const* name,
    char* out,
    int out_cap)
{
    size_t name_len = strlen(name);
    char const* line = request;

    for( ; *line; )
    {
        char const* end = strstr(line, "\r\n");
        size_t line_len = end ? (size_t)(end - line) : strlen(line);

        if( line_len > name_len && strncasecmp(line, name, name_len) == 0 &&
            line[name_len] == ':' )
        {
            char const* value = line + name_len + 1;
            size_t value_len;
            while( *value == ' ' || *value == '\t' )
                value++;
            value_len = (size_t)((line + line_len) - value);
            while( value_len > 0 && (value[value_len - 1] == ' ' || value[value_len - 1] == '\t') )
                value_len--;
            if( (int)value_len >= out_cap )
                value_len = (size_t)(out_cap - 1);
            memcpy(out, value, value_len);
            out[value_len] = '\0';
            return 1;
        }

        if( !end )
            break;
        line = end + 2;
    }
    out[0] = '\0';
    return 0;
}

/*
 * Read the upgrade request and answer it.
 *
 * The one subtlety is Sec-WebSocket-Protocol. Emscripten's socket layer asks
 * for "binary" by default, and a browser fails the connection outright if the
 * server does not confirm a requested subprotocol — so this echoes the first
 * one offered rather than ignoring the header.
 */
static int
ws_handshake(struct Mock230Conn* conn)
{
    char request[4096];
    int len = 0;
    char key[128];
    char protocol[128];
    char concat[256];
    uint8_t digest[20];
    struct Sha1 sha;
    char accept[64];
    char response[512];
    int response_len;
    char const* body_start;

    for( ;; )
    {
        ssize_t got;

        if( len >= (int)sizeof(request) - 1 )
        {
            fprintf(stderr, "mock230: websocket request headers too large\n");
            return 0;
        }
        got = read(conn->fd, request + len, sizeof(request) - 1 - (size_t)len);
        if( got < 0 )
        {
            if( errno == EINTR )
                continue;
            return 0;
        }
        if( got == 0 )
            return 0;
        len += (int)got;
        request[len] = '\0';
        if( strstr(request, "\r\n\r\n") )
            break;
    }

    if( !header_value(request, "Sec-WebSocket-Key", key, sizeof(key)) || key[0] == '\0' )
    {
        fprintf(stderr, "mock230: upgrade request without Sec-WebSocket-Key\n");
        return 0;
    }

    snprintf(concat, sizeof(concat), "%s%s", key, WS_GUID);
    sha1_init(&sha);
    sha1_update(&sha, (uint8_t const*)concat, (int)strlen(concat));
    sha1_final(&sha, digest);
    base64_encode(digest, 20, accept, sizeof(accept));

    header_value(request, "Sec-WebSocket-Protocol", protocol, sizeof(protocol));
    if( protocol[0] )
    {
        /* "binary, base64" — confirm exactly one, the first. */
        char* comma = strchr(protocol, ',');
        if( comma )
            *comma = '\0';
    }

    response_len = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "%s%s%s"
        "\r\n",
        accept,
        protocol[0] ? "Sec-WebSocket-Protocol: " : "",
        protocol[0] ? protocol : "",
        protocol[0] ? "\r\n" : "");

    if( write_all(conn->fd, (uint8_t const*)response, response_len) < 0 )
        return 0;

    /* Anything the client pipelined after the headers is already frame data. */
    body_start = strstr(request, "\r\n\r\n") + 4;
    conn->raw_len = len - (int)(body_start - request);
    if( conn->raw_len > 0 )
        memcpy(conn->raw, body_start, (size_t)conn->raw_len);
    else
        conn->raw_len = 0;

    conn->ws = 1;
    fprintf(
        stderr,
        "mock230: websocket client (subprotocol %s)\n",
        protocol[0] ? protocol : "none");
    return 1;
}

int
mock230_conn_open(
    struct Mock230Conn* conn,
    int fd)
{
    uint8_t first = 0;
    ssize_t peeked;

    memset(conn, 0, sizeof(*conn));
    conn->fd = fd;

    /*
     * One byte is enough to tell the two apart and is all that can be looked
     * at: a raw 230 client sends opcode 14 alone and then waits for the
     * response, so peeking any further would deadlock.
     */
    for( ;; )
    {
        peeked = recv(fd, &first, 1, MSG_PEEK);
        if( peeked < 0 && errno == EINTR )
            continue;
        break;
    }
    if( peeked <= 0 )
        return 0;

    if( first == 'G' )
        return ws_handshake(conn);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Byte stream                                                         */
/* ------------------------------------------------------------------ */

/* Turn whatever whole frames sit in `raw` into application bytes. */
static int
ws_deframe(struct Mock230Conn* conn)
{
    int pos = 0;

    for( ;; )
    {
        struct WsFrame frame;
        int consumed = 0;
        enum WsDecodeStatus status =
            ws_frame_decode(conn->raw + pos, conn->raw_len - pos, &frame, &consumed);

        if( status == WS_DECODE_INCOMPLETE )
            break;
        if( status == WS_DECODE_ERROR )
        {
            fprintf(stderr, "mock230: malformed websocket frame\n");
            return 0;
        }

        switch( frame.opcode )
        {
        case WS_OP_BINARY:
        case WS_OP_TEXT:
        case WS_OP_CONT:
            if( !app_append(conn, frame.payload, frame.payload_len) )
                return 0;
            break;
        case WS_OP_PING:
        {
            uint8_t pong[256];
            int n = frame.payload_len > 125 ? 125 : frame.payload_len;
            int framed = ws_frame_encode(WS_OP_PONG, frame.payload, n, NULL, pong, sizeof(pong));
            if( framed > 0 && write_all(conn->fd, pong, framed) < 0 )
                return 0;
            break;
        }
        case WS_OP_PONG:
            break;
        case WS_OP_CLOSE:
            return 0;
        default:
            fprintf(stderr, "mock230: unknown websocket opcode %d\n", frame.opcode);
            return 0;
        }

        pos += consumed;
    }

    if( pos > 0 )
    {
        conn->raw_len -= pos;
        if( conn->raw_len > 0 )
            memmove(conn->raw, conn->raw + pos, (size_t)conn->raw_len);
    }
    return 1;
}

/* One read() off the socket, deframed into `app`. Returns 0 once dead. */
static int
conn_fill(struct Mock230Conn* conn)
{
    ssize_t got;
    int space;

    if( conn->closed )
        return 0;

    if( !conn->ws )
    {
        space = MOCK230_CONN_APP_MAX - conn->app_len;
        if( space <= 0 )
            return 1;
        got = read(conn->fd, conn->app + conn->app_len, (size_t)space);
        if( got < 0 )
        {
            if( errno == EINTR )
                return 1;
            if( errno == EAGAIN || errno == EWOULDBLOCK )
                return 1;
            conn->closed = 1;
            return 0;
        }
        if( got == 0 )
        {
            conn->closed = 1;
            return 0;
        }
        conn->app_len += (int)got;
        return 1;
    }

    space = MOCK230_CONN_RAW_MAX - conn->raw_len;
    if( space <= 0 )
    {
        fprintf(stderr, "mock230: websocket frame larger than the read buffer\n");
        conn->closed = 1;
        return 0;
    }
    got = read(conn->fd, conn->raw + conn->raw_len, (size_t)space);
    if( got < 0 )
    {
        if( errno == EINTR )
            return 1;
        if( errno == EAGAIN || errno == EWOULDBLOCK )
            return 1;
        conn->closed = 1;
        return 0;
    }
    if( got == 0 )
    {
        conn->closed = 1;
        return 0;
    }
    conn->raw_len += (int)got;

    if( !ws_deframe(conn) )
    {
        conn->closed = 1;
        return 0;
    }
    return 1;
}

static int
app_take(
    struct Mock230Conn* conn,
    uint8_t* out,
    int max)
{
    int take = conn->app_len < max ? conn->app_len : max;
    if( take <= 0 )
        return 0;
    memcpy(out, conn->app, (size_t)take);
    conn->app_len -= take;
    if( conn->app_len > 0 )
        memmove(conn->app, conn->app + take, (size_t)conn->app_len);
    return take;
}

int
mock230_conn_recv(
    struct Mock230Conn* conn,
    uint8_t* out,
    int max)
{
    if( conn->app_len == 0 )
    {
        if( !conn_fill(conn) )
            return conn->app_len > 0 ? app_take(conn, out, max) : -1;
    }
    return app_take(conn, out, max);
}

int
mock230_conn_send(
    struct Mock230Conn* conn,
    uint8_t const* data,
    int len)
{
    if( !conn || conn->closed || conn->fd < 0 )
        return -1;
    if( len <= 0 )
        return 0;

    if( conn->ws )
    {
        /* Header only — the payload goes out unmasked and unmodified, so it
         * never has to be copied however large the packet is. */
        uint8_t header[16];
        int header_len = ws_frame_encode_header(WS_OP_BINARY, len, NULL, header, sizeof(header));
        if( header_len < 0 )
        {
            conn->closed = 1;
            return -1;
        }
        if( write_all(conn->fd, header, header_len) < 0 )
        {
            conn->closed = 1;
            return -1;
        }
    }

    if( write_all(conn->fd, data, len) < 0 )
    {
        conn->closed = 1;
        return -1;
    }
    return len;
}
