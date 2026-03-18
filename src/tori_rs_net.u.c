#ifndef TORI_RS_NET_U_C
#define TORI_RS_NET_U_C

#include "osrs/loginproto.h"
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

    char const* host = "127.0.0.1:45454";
    LibToriRS_NetPush(
        &game->net_shared->game_to_platform, TORI_RS_NET_MSG_CONNECT, (uint8_t*)host, strlen(host));
}

static void
LibToriRS_NetProcessPackets(struct GGame* game)
{
    if( !game || !game->packet_buffer )
        return;

    // The PacketBuffer has internally handled ISAAC decryption of the opcode
    // and accumulated the full payload.
    while( packetbuffer_ready(game->packet_buffer) )
    {
        int opcode = packetbuffer_packet_type(game->packet_buffer);
        int len = packetbuffer_size(game->packet_buffer);
        uint8_t* payload = (uint8_t*)packetbuffer_data(game->packet_buffer);

        // --- Route Packet to Game Logic ---
        // Example: if (opcode == 123) { handle_player_update(game, payload, len); }

        // Clear the state machine to start parsing the next opcode in the stream
        packetbuffer_reset(game->packet_buffer);
    }
}

void
LibToriRS_NetPump(struct GGame* game)
{
    if( !game || !game->net_shared )
        return;

    struct ToriRSNetMessageHeader header;
    uint8_t temp_buffer[4096];

    /* --- STEP 1: Drain Inbound messages from Platform --- */
    while( LibToriRS_NetPop(&game->net_shared->platform_to_game, &header, temp_buffer) )
    {
        switch( header.type )
        {
        case TORI_RS_NET_MSG_RECV_DATA:
        {
            if( game->net_state == GAME_NET_STATE_LOGIN && game->loginproto )
            {
                loginproto_recv(game->loginproto, temp_buffer, header.length);
            }
            else if( game->net_state == GAME_NET_STATE_GAME && game->packet_buffer )
            {
                // Feed raw bytes into the packet stream processor
                packetbuffer_read(game->packet_buffer, temp_buffer, header.length);
            }
            break;
        }

        case TORI_RS_NET_MSG_CONN_STATUS:
        {
            if( game->net_shared->status == TORI_RS_NET_STATUS_DISCONNECTED ||
                game->net_shared->status == TORI_RS_NET_STATUS_FAILED )
            {
                game->net_state = GAME_NET_STATE_DISCONNECTED;
            }
            break;
        }
        }
    }

    /* --- STEP 2: Progress Logic & Handle Outbound --- */
    if( game->net_state == GAME_NET_STATE_LOGIN && game->loginproto )
    {
        int poll_result = loginproto_poll(game->loginproto);

        // Flush outbound login bytes (handshakes/credentials)
        int bytes_to_send;
        while( (bytes_to_send =
                    loginproto_send(game->loginproto, temp_buffer, sizeof(temp_buffer))) > 0 )
        {
            LibToriRS_NetSend(game, temp_buffer, bytes_to_send);
        }

        if( poll_result == LOGINPROTO_SUCCESS )
        {
            game->net_state = GAME_NET_STATE_GAME;
            loginproto_free(game->loginproto);
            game->loginproto = NULL;
        }
        else if( poll_result == LOGINPROTO_ERROR )
        {
            game->net_state = GAME_NET_STATE_DISCONNECTED;
        }
    }
    else if( game->net_state == GAME_NET_STATE_GAME )
    {
        LibToriRS_NetProcessPackets(game);
    }
}

void
LibToriRS_NetConnectGame(struct GGame* game)
{
    if( !game )
        return;

    packetbuffer_init(game->packet_buffer, game->random_in, GAMEPROTO_REVISION_LC245_2);
    game->net_state = GAME_NET_STATE_GAME;
}

#endif
