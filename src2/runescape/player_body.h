#ifndef RUNESCAPE_PLAYER_BODY_H
#define RUNESCAPE_PLAYER_BODY_H

#include "appearance.h"
#include "toridraw/toridraw_types.h"

struct GameRunescape;

struct ToriDraw_ModelHandle
runescape_player_body_build(
    struct GameRunescape* game,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT]);

#endif
