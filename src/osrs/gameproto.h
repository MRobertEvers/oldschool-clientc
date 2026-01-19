#ifndef GAMEPROTO_H
#define GAMEPROTO_H

#include "game.h"

enum GameProtoRevision
{
    GAMEPROTO_REVISION_INVALID = 0,
    GAMEPROTO_REVISION_LC254 = 1,
};

void
gameproto_process(
    struct GGame* game,
    enum GameProtoRevision revision,
    int packet_type,
    uint8_t* data,
    int data_size);

#endif