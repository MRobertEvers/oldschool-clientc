#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_SCENE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_SCENE_H

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
#include "torirs_server_mapinstance.h"

struct CollisionMap;
struct ToriRS_FeatureTable;
struct ToriRSServerPlayer;

/* ------------------------------------------------------------------ */
/* Locs                                                                */
/* ------------------------------------------------------------------ */

/** Every op slot the loctype declares is shown — what a placement with nothing
 *  to hide carries, and what LOC_DEL's own reference call passes. */
#define TORIRSSERVER_LOC_OPS_ALL_SHOWN 0x1f
/** Longest replacement menu label. "Close"/"Open" are the whole of the current
 *  use; the cache's own longest loc op is 22 characters. */
#define TORIRSSERVER_LOC_OP_TEXT_MAX 32

/**
 * A right-click menu that belongs to one PLACEMENT of a loc, not to its type.
 *
 * These are LOC_ADD_CHANGE_V2's two extra fields, and they are the mechanism
 * the official client implements for a door that has no second cache record to
 * become. The 239 gamepack (`class69`, deob `src_osrs239_rl1_12_33`) keeps both
 * per scene loc and the loc menu builder (`class108`) reads them in this order:
 *
 *   1. skip op slot i entirely unless `flags` bit i is set    (class69.method1514)
 *   2. take the loctype's own label for slot i                (class393.method8819)
 *   3. REPLACE it with `name[i]` when the placement carries one (class69.method1518/1532)
 *
 * Step 3 fires on a slot the loctype leaves empty too — the row appears because
 * the override is non-NULL, not because the cache declared an op. That is what
 * lets one cache record be "Open" on the tile the map put it on and "Close" on
 * the tile it swung to, with no second loc id anywhere.
 *
 * `flags` = TORIRSSERVER_LOC_OPS_ALL_SHOWN and every `name` empty is the neutral
 * value: it is what a placement that overrides nothing carries, and it encodes
 * on the wire as the zero op-count the client reads as "use the loctype's own".
 */
struct ToriRSServerLocOps
{
    /** 5-bit mask of which op slots are SHOWN; bit 0 is op1. Zero is legal and
     *  means "hide every option" — a loc that draws, blocks and cannot be
     *  clicked. */
    int flags;
    /** Replacement label per op slot, or "" for "leave the loctype's own". */
    char name[5][TORIRSSERVER_LOC_OP_TEXT_MAX];
};

/** The neutral value: show everything, override nothing. Not a "missing ops"
 *  sentinel — it is the real menu of every loc the map placed. */
void
ToriRSServer_LocOpsDefault(struct ToriRSServerLocOps* ops);

/** 1 when `ops` is the neutral value, i.e. nothing needs to reach the wire. */
int
ToriRSServer_LocOpsIsDefault(const struct ToriRSServerLocOps* ops);

/**
 * The label this PLACEMENT shows for `op_num` (1..5), or NULL when the slot is
 * hidden or empty.
 *
 * `type_op` is what the loctype says, which the caller reads because this file
 * deliberately knows nothing about the content tree (see the overlay note
 * below). Steps 1-3 above, in that order.
 */
const char*
ToriRSServer_LocOpsLabel(
    const struct ToriRSServerLocOps* ops,
    int op_num,
    const char* type_op);

struct ToriRSServerSceneLoc
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
     *  addressable. `changed` used to be here; see torirs_server_scene.c on why the
     *  flag could not survive the rebuild it existed for. */
    int is_static;
    /** This placement's own menu (see struct ToriRSServerLocOps). A map-square loc
     *  carries the neutral value; only a `loc_add_ops` puts anything else here.
     *  Held on the scene as well as in the ZoneMap because the OPLOC validator
     *  reads the scene — a click on an op the placement invented has to be
     *  accepted, and a click on one it hid has to be dropped. */
    struct ToriRSServerLocOps ops;
};

/**
 * The loc the player is interacting with at (x, z, level).
 *
 * `loc_id` may be -1 to mean "whatever is there", which is what an OPLOC
 * carrying a stale id needs — the client names the loc it drew, and by the time
 * the packet lands another player may have opened it.
 *
 * Returns a slot index for ToriRSServer_SceneLoc, or -1.
 */
int
ToriRSServer_SceneFindLoc(
    int x,
    int z,
    int level,
    int loc_id);

struct ToriRSServerSceneLoc*
ToriRSServer_SceneLoc(int slot);

/**
 * The loc's `op_num`-th menu action ("Open", "Climb-up"), or NULL. This is where
 * a stair's direction comes from: the cache already says it, so content does not
 * have to.
 *
 * An authored `.loc` `opN=` wins over the cache record — rank 1 over rank 0, the
 * order `ToriRSServer_LocCategory` uses. See `ToriRSServer_SceneLocOpOverlay`.
 */
const char*
ToriRSServer_SceneLocOp(
    int loc_id,
    int op_num);

/**
 * State a loc's op from the *server* side: a `.loc` block's `op1=`..`op5=`.
 *
 * The op that matters is the one the cache does not carry. LostCity's skilling
 * loops resume on `p_oploc(3)` against an `op3=hidden`, and `p_oploc` returns
 * silently when the op it names is empty — so with no way to state one, every
 * chop and swing loop in this tree had to re-issue the *visible* op instead.
 *
 * "Hidden" needs no special handling and gets none: the client builds its menu
 * from its own cache group, so an op stated here and absent there is one no menu
 * offers, and `torirs_server_world.c`'s OPLOC handler drops the literal string
 * `hidden` off the wire. Authoring one widens what a script may re-issue, never
 * what a client may ask for.
 *
 * `op_num` is 1-based, as the packet and `p_oploc` both spell it. Copies `text`.
 */
void
ToriRSServer_SceneLocOpOverlay(
    int loc_id,
    int op_num,
    const char* text);

/** Drop every authored op. Paired with the content tree's own reload. */
void
ToriRSServer_SceneLocOpOverlayReset(void);

/** How many locs carry an authored op — a count for the boot report, and the
 *  one number that distinguishes "the overlay is empty" from "the overlay
 *  disagrees", which otherwise look identical from a failing script. */
int
ToriRSServer_SceneLocOpOverlayCount(void);

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
ToriRSServer_LocResolveTransform(
    struct ToriRSServerPlayer* player,
    int base_loc_id);

/** Replace a loc with another id (a door opening), moving collision with it.
 *  An inactive static slot is revived rather than refused, which is what puts a
 *  `loc_del`'d loc back. Returns 0 when `slot` is not a loc or `loc_id` is not
 *  in the cache. */
int
ToriRSServer_SceneReplaceLoc(
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
ToriRSServer_SceneAddLoc(
    int x,
    int z,
    int level,
    int loc_id,
    int shape,
    int angle);

/** Install this placement's own menu on a live scene slot (see
 *  struct ToriRSServerLocOps). Asserts the slot is live — a menu with nothing to
 *  hang it on is a caller bug, not an empty case. */
void
ToriRSServer_SceneLocSetOps(
    int slot,
    const struct ToriRSServerLocOps* ops);

/**
 * The label the PLACEMENT at `slot` shows for `op_num` (1..5), or NULL.
 *
 * The click validator's question, as against `ToriRSServer_SceneLocOp`'s: that
 * one answers for the *type* (cache plus content overlay) and cannot see an op
 * a single placement invented or hid. `slot` < 0 falls back to the type alone,
 * which is what a click on a loc outside the scene window has to be judged on.
 */
const char*
ToriRSServer_SceneLocPlacementOp(
    int slot,
    int loc_id,
    int op_num);

/** Remove a loc (`loc_del`), freeing its collision. A map-square loc leaves a
 *  tombstone behind; a `loc_add`ed one frees its slot for reuse. Returns 0 when
 *  `slot` is not a live loc. */
int
ToriRSServer_SceneRemoveLoc(int slot);

/**
 * The loc *exactly* at this tile with this shape, active or a tombstone.
 *
 * The mutation lookup, as against `ToriRSServer_SceneFindLoc`'s click lookup: a
 * LOC_ADD_CHANGE or LOC_DEL names a tile and a shape and the client matches on
 * both, so the server has to address the same thing. Returns a slot, or -1.
 */
int
ToriRSServer_SceneFindLocExact(
    int x,
    int z,
    int level,
    int shape);

/**
 * The *live* loc with this id whose south-west corner is this tile, or -1.
 *
 * `loc_find`'s lookup — the reference's `Zone.getLoc(x, z, locId)`, corner tile
 * and type both exact. Distinct from `ToriRSServer_SceneFindLoc`, whose footprint
 * match and any-loc fallback exist for stale OPLOC clicks and must not answer a
 * find, and from `_exact`, which matches tombstones because a mutation has to
 * address a deleted map loc.
 */
int
ToriRSServer_SceneFindLocId(
    int x,
    int z,
    int level,
    int loc_id);

/* ------------------------------------------------------------------ */
/* Scene windows                                                       */
/* ------------------------------------------------------------------ */

/**
 * One 104x104 collision window. The pool holds one ROOT window (index 0 — the
 * anchor world logic with no player in hand queries through) plus one window
 * per player slot (index 1 + pid). Opaque: the world addresses windows through
 * the functions below and the scene keeps overlapping windows coherent, so any
 * built window covering a tile gives the same answer for it.
 *
 * Every build, loc-slot lookup and loc mutation in this module acts on the
 * BOUND window; the stateless queries (routes, LoS, occupancy, tile flags)
 * resolve their own window per call and only prefer the bound one.
 */
struct ToriRSServerSceneWindow;

/** 1 root + one per player slot + one per vessel-deck slot.
 *  torirs_server_scene.h cannot include torirs_server.h, so this restates
 *  1 + TORIRSSERVER_PLAYER_MAX + TORIRSSERVER_VESSEL_WINDOW_MAX; the scene
 *  module holds the arithmetic to the same values at compile time.
 *
 *  The vessel slots exist because a rider needs TWO collision domains at
 *  once: their own steps resolve against the deck instance (pool
 *  coordinates), while their scene window follows the hull across the
 *  water (observed coordinates). A vessel's deck window pins the former
 *  while the rider's own window serves the latter. */
#define TORIRSSERVER_SCENE_WINDOW_MAX 17
#define TORIRSSERVER_SCENE_VESSEL_WINDOW_BASE 9
#define TORIRSSERVER_SCENE_VESSEL_WINDOW_MAX 8

/** The world's root window — the default binding, and what a fresh process is
 *  bound to before anything is built. */
struct ToriRSServerSceneWindow*
ToriRSServer_SceneWindowRoot(void);

/** Window by pool index (0 = root, 1 + pid = that player's). Aborts on an
 *  index outside the pool — there is no "maybe" window. */
struct ToriRSServerSceneWindow*
ToriRSServer_SceneWindowByIndex(int index);

/** Direct every bound-window operation (builds, loc slots, mutations, the
 *  legacy accessors) at `window` until the next bind. */
void
ToriRSServer_SceneBindWindow(struct ToriRSServerSceneWindow* window);

struct ToriRSServerSceneWindow*
ToriRSServer_SceneBoundWindow(void);

/** 1 once a build has given the window an origin; 0 for a fresh or released
 *  window (and for a build that found no cache). */
int
ToriRSServer_SceneWindowBuilt(const struct ToriRSServerSceneWindow* window);

/** Inside THIS window's built bounds — as against ToriRSServer_SceneContains,
 *  which asks whether ANY window covers the tile. */
int
ToriRSServer_SceneWindowContains(
    const struct ToriRSServerSceneWindow* window,
    int x,
    int z);

/** Some built window containing the absolute tile — the bound one when it
 *  qualifies — or NULL when the tile is outside every built window. */
struct ToriRSServerSceneWindow*
ToriRSServer_SceneWindowFind(
    int x,
    int z);

/** Free a window's collision and locs and mark it unbuilt. A deallocator —
 *  NULL is accepted. Releasing the bound window rebinds the root. */
void
ToriRSServer_SceneWindowRelease(struct ToriRSServerSceneWindow* window);

/* ------------------------------------------------------------------ */
/* Collision                                                           */
/* ------------------------------------------------------------------ */

/**
 * Load the map squares covering the scene at `zone_x`/`zone_z` and build
 * collision for all four levels — into the BOUND window, replacing whatever it
 * held. The other windows and the shared loc configs are untouched.
 *
 * Returns 1 on success, 0 when the cache or its XTEA keys are unavailable — in
 * which case every tile stays walkable and the mock behaves exactly as it did
 * before collision existed. That fallback is deliberate: a missing key file
 * should cost accuracy, not the ability to run.
 */
int
ToriRSServer_SceneBuild(
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
 * is the same structure `ToriRSServer_SendRebuildRegion` encodes, so the collision
 * the server routes on and the map the client draws come from one description.
 *
 * Returns 1 on success, 0 when the cache is unavailable — same fallback as above.
 */
int
ToriRSServer_SceneBuildInstance(
    const char* cache_dir,
    int zone_x,
    int zone_z,
    const struct ToriRSServerMapInstanceWindow* window);

/** Whole-module teardown as one scene: releases the BOUND window and drops the
 *  shared loc configs. The world releases its other windows itself, through
 *  ToriRSServer_SceneWindowRelease. */
void
ToriRSServer_SceneFree(void);

/** The BOUND window's scene-local (0..103) collision map for a level, or NULL. */
struct CollisionMap*
ToriRSServer_SceneCollision(int level);

/** Raw collision flags at an absolute tile; 0 outside the built scene. */
int
ToriRSServer_SceneTileFlags(int level, int x, int z);

/** Absolute tile of scene-local (0, 0). */
int
ToriRSServer_SceneDebugSettings(int level, int x, int z);

int
ToriRSServer_SceneBaseX(void);
int
ToriRSServer_SceneBaseZ(void);

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
ToriRSServer_SceneRoute(
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
ToriRSServer_SceneRouteOp(
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
ToriRSServer_SceneLocApproach(
    int slot,
    struct CollisionApproach* out);

/** NPC approach: size-aware under the OSRS era, 1x1 under LostCity. */
void
ToriRSServer_SceneNpcApproach(
    int size,
    struct CollisionApproach* out);

/** Ground-obj approach. retry_adjacent=0 is EXACT; 1 is the 1x1 adjacent retry. */
void
ToriRSServer_SceneObjApproach(
    int retry_adjacent,
    struct CollisionApproach* out);

/** Era op-click nearest-fallback options. */
void
ToriRSServer_SceneOpNearestOpts(struct CollisionNearestOpts* out);

/** Era ground/minimap-click nearest-fallback options — the era table's
 *  `ground_click_nearest_model`, resolved through
 *  collision_nearest_opts_from_model. This is the whole of "the click was
 *  unreachable, walk as close as you can" for a modern client, which sends the
 *  clicked tile and never routes — plus `ground_click_nearest_unbounded`, this
 *  client's answer for a click the era's box cannot reach at all. */
void
ToriRSServer_SceneGroundNearestOpts(struct CollisionNearestOpts* out);

/** Is (x,z) an arrival for approach at (dst_x,dst_z)? */
int
ToriRSServer_SceneReached(
    int level,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach);

/** Install the era feature table used for approach/nearest. */
void
ToriRSServer_SceneSetFeatures(struct ToriRS_FeatureTable const* features);

struct ToriRS_FeatureTable const*
ToriRSServer_SceneFeatures(void);

/** Can an actor at (x, z) step to the adjacent tile in `dir` (the client's
 *  World_CoordStep numbering)? True with no collision loaded. */
int
ToriRSServer_SceneCanStep(
    int level,
    int x,
    int z,
    int dir);

/** Like can_step, but ORs `extra_flag` into the can_travel occupancy test. */
int
ToriRSServer_SceneCanStepExtra(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag);

/** Like can_step_extra, under an `enum CollisionType` (an npc's moverestrict). */
int
ToriRSServer_SceneCanStepTyped(
    int level,
    int x,
    int z,
    int dir,
    int extra_flag,
    int coll_type);

int
ToriRSServer_SceneCanTravel(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag);

int
ToriRSServer_SceneCanTravelTyped(
    int level,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag,
    int coll_type);

int
ToriRSServer_SceneLineOfSight(
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
ToriRSServer_SceneLineOfWalk(
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
ToriRSServer_SceneApproached(
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
 * Same ray as ToriRSServer_SceneApproached, but symmetric when the era says so
 * (`los_symmetric_pvp` — the 2019 LMS change, PvP only). Player-vs-npc and
 * npc-vs-player must keep calling ToriRSServer_SceneApproached: PvM never became
 * symmetric.
 */
int
ToriRSServer_SceneApproachedPvp(
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
ToriRSServer_SceneChangeOccupancy(
    int level,
    int x,
    int z,
    int size,
    int mask,
    int add);

/** Naive path in world coords: writes one absolute tile into out_x/out_z. */
int
ToriRSServer_SceneNaivePath(
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
ToriRSServer_SceneContains(
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
ToriRSServer_SceneWalkBlocked(
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
ToriRSServer_SceneCheckvis(
    int checkvis,
    int level,
    int from_x,
    int from_z,
    int to_x,
    int to_z);

#endif
