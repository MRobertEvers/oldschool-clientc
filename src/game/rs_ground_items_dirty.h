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
 *
 * Beside it, RS_GroundItemsSettings decides when a settings change asks for
 * the whole-scene walk.
 */

#include <stdbool.h>
#include <stdint.h>

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

/**
 * Whether the ground-items settings have changed, which is a whole-scene
 * refresh.
 *
 * Two kinds of input are watched, two of each. The CARRIER VARPS hold the
 * overlay's own toggles; the caller resolves each from its revconfig varbit
 * once the varbit table is loaded, and the first value a carrier comes up with
 * is a seed, not a change -- reporting it would rebuild every overlay on the
 * first tick after the table lands. The AUX REVISIONS are the revision counters
 * of the Ignore and Highlight lists, which start at 0.
 *
 * This does not know the varps, the varbit table or the lists. The caller
 * resolves the carriers, reads their values and the revisions, and asks for
 * the whole-scene walk when one has changed.
 */
#define RS_GROUND_ITEMS_SETTINGS_WATCHED 2

struct RS_GroundItemsSettings
{
    /** The carrier varp per watched varbit, -1 until resolved. */
    int varp[RS_GROUND_ITEMS_SETTINGS_WATCHED];
    /** The value last seen in each carrier. */
    int seen[RS_GROUND_ITEMS_SETTINGS_WATCHED];
    /** The revision last seen of each aux list. */
    uint64_t aux_seen[RS_GROUND_ITEMS_SETTINGS_WATCHED];
};

/** No carrier resolved, nothing seen. */
void
RS_GroundItemsSettings_Reset(struct RS_GroundItemsSettings* settings);

/** The carrier varp of watched slot `i`, or a negative id while unresolved. */
int
RS_GroundItemsSettings_Varp(
    struct RS_GroundItemsSettings const* settings,
    int i);

/**
 * Slot `i`'s carrier is `varp` and its value now is `value`. Seeds the value:
 * the next change is measured from it, and this is not one.
 */
void
RS_GroundItemsSettings_Resolve(
    struct RS_GroundItemsSettings* settings,
    int i,
    int varp,
    int value);

/** True when slot `i`'s carrier no longer holds the value last seen;
 *  remembers the new one. */
bool
RS_GroundItemsSettings_VarpChanged(
    struct RS_GroundItemsSettings* settings,
    int i,
    int value);

/** True when aux list `i`'s revision is not the one last seen; remembers it. */
bool
RS_GroundItemsSettings_AuxChanged(
    struct RS_GroundItemsSettings* settings,
    int i,
    uint64_t revision);

#endif /* SRC_GAME_RS_GROUND_ITEMS_DIRTY_H */
