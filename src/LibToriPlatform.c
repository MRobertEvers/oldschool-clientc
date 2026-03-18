#include "LibToriPlatform.h"
#include "platforms/common/sockstream.h"

#include <string.h>

#define NET_HEADER_SIZE 8

static uint32_t
ring_get_write_pos(const uint8_t* buf)
{
    return (uint32_t)(buf[0]) | ((uint32_t)(buf[1]) << 8) | ((uint32_t)(buf[2]) << 16) |
           ((uint32_t)(buf[3]) << 24);
}

static uint32_t
ring_get_read_pos(const uint8_t* buf)
{
    return (uint32_t)(buf[4]) | ((uint32_t)(buf[5]) << 8) | ((uint32_t)(buf[6]) << 16) |
           ((uint32_t)(buf[7]) << 24);
}

static void
ring_set_read_pos(uint8_t* buf, uint32_t v)
{
    buf[4] = (uint8_t)(v);
    buf[5] = (uint8_t)(v >> 8);
    buf[6] = (uint8_t)(v >> 16);
    buf[7] = (uint8_t)(v >> 24);
}

static void
ring_set_write_pos(uint8_t* buf, uint32_t v)
{
    buf[0] = (uint8_t)(v);
    buf[1] = (uint8_t)(v >> 8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
}

/* Append data to ring at buf (capacity = region size). Returns 0 on success, -1 if full (caller may drop). */
static int
ring_append(uint8_t* buf, int capacity, const uint8_t* data, int size)
{
    if( !buf || capacity < NET_HEADER_SIZE + 1 || !data || size <= 0 )
        return -1;
    const int payload_cap = capacity - NET_HEADER_SIZE;
    uint32_t w = ring_get_write_pos(buf);
    uint32_t r = ring_get_read_pos(buf);
    int used = (int)(w - r + payload_cap) % payload_cap;
    if( used + size > payload_cap - 1 )
        return -1; /* full */
    uint8_t* payload = buf + NET_HEADER_SIZE;
    for( int i = 0; i < size; i++ )
    {
        payload[w % payload_cap] = data[i];
        w = (w + 1) % payload_cap;
    }
    ring_set_write_pos(buf, w);
    return 0;
}

static int
ring_available(const uint8_t* buf, int capacity)
{
    if( !buf || capacity < NET_HEADER_SIZE + 1 )
        return 0;
    const int payload_cap = capacity - NET_HEADER_SIZE;
    uint32_t w = ring_get_write_pos(buf);
    uint32_t r = ring_get_read_pos(buf);
    return (int)(w - r + payload_cap) % payload_cap;
}

static void
ring_advance(uint8_t* buf, int capacity, int n)
{
    if( !buf || capacity < NET_HEADER_SIZE + 1 || n <= 0 )
        return;
    const int payload_cap = capacity - NET_HEADER_SIZE;
    uint32_t r = ring_get_read_pos(buf);
    r = (r + (uint32_t)n) % (uint32_t)payload_cap;
    ring_set_read_pos(buf, r);
}

/* Peek up to size bytes at read_pos into out, without advancing. Returns bytes copied. */
static int
ring_peek(const uint8_t* buf, int capacity, uint8_t* out, int size)
{
    if( !buf || capacity < NET_HEADER_SIZE + 1 || !out || size <= 0 )
        return 0;
    const int payload_cap = capacity - NET_HEADER_SIZE;
    int avail = ring_available(buf, capacity);
    int to_copy = avail < size ? avail : size;
    if( to_copy <= 0 )
        return 0;
    uint32_t r = ring_get_read_pos(buf);
    const uint8_t* payload = buf + NET_HEADER_SIZE;
    for( int i = 0; i < to_copy; i++ )
    {
        out[i] = payload[r % payload_cap];
        r++;
    }
    return to_copy;
}

/* Read exactly n bytes from ring into out and advance. Returns 0 on success, -1 if not enough data. */
static int
ring_read_exact(uint8_t* buf, int capacity, uint8_t* out, int n)
{
    if( ring_available(buf, capacity) < n )
        return -1;
    int got = ring_peek(buf, capacity, out, n);
    if( got != n )
        return -1;
    ring_advance(buf, capacity, n);
    return 0;
}

static void
drain_outbound(
    struct SockStream* stream,
    uint8_t* out_buf,
    int out_cap,
    int* out_reconnect_requested)
{
    if( !out_buf || out_cap < NET_HEADER_SIZE + 1 )
        return;
    if( out_reconnect_requested )
        *out_reconnect_requested = 0;
    const int payload_cap = out_cap - NET_HEADER_SIZE;
    uint8_t header[LIBTORIRS_NET_MSG_HEADER_SIZE];
#define DRAIN_SCRATCH_SIZE 4096
    uint8_t scratch[DRAIN_SCRATCH_SIZE];

    while( ring_available(out_buf, out_cap) >= LIBTORIRS_NET_MSG_HEADER_SIZE )
    {
        if( ring_peek(out_buf, out_cap, header, LIBTORIRS_NET_MSG_HEADER_SIZE) != LIBTORIRS_NET_MSG_HEADER_SIZE )
            break;
        uint8_t type = header[0];
        int len = (int)(header[1] | (header[2] << 8));
        if( len < 0 || len > 65535 )
        {
            ring_advance(out_buf, out_cap, LIBTORIRS_NET_MSG_HEADER_SIZE);
            continue;
        }
        if( type == LIBTORIRS_NET_MSG_RECONNECT )
        {
            ring_advance(out_buf, out_cap, LIBTORIRS_NET_MSG_HEADER_SIZE);
            if( out_reconnect_requested )
                *out_reconnect_requested = 1;
            continue;
        }
        if( type == LIBTORIRS_NET_MSG_DATA )
        {
            if( ring_available(out_buf, out_cap) < LIBTORIRS_NET_MSG_HEADER_SIZE + len )
                break;
            ring_advance(out_buf, out_cap, LIBTORIRS_NET_MSG_HEADER_SIZE);
            while( len > 0 )
            {
                int chunk = len < DRAIN_SCRATCH_SIZE ? len : DRAIN_SCRATCH_SIZE;
                if( ring_read_exact(out_buf, out_cap, scratch, chunk) != 0 )
                    break;
                sockstream_send(stream, scratch, chunk);
                len -= chunk;
            }
            continue;
        }
        ring_advance(out_buf, out_cap, LIBTORIRS_NET_MSG_HEADER_SIZE);
    }
#undef DRAIN_SCRATCH_SIZE
}

int
LibToriPlatform_NetPoll(
    struct SockStream* stream,
    const LibToriRS_NetShared* shared,
    uint8_t* recv_scratch,
    int recv_scratch_size,
    int* out_reconnect_requested)
{
    if( !stream || !sockstream_is_connected(stream) || !shared || !recv_scratch || recv_scratch_size <= 0 )
        return LibToriPlatform_NetPoll_NotConnected;

    drain_outbound(stream, shared->out_buf, shared->out_cap, out_reconnect_requested);

    int received = sockstream_recv(stream, recv_scratch, recv_scratch_size);
    if( received > 0 && shared->in_buf && shared->in_cap >= NET_HEADER_SIZE + 1 )
    {
        if( ring_append(shared->in_buf, shared->in_cap, recv_scratch, received) != 0 )
        {
            /* Inbound ring full; drop this chunk (plan: prefer not to block). */
        }
    }
    else if( received < 0 )
    {
        int err = sockstream_lasterror(stream);
        if( err != SOCKSTREAM_ERROR_WOULDBLOCK )
            return LibToriPlatform_NetPoll_Error;
    }
    return LibToriPlatform_NetPoll_Ok;
}
