#ifndef RUNESCAPE_PLAYER_BODY_H
#define RUNESCAPE_PLAYER_BODY_H

#include "../world/world_entity.h"
#include "appearance.h"
#include "toridraw/toridraw_types.h"

struct GameRunescape;

struct ToriDraw_ModelHandle
runescape_player_body_build(
    struct GameRunescape* game,
    const int appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT]);

struct ToriDraw_ModelHandle
runescape_npc_body_build(
    struct GameRunescape* game,
    int npc_id);

struct WorldEntityFacet_IdleAnimations
runescape_npc_animation_from_config(
    struct GameRunescape* game,
    int npc_id);

int
runescape_npc_size_from_config(
    struct GameRunescape* game,
    int npc_id);

#endif
