#ifndef SRC_RENDER_TORIRS_PICK_H
#define SRC_RENDER_TORIRS_PICK_H

#include <stdbool.h>

struct World;
struct World_PickSet;

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
    int tile_level);

/** Classify raw render-order hits (back-to-front) into out_pickset (reset
 * first); the last terrain hit — the nearest — becomes the hover tile. */
void
ToriRS_PickHitsClassify(
    struct World* world,
    struct ToriRS_PickHits const* hits,
    struct World_PickSet* out_pickset,
    struct ToriRS_PickResult* out_result);

#endif
