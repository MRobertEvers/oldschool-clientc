#ifndef SRC_EDITOR_EDITOR_NPC_H
#define SRC_EDITOR_EDITOR_NPC_H

/**
 * What an npc WILL do, from what its record STATES (docs/NPC_EDITOR_DESIGN.md
 * section 1.1): the server's mode rule applied once, in one place, so the
 * NPC editor's header, the Spawns panel's preview, the encounter rings and
 * the World overlay never disagree about it.
 *
 * The rule (torirs_server_world.c, npc_default_mode / npc_spawn):
 *
 *   patrol rows            -> PATROL, whatever else is stated
 *   else defaultmode stated -> that mode
 *   else wanderrange > 0    -> WANDER, else NONE
 *   nomove (moverestrict=nomove) zeroes the wander radius at spawn
 *
 * so a record stating `patrol1..10` AND `wanderrange=5` (Hans) patrols, and
 * the 5 is dead data -- `unused_wanderrange` says so, and the preview draws
 * the route, not the square.
 *
 * Pure: the fields come in as (key, value) text, one statement at a time
 * (from a catalog record's fields or an Editor_CfgRecord's winning rows),
 * through Editor_NpcMovementRead; nothing here touches a document, a host
 * or the chrome. NULL arguments assert.
 */

#include <stddef.h>

/** The server's cap (TORIRSSERVER_NPC_PATROL_MAX). */
#define EDITOR_NPC_PATROL_MAX 16

/** The engine's defaults for a record stating neither
 *  (torirs_server_content.c: "wanderrange = 5; maxrange = 7"). */
#define EDITOR_NPC_WANDER_DEFAULT 5
#define EDITOR_NPC_LEASH_DEFAULT 7

/** The server teleports a patroller to its waypoint after this many ticks
 *  without progress (torirs_server_world.c, the patrol runtime). */
#define EDITOR_NPC_PATROL_STUCK_TICKS 32

enum Editor_NpcMode
{
    EDITOR_NPC_MODE_NONE = 0,
    EDITOR_NPC_MODE_WANDER = 1,
    EDITOR_NPC_MODE_PATROL = 2,
};

/** `moverestrict`'s words (fields/npc.ini): the value is the engine's. */
enum Editor_NpcRestrict
{
    EDITOR_NPC_RESTRICT_UNSTATED = -1,
    EDITOR_NPC_RESTRICT_NORMAL = 0,
    EDITOR_NPC_RESTRICT_BLOCKED = 1,
    EDITOR_NPC_RESTRICT_BLOCKED_NORMAL = 2,
    EDITOR_NPC_RESTRICT_INDOORS = 3,
    EDITOR_NPC_RESTRICT_OUTDOORS = 4,
    EDITOR_NPC_RESTRICT_NOMOVE = 5,
    EDITOR_NPC_RESTRICT_PASSTHRU = 6,
};

/** One `patrolN=<level>_<mx>_<mz>_<lx>_<lz>,<pause>` row, in absolute tiles. */
struct Editor_NpcWaypoint
{
    int x;
    int z;
    int level;
    /** Ticks held on arrival. */
    int pause;
};

/** The movement fields a record states; -1 (or 0 for the booleans) where it
 *  states nothing. Fill with Editor_NpcMovementInit then
 *  Editor_NpcMovementRead per statement. */
struct Editor_NpcMovement
{
    int defaultmode_stated;
    enum Editor_NpcMode defaultmode;
    int wanderrange;
    int maxrange;
    int huntrange;
    int nomove;
    enum Editor_NpcRestrict moverestrict;
    /** 0..7 (the register's compass words, northwest first), -1 unstated. */
    int facing;
    /** Footprint side; 1 when unstated. */
    int size;
    /** The other Movement tab fields: respawnrate and timer in ticks,
     *  blockwalk as the register's index (none 0, npc 1, all 2, player 3);
     *  -1 unstated. */
    int respawnrate;
    int timer;
    int blockwalk;
    struct Editor_NpcWaypoint patrol[EDITOR_NPC_PATROL_MAX];
    int patrol_count;
    /** `patrolN` rows past the cap, counted and dropped. */
    int patrol_dropped;
};

/** What the server does with it. */
struct Editor_NpcEffective
{
    enum Editor_NpcMode mode;
    /** The field that decided it (static text): "patrol rows",
     *  "defaultmode", "wanderrange", "no wanderrange". */
    const char* why;
    /** The radius it wanders (0 for NONE, PATROL and nomove). */
    int wander;
    /** maxrange, or the engine default. */
    int leash;
    int nomove;
    /** A wanderrange above 0 is stated and never read (a patroller, a
     *  stated none, a nomove). */
    int unused_wanderrange;
    /** A route with `defaultmode=wander` or `=none`: the files contradict
     *  themselves (the route still wins). */
    int route_contradicts_mode;
    /** PATROL: the lap in ticks at a tile a tick, the closing leg included:
     *  steps + pauses (a leg to another level is a teleport, 0 steps). */
    int lap_steps;
    int lap_pause;
};

void
Editor_NpcMovementInit(struct Editor_NpcMovement* movement);

/**
 * Take one statement. Recognised: wanderrange, maxrange, huntrange (and
 * `param=huntrange,<n>` as key "param" with value "huntrange,<n>"),
 * defaultmode, moverestrict, nomove, facing, size, patrol<N>. Returns 1
 * when the key was one of those (whatever its value's shape), 0 otherwise.
 * A later statement of a key replaces the earlier (the caller feeds winners
 * last, or winners only).
 */
int
Editor_NpcMovementRead(
    struct Editor_NpcMovement* movement,
    const char* key,
    const char* value);

/** The rule above. */
void
Editor_NpcEffectiveMode(
    const struct Editor_NpcMovement* movement,
    struct Editor_NpcEffective* out);

/** One sentence for the editor's header ("patrols 10 waypoints, 84 ticks a
 *  lap; wanderrange 5 is unused"). Returns the bytes it wanted. */
int
Editor_NpcEffectiveSentence(
    const struct Editor_NpcMovement* movement,
    const struct Editor_NpcEffective* effective,
    char* out,
    size_t out_size);

/** The steps the server walks from a to b at a tile a tick (a diagonal step
 *  moves both axes): the Chebyshev distance; 0 across levels (a teleport). */
int
Editor_NpcLegSteps(
    const struct Editor_NpcWaypoint* a,
    const struct Editor_NpcWaypoint* b);

/** `<level>_<mx>_<mz>_<lx>_<lz>` -> absolute tile. 1 when it reads. */
int
Editor_NpcCoordParse(
    const char* text,
    int* out_x,
    int* out_z,
    int* out_level);

/** The inverse, LostCity's spelling. Returns the bytes it wanted. */
int
Editor_NpcCoordFormat(
    int x,
    int z,
    int level,
    char* out,
    size_t out_size);

/** `<coord>,<pause>` (pause optional, 0) -> waypoint. 1 when it reads. */
int
Editor_NpcWaypointParse(
    const char* text,
    struct Editor_NpcWaypoint* out);

/** `<coord>,<pause>` as a patrol row's value. Returns the bytes it wanted. */
int
Editor_NpcWaypointFormat(
    const struct Editor_NpcWaypoint* waypoint,
    char* out,
    size_t out_size);

struct MapEditorWanderRect;

/**
 * The route as the World and the minimap draw it (map_editor_ghost.h): one
 * WAYPOINT per row and one LEG per row to the next, the last leg closing
 * back to the first (flagged); a leg across levels is flagged TELEPORT. A
 * single waypoint is a waypoint and no leg. Writes at most `max`, returns
 * the count.
 */
int
Editor_NpcRouteRects(
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    struct MapEditorWanderRect* out,
    int max);

/**
 * Everything the preview draws for one npc of `size` standing at (x, z,
 * level), from the EFFECTIVE behaviour: a patroller's route, a wanderer's
 * square and leash, a `nomove`'s pin, a stander's leash (the chase bound).
 * Writes at most `max`, returns the count.
 */
int
Editor_NpcPreviewRects(
    const struct Editor_NpcMovement* movement,
    int x,
    int z,
    int level,
    struct MapEditorWanderRect* out,
    int max);

struct Editor_CfgRecord;
struct Editor_Cmd;

/**
 * The commands that make record `record`'s route `waypoints` (0..count-1)
 * in layer `layer` (docs/NPC_EDITOR_DESIGN.md 3.5 "Writes"): patrol1..N set
 * to the waypoints' spellings, patrolN+1.. (every row the record states
 * past the new count) removed, and `defaultmode` set to `patrol` -- or, with
 * no waypoints, every patrol row removed and `defaultmode` set to `mode`'s
 * word (the Mode radio's choice). `maxrange` >= 0 also states the leash
 * (the caller's Editor_NpcRouteReach, so the route is inside it); -1 leaves
 * it. A layer the record has no block in yet gets ONE command that opens
 * it, however many fields follow. With waypoints, a bare `patrol=` marker
 * (`patrol=empty`, which Copy to new writes) in `layer` is removed: rows
 * beside it in one layer fail the pack. One stroke for the caller. Returns the
 * commands written, 0 when nothing changes, -1 when one cannot be
 * expressed or `max` is too small (nothing to apply then).
 */
int
Editor_NpcRoutePlan(
    const struct Editor_CfgRecord* record,
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    enum Editor_NpcMode mode,
    int maxrange,
    int layer,
    struct Editor_Cmd* out,
    int max);

/* ---- the walk the server takes ----------------------------------------------
 *
 * A patroller does not route: each tick it takes ONE step straight at its
 * waypoint (torirs_server_world.c npc_take_step: the diagonal, else the x
 * leg, else the z leg), and when none is open it stands; after
 * EDITOR_NPC_PATROL_STUCK_TICKS of that the server teleports it to the
 * waypoint. So "a path exists" (a flood fill) is the wrong question for a
 * leg: a waypoint round a corner is reachable and still never walked to.
 * Editor_NpcWalkLeg takes the same steps over the caller's collision and
 * says where they end.
 */

/** One step of a `size` mover on `level` from (x, z) by (dx, dz), each -1..1:
 *  1 open, 0 blocked, -1 the scene cannot say (the tile is not loaded). */
typedef int (*Editor_NpcStepFn)(void* user_data, int x, int z, int dx, int dz, int level, int size);

enum Editor_NpcWalkResult
{
    /** Walked onto the waypoint. */
    EDITOR_NPC_WALK_ARRIVES = 0,
    /** Stood still at (stuck_x, stuck_z): the server teleports from there. */
    EDITOR_NPC_WALK_STUCK = 1,
    /** The other waypoint is on another level: a teleport, no steps. */
    EDITOR_NPC_WALK_TELEPORT = 2,
    /** The collision could not answer (a square that is not loaded). */
    EDITOR_NPC_WALK_UNKNOWN = 3,
};

struct Editor_NpcWalkTile
{
    int x;
    int z;
};

struct Editor_NpcWalk
{
    enum Editor_NpcWalkResult result;
    /** Tiles stepped onto (the start is not one). */
    int steps;
    /** Where the walk ended: the waypoint when it arrives. */
    int stuck_x;
    int stuck_z;
    /** Tiles written to the caller's array (steps, capped at its size). */
    int tile_count;
};

/** Walk from `a` to `b` as the server does. `tiles` (may be NULL with max 0)
 *  takes each tile stepped onto, in order. Returns the steps taken. */
int
Editor_NpcWalkLeg(
    const struct Editor_NpcWaypoint* a,
    const struct Editor_NpcWaypoint* b,
    int size,
    Editor_NpcStepFn step,
    void* user_data,
    struct Editor_NpcWalkTile* tiles,
    int max,
    struct Editor_NpcWalk* out);

/** Tiles round a stuck leg's two ends Editor_NpcLegDetour searches for a way
 *  past what stops it. */
#define EDITOR_NPC_DETOUR_MARGIN 12

/**
 * The waypoints to put between `a` and `b` so that every leg of
 * a -> out[0] -> ... -> b walks the server's way (Editor_NpcWalkLeg): a
 * shortest path over `step` inside the box round the two ends grown by
 * EDITOR_NPC_DETOUR_MARGIN, then from each point the FARTHEST tile of that
 * path a walk still reaches is the next waypoint, so as few are added as
 * the path allows. Each is on `a`'s level with no pause. Returns the count
 * written (0: the leg walks already), -1 when the box holds no path or the
 * ends are on two levels, -2 when more than `max` would be needed.
 */
int
Editor_NpcLegDetour(
    const struct Editor_NpcWaypoint* a,
    const struct Editor_NpcWaypoint* b,
    int size,
    Editor_NpcStepFn step,
    void* user_data,
    struct Editor_NpcWaypoint* out,
    int max);

/**
 * How far the route reaches from a spawn tile: the largest distance (the
 * larger axis) from (spawn_x, spawn_z) to any waypoint. The server measures
 * `maxrange` from the SPAWN tile, not from the npc (npc_mode_target_valid):
 * a player more than maxrange + 1 from it stops being the npc's target, so
 * a patroller with the engine's leash of 7 turns away from whoever talks to
 * it at the far end of a longer route. A leash of at least this keeps the
 * whole route inside it. 0 for no waypoints.
 */
int
Editor_NpcRouteReach(
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    int spawn_x,
    int spawn_z);

/** "move:<shard>/<name>", the ledger's TEXT document key (the shard is the
 *  name's first byte lowercased, '_' when not a letter or digit, as
 *  gen_npc_movement.py shards). The name must be 1..63 of [A-Za-z0-9_]. */
void
Editor_NpcMoveDocKey(
    char out[128],
    const char* name);

/**
 * The movement ledger (`npc_movement/<shard>/<name>.move`) with
 * `movement = <word>` (patrols | wanders | stationary), `restrict = <word>`
 * (NULL: the line is left as it stands) and `source = authored` -- the one
 * thing that makes gen_npc_movement.py leave the file as it stands.
 * LINE-PRESERVING: a stated line keeps its key column and its `// note`,
 * only its value changes; a missing line is added after the last `key =`
 * line; every other byte is kept. `old_text` may be empty: the new file is
 * a header naming its writer. Writes into `out` (snprintf's contract), the
 * length into `*out_length`. Returns 1, or 0 for a word a line cannot hold.
 */
int
Editor_NpcMoveTextSet(
    const char* old_text,
    size_t old_length,
    const char* npc,
    const char* movement,
    const char* restrict_word,
    char* out,
    size_t out_capacity,
    size_t* out_length);

/** The register's compass words, index 0..7; NULL past the end. */
const char*
Editor_NpcFacingWord(int facing);

/** A compass word's index, -1 when it is none of them. */
int
Editor_NpcFacingParse(const char* word);

/** `blockwalk`'s words, index 0..3; NULL past the end. */
const char*
Editor_NpcBlockwalkWord(int blockwalk);

/** The mode's word (`defaultmode=`): "none" / "wander" / "patrol". */
const char*
Editor_NpcModeWord(enum Editor_NpcMode mode);

/** `moverestrict`'s word, NULL for UNSTATED. */
const char*
Editor_NpcRestrictWord(enum Editor_NpcRestrict restrict_mode);

int
Editor_NpcRestrictParse(const char* word);

#endif
