#ifndef TORI_RS_NET_U_C
#define TORI_RS_NET_U_C

#include "osrs/loginproto.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define imin(a, b) ((a) < (b) ? (a) : (b))

int
LibToriRS_NetIsReady(struct GGame* game)
{
    return game->scene != NULL;
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

int
LibToriRS_NetPump(struct GGame* game)
{
    int read_cnt = 1;
    char contiguous_buffer[4096];

    while( read_cnt > 0 )
    {
        switch( game->net_state )
        {
        case GAME_NET_STATE_DISCONNECTED:
            read_cnt = 1;
            game->net_state = GAME_NET_STATE_LOGIN;
            break;
        case GAME_NET_STATE_LOGIN:
        {
            int to_read_cnt = imin(game->loginproto->await_recv_cnt, sizeof(contiguous_buffer));

            read_cnt = ringbuf_read(game->netin, contiguous_buffer, to_read_cnt);

            loginproto_recv(game->loginproto, contiguous_buffer, read_cnt);
            int proto = loginproto_poll(game->loginproto);
            int send_cnt =
                loginproto_send(game->loginproto, contiguous_buffer, sizeof(contiguous_buffer));
            if( send_cnt > 0 )
            {
                ringbuf_write(game->netout, contiguous_buffer, send_cnt);
            }

            if( proto == LOGINPROTO_SUCCESS )
            {
                packetbuffer_init(game->packet_buffer, game->random_in, GAMEPROTO_REVISION_LC245_2);
                game->net_state = GAME_NET_STATE_GAME;
            }
            else
                read_cnt = 0;
        }
        break;
        case GAME_NET_STATE_GAME:
        {
            int to_read_cnt =
                imin(packetbuffer_amt_recv_cnt(game->packet_buffer), sizeof(contiguous_buffer));

            read_cnt = ringbuf_read(game->netin, contiguous_buffer, to_read_cnt);
            if( read_cnt == 0 )
                break;

            int pkt_read_cnt = packetbuffer_read(game->packet_buffer, contiguous_buffer, read_cnt);
            assert(pkt_read_cnt == read_cnt);

            if( packetbuffer_ready(game->packet_buffer) )
            {
                struct RevPacket_LC245_2 packet;
                memset(&packet, 0, sizeof(struct RevPacket_LC245_2));
                int success = 0;

                success = gameproto_parse_lc245_2(
                    packetbuffer_packet_type(game->packet_buffer),
                    packetbuffer_data(game->packet_buffer),
                    packetbuffer_size(game->packet_buffer),
                    &packet);

                if( success )
                    push_packet_lc245_2(game, &packet);

                packetbuffer_reset(game->packet_buffer);
            }
        }
        break;
        }
    }

    return 0;
}

void
LibToriRS_NetConnect(
    struct GGame* game,
    char* username,
    char* password)
{
    game->net_state = GAME_NET_STATE_LOGIN;
}

void
LibToriRS_NetRecv(
    struct GGame* game,
    uint8_t* data,
    int data_size)
{
    int ret = ringbuf_write(game->netin, data, data_size);
    assert(ret == RINGBUF_OK);
}

void
LibToriRS_NetDisconnected(struct GGame* game)
{
    ringbuf_clear(game->netin);
    ringbuf_clear(game->netout);
    game->net_state = GAME_NET_STATE_DISCONNECTED;
}

int
LibToriRS_NetGetOutgoing(
    struct GGame* game,
    uint8_t* buffer,
    int buffer_size)
{
    int outbound_cnt = 0;
    outbound_cnt = ringbuf_read(game->netout, buffer, buffer_size);
    return outbound_cnt;
}

#endif