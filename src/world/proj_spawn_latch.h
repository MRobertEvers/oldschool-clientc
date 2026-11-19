#ifndef SRC_WORLD_PROJ_SPAWN_LATCH_H
#define SRC_WORLD_PROJ_SPAWN_LATCH_H

/*
 * The developer projectile hotkey's two presses: the first marks a tile as
 * the source, the second launches from that source to the tile it is pressed
 * on.
 *
 * A second press on the source tile itself clears the source instead of
 * launching. "The same tile" compares x and z only; the level is recorded
 * with the source and handed back with it, and is not compared. Launching
 * clears the source too, so every launch starts from a fresh first press.
 *
 * This does not know what a projectile is, which tile is hovered, what a
 * launch spawns, or what gets logged. The caller passes the pressed tile,
 * performs the action that comes back, and says what it did.
 */

struct ProjSpawnLatch
{
    /** Source tile; x and z are -1 while no source is marked. */
    int src_x;
    int src_z;
    int src_level;
};

/** What the caller does after a press. */
enum ProjSpawnLatchAction
{
    /** The pressed tile is now the source. */
    PROJ_SPAWN_LATCH_MARKED = 0,
    /** The source tile was pressed again: there is no source now. */
    PROJ_SPAWN_LATCH_CLEARED,
    /** Launch from the source handed back to the pressed tile. */
    PROJ_SPAWN_LATCH_LAUNCH,
};

/** No source marked: x and z -1, level 0. */
void
ProjSpawnLatch_Reset(struct ProjSpawnLatch* latch);

/**
 * The hotkey was pressed on tile (`tile_x`, `tile_z`, `level`).
 *
 * For PROJ_SPAWN_LATCH_LAUNCH the source is written to the three out
 * parameters before it is cleared; for the other two actions they are left
 * untouched. The cleared source keeps its last level.
 */
enum ProjSpawnLatchAction
ProjSpawnLatch_Press(
    struct ProjSpawnLatch* latch,
    int tile_x,
    int tile_z,
    int level,
    int* out_src_x,
    int* out_src_z,
    int* out_src_level);

#endif /* SRC_WORLD_PROJ_SPAWN_LATCH_H */
