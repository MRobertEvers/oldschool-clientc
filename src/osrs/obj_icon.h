#ifndef OBJ_ICON_H
#define OBJ_ICON_H

#include "graphics/dash.h"
#include "osrs/game.h"

#define OBJ_ICON_CACHE_SIZE 200

/** One slot in the LRU sprite cache.  Mirrors ObjType.spriteCache in Client.ts (size 200). */
struct ObjIconCacheEntry
{
    int          obj_id;   /* -1 = empty slot */
    int          count;
    struct DashSprite* sprite;
    uint32_t     last_used; /* monotonic frame counter; 0 = empty */
};

/** Global LRU sprite cache shared across all inventory draws. */
extern struct ObjIconCacheEntry g_obj_icon_cache[OBJ_ICON_CACHE_SIZE];
extern uint32_t                 g_obj_icon_cache_frame;

/** Advance the frame counter (call once per frame). */
void
obj_icon_cache_tick(void);

/** Free all cached sprites and reset the cache. */
void
obj_icon_cache_clear(void);

// Generate a 32x32 sprite icon for an item. Returns a cached sprite when possible;
// generates and caches a new one on miss.  The caller must NOT free the returned sprite.
// Based on ObjType.getIcon() from Client.ts; mirrors ObjType.spriteCache LRU(200).
struct DashSprite*
obj_icon_get(
    struct GGame* game,
    int obj_id,
    int count);

/** DashModel for interface TYPE_MODEL modelType 4 (IF_SETOBJECT); caller frees via Scene2 release. */
struct DashModel*
obj_icon_new_dash_model_for_obj(struct GGame* game, int obj_id_0based);

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

// Render head model directly to a region of the main buffer (no intermediate buffer).
// Client.ts: Pix3D.centerX = childX + (child.width/2), Pix3D.centerY = childY + (child.height/2).
void
head_model_render_to_region(
    struct GGame* game,
    struct DashModel* dash_model,
    int* pixel_buffer,
    int stride,
    int x,
    int y,
    int width,
    int height,
    int zoom,
    int xan,
    int yan);

#endif
