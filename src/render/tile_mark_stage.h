#ifndef SRC_RENDER_TILE_MARK_STAGE_H
#define SRC_RENDER_TILE_MARK_STAGE_H

/*
 * Tile markers that are drawn INSIDE the 3D pass, and the painter command
 * each one is drawn after.
 *
 * A tile marker the cache did not flag "always on top" is drawn with the
 * tile's own ground instead of over the finished scene, so whatever the
 * painter puts down on that tile afterwards -- the locs, the npc, the player
 * standing on it -- covers it. The primitives are the same ones the overlay
 * list holds; while the stage is open around one draw_tile call they are kept
 * here instead, each tagged with the tile that call named.
 *
 * What makes them land in the right place is `after_command`, and it is
 * decided once a frame against the paint that was just made. The command
 * wanted is the LAST terrain command for the marked tile at the marked level:
 * a tile can put down several storeys' ground in one pass (a bridge deck over
 * the ground below it), and a marker emitted after the first of them would be
 * painted over by the rest. A mark whose tile the paint never drew keeps the
 * UNPLACED sentinel and sorts to the end, and it is counted rather than
 * dropped, because "behind the player" and "not drawn at all" look the same
 * on screen.
 *
 * The sort after placement must be STABLE: the primitives of one marker are a
 * run -- a polygon's begin, its points and its end -- that all carry the same
 * command index, and an unstable sort would turn the run into a polygon with
 * no beginning.
 *
 * The tile key is what survives from frame to frame. The command index does
 * not, because the paint is new every frame, and a retained overlay list can
 * leave these marks standing for several.
 *
 * Nothing here reads a world, a painter buffer or a frame. The caller walks
 * its own paint and reports each root-view terrain command's index and tile
 * key; this decides which command each mark follows and in what order the
 * marks are handed over. Whether a tile is in the scene, which view a command
 * belongs to, and what to say about overflow or unplaced marks are the
 * caller's.
 */

#include "render/torirs_frame.h"

#include <limits.h>
#include <stdbool.h>

enum
{
    /**
     * Overlay primitives a frame may stage into the scene, over every tile.
     *
     * The same 512 the per-frame draw budget allots a plugin, because these
     * come out of that budget: a tile marker is ten primitives (a filled quad
     * as a begin / four points / end, and four border segments), so this is
     * fifty tiles' worth and the budget bites first in every case that
     * matters. Past it the stage refuses and says so rather than wrapping.
     */
    TILE_MARK_STAGE_MAX = 512,
    /** Scene tiles a key can name per axis. 512 because that is what a
     *  painter terrain command's own nine-bit x and z can name, and the key
     *  is compared against one. */
    TILE_MARK_STAGE_SCENE_MAX = 512,
    /** Levels a key can name. The caller holds this equal to the world's
     *  terrain level count. */
    TILE_MARK_STAGE_LEVELS = 4,
};

/** "This paint never drew the marked tile." INT_MAX so the sort puts these
 *  last and the world walk's own "not reached yet" test stops at the first of
 *  them -- no second test, no second list. */
#define TILE_MARK_STAGE_UNPLACED INT_MAX

struct TileMarkStage
{
    /** The staged primitives, sorted by `after_command` once placed. */
    struct ToriRS_WorldTileMark marks[TILE_MARK_STAGE_MAX];
    /** The marked tile of each primitive, packed by TileMarkStage_Key. */
    int keys[TILE_MARK_STAGE_MAX];
    int count;
    /** A push was refused because the list was full. */
    int overflow;
    /** Marks whose tile a placement did not find in the paint -- off screen,
     *  culled, or under a hidden roof. A legitimate state, not an error.
     *  Only Reset clears it. */
    int unplaced;
    /** Open for the length of one draw_tile call. */
    int open;
    int open_key;
    /** The world viewport, stamped onto every primitive the open stage takes.
     *  An overlay item gets its clip from the desc it travels on; a mark has
     *  none -- it is emitted inside the 3D pass -- so it carries one, or a
     *  marker at the edge of the scene paints over the chrome. */
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
};

/**
 * One placement's working set: the distinct TILES, not the primitives.
 *
 * Ten primitives of one marker share one tile and one answer, and the caller
 * reports every terrain command of the paint, so matching against the
 * primitives would multiply the paint's thousands of terrain commands by ten
 * for no new information.
 */
struct TileMarkPlacement
{
    int tile_key[TILE_MARK_STAGE_MAX];
    int tile_at[TILE_MARK_STAGE_MAX];
    int tile_count;
};

/** The marked tile as one int. A key and not a struct because it is only ever
 *  compared, against the tile a painter command names. */
int
TileMarkStage_Key(
    int scene_x,
    int scene_z,
    int level);

/** Empty the list and close the stage. */
void
TileMarkStage_Reset(struct TileMarkStage* stage);

/** Open the stage for one marker on the tile `key`; every push until End is
 *  one of its primitives and is clipped to the given viewport. */
void
TileMarkStage_Begin(
    struct TileMarkStage* stage,
    int key,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h);

void
TileMarkStage_End(struct TileMarkStage* stage);

bool
TileMarkStage_IsOpen(struct TileMarkStage const* stage);

/**
 * Keep one primitive of the open marker.
 *
 * False when the list is full; the refusal is remembered in `overflow`. The
 * kept primitive carries the stage's clip and the UNPLACED sentinel until the
 * next placement.
 */
bool
TileMarkStage_Push(
    struct TileMarkStage* stage,
    struct UITreeEntityOverlay const* item);

int
TileMarkStage_Count(struct TileMarkStage const* stage);

struct ToriRS_WorldTileMark const*
TileMarkStage_Marks(struct TileMarkStage const* stage);

/**
 * Start placing against a new paint: collect the distinct tiles, all
 * UNPLACED. Returns how many there are; 0 means there is nothing to place and
 * the caller need not walk its paint.
 */
int
TileMarkStage_PlaceBegin(
    struct TileMarkStage const* stage,
    struct TileMarkPlacement* placement);

/** One root-view terrain command of the paint, in paint order. A later
 *  command for the same tile replaces an earlier one. */
void
TileMarkPlacement_Saw(
    struct TileMarkPlacement* placement,
    int command_index,
    int key);

/** Give every mark its tile's command, count the unplaced ones into
 *  `unplaced`, and stable-sort the marks by command. */
void
TileMarkStage_PlaceFinish(
    struct TileMarkStage* stage,
    struct TileMarkPlacement const* placement);

#endif
