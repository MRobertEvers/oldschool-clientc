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
 * This does not know which slot a world id maps to, or which modes the wire
 * may legally send. The caller maps the id to a slot, refuses what it cannot
 * accept, and passes slots and modes already checked; a world id is kept as
 * it is only for a view the caller does not have yet (RS_InteractionMode_Hold).
 *
 * A held pair is model state, not pending work, so it is not a task in the
 * pending index (game/rs_pending.h): the reference keeps both maps by world id
 * whether the world exists or not, nothing waits on an asset or a task for
 * it, and both clients apply it at packet time (game/rs_server_state.c). The
 * view takes it when it is created; a session end resets it.
 */

#include "world/worldview.h"

#include <stdint.h>

/** How many pairs can wait for a view the client does not have yet. */
#define RS_INTERACTION_MODE_HELD_MAX WORLDVIEW_MAX

/** A pair the server set for a view before the client created it. */
struct RS_InteractionModeHeld
{
    int wire_id;
    int8_t tile;
    int8_t entity;
};

struct RS_InteractionMode
{
    int default_tile;
    int default_entity;
    /** Per view slot; -1 means "use the default". */
    int8_t tile[WORLDVIEW_MAX];
    int8_t entity[WORLDVIEW_MAX];
    /** Pairs kept by the server's world id for views not created yet, oldest
     *  first (RS_InteractionMode_Hold). */
    struct RS_InteractionModeHeld held[RS_INTERACTION_MODE_HELD_MAX];
    int held_count;
};

/** Defaults 1/1, no view set, nothing held. */
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

/**
 * A pair for a view the client does not have yet, kept by the server's world
 * id until a view is created for it (RS_InteractionMode_TakeHeld). The
 * reference keys its two maps by world id whether that world exists or not
 * (deob class61.method1382), so a mode the server sets just before it spawns
 * the hull applies to the hull. A newer pair for the same id replaces the
 * older. The table is a ring: when it is full the OLDEST held pair -- the one
 * for the id longest without a view -- makes room, and this returns 1.
 */
int
RS_InteractionMode_Hold(
    struct RS_InteractionMode* modes,
    int wire_id,
    int tile_mode,
    int entity_mode);

/** RESET_INTERACTION_MODE for a view not created yet: nothing is held for it
 *  any more (class61.method1412 removes the id from both maps). */
void
RS_InteractionMode_ForgetHeld(
    struct RS_InteractionMode* modes,
    int wire_id);

/** A view `view` now exists for `wire_id`: it takes the pair held for that id,
 *  if one is, as its own. Returns 1 when it did. */
int
RS_InteractionMode_TakeHeld(
    struct RS_InteractionMode* modes,
    int wire_id,
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
