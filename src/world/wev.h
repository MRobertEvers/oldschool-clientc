#ifndef WEV_H
#define WEV_H

#include "world/worldview.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * OSRS world entities (sailing boats) — the entity that carries a Worldview
 * around the map. See docs/SAILING.md §5 and docs/SAILING_PLAN.md C1.
 *
 * struct WevConfig mirrors the deob's class387 (WorldEntityConfig, config
 * index archive 72); struct Wev mirrors class467 (WorldEntity): the wire id
 * (== its world-view id), the parent view it is drawn inside, the current
 * interpolated transform, and the target queue the WORLDENTITY_INFO packet
 * feeds. The interpolator is class458: each queued segment is evaluated over
 * exactly 30 client cycles (600 ms = one game tick) from its enqueue cycle —
 * linear x/z, shortest-arc angle — and height is never interpolated at all;
 * it is overwritten every frame from the terrain under the boat.
 *
 * Fine units throughout: 128 per tile (<<7), angles 0..2047 (& 0x7FF).
 */

/* -------------------------------------------------------------------- */
/* WevConfig — config archive 72 (deob class387)                        */
/* -------------------------------------------------------------------- */

/** Right-click op strings: wire opcodes 15..19 (NOT 14..19 — opcode 14 is a
 * parameterless flag; SAILING.md §5.4's table is off by one there, verified
 * against cache.osrs239's bytes, where files 5..9 carry 0x0e immediately
 * followed by the next opcode). */
#define WEV_CONFIG_OPS 5

/** Yaw buckets for the precomputed footprint boxes: 16 buckets of 128 angle
 * units (SAILING.md §5.5). */
#define WEV_ORIENTATIONS 16

/** The deob bakes the footprint box four times (fine units): once with no
 * inflation at all (class387.field4867 — the hull's true extent, and the one
 * the accessors expose) and then at the three margins 256/334/362
 * (class387.field4861[0..2]). Index 0 is the un-inflated box. */
#define WEV_FOOTPRINT_MARGINS 4

/** Default flattened-render HSL when opcode 27 is absent — which is every
 * entry of cache.osrs239 (h 38, s 2, l 84; SAILING.md §5.3). */
#define WEV_FLAT_HSL_DEFAULT 39188

struct WevConfig
{
    /** File id inside archive 72. -1 while the slot is an unpopulated hole
     * (ids are sparse: cache.osrs239 has files 1..14, no 0). */
    int id;

    int plane; /* op 2, u8 */
    /** Rotation-pivot offset, SIGNED fine units (op 4/5 as i16: 0xffc0 = -64
     * in the live cache). C3's descent recenter is
     * (-size*64 - pivot) per axis. */
    int pivot_x;
    int pivot_z;
    /**
     * Footprint bounds. Settled against the deob (class387 builds
     * `new class575(op8, op9, op6, op7)` and class556 prints that constructor
     * as "%dx%d (offset %d,%d)"): 8/9 are the box SIZE, read unsigned, and
     * 6/7 are its OFFSET, read signed. The sizes are exact tile multiples in
     * cache.osrs239 and they name the hull tiers — 128x384 is the 1x3 skiff
     * (#1), 256x640 the 2x5 sloop (#2, #10, #11), 384x1280 the 3x10 class
     * (#3, #12–#14), 384x1152 the 3x9 named ships (#5–#8). "The Zenith" (#9)
     * genuinely carries 0x0: its extent comes off the wire, not the config.
     */
    int bounds_w;     /* op 8, u16 */
    int bounds_h;     /* op 9, u16 */
    int bounds_off_x; /* op 6, i16 */
    int bounds_off_z; /* op 7, i16 (-256 on the ten-tile hulls) */

    char* name; /* op 12; heap, NULL when absent */
    /** Op 14: a parameterless flag of unknown meaning (present on the five
     * named hulls). Kept so the record re-encodes; nothing consumes it yet. */
    bool flag14;
    /** Ops 15..19: right-click op strings; heap, NULL when absent. */
    char* ops[WEV_CONFIG_OPS];

    int category;   /* op 20, u16; -1 when absent */
    int click_mode; /* op 23, u8; default 2 (SAILING.md §5.4 click routing) */
    int op24;       /* op 24, u8; undocumented, always 1 where present */
    /** Op 25, u16: the default animation (the bob). Values in cache.osrs239
     * (13424..13428) are < 32768 so a u16 read cannot be told apart from a
     * bigsmart; confirm against the deob if a cache ever exceeds it. */
    int anim_id;
    int op26;     /* op 26, u16; undocumented, seq-id-like (7288..7292) */
    int flat_hsl; /* op 27, u16; default WEV_FLAT_HSL_DEFAULT */

    /**
     * Precomputed footprint corner tables (deob class575/class556): for each
     * inflation margin and each of the 16 yaw buckets, the 4 corners of the
     * oriented box in fine units relative to the entity position. Corner
     * order: (-x,-z), (+x,-z), (+x,+z), (-x,+z) before rotation.
     *
     * The box is centered at (bounds_off_x, bounds_off_z) with half-extents
     * (bounds_w/2 + margin, bounds_h/2 + margin), rotated by the bucket's
     * angle (bucket * 128 units of 2048). Margin 0 is 0, so index 0 is the
     * hull's true extent.
     */
    int corner_x[WEV_FOOTPRINT_MARGINS][WEV_ORIENTATIONS][4];
    int corner_z[WEV_FOOTPRINT_MARGINS][WEV_ORIENTATIONS][4];

    /** Bytes the decoder consumed; equal to the record size on a clean parse.
     * The loader warns on a mismatch instead of trusting a half-read record. */
    int _consumed;
};

/** The three baked margins, in fine units. */
extern const int WEV_FOOTPRINT_MARGIN[WEV_FOOTPRINT_MARGINS];

/** Reset `config` to the pre-decode defaults (id kept as passed). */
void
WevConfig_Init(
    struct WevConfig* config,
    int id);

/**
 * Decode one archive-72 record and bake the corner tables. Returns 1 on a
 * clean decode (every byte consumed, terminating 0 seen), 0 when an unknown
 * opcode stopped the parse early — `_consumed` says where. The record is
 * Init()ed first either way.
 */
int
WevConfig_Decode(
    struct WevConfig* config,
    int id,
    uint8_t const* data,
    int size);

/** Free the heap strings a decode allocated. The struct itself is the
 * caller's. Not a deallocator for the struct — asserts config. */
void
WevConfig_FreeContents(struct WevConfig* config);

/** The decoded table (App-owned, loaded once at boot by
 * CreateTask_Dat2WevConfigLoad). Empty — count 0 — on a cache without
 * archive 72, which is every pre-sailing cache and not an error. */
struct WevConfigTable
{
    struct WevConfig* entries; /* entries[id]; holes have id == -1 */
    int count;                 /* max id + 1 */
};

void
WevConfigTable_Init(struct WevConfigTable* table);

/** Install the decoded entries (takes ownership). The table must be empty. */
void
WevConfigTable_Set(
    struct WevConfigTable* table,
    struct WevConfig* entries,
    int count);

/** Accepts NULL (deallocator convention). */
void
WevConfigTable_Free(struct WevConfigTable* table);

/** True when `id` names a populated record. The existence test lives here,
 * with the caller — Get() asserts. */
bool
WevConfigTable_Has(
    struct WevConfigTable const* table,
    int id);

struct WevConfig const*
WevConfigTable_Get(
    struct WevConfigTable const* table,
    int id);

/* -------------------------------------------------------------------- */
/* Wev — one live world entity (deob class467)                          */
/* -------------------------------------------------------------------- */

/** Target queue slots; at most WEV_TARGET_PENDING_MAX may be pending
 * (deob class462[10], slot 0 = newest). */
#define WEV_TARGET_QUEUE_SLOTS 10
#define WEV_TARGET_PENDING_MAX 9

/* The enqueue rotate writes slot `queue_count`, and queue_count saturates at
 * WEV_TARGET_PENDING_MAX — so the spare slot is what keeps that write in
 * bounds. Checked here, at build time, because the bound cannot be an
 * assert(): the release lane defines NDEBUG (src/makefile). */
_Static_assert(
    WEV_TARGET_PENDING_MAX < WEV_TARGET_QUEUE_SLOTS,
    "the target ring needs a spare slot above the pending cap");

/** One queued movement segment is evaluated over exactly this many client
 * cycles (600 ms = one game tick) from its enqueue cycle. */
#define WEV_INTERP_CYCLES 30

/** Every right-click op enabled — what the deob's class467 constructor seeds
 * field5694 with. Mirrors PKT_WEV_OP_MASK_ALL on the wire side. */
#define WEV_OP_MASK_ALL 31

struct WevTarget
{
    int x; /* fine units */
    int z;
    int angle; /* 0..2047 */
    /** Client cycle the segment was enqueued at; the segment ends at
     * enqueue_cycle + WEV_INTERP_CYCLES. Double, not float: the clock counts
     * 50 cycles a second for the length of a session, and a float's mantissa
     * runs out of tenths-of-a-cycle resolution inside a day's uptime. */
    double enqueue_cycle;
};

struct Wev
{
    bool live;
    /** Wire id — also the entity's own world-view id in the registry. */
    int id;
    int view_id;
    /** View this entity is drawn inside (deob field5700). */
    int parent_view_id;
    /**
     * The level this hull floats on inside `parent_view_id`'s world — off
     * SET_ACTIVE_WORLD, mirroring `Worldview.parent_level`.
     *
     * The terrain the per-frame height sample reads must come from this level,
     * NOT from `config->plane`: the config's plane is where the deck is
     * authored inside the entity's OWN world (the off-map staging region), and
     * sampling the root's terrain there lifts a hull floating on open water to
     * the height of whatever happens to sit one storey up at those coordinates
     * — beside Lumbridge castle that is a 600-unit hop into the air.
     *
     * 0 until the first REBUILD_WORLDENTITY lands, which is both the common
     * case (a hull floats on the surface plane) and the only safe guess.
     */
    int parent_level;

    struct WevConfig const* config;
    int config_id;

    /* Current interpolated transform, fine units / 0..2047. y is overwritten
     * every frame from the terrain under the boat (never interpolated). */
    int x;
    int y;
    int z;
    int angle;

    /**
     * Targets, slot 0 = newest (packet-arrival order reversed).
     *
     * Slot 0 is ALWAYS meaningful, `queue_count` included: it is the
     * entity's current target — where a delta chains off, and where the hull
     * rests once nothing is pending. `queue_count` is only the number of
     * segments still owed, which is why an over-long backlog can be dropped
     * (deob class467.method10446) without losing the destination.
     */
    struct WevTarget queue[WEV_TARGET_QUEUE_SLOTS];
    int queue_count;

    /*
     * Interpolator state (deob class458 through class467.method10477).
     *
     * The client does not walk the queue oldest-first. It arms one segment at
     * a time from wherever the hull currently is toward queue[0] — the NEWEST
     * target — over the window [arm - 1, queue[0].enqueue_cycle + 30], and on
     * completion merely drops the pending count by one and re-arms. A backlog
     * therefore drains toward the newest target rather than replaying the
     * stale ones, which is what keeps a lagged boat from sailing the whole
     * missed path at 30 cycles a segment.
     */
    bool interp_armed;
    int interp_from_x;
    int interp_from_z;
    int interp_from_angle;
    int interp_to_x;
    int interp_to_z;
    int interp_to_angle;
    double interp_start_cycle;
    double interp_end_cycle;

    /** ownerTypeIndex off the wire — the class276 draw-priority group
     * (draw order 2, 0, 1; SAILING.md §5.3). */
    int priority_group;
    /**
     * Five-bit right-click op-enabled mask (deob class467.field5694), seeded
     * to WEV_OP_MASK_ALL at spawn and replaced only by an updateFlags bit-0x2
     * payload. It is NOT the updateFlags byte.
     */
    unsigned op_mask;

    /* Seq state (updateFlags bit 0x1 payload). -1 = none/cleared (65535 on
     * the wire clears). */
    int seq_id;
    int seq_delay;
};

/**
 * All live world entities plus each view's server-ordered entity list.
 * WORLDENTITY_INFO is per view: its count/ops walk the ACTIVE view's list in
 * list order, so the order is state, not presentation.
 */
struct Wevs
{
    /** Indexed by wire id (== view id). Slot 0 (the root) is never a Wev. */
    struct Wev wevs[WORLDVIEW_MAX];
    /** Per parent view: its entities in server list order. */
    struct
    {
        int ids[WORLDVIEW_MAX];
        int count;
    } lists[WORLDVIEW_MAX];
    /** Client cycle clock the interpolator runs on; advanced by
     * Wevs_Frame(frame_cycles), stamped onto targets at enqueue. */
    double clock;
};

/**
 * Terrain height under a hull, for the per-frame driver: World_HeightFn plus
 * the view the entity floats in, without pulling world.h in here. `view_id`
 * is the entity's PARENT view — a boat samples the root's terrain, a nested
 * entity its carrier's deck — and the x/z are that view's own coordinate
 * space, which for every view is absolute root-world fine units.
 */
typedef int (*WevHeightFn)(
    void* userdata,
    int view_id,
    int world_x,
    int world_z,
    int level);

void
Wevs_Init(struct Wevs* wevs);

bool
Wevs_IsLive(
    struct Wevs const* wevs,
    int id);

/** Asserts `id` names a live entity (packet routing: the deob throws on an
 * unknown world-entity id). */
struct Wev*
Wevs_Get(
    struct Wevs* wevs,
    int id);

/**
 * Spawn from the WORLDENTITY_INFO new-entity trailer. Appends to
 * `parent_view_id`'s list. The transform is the trailer's absolute one; the
 * queue starts empty (the entity holds position until a move op arrives).
 */
struct Wev*
Wevs_Spawn(
    struct Wevs* wevs,
    int id,
    int parent_view_id,
    struct WevConfig const* config,
    int config_id,
    int x,
    int z,
    int angle,
    int priority_group,
    unsigned op_mask);

/** Remove `id` from its parent's list and clear the slot. The caller releases
 * the entity's Worldview — this layer does not own views. */
void
Wevs_Despawn(
    struct Wevs* wevs,
    int id);

/** Server-ordered entity list of one view (for the packet's count/op walk). */
int
Wevs_ViewListCount(
    struct Wevs const* wevs,
    int view_id);

struct Wev*
Wevs_ViewListAt(
    struct Wevs* wevs,
    int view_id,
    int index);

/**
 * Apply one movement op's deltas. The reference point is the newest queued
 * target when one is pending, else the last landed target — the server's
 * deltas chain off where the entity will be, not where the lerp currently
 * shows it. `snap` (wire op 3) clears the queue and teleports; otherwise
 * (op 2) the target is enqueued to play out over WEV_INTERP_CYCLES from
 * `cycle`. dy is accepted for wire symmetry and ignored: height comes from
 * the terrain every frame.
 */
void
Wev_ApplyMove(
    struct Wev* wev,
    int dx,
    int dy,
    int dz,
    int dangle,
    bool snap,
    double cycle);

/**
 * Evaluate the interpolator at `now_cycle`: completed segments land and pop
 * (their target becomes the next segment's "from"), the oldest still-running
 * segment lerps x/z linearly and the angle along the shortest arc
 * (d = (to-from) & 0x7FF; d > 1024 → d -= 2048), and an empty queue holds
 * the last target. y is untouched.
 */
void
Wev_Interpolate(
    struct Wev* wev,
    double now_cycle);

/**
 * The entity's footprint this frame, in ABSOLUTE root-world tiles: the
 * axis-aligned bound of its baked corner box for the current heading.
 *
 * Must be re-evaluated every frame — the box is the hull ROTATED by `angle`,
 * so its tile extent grows and shrinks as the boat turns (widest near 45
 * degrees off-axis, by up to the diagonal). The painter's pseudo-loc takes its
 * size from this; a fixed extent under-covers at some headings, which reads
 * downstream as scenery sorting in front of a hull it is behind.
 *
 * `wev->config` must be present — "does this entity have a config?" is the
 * caller's question. `margin_index` selects an inflation variant; index 0 is
 * the un-inflated box and is what the painter wants.
 *
 * Evaluated at the entity's EXACT angle, not at one of the 16 baked buckets.
 * The buckets exist because the deob queries the box's four corners
 * (class570.method12440 rounds to `((angle + 64) & 0x7ff) / 128`), and an
 * inflated box swallows the up-to-11.25-degree residual. The painter's
 * footprint has no such slack once margin 0 is the true box: a hull halfway
 * between two buckets — which is every frame of a turn, since the
 * interpolator sweeps the angle continuously — would be under-covered by a
 * tile, and an under-covered footprint is exactly how the parent's water gets
 * to sort in front of the hull. The rotated AABB is closed-form, so this
 * takes the exact answer rather than the nearest bucket's.
 *
 * `out_min_tile_*` is the MIN corner, not the centre, matching what
 * painter_add_world_entity expects.
 */
void
Wev_FootprintTiles(
    struct Wev const* wev,
    int margin_index,
    int* out_min_tile_x,
    int* out_min_tile_z,
    int* out_size_x,
    int* out_size_z);

/**
 * Per-frame driver: advance the clock by `frame_cycles`, then interpolate
 * every live view's entities — root first, then each entity's own view as it
 * is reached (iterative worklist, capped at WORLDVIEW_MAX; nesting comes
 * free). After interpolation each entity's height is overwritten from
 * `height_fn`, called with the entity's parent view: height is never
 * interpolated, it is re-sampled from the terrain the hull sits on every
 * frame. `height_fn` is required — a driver with nowhere to read terrain
 * from would silently leave every boat at y 0.
 */
void
Wevs_Frame(
    struct Wevs* wevs,
    double frame_cycles,
    WevHeightFn height_fn,
    void* height_userdata);

/* -------------------------------------------------------------------- */
/* Deck box — the view's base rectangle and the transform into it        */
/* -------------------------------------------------------------------- */

/**
 * One entity's placement inside its PARENT view, plus the size of the deck it
 * carries — everything needed to move a point between the two spaces and to
 * answer "is this point aboard?".
 *
 * The forward transform (SAILING.md §5.2, and what C3's `frame_view_push`
 * composes) is
 *
 *     parent = R(angle) * (deck + recenter) + pos
 *
 * with `recenter = (-size_x_tiles*64 - pivot_x, -size_z_tiles*64 - pivot_z)`,
 * so the inverse is `deck = R(-angle) * (parent - pos) - recenter`.
 *
 * Every coordinate here is FINE units (128 per tile). `pos` is scene-local to
 * the parent view — that is, absolute root fine units minus the parent world's
 * `_base_tile_* << 7` — because that is the space scene elements and the
 * painter grid live in. `recenter` is stored rather than derived so the caller
 * that already has the config does not pass the pivot down twice.
 */
struct WevDeckBox
{
    int pos_x;
    int pos_z;
    int angle; /* 0..2047 */
    int recenter_x;
    int recenter_z;
    /** The deck's base rectangle, in tiles; membership is [0, size*128). */
    int size_x_tiles;
    int size_z_tiles;
};

/** Parent-view fine (x,z) -> deck-local fine. Both outputs are required. */
void
Wev_DeckFromParent(
    struct WevDeckBox const* box,
    int parent_x,
    int parent_z,
    int* out_deck_x,
    int* out_deck_z);

/** Deck-local fine (x,z) -> parent-view fine. Both outputs are required. */
void
Wev_ParentFromDeck(
    struct WevDeckBox const* box,
    int deck_x,
    int deck_z,
    int* out_parent_x,
    int* out_parent_z);

/**
 * Membership, the deob's geometric rule (SAILING_PLAN C5.1): a parent-view
 * point is aboard when its deck-local image falls inside the base rectangle.
 * Half-open on both axes, so a hull's tiles partition cleanly and a point on
 * the far edge belongs to whatever is out there instead.
 */
bool
Wev_DeckContainsParentPoint(
    struct WevDeckBox const* box,
    int parent_x,
    int parent_z);

/** The same test on an already-transformed deck-local point. */
bool
Wev_DeckContainsDeckPoint(
    struct WevDeckBox const* box,
    int deck_x,
    int deck_z);

#endif
