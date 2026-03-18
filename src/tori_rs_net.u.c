#ifndef TORI_RS_NET_U_C
#define TORI_RS_NET_U_C

#include "osrs/loginproto.h"
#include "tori_rs.h"
#include "tori_rs_net_shared.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define imin(a, b) ((a) < (b) ? (a) : (b))

#define NET_HEADER_SIZE 8

/* Shared ring: buffer[0..3] = write_pos (game), buffer[4..7] = read_pos (platform), payload at
 * buffer+8. payload_cap = capacity - 8; one slot reserved so max storable = payload_cap - 1.
 */
static uint32_t
shared_ring_get_write_pos(const uint8_t* buf)
{
    return (uint32_t)(buf[0]) | ((uint32_t)(buf[1]) << 8) | ((uint32_t)(buf[2]) << 16) |
           ((uint32_t)(buf[3]) << 24);
}

static uint32_t
shared_ring_get_read_pos(const uint8_t* buf)
{
    return (uint32_t)(buf[4]) | ((uint32_t)(buf[5]) << 8) | ((uint32_t)(buf[6]) << 16) |
           ((uint32_t)(buf[7]) << 24);
}

static void
shared_ring_set_write_pos(
    uint8_t* buf,
    uint32_t v)
{
    buf[0] = (uint8_t)(v);
    buf[1] = (uint8_t)(v >> 8);
    buf[2] = (uint8_t)(v >> 16);
    buf[3] = (uint8_t)(v >> 24);
}

static void
shared_ring_set_read_pos(
    uint8_t* buf,
    uint32_t v)
{
    buf[4] = (uint8_t)(v);
    buf[5] = (uint8_t)(v >> 8);
    buf[6] = (uint8_t)(v >> 16);
    buf[7] = (uint8_t)(v >> 24);
}

static int
shared_ring_append(
    uint8_t* buf,
    int capacity,
    const uint8_t* data,
    int size)
{
    if( !buf || capacity < NET_HEADER_SIZE + 1 || !data || size <= 0 )
        return -1;

    const int payload_cap = capacity - NET_HEADER_SIZE;
    uint32_t w = shared_ring_get_write_pos(buf);
    uint32_t r = shared_ring_get_read_pos(buf);
    int used = (int)(w - r + payload_cap) % payload_cap;
    if( used + size > payload_cap - 1 )
        return -1; /* full */

    uint8_t* payload = buf + NET_HEADER_SIZE;
    for( int i = 0; i < size; i++ )
    {
        payload[w % payload_cap] = data[i];
        w = (w + 1) % payload_cap;
    }
    shared_ring_set_write_pos(buf, w);
    return 0;
}

static void
shared_ring_clear(uint8_t* buf)
{
    if( buf )
    {
        memset(buf, 0, NET_HEADER_SIZE);
    }
}

/* Read from ring at buf (capacity = region size). Copy up to out_size bytes into out_buf, advance read_pos. Return bytes read. */
static int
shared_ring_read(
    uint8_t* buf,
    int capacity,
    uint8_t* out_buf,
    int out_size)
{
    if( !buf || capacity < NET_HEADER_SIZE + 1 || !out_buf || out_size <= 0 )
        return 0;
    const int payload_cap = capacity - NET_HEADER_SIZE;
    uint32_t w = shared_ring_get_write_pos(buf);
    uint32_t r = shared_ring_get_read_pos(buf);
    int available = (int)(w - r + payload_cap) % payload_cap;
    if( available <= 0 )
        return 0;
    int to_copy = available < out_size ? available : out_size;
    uint8_t* payload = buf + NET_HEADER_SIZE;
    if( r + to_copy <= (uint32_t)payload_cap )
    {
        memcpy(out_buf, payload + r, (size_t)to_copy);
    }
    else
    {
        int first = payload_cap - (int)r;
        memcpy(out_buf, payload + r, (size_t)first);
        memcpy(out_buf + first, payload, (size_t)(to_copy - first));
    }
    r = (r + to_copy) % payload_cap;
    shared_ring_set_read_pos(buf, r);
    return to_copy;
}

static void
push_packet_lc245_2(
    struct GGame* game,
    struct RevPacket_LC245_2* packet)
{
    struct RevPacket_LC245_2_Item* item = malloc(sizeof(struct RevPacket_LC245_2_Item));
    memset(item, 0, sizeof(struct RevPacket_LC245_2_Item));

    item->packet = *packet;
    if( !game->packets_lc245_2 )
    {
        game->packets_lc245_2 = item;
    }
    else
    {
        struct RevPacket_LC245_2_Item* list = game->packets_lc245_2;
        while( list->next_nullable )
        {
            list = list->next_nullable;
        }
        list->next_nullable = item;
    }
}

/* Append one outbound message [type:1][len_lo:1][len_hi:1][payload]. Returns 0 on success, -1 if full or invalid. */
static int
shared_ring_append_message(
    uint8_t* buf,
    int capacity,
    uint8_t type,
    const uint8_t* payload,
    int payload_size)
{
    if( payload_size < 0 || payload_size > 65535 )
        return -1;
    const int total = LIBTORIRS_NET_MSG_HEADER_SIZE + payload_size;
    if( total <= 0 )
        return -1;
    uint8_t header[LIBTORIRS_NET_MSG_HEADER_SIZE];
    header[0] = type;
    header[1] = (uint8_t)(payload_size & 0xff);
    header[2] = (uint8_t)(payload_size >> 8);
    if( shared_ring_append(buf, capacity, header, LIBTORIRS_NET_MSG_HEADER_SIZE) != 0 )
        return -1;
    if( payload_size > 0 && shared_ring_append(buf, capacity, payload, payload_size) != 0 )
        return -1;
    return 0;
}

/* Write protocol output to game outbound (shared ring) as a DATA message. */
static void
net_write_outbound(
    struct GGame* game,
    const uint8_t* data,
    int size)
{
    if( game->net_shared && game->net_shared->out_buf && game->net_shared->out_cap >= NET_HEADER_SIZE + 1 && size > 0 )
    {
        shared_ring_append_message(
            game->net_shared->out_buf,
            game->net_shared->out_cap,
            LIBTORIRS_NET_MSG_DATA,
            data,
            size);
    }
}

NetStatus
LibToriRS_NetGetStatus(struct GGame* game)
{
    if( !game )
        return NET_IDLE;
    return (NetStatus)game->net_status;
}

void
LibToriRS_NetSharedFromBuffer(
    LibToriRS_NetShared* out,
    uint8_t* buf,
    int capacity)
{
    if( !out || !buf || capacity < NET_HEADER_SIZE * 2 )
        return;
    int half = capacity / 2;
    out->out_buf = buf;
    out->out_cap = half;
    out->in_buf = buf + half;
    out->in_cap = half;
}

LibToriRS_NetContext*
LibToriRS_NetNewContext(void)
{
    LibToriRS_NetContext* ctx = (LibToriRS_NetContext*)malloc(sizeof(LibToriRS_NetContext));
    if( !ctx )
        return NULL;
    memset(ctx, 0, sizeof(LibToriRS_NetContext));
    ctx->p2g_cap = LIBTORIRS_NET_DEFAULT_RING_SIZE;
    ctx->g2p_cap = LIBTORIRS_NET_DEFAULT_RING_SIZE;
    ctx->p2g_buf = (uint8_t*)malloc((size_t)ctx->p2g_cap);
    ctx->g2p_buf = (uint8_t*)malloc((size_t)ctx->g2p_cap);
    if( !ctx->p2g_buf || !ctx->g2p_buf )
    {
        free(ctx->p2g_buf);
        free(ctx->g2p_buf);
        free(ctx);
        return NULL;
    }
    memset(ctx->p2g_buf, 0, (size_t)ctx->p2g_cap);
    memset(ctx->g2p_buf, 0, (size_t)ctx->g2p_cap);
    ctx->shared_view.in_buf = ctx->p2g_buf;
    ctx->shared_view.in_cap = ctx->p2g_cap;
    ctx->shared_view.out_buf = ctx->g2p_buf;
    ctx->shared_view.out_cap = ctx->g2p_cap;
    return ctx;
}

void
LibToriRS_NetFreeContext(LibToriRS_NetContext* ctx)
{
    if( !ctx )
        return;
    if( ctx->game )
        ctx->game->net_shared = NULL;
    free(ctx->p2g_buf);
    free(ctx->g2p_buf);
    free(ctx);
}

void
LibToriRS_NetInit(
    struct GGame* game,
    LibToriRS_NetContext* ctx)
{
    if( !game )
        return;
    if( !ctx )
    {
        game->net_shared = NULL;
        return;
    }
    ctx->game = game;
    game->net_shared = &ctx->shared_view;
    ctx->state = (int)NET_IDLE;
    shared_ring_clear(ctx->p2g_buf);
    shared_ring_clear(ctx->g2p_buf);
}

void
LibToriRS_NetPoll(LibToriRS_NetContext* ctx)
{
    if( !ctx || !ctx->game )
        return;
    if( ctx->state != ctx->game->net_status )
        LibToriRS_OnStatusChange(ctx->game, (NetStatus)ctx->state);
    LibToriRS_NetProcessInbound(ctx->game);
}


void
LibToriRS_NetSend(
    struct GGame* game,
    const uint8_t* data,
    int size)
{
    if( !game || !data || size <= 0 )
        return;
    if( game->net_shared && game->net_shared->out_buf && game->net_shared->out_cap >= NET_HEADER_SIZE + 1 )
    {
        shared_ring_append_message(
            game->net_shared->out_buf,
            game->net_shared->out_cap,
            LIBTORIRS_NET_MSG_DATA,
            data,
            size);
    }
}

void
LibToriRS_NetSendReconnect(struct GGame* game)
{
    if( !game )
        return;
    if( game->net_shared && game->net_shared->out_buf && game->net_shared->out_cap >= NET_HEADER_SIZE + 1 )
    {
        shared_ring_append_message(
            game->net_shared->out_buf,
            game->net_shared->out_cap,
            LIBTORIRS_NET_MSG_RECONNECT,
            NULL,
            0);
    }
}

void
LibToriRS_OnMessage(
    struct GGame* game,
    const uint8_t* data,
    int size)
{
    if( !game || !data || size <= 0 )
        return;
    int ret = ringbuf_write(game->netin, data, (size_t)size);
    assert(ret == RINGBUF_OK);
}

void
LibToriRS_OnStatusChange(
    struct GGame* game,
    NetStatus status)
{
    if( !game )
        return;
    game->net_status = (int)status;
    ringbuf_clear(game->netin);
    if( game->net_shared && game->net_shared->out_buf && game->net_shared->out_cap >= NET_HEADER_SIZE )
        shared_ring_clear(game->net_shared->out_buf);
    if( game->net_shared && game->net_shared->in_buf && game->net_shared->in_cap >= NET_HEADER_SIZE )
        shared_ring_clear(game->net_shared->in_buf);
    game->net_state = GAME_NET_STATE_DISCONNECTED;
}

static void
process_login_state(struct GGame* game)
{
    if( !game->loginproto )
        return;

    uint8_t contiguous_buffer[4096];
    int to_read = game->loginproto->await_recv_cnt > 0
                      ? imin(game->loginproto->await_recv_cnt, (int)sizeof(contiguous_buffer))
                      : 0;

    if( to_read > 0 )
    {
        int read_cnt;
        if( game->net_shared && game->net_shared->in_buf && game->net_shared->in_cap >= NET_HEADER_SIZE + 1 )
            read_cnt = shared_ring_read(
                game->net_shared->in_buf, game->net_shared->in_cap, contiguous_buffer, to_read);
        else
            read_cnt = (int)ringbuf_read(game->netin, contiguous_buffer, (size_t)to_read);
        if( read_cnt > 0 )
        {
            loginproto_recv(game->loginproto, contiguous_buffer, read_cnt);
        }
    }

    int proto = loginproto_poll(game->loginproto);
    int send_cnt = loginproto_send(game->loginproto, contiguous_buffer, sizeof(contiguous_buffer));
    if( send_cnt > 0 )
    {
        net_write_outbound(game, contiguous_buffer, send_cnt);
    }

    if( proto == LOGINPROTO_SUCCESS )
    {
        packetbuffer_init(game->packet_buffer, game->random_in, GAMEPROTO_REVISION_LC245_2);
        game->net_state = GAME_NET_STATE_GAME;
    }
}

static void
process_game_state(struct GGame* game)
{
    uint8_t contiguous_buffer[4096];
    int to_read =
        imin(packetbuffer_amt_recv_cnt(game->packet_buffer), (int)sizeof(contiguous_buffer));

    if( to_read <= 0 )
        return;

    int read_cnt;
    if( game->net_shared && game->net_shared->in_buf && game->net_shared->in_cap >= NET_HEADER_SIZE + 1 )
        read_cnt = shared_ring_read(
            game->net_shared->in_buf, game->net_shared->in_cap, contiguous_buffer, to_read);
    else
        read_cnt = (int)ringbuf_read(game->netin, contiguous_buffer, (size_t)to_read);
    if( read_cnt <= 0 )
        return;

    int pkt_read_cnt = packetbuffer_read(game->packet_buffer, contiguous_buffer, read_cnt);
    assert(pkt_read_cnt == read_cnt);

    if( !packetbuffer_ready(game->packet_buffer) )
        return;

    struct RevPacket_LC245_2 packet;
    memset(&packet, 0, sizeof(struct RevPacket_LC245_2));
    int success = gameproto_parse_lc245_2(
        packetbuffer_packet_type(game->packet_buffer),
        packetbuffer_data(game->packet_buffer),
        packetbuffer_size(game->packet_buffer),
        &packet);

    if( success )
        push_packet_lc245_2(game, &packet);

    packetbuffer_reset(game->packet_buffer);
}

void
LibToriRS_NetConnectLogin(
    struct GGame* game,
    const char* username,
    const char* password)
{
    if( !game )
        return;

    if( username )
    {
        size_t ulen = strlen(username);
        if( ulen >= sizeof(game->login_username) )
            ulen = sizeof(game->login_username) - 1;
        memcpy(game->login_username, username, ulen);
        game->login_username[ulen] = '\0';
    }
    else
        game->login_username[0] = '\0';

    if( password )
    {
        size_t plen = strlen(password);
        if( plen >= sizeof(game->login_password) )
            plen = sizeof(game->login_password) - 1;
        memcpy(game->login_password, password, plen);
        game->login_password[plen] = '\0';
    }
    else
        game->login_password[0] = '\0';

    if( game->loginproto )
    {
        loginproto_free(game->loginproto);
        game->loginproto = NULL;
    }

    game->loginproto = loginproto_new(
        game->random_in,
        game->random_out,
        &game->rsa,
        game->login_username,
        game->login_password,
        NULL);

    game->net_state = GAME_NET_STATE_LOGIN;
    game->net_status = (int)NET_CONNECTING;
}

void
LibToriRS_NetConnectGame(struct GGame* game)
{
    if( !game )
        return;

    packetbuffer_init(game->packet_buffer, game->random_in, GAMEPROTO_REVISION_LC245_2);
    game->net_state = GAME_NET_STATE_GAME;
}

void
LibToriRS_NetProcessInbound(struct GGame* game)
{
    if( !game )
        return;

    switch( game->net_state )
    {
    case GAME_NET_STATE_DISCONNECTED:
        break;
    case GAME_NET_STATE_LOGIN:
        process_login_state(game);
        break;
    case GAME_NET_STATE_GAME:
        process_game_state(game);
        break;
    }
}

#endif
