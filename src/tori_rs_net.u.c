#ifndef TORI_RS_NET_U_C
#define TORI_RS_NET_U_C

#include "osrs/gameproto_parse.h"
#include "osrs/gameproto_revisions.h"
#include "osrs/loginproto.h"
#include "osrs/packetbuffer.h"
#include "tori_rs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define imin(a, b) ((a) < (b) ? (a) : (b))

NetStatus
LibToriRS_NetGetStatus(struct GGame* game)
{
    assert(game->net_shared);
    return game->net_shared->status;
}

void
LibToriRS_NetSend(
    struct GGame* game,
    const uint8_t* data,
    int size)
{
    assert(game->net_shared);
    assert(data);
    assert(size > 0);
    LibToriRS_NetPush(game->net_shared, TORI_RS_NET_MSG_SEND_DATA, data, size);
}

void
LibToriRS_NetConnectLogin(
    struct GGame* game,
    const char* host,
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
    assert(game->loginproto);

    game->net_state = GAME_NET_STATE_LOGIN;

    LibToriRS_NetPush(
        &game->net_shared->game_to_platform, TORI_RS_NET_MSG_CONNECT, (uint8_t*)host, strlen(host));
}

void
LibToriRS_NetConnectGame(
    struct GGame* game,
    const char* host)
{
    if( !game )
        return;

    packetbuffer_init(game->packet_buffer, game->random_in, GAMEPROTO_REVISION_LC245_2);
    game->net_state = GAME_NET_STATE_GAME;

    LibToriRS_NetPush(
        &game->net_shared->game_to_platform, TORI_RS_NET_MSG_CONNECT, (uint8_t*)host, strlen(host));
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

static void
net_process_packets(struct GGame* game)
{
    if( !game || !game->packet_buffer )
        return;

    // The PacketBuffer has internally handled ISAAC decryption of the opcode
    // and accumulated the full payload.
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

static void
loginproto_drive(struct GGame* game)
{
    if( !game || !game->loginproto )
        return;

    char scratch_out_buffer[4096];

    int poll_result = loginproto_poll(game->loginproto);

    // Flush outbound login bytes (handshakes/credentials)
    int bytes_to_send;
    while( (bytes_to_send = loginproto_send(
                game->loginproto, scratch_out_buffer, sizeof(scratch_out_buffer))) > 0 )
    {
        LibToriRS_NetSend(game, scratch_out_buffer, bytes_to_send);
    }

    if( poll_result == LOGINPROTO_SUCCESS )
    {
        game->net_state = GAME_NET_STATE_GAME;

        loginproto_free(game->loginproto);

        printf("[DEBUG] Login successful\n");
        game->loginproto = NULL;
    }
    else if( poll_result == LOGINPROTO_ERROR )
    {
        game->net_state = GAME_NET_STATE_DISCONNECTED;
    }
}

static int
loginproto_drain(
    struct GGame* game,
    uint8_t* buffer,
    int size)
{
    int remaining = size;
    int consumed = 0;
    do
    {
        consumed = loginproto_recv(game->loginproto, buffer, remaining);

        buffer += consumed;
        remaining -= consumed;

        loginproto_drive(game);
    } while( consumed > 0 && remaining > 0 );

    return size - remaining;
}

static void
gameproto_drive(struct GGame* game)
{
    // No Op
}

static int
gameproto_drain(
    struct GGame* game,
    uint8_t* data,
    int size)
{
    if( !game || !game->packet_buffer )
        return 0;

    int remaining = size;

    int consumed = 0;
    do
    {
        consumed = packetbuffer_read(game->packet_buffer, data, size);
        data += consumed;
        remaining -= consumed;

        net_process_packets(game);
    } while( consumed > 0 && remaining > 0 );

    return size - remaining;
}

static void
net_drain(
    struct GGame* game,
    uint8_t* data,
    int size)
{
    if( !game || !game->net_shared )
        return;

    int remaining = size;
    int consumed = 0;

    do
    {
        switch( game->net_state )
        {
        case GAME_NET_STATE_LOGIN:
            consumed = loginproto_drain(game, data, remaining);
            data += consumed;
            remaining -= consumed;
            break;
        case GAME_NET_STATE_GAME:
            consumed = gameproto_drain(game, data, remaining);
            data += consumed;
            remaining -= consumed;
            break;
        case GAME_NET_STATE_DISCONNECTED:
            break;
        }
    } while( consumed > 0 && remaining > 0 );
}

static void
game_poll(struct GGame* game)
{
    if( !game || !game->packet_buffer )
        return;

    switch( game->net_state )
    {
    case GAME_NET_STATE_LOGIN:
        loginproto_drive(game);
        break;
    case GAME_NET_STATE_GAME:
        gameproto_drive(game);
        break;
    case GAME_NET_STATE_DISCONNECTED:
        break;
    }
}

static void
net_status_check(struct GGame* game)
{
    if( !game || !game->net_shared )
        return;

    if( game->net_shared->status == TORI_RS_NET_STATUS_DISCONNECTED ||
        game->net_shared->status == TORI_RS_NET_STATUS_FAILED )
    {
        game->net_state = GAME_NET_STATE_DISCONNECTED;
    }
}

void
LibToriRS_NetPump(struct GGame* game)
{
    if( !game || !game->net_shared )
        return;

    struct ToriRSNetMessageHeader header;
    uint8_t temp_buffer[4096];

    game_poll(game);

    /* --- STEP 1: Drain Inbound messages from Platform --- */
    while( LibToriRS_NetPop(&game->net_shared->platform_to_game, &header, temp_buffer) )
    {
        switch( header.type )
        {
        case TORI_RS_NET_MSG_RECV_DATA:
            net_drain(game, temp_buffer, header.length);
            break;

        case TORI_RS_NET_MSG_CONN_STATUS:
            net_status_check(game);
            break;
        }
    }
}

#endif
