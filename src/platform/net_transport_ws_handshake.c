/*
 * RFC 6455 server handshake. See the header for why it is a pure function.
 *
 * SHA-1 lives here rather than being reached for from a crypto library on
 * purpose: it is used for exactly one thing, the Sec-WebSocket-Accept digest,
 * which is a fixed protocol constant and not a security property. Nothing here
 * authenticates anything.
 */

#include "platform/net_transport_ws_handshake.h"
#include <assert.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

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

/* Case-insensitive header lookup, value trimmed of surrounding spaces. Header
 * names are case-insensitive per RFC 7230 and browsers do vary. */
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

enum WsHandshakeStatus
WsHandshake_Consume(
    uint8_t const* data,
    int len,
    struct WsHandshake* out)
{
    char request[WS_HANDSHAKE_REQUEST_MAX + 1];
    char key[128];
    char concat[256];
    uint8_t digest[20];
    struct Sha1 sha;
    char accept[64];
    char const* body_start;

    if( len < 0 )
        return WS_HANDSHAKE_ERROR;
    assert(data);
    assert(out);
    memset(out, 0, sizeof(*out));
    if( len > WS_HANDSHAKE_REQUEST_MAX )
        return WS_HANDSHAKE_ERROR;

    /* Headers are text, and the parse below is string-based, so the working
     * copy is terminated rather than the caller's buffer being modified. */
    memcpy(request, data, (size_t)len);
    request[len] = '\0';

    body_start = strstr(request, "\r\n\r\n");
    if( !body_start )
        return len == WS_HANDSHAKE_REQUEST_MAX ? WS_HANDSHAKE_ERROR : WS_HANDSHAKE_INCOMPLETE;
    body_start += 4;

    if( !header_value(request, "Sec-WebSocket-Key", key, sizeof(key)) || key[0] == '\0' )
        return WS_HANDSHAKE_ERROR;

    snprintf(concat, sizeof(concat), "%s%s", key, WS_GUID);
    sha1_init(&sha);
    sha1_update(&sha, (uint8_t const*)concat, (int)strlen(concat));
    sha1_final(&sha, digest);
    base64_encode(digest, 20, accept, sizeof(accept));

    header_value(request, "Sec-WebSocket-Protocol", out->protocol, sizeof(out->protocol));
    if( out->protocol[0] )
    {
        /* "binary, base64" — confirm exactly one, the first. */
        char* comma = strchr(out->protocol, ',');
        if( comma )
            *comma = '\0';
    }

    out->response_len = snprintf(
        out->response,
        sizeof(out->response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "%s%s%s"
        "\r\n",
        accept,
        out->protocol[0] ? "Sec-WebSocket-Protocol: " : "",
        out->protocol[0] ? out->protocol : "",
        out->protocol[0] ? "\r\n" : "");
    if( out->response_len < 0 || out->response_len >= (int)sizeof(out->response) )
        return WS_HANDSHAKE_ERROR;

    out->consumed = (int)(body_start - request);
    return WS_HANDSHAKE_OK;
}
