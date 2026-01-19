#ifndef GAMESTREAM_H
#define GAMESTREAM_H

#include "game.h"
#include "packetbuffer.h"
#include "packetin.h"

void
gamestream_process(
    struct GGame* game,
    int packet_type,
    uint8_t* data,
    int data_size);

#endif