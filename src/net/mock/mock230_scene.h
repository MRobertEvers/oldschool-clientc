#ifndef SRC_NET_MOCK_MOCK230_SCENE_H
#define SRC_NET_MOCK_MOCK230_SCENE_H

/*
 * The server's own copy of the world: collision, and the locs it is built from.
 *
 * Until now the mock walked the player through walls, because it had no idea
 * where the walls were — the client loads the map squares, the server did not.
 * It loads the same ones, out of the same cache, and builds the same
 * CollisionMap the client builds (src/engine/world_builder/collision_map.c is
 * linked here rather than reimplemented, so a route the server computes and a
 * route the client would have computed cannot drift).
 *
 * Scope is the 104x104 scene, rebuilt when the scene is. That is the same
 * window the entity streams cover, so anything the server can be asked to walk
 * to is inside it.
 *
 * Locs are kept as well as collision because doors need them: opening one means
 * finding the loc the client is looking at, swapping it for its open form, and
 * moving the collision with it.
 */

#include <stdint.h>

struct CollisionMap;

/* ------------------------------------------------------------------ */
/* Locs                                                                */
/* ------------------------------------------------------------------ */

struct Mock230SceneLoc
{
    int loc_id;
    int shape;
    int angle;
    /** Absolute tile of the loc's south-west corner. */
    int x, z, level;
    /** Cache-derived footprint, already rotated: `size_x` is along x. */
    int size_x, size_z;
    /** 0 when a runtime change removed it. Kept rather than compacted so a
     *  door's slot stays stable while a script holds on to it. */
    int active;
    /** 1 when this loc is not what the map square said — a door that has been
     *  opened. A rebuild re-sends these, because REBUILD_NORMAL resets the
     *  client's scene to the cache's version. */
    int changed;
};

/**
 * The loc the player is interacting with at (x, z, level).
 *
 * `loc_id` may be -1 to mean "whatever is there", which is what an OPLOC
 * carrying a stale id needs — the client names the loc it drew, and by the time
 * the packet lands another player may have opened it.
 *
 * Returns a slot index for mock230_scene_loc, or -1.
 */
int
mock230_scene_find_loc(
    int x,
    int z,
    int level,
    int loc_id);

struct Mock230SceneLoc*
mock230_scene_loc(int slot);

/** The loc's `op_num`-th menu action from the cache ("Open", "Climb-up"), or
 *  NULL. This is where a stair's direction comes from: the cache already says
 *  it, so content does not have to. */
const char*
mock230_scene_loc_op(
    int loc_id,
    int op_num);

/** Replace a loc with another id (a door opening). Moves collision with it and
 *  marks the slot changed. Returns 0 when `slot` is not a live loc. */
int
mock230_scene_replace_loc(
    int slot,
    int loc_id,
    int angle);

/** Iterate the locs a rebuild has to re-send. Returns -1 when done. */
int
mock230_scene_next_changed_loc(int from);

/* ------------------------------------------------------------------ */
/* Collision                                                           */
/* ------------------------------------------------------------------ */

/**
 * Load the map squares covering the scene at `zone_x`/`zone_z` and build
 * collision for all four levels.
 *
 * Returns 1 on success, 0 when the cache or its XTEA keys are unavailable — in
 * which case every tile stays walkable and the mock behaves exactly as it did
 * before collision existed. That fallback is deliberate: a missing key file
 * should cost accuracy, not the ability to run.
 */
int
mock230_scene_build(
    const char* cache_dir,
    int zone_x,
    int zone_z);

void
mock230_scene_free(void);

/** Scene-local (0..103) collision map for a level, or NULL. */
struct CollisionMap*
mock230_scene_collision(int level);

/** Absolute tile of scene-local (0, 0). */
int
mock230_scene_base_x(void);
int
mock230_scene_base_z(void);

/**
 * Route from one absolute tile to another, writing absolute tiles into
 * `path_x`/`path_z` in walk order (nearest step first).
 *
 * Returns the number of steps, 0 when already there, or -1 when no route
 * exists. With no collision loaded this degrades to the straight-line
 * interpolation the mock used before, so a caller never has to branch on
 * whether the scene is built.
 */
int
mock230_scene_route(
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z,
    int* path_x,
    int* path_z,
    int max_steps);

/** Can an actor at (x, z) step to the adjacent tile in `dir` (the client's
 *  World_CoordStep numbering)? True with no collision loaded. */
int
mock230_scene_can_step(
    int level,
    int x,
    int z,
    int dir);

/** Is this absolute tile inside the built scene? */
int
mock230_scene_contains(
    int x,
    int z);

/**
 * Does this absolute tile block walking? (RuneScript `map_blocked`.)
 *
 * The reference is `isFlagged(x, z, level, CollisionFlag.WALK_BLOCKED)`, and
 * that composite is `COLL_FLAG_WALK_BLOCKED` here — so both ends read one
 * CollisionMap rather than keeping two models that can drift.
 *
 * **A tile outside the built scene reports blocked**, which is the safe
 * direction and not the obvious one. Content asks this before dropping a fire or
 * choosing a wander target, so "I do not know" has to mean "do not", or the
 * caller happily places things in map that was never loaded. Returning walkable
 * would also make the flag test the *only* thing standing between a script and
 * `collision_map_tile`'s bounds assert.
 */
int
mock230_scene_walk_blocked(
    int level,
    int x,
    int z);

#endif
