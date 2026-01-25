#ifndef SERVER_H
#define SERVER_H

#include "prot.h"

#include <stdint.h>

struct Pathing
{
    int x;
    int z;
};

struct PlayerEntity
{
    int x;
    int z;

    int appearance[12];
    int walkanim;   // 819
    int runanim;    // 824
    int walkanim_b; // 820
    int walkanim_r; // 821
    int walkanim_l; // 822
    int turnanim;   // 823
    int readyanim;  // 808

    struct Pathing pathing;
};

struct ClientPackets
{
    enum ProtKind kind;
    union
    {
        struct ProtConnect _connect;
        struct ProtTileClick _tile_click;
    };
};

struct Server
{
    uint64_t tick_ms;
    uint64_t next_tick_ms;
    int timestamp_ms;

    // Partial packet handling
    uint8_t packet_buffer[65536];
    int packet_kind;
    int packet_bytes_received;
    int packet_expected_size;

    // Outgoing message buffer for client protocol
    uint8_t outgoing_message_buffer[65536];
    int write_offset;

    struct PlayerEntity players[100];
    int player_count;

    struct ClientPackets queue[100];
    int queue_size;
};

struct Server*
server_new(void);
void
server_free(struct Server* server);

void
server_step(
    struct Server* server,
    int timestamp_ms);

void
server_receive(
    struct Server* server,
    uint8_t* data,
    int data_size);

void
server_get_outgoing_message(
    struct Server* server,
    uint8_t** data,
    int* data_size);

#endif