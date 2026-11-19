#ifndef SRC_GAME_RS_NPC_UPDATE_ORIGIN_H
#define SRC_GAME_RS_NPC_UPDATE_ORIGIN_H

/*
 * The tile NPC_INFO's low-resolution deltas are measured from, as the server
 * last named it with SET_NPC_UPDATE_ORIGIN (revision 239).
 *
 * Until the server names one there is none, and the root's deltas are
 * measured from the local player instead. Once named, it stays until the
 * caller forgets it; forgetting keeps the tile, and only the "named" flag
 * goes. A world view other than the root reads the tile whether or not it is
 * named, which is why the tile outlives the flag.
 *
 * This does not know which world view an update describes, where the local
 * player stands, or which plane applies. The caller decides when the origin
 * is used and what to fall back to.
 */

#include <stdbool.h>

struct RS_NpcUpdateOrigin
{
    /** Scene-local tile. */
    int x;
    int z;
    /** The server has named the tile. */
    int valid;
};

/** SET_NPC_UPDATE_ORIGIN arrived: this tile is the origin. */
void
RS_NpcUpdateOrigin_Set(
    struct RS_NpcUpdateOrigin* origin,
    int x,
    int z);

/** No origin is named any more. The tile is kept. */
void
RS_NpcUpdateOrigin_Forget(struct RS_NpcUpdateOrigin* origin);

bool
RS_NpcUpdateOrigin_IsSet(struct RS_NpcUpdateOrigin const* origin);

/** The last tile named, whether or not it is still named; 0,0 before any. */
int
RS_NpcUpdateOrigin_X(struct RS_NpcUpdateOrigin const* origin);

int
RS_NpcUpdateOrigin_Z(struct RS_NpcUpdateOrigin const* origin);

#endif /* SRC_GAME_RS_NPC_UPDATE_ORIGIN_H */
