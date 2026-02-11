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

// Render a head model to a buffer (for chat/interface MODEL components).
// Client.ts drawInterface: model.drawSimple(0, yan, 0, xan, 0, eyeY, eyeZ)
// zoom/xan/yan from component. Does not free dash_model.
void
head_model_render(
    struct GGame* game,
    struct DashModel* dash_model,
    int* buffer,
    int width,
    int height,
    int zoom,
    int xan,
    int yan);

#endif
