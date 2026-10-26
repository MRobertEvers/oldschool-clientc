#ifndef WORLDVIEW_H
#define WORLDVIEW_H

#include <stdbool.h>

struct World;
struct WorldBuilder;

/*
 * Multi-world views for OSRS world entities (sailing) — see docs/SAILING.md §5.
 *
 * A Worldview is one drawable map view: the root scene, or the small stationary
 * rectangle of map a boat's deck lives on (deob class100 + the class61
 * registry). Every view pairs a World simulation with the WorldBuilder that
 * keeps it in sync with the shared ToriDraw scene.
 *
 * The registry indexes views by a local SLOT, not by the wire's world-entity
 * id. Slot 0 is always the root; slots 1..WORLDVIEW_MAX-1 are handed out to
 * WORLDENTITY_INFO spawns, and each remembers the wire id it was spawned
 * under (WorldviewRegistry_SlotForWire). The wire's ids are global indices
 * the server shares between every observer -- RSProx 4281 (rev 235) spawns
 * 250 distinct hulls with ids 0..4090 -- so they cannot be array indices.
 *
 * Named Worldview, not WorldEntity_*: this codebase already uses that prefix
 * for ordinary in-world entities (entity_facets.h).
 */

/**
 * Local view slots, the root included.
 *
 * The deob has no such bound: class61 keeps its views in a NodeHashTable of
 * 16 BUCKETS keyed by id (class622(16)), and the root view's entity table is
 * one of 32 buckets (class100's class622(32)). A busy Salvaging spot already
 * has 25 hulls in view at once (RSProx 4281), so this is sized well past
 * that; the scene's one-byte pool tag caps it at TORIDRAW_SCENE_POOL_VIEW_MAX.
 */
#define WORLDVIEW_MAX 64

/** The root (main-world) view slot — always live once registered. */
#define WORLDVIEW_ROOT 0

/**
 * The root's wire id as the packet layer hands it over, whatever the
 * revision wrote: up to 236 the root went out as -1 and a hull could be id 0;
 * 237 made the root 0 (RSProx SetActiveWorldV2Decoder). The adapters
 * normalise to this so a hull's id 0 never reads as the root.
 */
#define WORLDVIEW_WIRE_ROOT (-1)

/** WorldviewRegistry_SlotForWire / _FreeSlot: no such view, no free slot. */
#define WORLDVIEW_SLOT_NONE (-1)

/** parent_id of the root: it hangs under nothing. */
#define WORLDVIEW_PARENT_NONE (-1)

struct Worldview
{
    /** Registry slot (local; never on the wire). */
    int id;
    /** The world-entity id the server spawned this view under;
     *  WORLDVIEW_WIRE_ROOT for the root. Meaningful only while live. */
    int wire_id;
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
    /**
     * The level this view's carrier occupies in its PARENT world — the level
     * its pseudo-loc is registered on: the parent view's own `level` when the
     * spawn record executes under the parent's cursor.
     *
     * NOT the plane the deck sits on inside this view's own world: that is
     * `WevConfig.plane`, a coordinate in the staging region the deck is
     * authored at, and using it in the parent registers a boat floating on
     * open water at level 1. The parent then masks it away entirely, because
     * the root's draw mask is clamped to the player's own roof level
     * (app.c, `app_world_roof_check`) — the loc paints every frame and is
     * culled every frame.
     *
     * Set at the spawn (App_WevSpawn), never by the deck's own
     * SET_ACTIVE_WORLD: that names the DECK's plane (`level` below).
     */
    int parent_level;
    /**
     * This view's OWN level (deob class100.field1400): what the last
     * SET_ACTIVE_WORLD addressing it named, and kept after the tick fence
     * puts the cursor back on the root. Jagex names a deck's own plane for
     * the deck (01 in RSProx 4281) and the root's plane for the root (00);
     * root-world judgments read the root's (Statics.java's
     * method109(field3266, .., field3266.field1400)).
     */
    int level;
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
 * Register a non-root view in slot `id` (one WorldviewRegistry_FreeSlot gave
 * out) under the server's world-entity id `wire_id`, which must not already
 * name a live view. Takes OWNERSHIP of `world`/`builder`: both are freed when
 * the view is released. `parent_id` must name a live view (a boat hangs under
 * the root; a nested entity under its boat).
 */
struct Worldview*
WorldviewRegistry_Register(
    struct WorldviewRegistry* reg,
    int id,
    int wire_id,
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

/**
 * The live view the server knows as `wire_id` (WORLDVIEW_WIRE_ROOT is the
 * root, slot 0), or WORLDVIEW_SLOT_NONE. Unknown is an answer here: the id is
 * wire data, and the caller decides whether naming a dead view is refused.
 * A linear walk of WORLDVIEW_MAX slots, which is cheaper than any table a
 * 16-bit id space would need.
 */
int
WorldviewRegistry_SlotForWire(
    struct WorldviewRegistry const* reg,
    int wire_id);

/** The lowest slot no live view holds, or WORLDVIEW_SLOT_NONE when every one
 * of WORLDVIEW_MAX-1 is taken. */
int
WorldviewRegistry_FreeSlot(struct WorldviewRegistry const* reg);

/**
 * Which view's staging rectangle holds an absolute root tile, and where the
 * tile lands inside it.
 *
 * This is the registry's half of actor membership: every spawned view reserves
 * a rectangle of off-map map square, and a wire coordinate that falls in one of
 * them names a spot on that view's deck rather than a spot on the open map. An
 * entity's HOME view is the answer, which is what a spawn or an entity-info
 * update needs before it can decide which world to put the actor in.
 *
 * The root is the answer for everything unclaimed, and then the outputs are the
 * input: the root's "rectangle" is whatever no sub-view took, so a tile out on
 * the real map is already in root-local terms. Both outputs are required.
 *
 * Reservations do not overlap, so the first match is the only match. This is
 * the rectangle test alone -- the deck's own geometry (a hull is not its whole
 * rectangle) is Wev_DeckContainsParentPoint's question, asked later and in the
 * parent's fine units rather than in absolute tiles.
 */
int
WorldviewRegistry_HomeViewForAbsTile(
    struct WorldviewRegistry const* reg,
    int abs_tile_x,
    int abs_tile_z,
    int* out_local_x,
    int* out_local_z);

#endif
