#ifndef SRC_GAME_RS_INTERACTION_MODE_H
#define SRC_GAME_RS_INTERACTION_MODE_H

/*
 * What a click may do in each world view: SET_INTERACTION_MODE and
 * RESET_INTERACTION_MODE (deob class61).
 *
 * Two modes per view. The tile mode (class481) is 0 nothing, 1 "Walk here",
 * 2 the sail-heading control. The entity mode (class500) is 0 nothing,
 * 1 everything, 2 examine and player ops, 3 everything.
 *
 * A view either was given its own pair or holds -1 and answers with the
 * defaults. The defaults are their own pair, set by the packet's world -2;
 * resetting a view puts it back to -1, so it follows the defaults again. The
 * boot value, and the value every session end returns to (class61.method1386),
 * is defaults 1/1 and no view set.
 *
 * This does not know the wire's world ids, which slot a world id maps to, or
 * which modes the wire may legally send. The caller maps the id to a slot,
 * refuses what it cannot accept, and passes slots and modes already checked.
 */

#include "world/worldview.h"

#include <stdint.h>

struct RS_InteractionMode
{
    int default_tile;
    int default_entity;
    /** Per view slot; -1 means "use the default". */
    int8_t tile[WORLDVIEW_MAX];
    int8_t entity[WORLDVIEW_MAX];
};

/** Defaults 1/1, no view set. */
void
RS_InteractionMode_Reset(struct RS_InteractionMode* modes);

/** World -2: the pair a view with no pair of its own answers with. */
void
RS_InteractionMode_SetDefaults(
    struct RS_InteractionMode* modes,
    int tile_mode,
    int entity_mode);

/** A view's own pair. */
void
RS_InteractionMode_SetView(
    struct RS_InteractionMode* modes,
    int view,
    int tile_mode,
    int entity_mode);

/** A view back to following the defaults: RESET_INTERACTION_MODE, and a view
 *  slot handed back to the pool. */
void
RS_InteractionMode_ResetView(
    struct RS_InteractionMode* modes,
    int view);

/** The tile / entity mode view `view` answers with. */
int
RS_InteractionMode_Tile(
    struct RS_InteractionMode const* modes,
    int view);
int
RS_InteractionMode_Entity(
    struct RS_InteractionMode const* modes,
    int view);

#endif /* SRC_GAME_RS_INTERACTION_MODE_H */
