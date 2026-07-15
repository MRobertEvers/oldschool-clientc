#ifndef SERVER_PROT_H
#define SERVER_PROT_H

#include <stdint.h>

enum ProtKind
{
    PROT_KIND_CONNECT,
    PROT_KIND_TILE_CLICK,

    PROTLC_REBUILD_NORMAL,
    PROT_KIND_MAX,
};

enum ClientProtKind
{
    CLIENT_PROT_PLAYER,
    CLIENT_PROT_PLAYER_MOVE,
    CLIENT_PROT_MAX,
};

struct ProtConnect
{
    int pid;
};

struct ProtTileClick
{
    int x;
    int z;
};

struct ClientProtPlayerMove
{
    int x;
    int z;
    int pathing_x;
    int pathing_z;
};

struct ClientProtPlayer
{
    int pid;
    int x;
    int z;

    int slots[12];
    int walkanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
    int turnanim;
    int readyanim;
};

int
prot_connect_encode(
    struct ProtConnect* connect,
    uint8_t* data,
    int data_size);

int
prot_connect_decode(
    struct ProtConnect* connect,
    uint8_t* data,
    int data_size);

int
prot_tile_click_encode(
    struct ProtTileClick* tile_click,
    uint8_t* data,
    int data_size);

int
prot_tile_click_decode(
    struct ProtTileClick* tile_click,
    uint8_t* data,
    int data_size);

int
client_prot_player_move_encode(
    struct ClientProtPlayerMove* player_move,
    uint8_t* data,
    int data_size);

int
client_prot_player_move_decode(
    struct ClientProtPlayerMove* player_move,
    uint8_t* data,
    int data_size);

int
client_prot_player_encode(
    struct ClientProtPlayer* player,
    uint8_t* data,
    int data_size);

int
client_prot_player_decode(
    struct ClientProtPlayer* player,
    uint8_t* data,
    int data_size);

int
prot_get_packet_size(enum ProtKind kind);

int
client_prot_get_packet_size(enum ClientProtKind kind);

#endif