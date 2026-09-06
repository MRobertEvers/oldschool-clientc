#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_VESSEL_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_VESSEL_H

/*
 * Vessels — the server side of a sailable boat (docs/SAILING_PLAN.md S1).
 *
 * A vessel is two things stitched together by a transform:
 *
 *   1. A **deck**: an ordinary map instance from the shared pool
 *      (torirs_server_mapinstance.h), assembled at pool coordinates far off the
 *      real map. Players who board stand on those tiles like any instance.
 *
 *   2. A **hull**: a `size_x` x `size_z` tile rectangle projected onto the sea
 *      at a fine-unit position and a 2048-space angle. The hull is what turns,
 *      moves and collides; the deck never moves.
 *
 * Units, all inherited from the client engine (docs/SAILING.md §5.5):
 *
 *   - fine coordinates: 128 units per tile (tile = fine >> 7)
 *   - angles: 2048 units per full turn (& 0x7FF)
 *   - headings: 16 compass points, i.e. multiples of 128 in angle space
 *   - movement quantum: 32 fine units (a quarter tile) per axis per tick
 *   - speed tiers 1..7: 64-unit increments, capped by native boat stats
 *
 * Movement convention matches rsprot's WorldEntityAvatar precedent:
 * dx = -sin(angle), dz = -cos(angle) — angle 0 sails south, 512 sails west.
 *
 * S1 scope: vessels exist, turn and move server-side, and refuse to drive onto
 * land. Nothing here reaches the wire — WORLDENTITY_INFO is S2.
 */

#include <stdint.h>

struct ToriRSServer;
struct ToriRSServerPlayer;

/** Concurrent vessels. Same order of magnitude as the map-instance pool that
 *  feeds their decks (each vessel owns one reservation of the 8). */
#define TORIRSSERVER_VESSEL_MAX 32

/**
 * World-view ids a vessel can be given on the wire, 1..15.
 *
 * WORLDENTITY_INFO's id IS the client's world-view id (src/world/worldview.h
 * has 16 views and view 0 is the root), so the wire cannot name more than 15
 * live entities however many hulls the server pool holds. Vessels beyond that
 * exist server-side and simply have no view — `view_id == 0`.
 */
#define TORIRSSERVER_WEV_VIEW_MAX 15

/** All five WORLDENTITY_INFO right-click ops enabled — the client's own
 *  default (PKT_WEV_OP_MASK_ALL / WEV_OP_MASK_ALL), restated here because the
 *  server tree does not include the client's headers. */
#define TORIRSSERVER_WEV_OP_MASK_ALL 31

/** Fine units per tile — the client's 128-unit tile, restated here because the
 *  server tree does not include the renderer's headers. */
#define TORIRSSERVER_VESSEL_FINE_PER_TILE 128

/** Movement quantum: every per-tick step is a multiple of a quarter tile. */
#define TORIRSSERVER_VESSEL_FINE_QUANTUM 32

/** Angle units in a full circle, and the mask that wraps one. */
#define TORIRSSERVER_VESSEL_ANGLE_UNITS 2048
#define TORIRSSERVER_VESSEL_ANGLE_MASK 0x7FF

/** Angle units between adjacent 16-point compass headings. */
#define TORIRSSERVER_VESSEL_HEADING_STEP 128

/** Core parts, thirteen facility hotspots, keel and cosmetic slots. Values
 *  are one-based selections in the native boat row's option columns. */
#define TORIRSSERVER_VESSEL_FACILITY_SLOTS 21
#define TORIRSSERVER_VESSEL_FACILITY_KEEL 16
#define TORIRSSERVER_VESSEL_FACILITY_FLAG 17
#define TORIRSSERVER_VESSEL_FACILITY_BRAZIER 18
#define TORIRSSERVER_VESSEL_FACILITY_TRIM 19
#define TORIRSSERVER_VESSEL_FACILITY_PATTERN 20
#define TORIRSSERVER_VESSEL_FACILITY_HOTSPOT_BASE 3
#define TORIRSSERVER_VESSEL_FACILITY_SAIL 0
#define TORIRSSERVER_VESSEL_FACILITY_HELM 1
#define TORIRSSERVER_VESSEL_FACILITY_HULL 2

/** How far a gangplank looks for ground when putting a rider ashore. Wide
 *  enough to clear the longest hull's footprint from its centre, short enough
 *  that "there is no shore here" still means it. */
#define TORIRSSERVER_VESSEL_DISEMBARK_RANGE 12

/** Current server turn cap: 128 angle units per tick = 90 degrees in 4 ticks.
 *  Rotation, including anchored turns, always sweeps the native hull bounds. */
#define TORIRSSERVER_VESSEL_TURN_RATE_DEFAULT 128

/** Speed tiers, 1-based; tier * 64 fine units per tick. */
#define TORIRSSERVER_VESSEL_SPEED_TIER_MIN 1
#define TORIRSSERVER_VESSEL_SPEED_TIER_MAX 7

enum ToriRSServerVesselState
{
    /** Parked: no command at all — the mover leaves the vessel alone. */
    TORIRSSERVER_VESSEL_IDLE = 0,
    /** Sail on the commanded 16-point heading until told otherwise. */
    TORIRSSERVER_VESSEL_HEADING = 1,
    /** Sail toward a fine-coordinate target, re-deriving the 16-point heading
     *  every tick, and stop (-> IDLE) on arrival or on a blocked step. */
    TORIRSSERVER_VESSEL_TARGET = 2,
};

struct ToriRSServerVessel
{
    /** 0 = free pool slot. Everything below is meaningful only when set. */
    int in_use;
    /** 1-based handle, == pool index + 1. 0 is the surface's only "no
     *  vessel" value, mirroring the map-instance convention. */
    int index;

    /**
     * The client world-view id this hull is published under, 1..15, or 0 when
     * every view was taken at spawn time.
     *
     * Distinct from `index` on purpose: the vessel pool is 32 deep and the
     * wire's registry is 15, so a 1:1 mapping would put ids on the wire the
     * client rejects as malformed (its decoder refuses id > 16 outright).
     */
    int view_id;

    /**
     * A number no other hull has ever carried, handed out at spawn and never
     * recycled — unlike `index` (a pool slot) and `view_id` (15 of them, taken
     * lowest-free).
     *
     * Both of those come back around, and a free followed by a spawn inside one
     * tick hands the new hull the old hull's slot AND the old hull's view. A
     * wire encoder comparing either would then describe the new boat as a
     * MOVE of the old one: the client keeps the previous config's model and
     * deck size and slides it across the water to the new position, with no
     * packet malformed anywhere. Comparing serials is what makes that case a
     * despawn and a respawn instead.
     */
    int serial;

    /** Archive-72 hull id, decoded by the same implementation as the client.
     *  Native bounds, offsets, pivot and deck plane drive movement/projection. */
    int config_id;

    /** Deck reservation extents. Native archive-72 hull bounds and signed
     *  offsets determine collision; these dimensions are the fallback for a
     *  config with no authored hull bounds (the Zenith or synthetic tests). */
    int size_x_tiles;
    int size_z_tiles;

    /** Hull integrity, published to the native sidepanel. Grounding damage
     *  and real repair-kit consumption are handled by sailing content. */
    int hp;
    int hp_max;

    /** The boat's name, as the cache composes it: 1-based picks into the
     *  `sailing_boat_name_options` descriptor and noun tables (varbits
     *  19149/19150 on varp `sailing_boarded_boat_name`; the prefix table
     *  ships no options). 0/0 = unnamed, which the panel shows as the noun
     *  table's default, "Boat". Spawn picks a deterministic pair so every
     *  hull reads as a named ship until shipyard content lets players
     *  choose. */
    int name_descriptor;
    int name_noun;

    /**
     * Set by the mover on the tick a commanded hull is refused by the water
     * and parks, cleared once reported. The park itself is correct (a whole
     * step or none), but it is SILENT: content has just told the helmsman
     * "the boat gets under way", and without this the boat simply stops with
     * nothing said. ToriRSServer_VesselTakeBlocked drains it.
     */
    int blocked_notice;

    /**
     * Which option each facility slot holds — 1-based picks into the boat's
     * `sailing_boat` dbrow option columns, 0 for an empty slot. The sailing
     * sidepanel's Facilities tab renders these (varbits
     * `sailing_sidepanel_facility_{sail,helm,hull}`, resolved client-side
     * through cs2 9026), so what the panel lists is what content actually
     * placed on the deck rather than a fixed guess.
     */
    int facility[TORIRSSERVER_VESSEL_FACILITY_SLOTS];

    /** The template zone base this deck was filled from (absolute tiles;
     *  -1,-1 before the deck is built). The facility dbrows state their
     *  placements as template-absolute coords, so content's deck-furnishing
     *  proc needs the base to rebase them onto the instance. */
    int deck_src_x;
    int deck_src_z;

    /** Deck map-instance handle (1-based), owned by this vessel: spawned with
     *  it, released by ToriRSServer_VesselFree. */
    int instance;

    /** Priority group for S2's worldentity info protocol; 0 for now. */
    int priority;
    /** Owning player uid, or 0 for a world-owned vessel. */
    int owner_uid;
    /** 1..5 indexes the captain's native cargo containers; 0 until claimed. */
    int cargo_slot;

    /**
     * One-shot wire seq (WORLDENTITY_INFO updateFlags 0x1) — the cache's
     * `sailing_worldentity_boat_*_sink_01` family. Content sets the pair and
     * bumps seq_stamp (::vesselseq); the encoder sends it once per stamp per
     * observer (player->wev_seq_stamps). seq_id -1 = nothing pending; 65535
     * is the wire's explicit clear.
     */
    int seq_id;
    int seq_delay;
    int seq_stamp;
    /** Monotonic discontinuity stamp; each observer receives one op-3 snap. */
    int teleport_stamp;

    /** Root-world transform: plane, fine-unit position of the hull's CENTER,
     *  and the yaw in 2048-space. */
    int level;
    int fine_x;
    int fine_z;
    int angle;

    /** Movement command. `heading` is a 16-point index (0..15) whose meaning
     *  is heading * 128 in angle space; `speed_tier` is 1..7, bounded by the boat's speed cap. */
    enum ToriRSServerVesselState state;
    int heading;
    int speed_tier;
    /** Native boat/facility stats, populated by content from sailing dbrows.
     *  Speeds use fine units per server tick; a zero cap in a synthetic fixture
     *  preserves the historical 256-unit limit. */
    int base_speed_fine;
    int speed_cap_fine;
    int acceleration_fine;
    int boost_duration;
    int armour;
    /**
     * The launch-model controls (docs/SAILING.md §7, OSRS wiki "Sailing"):
     * `sails_set` is the instant go/stop gate — a HEADING command turns the
     * hull with sails down, but it only translates once the sails are set.
     * `reversing` nudges the hull BACKWARD at the base 0.5 tiles/tick and is
     * only honoured with the sails un-set, the wiki's "reverse the boat if
     * stationary with the sails un-set". TARGET sails ignore both: a targeted
     * sail is a harness/scripted move, not a helm.
     */
    int sails_set;
    int reversing;
    int anchored; /* Stops translation while retaining the chosen heading/sails. */
    /** Max angle units turned per tick (shortest arc, clamped to this). */
    int turn_rate;
    /** TARGET state's destination, fine units (already quantum-aligned). */
    int target_fine_x;
    int target_fine_z;

    /** Sub-quantum movement carry per axis: the ideal (trig) displacement
     *  minus the quantized steps actually taken, so quarter-tile steps sum to
     *  the commanded path instead of drifting off the diagonal. */
    int residual_x;
    int residual_z;

    /**
     * Scene-window pool index holding this vessel's DECK collision, or 0 for
     * none (the pool ran out — the hull still sails, but a rider whose own
     * window has followed the hull away from the pool cannot walk the deck).
     *
     * A rider needs two collision domains at once — deck tiles under their
     * feet, water under the hull — and their own per-player window can only
     * be one of them. This window pins the deck's; it is built when the deck
     * instance is (ToriRSServer_WorldMapInstanceBuilt) and released with the
     * vessel.
     */
    int deck_window;
};

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/**
 * Spawn a vessel: reserve a pool slot, allocate its deck instance (sized up
 * from the hull footprint to whole zones), and place the hull.
 *
 * `tile_x`/`tile_z` are the absolute root-world tile the hull centers on;
 * `angle` is initial yaw in 2048-space. The hull spawns IDLE at speed tier 1.
 *
 * Returns the 1-based vessel handle, or 0 when the map-instance pool is
 * exhausted (a legitimate runtime state content checks, exactly as it does for
 * `map_instance_alloc`). Exhausting the VESSEL pool itself is asserted — 32
 * concurrent hulls is a capacity decision, not a load content can reach.
 *
 * The deck instance is opted out of the engine's linger teardown
 * (`ToriRSServer_MapInstanceSetLinger(h, 0)`) because the vessel owns its
 * lifetime: an empty deck is a boat nobody boarded, not an abandoned one.
 */
int
ToriRSServer_VesselSpawn(
    struct ToriRSServer* srv,
    int config_id,
    int size_x_tiles,
    int size_z_tiles,
    int level,
    int tile_x,
    int tile_z,
    int angle);

/**
 * The live hull whose ROOT position is within `range` tiles of a root tile,
 * nearest first — the "is there a boat at this dock?" question a gangplank
 * asks. 0 when nothing is in range. Deck instances are not searched: this is
 * a root-frame query, and a rider is already aboard.
 */
int
ToriRSServer_VesselNearest(
    struct ToriRSServer* srv,
    int tile_x,
    int tile_z,
    int level,
    int range);

/**
 * Stand a player on this hull's deck — the boarding teleport, at the deck's
 * own walkable plane (ToriRSServer_VesselDeckPlane) and the deck box's
 * centre, which is what `::vesselboard` does and what content's gangplank
 * needs. Returns 0 for a hull with no built deck instance.
 */
int
ToriRSServer_VesselBoardPlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player,
    struct ToriRSServerVessel* vessel);

/**
 * Put an aboard player ashore: the nearest WALKABLE root tile just outside
 * the hull's footprint, searched outward from the hull's projected position.
 * Returns 0 when there is nowhere to step — a hull at sea has no shore, and
 * saying so is content's job (the caller words the refusal).
 */
int
ToriRSServer_VesselDisembarkPlayer(
    struct ToriRSServer* srv,
    struct ToriRSServerPlayer* player);

/** Did this hull just get refused by the water? One-shot: reading clears it.
 *  The tick loop reports it to whoever is at the helm. */
int
ToriRSServer_VesselTakeBlocked(struct ToriRSServerVessel* vessel);

/** Release the vessel and its deck instance. Returns 0 for a dead handle —
 *  the map-instance convention for handle-shaped deallocators. */
int
ToriRSServer_VesselFree(
    struct ToriRSServer* srv,
    int handle);

/** The live vessel behind a handle, or NULL for 0 / out-of-range / freed —
 *  handles arrive from scripts, so a dead one is data, not a caller bug. */
struct ToriRSServerVessel*
ToriRSServer_VesselGet(
    struct ToriRSServer* srv,
    int handle);

/** How many vessels are live — the leak check's number. */
int
ToriRSServer_VesselLiveCount(struct ToriRSServer* srv);

/** The live vessel published under this client world-view id, or NULL. View 0
 *  (the root) and ids past the registry answer NULL — both arrive off the
 *  wire, so neither is a caller bug. */
struct ToriRSServerVessel*
ToriRSServer_VesselByView(
    struct ToriRSServer* srv,
    int view_id);

/** The vessel whose DECK INSTANCE reservation contains this absolute tile, or
 *  NULL — the "is this player aboard something?" question, answered from the
 *  map-instance pool so the two cannot disagree. */
struct ToriRSServerVessel*
ToriRSServer_VesselAtTile(
    struct ToriRSServer* srv,
    int tile_x,
    int tile_z);

/** The deck's size in whole zones — the reservation VesselSpawn made, and the
 *  size nibbles WORLDENTITY_INFO's spawn trailer carries. */
void
ToriRSServer_VesselDeckZones(
    const struct ToriRSServerVessel* vessel,
    int* out_zones_x,
    int* out_zones_z);

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

/** Sail toward the CENTER of an absolute tile; state -> TARGET. The 16-point
 *  heading is re-derived toward the target every tick, so a moving start or a
 *  mid-course retarget curves in rather than needing a stop. */
void
ToriRSServer_VesselSetTarget(
    struct ToriRSServerVessel* vessel,
    int tile_x,
    int tile_z);

/** Sail on a 16-point compass heading (0..15, meaning heading*128 in angle
 *  space) until told otherwise; state -> HEADING. */
void
ToriRSServer_VesselSetHeading(
    struct ToriRSServerVessel* vessel,
    int heading);

/** Speed tier 1..4 -> 64/128/192/256 fine units per tick. */
void
ToriRSServer_VesselSetSpeed(
    struct ToriRSServerVessel* vessel,
    int speed_tier);

/** Drop the movement command; state -> IDLE. Position and angle keep. */
void
ToriRSServer_VesselStop(struct ToriRSServerVessel* vessel);

/** The 16-point compass heading whose angle points closest along (dx, dz)
 *  under the mover's own convention (heading angle A moves dx = -sin(A),
 *  dz = -cos(A)). The steering click's quantizer. */
int
ToriRSServer_VesselHeadingToward(
    int dx,
    int dz);

/* ------------------------------------------------------------------ */
/* Mover                                                               */
/* ------------------------------------------------------------------ */

/**
 * One tick for every live vessel: turn toward the commanded heading capped by
 * `turn_rate` (shortest arc), advance dx = -sin, dz = -cos scaled by the speed
 * tier and quantized to 32-fine-unit multiples, and refuse any step whose hull
 * footprint would leave sailable water — a blocked step stops the boat.
 *
 * Runs from its own phase of ToriRSServer_WorldTick, before player info.
 */
void
ToriRSServer_VesselTickAll(struct ToriRSServer* srv);

/** Is this tile open on the independent boat map? Unknown terrain blocks. */
int
ToriRSServer_VesselTileSailable(int level, int tile_x, int tile_z);

/** Full rotated hull occupancy, including partial-tile edge overlaps. */
int
ToriRSServer_VesselCanOccupy(const struct ToriRSServerVessel* vessel,
                           int fine_x, int fine_z, int angle);

/* ------------------------------------------------------------------ */
/* Deck <-> root projection                                            */
/* ------------------------------------------------------------------ */

/*
 * Deck space is fine units with the origin at the deck instance's south-west
 * reservation corner; the hull occupies tiles [0, size_x) x [0, size_z) of it,
 * so the pivot every rotation turns about is the hull center
 * (size_x*64, size_z*64). The forward map rotates deck-local offsets by the
 * vessel's yaw and translates by its root position:
 *
 *     lx = deck_fx - size_x*64        c = cos(angle), s = sin(angle)
 *     lz = deck_fz - size_z*64        (65536-scaled, 2048-space)
 *     root_fx = fine_x + (lx*c + lz*s) >> 16
 *     root_fz = fine_z + (lz*c - lx*s) >> 16
 *
 * which agrees with the mover's dx = -sin, dz = -cos: the deck's bow direction
 * (0, -1) lands on (-s, -c). The inverse applies the transposed rotation to
 * the root-space offset. The pair round-trips exactly at cardinal angles and
 * within +-1 fine unit elsewhere (16.16 rounding).
 */

/** Deck fine coords -> root-world fine coords through the vessel transform. */
void
ToriRSServer_VesselDeckToRoot(
    const struct ToriRSServerVessel* vessel,
    int deck_fine_x,
    int deck_fine_z,
    int* out_fine_x,
    int* out_fine_z);

/** Root-world fine coords -> deck fine coords; exact inverse of the above up
 *  to the 16.16 rounding. */
/** The plane a rider STANDS on aboard this hull — the config's deck plane
 *  (archive-72 op 2), mirrored server-side like the pivots. Every real boat
 *  authors its walkable planking at plane 1; plane 0 is the loc-built shell
 *  whose collision is solid. */
int
ToriRSServer_VesselDeckPlane(const struct ToriRSServerVessel* vessel);

/** Absolute half-open walkable hull bounds, excluding navigation-only bow space. */
int ToriRSServer_VesselDeckWalkBounds(
    const struct ToriRSServerVessel* vessel,
    int* min_x, int* min_z, int* max_x, int* max_z);

void
ToriRSServer_VesselRootToDeck(
    const struct ToriRSServerVessel* vessel,
    int root_fine_x,
    int root_fine_z,
    int* out_deck_fine_x,
    int* out_deck_fine_z);

/**
 * Where an absolute DECK-INSTANCE tile (the tile a boarded player stands on)
 * projects to in the root world, as fine coords of that tile's center.
 * Resolves the instance base itself, so callers hand it the same coordinates
 * the player struct carries.
 */
void
ToriRSServer_VesselDeckTileToRoot(
    const struct ToriRSServerVessel* vessel,
    int deck_tile_x,
    int deck_tile_z,
    int* out_fine_x,
    int* out_fine_z);

#endif
