#ifndef GAMEPROTO_H
#define GAMEPROTO_H

#include "game.h"
#include "gameproto_revisions.h"
#include "packets/revpacket_lc245_2.h"

int
gameproto_parse_lc245_2(
    int packet_type,
    uint8_t* data,
    int data_size,
    struct RevPacket_LC245_2* packet);

#endif