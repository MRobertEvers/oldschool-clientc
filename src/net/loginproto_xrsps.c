#include "loginproto_xrsps.h"

#include "loginproto.h" /* LOGINPROTO_SUCCESS / _ERROR / _AWAIT_* */
#include "net.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/* xrsps client opcodes (ClientPacketId.ts, active set). */
#define XR_OP_HELLO 200
#define XR_OP_HANDSHAKE 202
#define XR_OP_LOGIN 204

/* xrsps server opcodes we care about at the login layer. */
#define XR_OP_WELCOME 0
#define XR_OP_LOGIN_RESPONSE 3

enum xr_login_state
{
    XR_LOGIN_SEND_INITIAL = 0, /* stage hello + login */
    XR_LOGIN_AWAIT_RESPONSE,   /* waiting for login_response(3) */
    XR_LOGIN_GOT_SUCCESS,      /* login accepted; stage handshake next poll */
    XR_LOGIN_DONE,
    XR_LOGIN_ERR,
};

struct XrLogin
{
    struct ToriRS_Network* net; /* for rev->client_version + rev->packetin_size */
    char username[64];
    char password[64];
    int revision;
    enum xr_login_state state;

    /* Staged outbound frames. The xrsps server decodes exactly ONE packet per
     * WebSocket message, so each packet must reach the transport as its own
     * SendRaw -> its own WS frame. We record frame boundaries and hand back one
     * frame per xr_send call. */
    uint8_t out[512];
    int frame_end[4]; /* cumulative end offset of each staged frame */
    int frame_count;
    int frame_idx;  /* next frame to emit */
    int send_off;   /* byte offset of the next frame's start */
};

static void
stage_reset(struct XrLogin* h)
{
    h->frame_count = 0;
    h->frame_idx = 0;
    h->send_off = 0;
}

static void
stage_push(struct XrLogin* h, int frame_len)
{
    assert(h->frame_count < (int)(sizeof(h->frame_end) / sizeof(h->frame_end[0])));
    int prev = h->frame_count ? h->frame_end[h->frame_count - 1] : 0;
    h->frame_end[h->frame_count++] = prev + frame_len;
}

/* --- little-endian-free wire writers (all big-endian, matching xrsps) --- */

static int
w_string(uint8_t* p, char const* s)
{
    int n = 0;
    for( ; s[n]; n++ )
        p[n] = (uint8_t)s[n];
    p[n++] = 0; /* null terminator */
    return n;
}

static int
w_int_be(uint8_t* p, int v)
{
    p[0] = (uint8_t)((v >> 24) & 0xff);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
    return 4;
}

static int
frame_varu8(uint8_t* out, int opcode, uint8_t const* payload, int plen)
{
    out[0] = (uint8_t)opcode;
    out[1] = (uint8_t)plen;
    memcpy(out + 2, payload, (size_t)plen);
    return plen + 2;
}

static int
frame_varu16(uint8_t* out, int opcode, uint8_t const* payload, int plen)
{
    out[0] = (uint8_t)opcode;
    out[1] = (uint8_t)((plen >> 8) & 0xff);
    out[2] = (uint8_t)(plen & 0xff);
    memcpy(out + 3, payload, (size_t)plen);
    return plen + 3;
}

static void
stage_hello_and_login(struct XrLogin* h)
{
    uint8_t payload[256];
    int end = 0;
    int n;

    stage_reset(h);

    /* hello(200): client + version strings. Its own frame -> own WS message. */
    n = 0;
    n += w_string(payload + n, "osrs-typescript");
    n += w_string(payload + n, "");
    int flen = frame_varu8(h->out + end, XR_OP_HELLO, payload, n);
    stage_push(h, flen);
    end += flen;

    /* login(204): username + password + revision int. Separate frame. */
    n = 0;
    n += w_string(payload + n, h->username);
    n += w_string(payload + n, h->password);
    n += w_int_be(payload + n, h->revision);
    flen = frame_varu16(h->out + end, XR_OP_LOGIN, payload, n);
    stage_push(h, flen);
}

static void
stage_handshake(struct XrLogin* h)
{
    uint8_t payload[128];
    int n = 0;

    stage_reset(h);

    n += w_string(payload + n, h->username); /* name */
    payload[n++] = 0;                        /* hasAppearance = false */
    payload[n++] = 0;                        /* clientType = 0 (desktop) */
    int flen = frame_varu8(h->out, XR_OP_HANDSHAKE, payload, n);
    stage_push(h, flen);
}

/* Frame size for a server opcode via the rev table (>=0 fixed, -1 u8, -2 u16). */
static int
server_frame_len(struct XrLogin* h, int opcode, uint8_t const* data, int avail, int* hdr_out)
{
    int size = h->net->rev->packetin_size(opcode);
    if( size == -1 ) /* var-u8 */
    {
        if( avail < 2 )
            return -1;
        *hdr_out = 2;
        return data[1];
    }
    if( size == -2 ) /* var-u16 */
    {
        if( avail < 3 )
            return -1;
        *hdr_out = 3;
        return (data[1] << 8) | data[2];
    }
    *hdr_out = 1;
    return size;
}

/* --- NetLoginVTable impl --- */

static void*
xr_new(struct ToriRS_Network* net, char const* username, char const* password)
{
    struct XrLogin* h = calloc(1, sizeof(*h));
    assert(h);
    h->net = net;
    snprintf(h->username, sizeof(h->username), "%s", username ? username : "");
    snprintf(h->password, sizeof(h->password), "%s", password ? password : "");
    h->revision = net->rev->client_version;
    h->state = XR_LOGIN_SEND_INITIAL;
    return h;
}

static int
xr_recv(void* handle, uint8_t const* data, int size)
{
    struct XrLogin* h = handle;
    int off = 0;

    while( off < size )
    {
        int opcode = data[off];
        int hdr = 0;
        int plen = server_frame_len(h, opcode, data + off, size - off, &hdr);
        if( plen < 0 )
            break; /* header not fully present yet */
        if( off + hdr + plen > size )
            break; /* payload not fully present yet */

        uint8_t const* payload = data + off + hdr;

        if( opcode == XR_OP_LOGIN_RESPONSE )
        {
            int success = plen >= 1 ? payload[0] : 0;
            int err_code = plen >= 2 ? payload[1] : 0;
            if( success )
            {
                h->state = XR_LOGIN_GOT_SUCCESS;
                off += hdr + plen;
                break; /* stop; poll stages the handshake, remainder is GAME */
            }
            TORIRS_ERR("xrsps login: rejected (errorCode=%d)\n", err_code);
            h->state = XR_LOGIN_ERR;
            off += hdr + plen;
            break;
        }

        /* welcome(0), tick(1), anything else pre-login: skip. */
        (void)XR_OP_WELCOME;
        off += hdr + plen;
    }

    return off;
}

static int
xr_send(void* handle, uint8_t* out, int out_size)
{
    (void)out_size;
    struct XrLogin* h = handle;
    if( h->frame_idx >= h->frame_count )
        return 0;

    /* One frame per call so each packet becomes its own SendRaw -> WS message
     * (the server decodes exactly one packet per WebSocket message). */
    int end = h->frame_end[h->frame_idx];
    int len = end - h->send_off;
    assert(len <= out_size);
    memcpy(out, h->out + h->send_off, (size_t)len);
    h->send_off = end;
    h->frame_idx++;
    if( h->frame_idx >= h->frame_count )
        stage_reset(h);
    return len;
}

static int
xr_poll(void* handle)
{
    struct XrLogin* h = handle;
    switch( h->state )
    {
    case XR_LOGIN_SEND_INITIAL:
        stage_hello_and_login(h);
        h->state = XR_LOGIN_AWAIT_RESPONSE;
        return LOGINPROTO_AWAIT_RECV;
    case XR_LOGIN_AWAIT_RESPONSE:
        return LOGINPROTO_AWAIT_RECV;
    case XR_LOGIN_GOT_SUCCESS:
        stage_handshake(h);
        h->state = XR_LOGIN_DONE;
        return LOGINPROTO_SUCCESS;
    case XR_LOGIN_DONE:
        return LOGINPROTO_SUCCESS;
    case XR_LOGIN_ERR:
    default:
        return LOGINPROTO_ERROR;
    }
}

static void
xr_free(void* handle)
{
    free(handle);
}

struct NetLoginVTable const g_xr_login_vtable = {
    .new_ = xr_new,
    .recv = xr_recv,
    .send = xr_send,
    .poll = xr_poll,
    .free_ = xr_free,
};
