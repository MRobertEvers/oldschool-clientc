#ifndef SRC_RENDER_TORIRS_PICK_H
#define SRC_RENDER_TORIRS_PICK_H

#include <stdbool.h>

struct World;
struct World_PickSet;
struct WorldviewRegistry;

/**
 * Render-time world hittest: the pickset is expensive to build standalone, so
 * the executor tests the mouse point in the only window where it is cheap —
 * right after ToriDraw_RenderModel1Project returns VISIBLE for a pickable
 * DRAW_MODEL, while the scene's per-model scratch (screen_vertices/aabb) still
 * holds that model's projection. The executor records raw hits only;
 * ToriRS_PickHitsClassify turns them into a World_PickSet after the frame, so
 * the renderer never touches struct World. Pick/render parity is automatic:
 * the hittest uses the executor's own BEGIN_3D viewport and projection.
 */
struct ToriRS_PickHit
{
    int element_id;
    bool is_terrain;
    int tile_x; /* terrain tile; -1 for non-terrain */
    int tile_z;
    int tile_level;
    /** World-entity view the draw came out of; 0 = root scene. Non-zero
     *  terrain hits carry the VIEW's own (deck-local) tiles. */
    int view_id;
};

#define TORIRS_PICK_HITS_MAX 256

struct ToriRS_PickHits
{
    struct ToriRS_PickHit items[TORIRS_PICK_HITS_MAX];
    int count;
};

struct ToriRS_PickResult
{
    bool hover_tile_valid;
    int hover_tile_x;
    int hover_tile_z;
    int hover_tile_level;
    /** The nearest hovered DECK tile, when the pointer is over a world
     *  entity's own terrain: view id + the view's deck-local tile. Latched
     *  separately from the root hover — the root latch feeds the click
     *  cross and spawn hotkeys, which speak root scene tiles only — and a
     *  consumer that wants "the tile under the pointer, boat included"
     *  (the tile-indicator plugins) prefers this one when set: the deck is
     *  always the depth-nearer surface on a ray that hits the hull. */
    bool hover_view_valid;
    int hover_view;
    int hover_view_x;
    int hover_view_z;
    int hover_view_level;
};

void
ToriRS_PickHitsReset(struct ToriRS_PickHits* hits);

/** Append a raw hit; silently drops on overflow (matches pickset behavior). */
void
ToriRS_PickHitsAdd(
    struct ToriRS_PickHits* hits,
    int element_id,
    bool is_terrain,
    int tile_x,
    int tile_z,
    int tile_level,
    int view_id);

/** Classify raw render-order hits (back-to-front) into out_pickset (reset
 * first); the last terrain hit — the nearest — becomes the hover tile.
 * Only hits on player_level are kept: the player can never interact with
 * scenery/NPCs/tiles on a level other than the one they stand on, even though
 * lower levels are still rendered (and hittested) under them. Pass a negative
 * player_level to disable the filter (e.g. no local player resolved yet).
 *
 * `views` (optional, NULL to disable) resolves world-entity views so a
 * non-terrain hit inside a view can classify against that view's OWN world —
 * a deck loc becomes a SCENERY pick carrying the view id and deck-local
 * tiles. Without it every non-actor view hit stays a hull (WEV) pick. */
void
ToriRS_PickHitsClassify(
    struct World* world,
    struct WorldviewRegistry* views,
    struct ToriRS_PickHits const* hits,
    int player_level,
    struct World_PickSet* out_pickset,
    struct ToriRS_PickResult* out_result);

#endif
