#ifndef WORLDVIEW_H
#define WORLDVIEW_H

#include <stdbool.h>

struct World;
struct WorldBuilder;

/*
 * Multi-world views for OSRS world entities (sailing) — see docs/SAILING.md §5.
 *
 * A Worldview is one drawable map view: the root scene, or the small stationary
 * rectangle of map a boat's deck lives on (deob class100 + the class61 registry
 * of 16). Every view pairs a World simulation with the WorldBuilder that keeps
 * it in sync with the shared ToriDraw scene. The registry indexes views by the
 * wire's world-entity id: id 0 is always the root, ids 1..15 are spawned by
 * WORLDENTITY_INFO (later phase) and addressed by SET_ACTIVE_WORLD and the
 * per-view rebuild.
 *
 * Named Worldview, not WorldEntity_*: this codebase already uses that prefix
 * for ordinary in-world entities (entity_facets.h).
 */

/** Deob class61 registers all views in one table of 16. */
#define WORLDVIEW_MAX 16

/** The root (main-world) view id — always live once registered. */
#define WORLDVIEW_ROOT 0

/** parent_id of the root: it hangs under nothing. */
#define WORLDVIEW_PARENT_NONE (-1)

struct Worldview
{
    /** Registry slot == the wire's world-entity id. */
    int id;
    struct World* world;
    struct WorldBuilder* builder;
    /**
     * South-west corner of the view's rectangle in root-world tile space (the
     * off-map staging region a boat deck is authored at). Actor membership is
     * geometric against this rectangle (deob class109.method3823). 0,0 for the
     * root, whose "rectangle" is everything no sub-view claims.
     */
    int base_x;
    int base_z;
    /** Rectangle size in tiles, multiples of 8 (packed-nibble zones × 8).
     * 0 for the root — its extent is the scene, not a membership box. */
    int size_x_tiles;
    int size_z_tiles;
    /** View this one is drawn inside (deob field5700); WORLDVIEW_PARENT_NONE
     * for the root. */
    int parent_id;
    bool live;
    /**
     * True when the registry owns world/builder and frees them on release.
     * The root borrows the App's pair (hundreds of call sites keep reading
     * app->world directly); every spawned view owns its own.
     */
    bool owns;
};

struct WorldviewRegistry
{
    struct Worldview views[WORLDVIEW_MAX];
};

void
WorldviewRegistry_Init(struct WorldviewRegistry* reg);

/**
 * Release every live owned view and clear the table. The root's borrowed
 * world/builder are NOT freed — their owner (the App) tears them down itself.
 * Accepts NULL (deallocator convention).
 */
void
WorldviewRegistry_Free(struct WorldviewRegistry* reg);

/**
 * Register the root view (id 0). BORROWS `world`/`builder`: the caller keeps
 * ownership and must outlive the registry's use of them.
 */
struct Worldview*
WorldviewRegistry_RegisterRoot(
    struct WorldviewRegistry* reg,
    struct World* world,
    struct WorldBuilder* builder);

/**
 * Register a non-root view. Takes OWNERSHIP of `world`/`builder`: both are
 * freed when the view is released. `parent_id` must name a live view (a boat
 * hangs under the root; a nested entity under its boat).
 */
struct Worldview*
WorldviewRegistry_Register(
    struct WorldviewRegistry* reg,
    int id,
    struct World* world,
    struct WorldBuilder* builder,
    int base_x,
    int base_z,
    int size_x_tiles,
    int size_z_tiles,
    int parent_id);

/**
 * Despawn a non-root view: frees its builder then its world, clears the slot.
 * The root is never released this way — it borrows the App's pair and lives
 * for the session (WorldviewRegistry_Free clears it at teardown).
 */
void
WorldviewRegistry_Release(
    struct WorldviewRegistry* reg,
    int id);

/**
 * Lookup by wire id, asserting the id names a live view. This is the packet
 * routing entry point: the deob throws on an unknown world-entity id, and a
 * server that addresses one is a protocol violation, not a state to limp past.
 */
struct Worldview*
WorldviewRegistry_Get(
    struct WorldviewRegistry* reg,
    int id);

/** True when `id` names a live view. For paths where "unknown" is an answer
 * (spawn-slot checks), not a violation. Asserts only the range. */
bool
WorldviewRegistry_IsLive(
    struct WorldviewRegistry const* reg,
    int id);

#endif
