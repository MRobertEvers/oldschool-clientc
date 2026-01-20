#ifndef GAMEPROTO_H
#define GAMEPROTO_H

#include "game.h"
#include "gameproto_revisions.h"

void
gameproto_process(
    struct GGame* game,
    enum GameProtoRevision revision,
    int packet_type,
    uint8_t* data,
    int data_size);

#endif