#include "net.h"

#include "cmd/cmdbus.h"
#include "rev/lc245_2/gameproto_parse.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
ToriRS_Network_Init(
    struct ToriRS_Network* net,
    struct GameProtoRevTable const* rev,
    char const* rsa_exp_hex,
    char const* rsa_mod_hex)
{
    assert(net && rev);
    memset(net, 0, sizeof(*net));
    net->state = TORIRS_NET_DISCONNECTED;
    net->rev = rev;
    net->random_in = isaac_new(NULL, 0);
    net->random_out = isaac_new(NULL, 0);
    net->conn_status = TORIRS_NET_STATUS_DISCONNECTED;
    CmdRing_Init(&net->out);

    if( rsa_exp_hex && rsa_mod_hex )
        net->rsa_ready = (rsa_init(&net->rsa, rsa_exp_hex, rsa_mod_hex) == 0);
}

void
ToriRS_Network_SetSeedFn(
    struct ToriRS_Network* net,
    loginproto_seed_fn seed_fn,
    void* user)
{
    assert(net);
    net->seed_fn = seed_fn;
    net->seed_user = user;
    if( net->loginproto )
        loginproto_set_seed_fn(net->loginproto, seed_fn, user);
}

static void
free_packet_list(struct ToriRS_Network* net)
{
    struct RevPacket_LC245_2_Item* item = net->packets_head;
    while( item )
    {
        struct RevPacket_LC245_2_Item* next = item->next_nullable;
        gameproto_free_lc245_2(&item->packet);
        free(item);
        item = next;
    }
    net->packets_head = NULL;
    net->packets_tail = NULL;
}

void
ToriRS_Network_Free(struct ToriRS_Network* net)
{
    if( !net )
        return;
    if( net->loginproto )
    {
        loginproto_free(net->loginproto);
        net->loginproto = NULL;
    }
    if( net->packet_buffer.data )
        packetbuffer_reset(&net->packet_buffer);
    free_packet_list(net);
    if( net->random_in )
        isaac_free(net->random_in);
    if( net->random_out )
        isaac_free(net->random_out);
    net->random_in = NULL;
    net->random_out = NULL;
}

static void
loginproto_drive(struct ToriRS_Network* net);

static void
push_out(
    struct ToriRS_Network* net,
    uint32_t type,
    uint8_t const* data,
    int len)
{
    CmdRing_Push(&net->out, type, data, (uint16_t)len);
}

void
ToriRS_Network_ConnectLogin(
    struct ToriRS_Network* net,
    char const* host,
    char const* username,
    char const* password)
{
    assert(net);

    strncpy(net->host, host ? host : "", sizeof(net->host) - 1);
    strncpy(net->username, username ? username : "", sizeof(net->username) - 1);
    strncpy(net->password, password ? password : "", sizeof(net->password) - 1);

    if( net->loginproto )
    {
        loginproto_free(net->loginproto);
        net->loginproto = NULL;
    }

    net->loginproto = loginproto_new(
        net->random_in, net->random_out, &net->rsa, net->rev, net->username, net->password);
    assert(net->loginproto);
    if( net->seed_fn )
        loginproto_set_seed_fn(net->loginproto, net->seed_fn, net->seed_user);

    net->state = TORIRS_NET_LOGIN;
    push_out(net, TORIRS_NET_OUT_CONNECT, (uint8_t const*)net->host, (int)strlen(net->host));

    /* Run SEND_CONNECT now (v0 drove the login machine from game_poll before
     * the first drain): emits the connect opcode and arms await_recv_cnt so
     * the first inbound bytes are consumed rather than dropped. */
    loginproto_drive(net);
}

void
ToriRS_Network_SendRaw(
    struct ToriRS_Network* net,
    uint8_t const* data,
    int len)
{
    assert(net);
    if( len > 0 && data )
        push_out(net, TORIRS_NET_OUT_SEND_DATA, data, len);
}

/* Flush login outbound bytes, then act on the poll result (port of v0
 * loginproto_drive): on success free the login machine and arm the packet
 * buffer for the game stream — fixing v0's missing packetbuffer_init. */
static void
loginproto_drive(struct ToriRS_Network* net)
{
    uint8_t scratch[4096];
    int poll_result;
    int bytes;

    if( !net->loginproto )
        return;

    poll_result = loginproto_poll(net->loginproto);

    while( (bytes = loginproto_send(net->loginproto, scratch, sizeof(scratch))) > 0 )
        ToriRS_Network_SendRaw(net, scratch, bytes);

    if( poll_result == LOGINPROTO_SUCCESS )
    {
        loginproto_free(net->loginproto);
        net->loginproto = NULL;
        packetbuffer_init(&net->packet_buffer, net->random_in, net->rev);
        net->state = TORIRS_NET_GAME;
    }
    else if( poll_result == LOGINPROTO_ERROR )
    {
        net->state = TORIRS_NET_DISCONNECTED;
    }
}

static int
loginproto_drain(
    struct ToriRS_Network* net,
    uint8_t const* buffer,
    int size)
{
    int remaining = size;
    int consumed;

    do
    {
        consumed = loginproto_recv(net->loginproto, (uint8_t*)buffer, remaining);
        buffer += consumed;
        remaining -= consumed;
        loginproto_drive(net);
    } while( consumed > 0 && remaining > 0 && net->state == TORIRS_NET_LOGIN );

    return size - remaining;
}

static void
push_parsed_packet(
    struct ToriRS_Network* net,
    struct RevPacket_LC245_2 const* packet)
{
    struct RevPacket_LC245_2_Item* item = malloc(sizeof(*item));
    assert(item);
    item->packet = *packet;
    item->next_nullable = NULL;
    if( net->packets_tail )
        net->packets_tail->next_nullable = item;
    else
        net->packets_head = item;
    net->packets_tail = item;
}

static void
net_process_packets(struct ToriRS_Network* net)
{
    if( !packetbuffer_ready(&net->packet_buffer) )
        return;

    {
        struct RevPacket_LC245_2 packet;
        memset(&packet, 0, sizeof(packet));
        if( gameproto_parse_lc245_2(
                packetbuffer_packet_type(&net->packet_buffer),
                packetbuffer_data(&net->packet_buffer),
                packetbuffer_size(&net->packet_buffer),
                &packet) )
            push_parsed_packet(net, &packet);
    }
    packetbuffer_reset(&net->packet_buffer);
}

static int
gameproto_drain(
    struct ToriRS_Network* net,
    uint8_t const* data,
    int size)
{
    int remaining = size;
    int consumed;

    do
    {
        consumed = packetbuffer_read(&net->packet_buffer, (uint8_t*)data, remaining);
        data += consumed;
        remaining -= consumed;
        net_process_packets(net);
    } while( consumed > 0 && remaining > 0 );

    return size - remaining;
}

static void
net_drain(
    struct ToriRS_Network* net,
    uint8_t const* data,
    int size)
{
    int remaining = size;
    int consumed;

    do
    {
        consumed = 0;
        switch( net->state )
        {
        case TORIRS_NET_LOGIN:
            consumed = loginproto_drain(net, data, remaining);
            break;
        case TORIRS_NET_GAME:
            consumed = gameproto_drain(net, data, remaining);
            break;
        case TORIRS_NET_DISCONNECTED:
            break;
        }
        data += consumed;
        remaining -= consumed;
    } while( consumed > 0 && remaining > 0 );
}

void
ToriRS_Network_HandleCmd(
    struct ToriRS_Network* net,
    uint32_t cmd_type,
    uint8_t const* data,
    int len)
{
    assert(net);

    switch( cmd_type )
    {
    case TORIRS_CMD_NET_RECV:
        if( len > 0 && data )
            net_drain(net, data, len);
        break;
    case TORIRS_CMD_NET_STATUS:
        if( len >= (int)sizeof(int32_t) )
        {
            int32_t status;
            memcpy(&status, data, sizeof(status));
            net->conn_status = status;
            if( status == TORIRS_NET_STATUS_DISCONNECTED ||
                status == TORIRS_NET_STATUS_FAILED )
                net->state = TORIRS_NET_DISCONNECTED;
        }
        break;
    case TORIRS_CMD_NET_CONNECT:
        /* Producer-only echo; ConnectLogin drives the real transition. */
        break;
    default:
        break;
    }
}

int
ToriRS_Network_PopPacket(
    struct ToriRS_Network* net,
    struct RevPacket_LC245_2* out)
{
    struct RevPacket_LC245_2_Item* item;

    assert(net && out);
    item = net->packets_head;
    if( !item )
        return 0;

    net->packets_head = item->next_nullable;
    if( !net->packets_head )
        net->packets_tail = NULL;

    *out = item->packet; /* heap fields transfer to the caller */
    free(item);
    return 1;
}

int
ToriRS_Network_PopOut(
    struct ToriRS_Network* net,
    struct ToriRS_CmdHeader* header,
    uint8_t* payload)
{
    assert(net && header && payload);
    return CmdRing_Pop(&net->out, header, payload);
}
