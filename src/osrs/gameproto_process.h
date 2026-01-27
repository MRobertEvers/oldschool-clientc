#ifndef GAMEPROTO_PROCESS_H
#define GAMEPROTO_PROCESS_H

#include "game.h"
#include "gametask_status.h"
#include "gio.h"
#include "query_engine.h"

void
gameproto_process(
    struct GGame* game,
    struct GIOQueue* io);

#endif