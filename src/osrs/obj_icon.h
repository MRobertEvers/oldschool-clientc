#ifndef OBJ_ICON_H
#define OBJ_ICON_H

#include "graphics/dash.h"
#include "osrs/game.h"

// Generate a 32x32 sprite icon for an item
// Based on ObjType.getIcon() from Client.ts
struct DashSprite*
obj_icon_get(
    struct GGame* game,
    int obj_id,
    int count);

#endif
