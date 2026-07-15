#include "LibToriRSPlatformC.h"

#include "tori_rs.h"

#include <stdio.h>
#include <string.h>

void
LibToriRSPlatformC_NetInit(struct SockStream* stream_ptr)
{
    if( !stream_ptr )
        return;

    sockstream_init();
}

static void
drain_pending(
    struct ToriRSNetSharedBuffer* sb,
    struct SockStream* stream_ptr)
{
    if( !sb )
        return;

    struct ToriRSNetMessageHeader header;
    uint8_t payload[4096];
    while( LibToriRS_NetPop(&sb->pending, &header, payload) )
    {
        sockstream_send(stream_ptr, payload, header.length);
    }
}

/**
 * Helper to completely reset a ring buffer's pointers.
 */
static void
clear_pending(struct ToriRSNetSharedBuffer* sb)
{
    sb->pending.head = 0;
    sb->pending.tail = 0;
}

void
LibToriRSPlatformC_NetPoll(
    struct ToriRSNetSharedBuffer* sb,
    struct SockStream* stream_ptr)
{
    if( !sb )
        return;

    struct ToriRSNetMessageHeader header = { 0 };
    uint8_t payload[4096] = { 0 };

    // --- 1. Process Game -> Platform (Outgoing Commands) ---
    while( LibToriRS_NetPop(&sb->game_to_platform, &header, payload) )
    {
        if( header.type == TORI_RS_NET_MSG_CONNECT )
        {
            // Close existing if we are already connected/connecting
            sockstream_close(stream_ptr);

            // Extract host/port from payload (Assuming simple "host:port" or just host)
            // For this example, we use your existing port constant
            payload[header.length] = '\0';
            printf("Connecting to %s\n", (char*)payload);
            // Parse "<ip>:<port>" string (default to 43594 if port missing)
            char* host = (char*)payload;
            int port = 43594; // default port
            char* colon = strchr(host, ':');
            if( colon )
            {
                *colon = '\0';          // Split host and port
                port = atoi(colon + 1); // Convert port string to int
            }
            sockstream_connect(stream_ptr, (char*)payload, port, 5);

            sb->status = NET_CONNECTING;
        }
        else if( header.type == TORI_RS_NET_MSG_SEND_DATA )
        {
            if( sb->status == NET_CONNECTED )
            {
                // Send immediately if connected
                sockstream_send(stream_ptr, payload, header.length);
            }
            else if( sb->status == NET_CONNECTING )
            {
                if( !LibToriRS_NetPush(
                        &sb->pending, TORI_RS_NET_MSG_SEND_DATA, payload, header.length) )
                {
                    printf("[Platform] Warning: Pending buffer full, dropping message.\n");
                }
            }
        }
    }

    // --- 2. Process Network -> Game (Incoming Data & Status) ---
    // If we are waiting for the non-blocking connect to finish
    if( !sockstream_is_connected(stream_ptr) )
    {
        int connect_result = sockstream_poll_connect(stream_ptr);
        if( connect_result == SOCKSTREAM_CONNECT_SUCCESS )
        {
            sb->status = NET_CONNECTED;
            LibToriRS_NetPush(&sb->platform_to_game, TORI_RS_NET_MSG_CONN_STATUS, NULL, 0);
            drain_pending(sb, stream_ptr);
        }
        else if( connect_result == SOCKSTREAM_CONNECT_FAILED )
        {
            sb->status = NET_ERROR;
            LibToriRS_NetPush(&sb->platform_to_game, TORI_RS_NET_MSG_CONN_STATUS, NULL, 0);
            clear_pending(sb);
        }
        else if( connect_result == SOCKSTREAM_CONNECT_INFLIGHT )
        {
            // Still connecting
            // printf("Still connecting...\n");
        }
    }
    else
    {
        // We are connected, poll for data
        int bytes_received = sockstream_recv(stream_ptr, payload, sizeof(payload));

        if( bytes_received > 0 )
        {
            LibToriRS_NetPush(
                &sb->platform_to_game,
                TORI_RS_NET_MSG_RECV_DATA,
                payload,
                (uint16_t)bytes_received);
        }
        else if( bytes_received == 0 )
        {
            // Remote host closed connection
            sb->status = NET_IDLE;
            LibToriRS_NetPush(&sb->platform_to_game, TORI_RS_NET_MSG_CONN_STATUS, NULL, 0);
            sockstream_close(stream_ptr);
            stream_ptr = NULL;
        }
    }
}