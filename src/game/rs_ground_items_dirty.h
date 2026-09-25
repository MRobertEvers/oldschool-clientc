#ifndef SRC_GAME_RS_GROUND_ITEMS_DIRTY_H
#define SRC_GAME_RS_GROUND_ITEMS_DIRTY_H

/**
 * Tiles whose ground-item pile changed since the last logic tick, as packed
 * absolute coords.
 *
 * The overlay is rebuilt per TILE, by a cache script nothing in the cache
 * calls, because a pile changing is something only the client knows. The list
 * is drained once per tick rather than fired from inside the zone executor:
 * one OBJ_ADD burst can touch the same tile several times, and a script per
 * packet would rebuild the same overlay three times over. Hence the dedup.
 *
 * `refresh_all` is the overflow AND the whole-scene case together -- a rebuild
 * shift, a settings change, or more tiles in one tick than the list holds. It
 * costs a walk of every stack in the pool, which is bounded by the scene, and
 * that is precisely why the list can stay small.
 *
 * The two ways in are NOT the same, and the difference is the subtle part:
 *
 *   - `Mark` is the per-change path. Overflowing it promotes to `refresh_all`,
 *     because from that many tiles a whole-scene walk is the cheaper answer.
 *   - `Append` is what the whole-scene walk itself uses. Overflowing it is a
 *     SCRATCHPAD full, not a budget blown: it stops, and the rest come back
 *     next tick. Promoting there would restart the walk that is already
 *     running.
 */

#include <stdbool.h>

/**
 * Tiles the driver can queue in one logic tick before it gives up and rebuilds
 * the whole scene instead.
 *
 * Small on purpose. The overflow path is not a failure -- it is the CHEAPER
 * branch once a tick touches this many piles, because a whole-scene rebuild
 * walks the obj-stack pool once while the per-tile path runs a clientscript
 * per entry. A zone burst on login is exactly that case.
 */
#define RS_GROUND_ITEMS_DIRTY_MAX 32

struct RS_GroundItemsDirty
{
    int coords[RS_GROUND_ITEMS_DIRTY_MAX];
    int count;
    /** Drain the whole scene next tick, and ignore per-tile marks until then. */
    int refresh_all;
};

/**
 * One tile changed.
 *
 * A no-op once `refresh_all` is set: the whole-scene walk will cover it. Also
 * a no-op for a coord already listed.
 */
void
RS_GroundItemsDirty_Mark(
    struct RS_GroundItemsDirty* dirty,
    int coord);

/**
 * Append during the whole-scene walk, past the `refresh_all` gate that Mark
 * respects.
 *
 * Returns false when the list is full, which ends the walk -- the rest of the
 * scene is picked up next tick.
 */
bool
RS_GroundItemsDirty_Append(
    struct RS_GroundItemsDirty* dirty,
    int coord);

/** Ask for a whole-scene walk on the next tick. */
void
RS_GroundItemsDirty_MarkAll(struct RS_GroundItemsDirty* dirty);

/**
 * Take the `refresh_all` flag, clearing it.
 *
 * Cleared BEFORE the walk rather than after, so that a mark arriving during
 * the walk is recorded normally instead of being swallowed by a flag that is
 * about to be reset.
 */
bool
RS_GroundItemsDirty_TakeRefreshAll(struct RS_GroundItemsDirty* dirty);

/** Forget every queued tile. The tick does this once it has fired them. */
void
RS_GroundItemsDirty_Clear(struct RS_GroundItemsDirty* dirty);

#endif /* SRC_GAME_RS_GROUND_ITEMS_DIRTY_H */
