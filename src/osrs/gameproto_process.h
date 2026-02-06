#ifndef GAMEPROTO_PROCESS_H
#define GAMEPROTO_PROCESS_H

#include "game.h"
#include "gio.h"

void
gameproto_process(
    struct GGame* game,
    struct GIOQueue* io);

#endif