

#include "server.h"

#include "prot.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Server*
server_new(void)
{
    struct Server* server = malloc(sizeof(struct Server));
    memset(server, 0, sizeof(struct Server));

    server->next_tick_ms = 0;
    server->timestamp_ms = 0;
    server->packet_kind = -1;
    server->packet_bytes_received = 0;
    server->packet_expected_size = 0;
    server->write_offset = 0;

    server->player_count = 0;
    server->players[0].x = 10;
    server->players[0].z = 10;
    server->players[0].pathing.x = -1;
    server->players[0].pathing.z = -1;

    // 0X100 bit indicates to use an idk.
    // 0X200 bit indicates to use an equip model.

    server->players[0].appearance[0] = 0;
    server->players[0].appearance[1] = 0;
    server->players[0].appearance[2] = 0;
    server->players[0].appearance[3] = 0x200 + 1333; // Rune scimitar
    server->players[0].appearance[4] = 0x100 + 18;   // 274;
    server->players[0].appearance[5] = 0;
    server->players[0].appearance[6] = 0x100 + 26;  // 282;
    server->players[0].appearance[7] = 0x100 + 36;  // 292;
    server->players[0].appearance[8] = 0x100 + 0;   // 256;
    server->players[0].appearance[9] = 0x100 + 33;  // 289;
    server->players[0].appearance[10] = 0x100 + 42; // 298;
    server->players[0].appearance[11] = 0x100 + 10; // 266;

    server->players[0].walkanim = 819;
    server->players[0].runanim = 824;
    server->players[0].walkanim_b = 820;
    server->players[0].walkanim_r = 821;
    server->players[0].walkanim_l = 822;
    server->players[0].turnanim = 823;
    server->players[0].readyanim = 808;

    return server;
}

void
server_free(struct Server* server)
{
    free(server);
}

static void*
write_buffer(struct Server* server)
{
    return server->outgoing_message_buffer + server->write_offset;
}

static int
write_buffer_size(struct Server* server)
{
    return sizeof(server->outgoing_message_buffer) - server->write_offset;
}

void
server_step(
    struct Server* server,
    int timestamp_ms)
{
    if( timestamp_ms < server->next_tick_ms )
        return;

    server->timestamp_ms = timestamp_ms;
    while( server->next_tick_ms < timestamp_ms )
        server->next_tick_ms += 600;

    server->write_offset = 0;

    for( int i = 0; i < server->queue_size; i++ )
    {
        struct ClientPackets* client_packet = &server->queue[i];
        switch( client_packet->kind )
        {
        case PROT_KIND_CONNECT:
        {
            struct ClientProtPlayer player = { 0 };
            player.pid = client_packet->_connect.pid;
            player.x = server->players[0].x;
            player.z = server->players[0].z;
            for( int j = 0; j < 12; j++ )
            {
                player.slots[j] = server->players[0].appearance[j];
            }
            player.walkanim = server->players[0].walkanim;
            player.runanim = server->players[0].runanim;
            player.walkanim_b = server->players[0].walkanim_b;
            player.walkanim_r = server->players[0].walkanim_r;
            player.walkanim_l = server->players[0].walkanim_l;
            player.turnanim = server->players[0].turnanim;
            player.readyanim = server->players[0].readyanim;

            server->write_offset +=
                client_prot_player_encode(&player, write_buffer(server), write_buffer_size(server));

            server->player_count += 1;
            break;
        }
        break;
        case PROT_KIND_TILE_CLICK:
        {
            server->players[0].pathing.x = client_packet->_tile_click.x;
            server->players[0].pathing.z = client_packet->_tile_click.z;
        }
        break;
        }
    }

    for( int i = 0; i < server->player_count; i++ )
    {
        if( server->players[i].pathing.x != -1 && server->players[i].pathing.z != -1 )
        {
            int xdiff = server->players[i].pathing.x - server->players[i].x;
            int zdiff = server->players[i].pathing.z - server->players[i].z;
            if( xdiff != 0 )
                server->players[i].x += xdiff > 0 ? 1 : -1;
            if( zdiff != 0 )
                server->players[i].z += zdiff > 0 ? 1 : -1;

            if( server->players[i].x == server->players[i].pathing.x &&
                server->players[i].z == server->players[i].pathing.z )
            {
                server->players[i].pathing.x = -1;
                server->players[i].pathing.z = -1;
            }
        }

        struct ClientProtPlayerMove player_move = { 0 };
        player_move.x = server->players[i].x;
        player_move.z = server->players[i].z;
        player_move.pathing_x = server->players[i].pathing.x;
        player_move.pathing_z = server->players[i].pathing.z;

        server->write_offset += client_prot_player_move_encode(
            &player_move, write_buffer(server), write_buffer_size(server));
    }

    server->queue_size = 0;
}

static void
queue_client_packet(
    struct Server* server,
    struct ClientPackets* packet)
{
    struct ClientPackets* client_packet = &server->queue[server->queue_size];
    memcpy(client_packet, packet, sizeof(struct ClientPackets));
    server->queue_size++;
}

void
server_receive(
    struct Server* server,
    uint8_t* data,
    int data_size)
{
    if( !data || data_size <= 0 )
    {
        return;
    }

    int data_offset = 0;

    while( data_offset < data_size )
    {
        // If no packet in progress, read the packet kind
        if( server->packet_kind == -1 )
        {
            if( data_offset >= data_size )
            {
                break;
            }

            server->packet_kind = data[data_offset++];

            // Validate packet kind
            if( server->packet_kind < 0 || server->packet_kind >= PROT_KIND_MAX )
            {
                // Invalid packet kind, reset
                server->packet_kind = -1;
                server->packet_bytes_received = 0;
                server->packet_expected_size = 0;
                continue;
            }

            // Calculate expected packet size: 1 byte (kind) + packet data size
            server->packet_expected_size =
                1 + prot_get_packet_size((enum ProtKind)server->packet_kind);
            server->packet_bytes_received = 0;

            // Store the kind byte in the buffer
            server->packet_buffer[0] = server->packet_kind;
            server->packet_bytes_received = 1;
        }

        // Calculate how many bytes we still need
        int bytes_needed = server->packet_expected_size - server->packet_bytes_received;
        int bytes_available = data_size - data_offset;
        int bytes_to_copy = bytes_needed < bytes_available ? bytes_needed : bytes_available;

        // Copy data into packet buffer
        memcpy(
            server->packet_buffer + server->packet_bytes_received,
            data + data_offset,
            bytes_to_copy);

        server->packet_bytes_received += bytes_to_copy;
        data_offset += bytes_to_copy;

        struct ClientPackets client_packet;

        // Check if packet is complete
        if( server->packet_bytes_received >= server->packet_expected_size )
        {
            // Packet is complete, parse and execute it
            switch( server->packet_kind )
            {
            case PROT_KIND_TILE_CLICK:
            {
                struct ProtTileClick tile_click;
                prot_tile_click_decode(
                    &tile_click, server->packet_buffer, server->packet_bytes_received);

                printf(
                    "Server received tile click: (%d, %d), setting pathing target\n",
                    tile_click.x,
                    tile_click.z);

                client_packet.kind = PROT_KIND_TILE_CLICK;
                memcpy(&client_packet._tile_click, &tile_click, sizeof(tile_click));
                queue_client_packet(server, &client_packet);
                break;
            }
            case PROT_KIND_CONNECT:
            {
                struct ProtConnect connect;
                prot_connect_decode(&connect, server->packet_buffer, server->packet_bytes_received);

                client_packet.kind = PROT_KIND_CONNECT;
                memcpy(&client_packet._connect, &connect, sizeof(connect));
                queue_client_packet(server, &client_packet);
                break;
            }
            default:
                // Unknown packet kind, ignore
                break;
            }

            // Reset for next packet
            server->packet_kind = -1;
            server->packet_bytes_received = 0;
            server->packet_expected_size = 0;
        }
    }
}

void
server_get_outgoing_message(
    struct Server* server,
    uint8_t** data,
    int* data_size)
{
    if( server->write_offset > 0 )
    {
        *data = server->outgoing_message_buffer;
        *data_size = server->write_offset;
        server->write_offset = 0;
    }
    else
    {
        *data = NULL;
        *data_size = 0;
    }
}