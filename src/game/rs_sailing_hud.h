#ifndef SRC_GAME_RS_SAILING_HUD_H
#define SRC_GAME_RS_SAILING_HUD_H

/*
 * The sailing helm's client state: the revision ids it reads, and the two
 * native heading arrows over the boat.
 *
 * The ids are resolved once from the revconfig when the App is reset -- the
 * at-helm and captain-role varbits, the five crew-duty and crew-roster
 * varbits, the ship-crew npc category, and the two arrow models (index 0 the
 * hover arrow, 1 the selected one). Any of them is -1 on a revision that does
 * not declare it, and every reader treats -1 as "this revision has no such
 * thing".
 *
 * The one thing decided here is how long a chosen heading stays highlighted:
 * a heading sent with Set heading is the selected arrow for 30 client cycles
 * from the cycle it was sent on, and none after. Remaining cycles are counted
 * the same way, so a selection saved and put back on another clock keeps what
 * it had left.
 *
 * The arrows' load state is kept beside it: whether a model load has been
 * queued for an arrow (queued once, never again), and the scene element an
 * arrow was built into (-1 until it is).
 *
 * This does not know the clock, the varps, the cache, the scene or the boat.
 * The caller passes the client cycle, reads the varbits these ids name,
 * queues the model load and builds the element, and decides whether the
 * player can steer at all.
 */

#include <stdbool.h>
#include <stdint.h>

struct RevConfigRefs;

/** Crew slots a boat has, each with a duty and a roster varbit. */
#define RS_SAILING_HUD_CREW_SLOTS 5
/** The heading arrows: the hover arrow and the selected arrow. */
#define RS_SAILING_HUD_ARROWS 2
#define RS_SAILING_HUD_ARROW_HOVER 0
#define RS_SAILING_HUD_ARROW_SELECTED 1
/** Client cycles a sent heading stays the selected arrow. */
#define RS_SAILING_HUD_SELECTED_CYCLES 30

/** The revision's ids, resolved once and only read afterwards. -1 = absent. */
struct RS_SailingHudIds
{
    int at_helm_varbit;
    int captain_role_varbit;
    int crew_duty_varbit[RS_SAILING_HUD_CREW_SLOTS];
    int crew_roster_varbit[RS_SAILING_HUD_CREW_SLOTS];
    int crew_category;
    int arrow_model[RS_SAILING_HUD_ARROWS];
};

struct RS_SailingHud
{
    struct RS_SailingHudIds ids;
    /** Scene element each arrow was built into, -1 until it is. */
    int arrow_element[RS_SAILING_HUD_ARROWS];
    /** 1 once a model load has been queued for that arrow. */
    int arrow_loading[RS_SAILING_HUD_ARROWS];
    int selected_heading;
    /** First client cycle the selected heading is no longer shown on. */
    uint64_t selected_until;
};

/**
 * Resolve the ids from `refs` and clear the rest: no arrow built, no load
 * queued, no heading selected.
 */
void
RS_SailingHud_Reset(
    struct RS_SailingHud* hud,
    struct RevConfigRefs const* refs);

/** A heading was sent on client cycle `cycle`: show it for the next 30. */
void
RS_SailingHud_Select(
    struct RS_SailingHud* hud,
    int heading,
    uint64_t cycle);

/** The heading the selected arrow shows on `cycle`, or -1 once it expired. */
int
RS_SailingHud_SelectedHeading(
    struct RS_SailingHud const* hud,
    uint64_t cycle);

/**
 * The selection as a heading and the cycles it has left on `cycle` (0 once it
 * expired). The heading is the last one sent, expired or not.
 */
void
RS_SailingHud_SelectionSave(
    struct RS_SailingHud const* hud,
    uint64_t cycle,
    int* out_heading,
    uint64_t* out_remaining);

/** Put back a saved selection with `remaining` cycles left from `cycle`. */
void
RS_SailingHud_SelectionRestore(
    struct RS_SailingHud* hud,
    uint64_t cycle,
    int heading,
    uint64_t remaining);

/**
 * True the first time it is asked for `arrow`, and remembers it: the caller
 * queues that arrow's model load exactly once.
 */
bool
RS_SailingHud_ArrowLoadBegin(
    struct RS_SailingHud* hud,
    int arrow);

/** The scene element `arrow` was built into, or -1. */
int
RS_SailingHud_ArrowElement(
    struct RS_SailingHud const* hud,
    int arrow);

/** Remember the scene element `arrow` was built into. */
void
RS_SailingHud_ArrowElementSet(
    struct RS_SailingHud* hud,
    int arrow,
    int element);

/**
 * Write the built arrows' elements to `out`, hover arrow first, at most `max`
 * of them. Returns how many were written.
 */
int
RS_SailingHud_ArrowElementsClaim(
    struct RS_SailingHud const* hud,
    int* out,
    int max);

#endif /* SRC_GAME_RS_SAILING_HUD_H */
