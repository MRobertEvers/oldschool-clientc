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

#include "engine/world_builder/collision_map.h"
#include "mock230_mapinstance.h"

struct CollisionMap;
struct ToriRS_FeatureTable;
struct Mock230Player;

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
    /** 1 when the map square put it here. An inactive static loc is a
     *  *tombstone*: its slot is never handed to a `loc_add`, because the square
     *  still has a loc on that tile and "it has been removed" has to stay
     *  addressable. `changed` used to be here; see mock230_scene.c on why the
     *  flag could not survive the rebuild it existed for. */
    int is_static;
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

/**
 * Multiloc resolve for a player's current varbits (OpenRS2 LocType.getMultiLoc /
 * LostCity OpLocHandler remap). Returns the active child loc id. When the base
 * has no transform table, returns `base_loc_id`. When the selected slot (or the
 * last-entry fallback) is -1, returns -1 (hidden / no ops).
 *
 * The scene loc entity stays the BASE id; callers use this only for op
 * validation and trigger type/category lookup.
 */
int
mock230_loc_resolve_transform(
    struct Mock230Player* player,
    int base_loc_id);

/** Replace a loc with another id (a door opening), moving collision with it.
 *  An inactive static slot is revived rather than refused, which is what puts a
 *  `loc_del`'d loc back. Returns 0 when `slot` is not a loc or `loc_id` is not
 *  in the cache. */
int
mock230_scene_replace_loc(
    int slot,
    int loc_id,
    int angle);

/**
 * Add a loc the map square did not have (`loc_add`).
 *
 * Returns its slot, or -1 when the loc id is not in the cache or the tile is
 * outside the scene. A freed slot is reused before the array grows, because
 * slots are never compacted and content that cycles a loc on a timer would
 * otherwise leak one per cycle.
 */
int
mock230_scene_add_loc(
    int x,
    int z,
    int level,
    int loc_id,
    int shape,
    int angle);

/** Remove a loc (`loc_del`), freeing its collision. A map-square loc leaves a
 *  tombstone behind; a `loc_add`ed one frees its slot for reuse. Returns 0 when
 *  `slot` is not a live loc. */
int
mock230_scene_remove_loc(int slot);

/**
 * The loc *exactly* at this tile with this shape, active or a tombstone.
 *
 * The mutation lookup, as against `mock230_scene_find_loc`'s click lookup: a
 * LOC_ADD_CHANGE or LOC_DEL names a tile and a shape and the client matches on
 * both, so the server has to address the same thing. Returns a slot, or -1.
 */
int
mock230_scene_find_loc_exact(
    int x,
    int z,
    int level,
    int shape);

/**
 * The *live* loc with this id whose south-west corner is this tile, or -1.
 *
 * `loc_find`'s lookup — the reference's `Zone.getLoc(x, z, locId)`, corner tile
 * and type both exact. Distinct from `mock230_scene_find_loc`, whose footprint
 * match and any-loc fallback exist for stale OPLOC clicks and must not answer a
 * find, and from `_exact`, which matches tombstones because a mutation has to
 * address a deleted map loc.
 */
int
mock230_scene_find_loc_id(
    int x,
    int z,
    int level,
    int loc_id);

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

/**
 * The same build, but for a **map instance**: every zone of the scene comes from
 * the `window` descriptor rather than from the square under it.
 *
 * A zone the window does not set stays void — no cache square is read at the
 * destination at all, which is both what makes an instance private and what the
 * wire means (a REBUILD_REGION descriptor bit of 0 is "no source"). The window
 * is the same structure `mock230_send_rebuild_region` encodes, so the collision
 * the server routes on and the map the client draws come from one description.
 *
 * Returns 1 on success, 0 when the cache is unavailable — same fallback as above.
 */
int
mock230_scene_build_instance(
    const char* cache_dir,
    int zone_x,
    int zone_z,
    const struct Mock230MapInstanceWindow* window);

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
 * interpolation the mock used before. An endpoint outside the built scene
 * returns -1 rather than walking through unmapped walls.
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

/**
 * Route toward a target under an approach predicate (loc/npc/obj click).
 *
 * Uses the era's op-click nearest fallback. Writes walk-order tiles; on
 * success *out_arrive_x/z (when non-NULL) receive the arrival tile, which may
 * differ from (to_x,to_z) when the fallback fired.
 */
int
mock230_scene_route_op(
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z,
    struct CollisionApproach const* approach,
    int* path_x,
    int* path_z,
    int max_steps,
    int* out_arrive_x,
    int* out_arrive_z);

/** Build the approach descriptor for a scene loc slot (mirrors app_scenery_approach). */
void
mock230_scene_loc_approach(
    int slot,
    struct CollisionApproach* out);

/** NPC approach: size-aware under the OSRS era, 1x1 under LostCity. */
void
mock230_scene_npc_approach(
    int size,
    struct CollisionApproach* out);

/** Ground-obj approach. retry_adjacent=0 is EXACT; 1 is the 1x1 adjacent retry. */
void
mock230_scene_obj_approach(
    int retry_adjacent,
    struct CollisionApproach* out);

/** Era op-click nearest-fallback options. */
void
mock230_scene_op_nearest_opts(struct CollisionNearestOpts* out);

/** Era ground/minimap-click nearest-fallback options — the era table's
 *  `ground_click_nearest_model`, resolved through
 *  collision_nearest_opts_from_model. This is the whole of "the click was
 *  unreachable, walk as close as you can" for a modern client, which sends the
 *  clicked tile and never routes. */
void
mock230_scene_ground_nearest_opts(struct CollisionNearestOpts* out);

/** Is (x,z) an arrival for approach at (dst_x,dst_z)? */
int
mock230_scene_reached(
    int level,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach);

/** Install the era feature table used for approach/nearest. */
void
mock230_scene_set_features(struct ToriRS_FeatureTable const* features);

struct ToriRS_FeatureTable const*
mock230_scene_features(void);

/** Can an actor at (x, z) step to the adjacent tile in `dir` (the client's
 *  World_CoordStep numbering)? True with no collision loaded. */
int
mock230_scene_can_step(
    int level,
    int x,
    int z,
    int dir);

/** Like can_step, but ORs `extra_flag` into the can_travel occupancy test. */
int
mock230_scene_can_step_extra(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag);

/** Like can_step_extra, under an `enum CollisionType` (an npc's moverestrict). */
int
mock230_scene_can_step_typed(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag,
    int coll_type);

int
mock230_scene_can_travel(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag);

int
mock230_scene_can_travel_typed(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag,
    int coll_type);

int
mock230_scene_line_of_sight(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh,
    int extra);

int
mock230_scene_line_of_walk(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh,
    int extra);

int
mock230_scene_approached(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh);

/**
 * AP LoS between two *players*.
 *
 * Same ray as mock230_scene_approached, but symmetric when the era says so
 * (`los_symmetric_pvp` — the 2019 LMS change, PvP only). Player-vs-npc and
 * npc-vs-player must keep calling mock230_scene_approached: PvM never became
 * symmetric.
 */
int
mock230_scene_approached_pvp(
    int level,
    int sx,
    int sz,
    int dx,
    int dz,
    int sw,
    int sh,
    int dw,
    int dh);

void
mock230_scene_change_occupancy(
    int level,
    int x,
    int z,
    int size,
    int mask,
    int add);

/** Naive path in world coords: writes one absolute tile into out_x/out_z. */
int
mock230_scene_naive_path(
    int level,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag,
    unsigned* rng,
    int* out_x,
    int* out_z);

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

/**
 * LostCity `HuntVis`: 0 = off, 1 = lineofsight, 2 = lineofwalk.
 *
 * Returns 1 when the candidate passes. Direction is caller-owned — npc finds
 * cast source→candidate; player `huntall` casts candidate→source
 * (`ScriptIterators.ts`). Casts a 1×1→1×1 ray with extra_flag 0.
 */
int
mock230_scene_checkvis(
    int checkvis,
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z);

#endif
