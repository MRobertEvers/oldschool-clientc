#ifndef SRC_GAME_RS_TILE_HIGHLIGHT_WATCH_H
#define SRC_GAME_RS_TILE_HIGHLIGHT_WATCH_H

/*
 * When the three tile-highlight refreshers fire again: the hovered tile, the
 * local player's route, and the destination flag.
 *
 * Each refresher clears its highlight group and re-adds one tile, so it is
 * fired on the EDGE of its subject changing rather than every frame. This
 * keeps the value each one was last fired for and answers, per value, whether
 * the one seen now differs. Asking remembers the new value whether or not the
 * caller then fires.
 *
 * Reset seeds all three to -2, a value none of them can take. -1 is a real
 * state ("no tile", "no destination") that the refreshers must still run for
 * once, so seeding to -1 would skip that first run; -2 makes the first frame
 * after a reset fire all three.
 *
 * This does not know what a coord or a route signature is made of, whether
 * the world is loaded, or which scripts exist. The caller computes the three
 * values, asks only on frames the refreshers may run on, and decides what to
 * fire with -- the hovered tile is fired only for a real tile, and a cleared
 * destination is fired with the player's own tile.
 */

#include <stdbool.h>

/** The seed: a value no coord or route signature takes. */
#define RS_TILE_HIGHLIGHT_WATCH_UNSEEN (-2)

struct RS_TileHighlightWatch
{
    int hover_coord;
    int route;
    int dest_coord;
};

/** Forget every value, so each refresher fires on the next ask. */
void
RS_TileHighlightWatch_Reset(struct RS_TileHighlightWatch* watch);

/** True when the hovered tile is not the one last seen; remembers it. */
bool
RS_TileHighlightWatch_HoverChanged(
    struct RS_TileHighlightWatch* watch,
    int hover_coord);

/** True when the route signature is not the one last seen; remembers it. */
bool
RS_TileHighlightWatch_RouteChanged(
    struct RS_TileHighlightWatch* watch,
    int route);

/** True when the destination is not the one last seen; remembers it. */
bool
RS_TileHighlightWatch_DestChanged(
    struct RS_TileHighlightWatch* watch,
    int dest_coord);

#endif /* SRC_GAME_RS_TILE_HIGHLIGHT_WATCH_H */
