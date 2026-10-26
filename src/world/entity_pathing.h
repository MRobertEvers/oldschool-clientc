#ifndef WORLD_ENTITY_PATHING_H
#define WORLD_ENTITY_PATHING_H

#include "entity_facets.h"

#include <stdbool.h>

/* A queued step's traversal, rev-239 class174: run doubles the step speed and
 * crawl halves it (class105.method3520 / method3611). The 2004 wire has no
 * crawl, so only the rev-239 decoders queue one. */
#define WORLD_PATHSTEP_WALK 0
#define WORLD_PATHSTEP_RUN 1
#define WORLD_PATHSTEP_CRAWL 2

struct CollisionMap;

enum World_PathingJump
{
    WORLD_PATHING_JUMP_TELEPORT,
    WORLD_PATHING_JUMP_WALK,
};

/** The tile delta of one step in `direction` (0..7, the entity-info
 *  stream's WALKDIR / RUNDIR numbering): the same table PushStep walks. */
void
World_DirectionDelta(
    int direction,
    int* dx,
    int* dz);

void
World_EntityPathingPushXZ(
    struct WorldEntityFacet_Pathing* pathing,
    int x,
    int z,
    int step_type);

void
World_EntityPathingPushStep(
    struct WorldEntityFacet_Pathing* pathing,
    int step_type,
    int direction);

enum World_PathingJump
World_EntityPathingJump(
    struct WorldEntityFacet_Pathing* pathing,
    bool force_teleport,
    int x,
    int z);

/* rev-239 Statics.method1338 (the GPI player move): queue (x, z) whenever it
 * and the newest queued tile are inside a `scene_size` scene -- no 8-tile
 * window -- and teleport otherwise. A RUN step first pathfinds locally to the
 * endpoint and queues the intermediate turns (method2600). */
enum World_PathingJump
World_EntityPathingJumpCollisionAware(
    struct WorldEntityFacet_Pathing* pathing,
    struct CollisionMap* collision,
    bool force_teleport,
    int x,
    int z,
    int step_type,
    int scene_size);

void
World_EntityDrawPositionSetToTile(
    struct WorldEntityFacet_DrawPosition* draw_position,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z);

/**
 * Slot-full policy, the hitsplat config's opcode 12 (`field5318` in the rev-239
 * deob's `class420`). The reference's own default is DISCARD; a zeroed value is
 * NOT the default, which is why this is passed rather than defaulted to 0.
 */
enum World_HitmarkSlotPolicy
{
    /** Drop the incoming splat. The reference's default (-1). */
    WORLD_HITMARK_POLICY_DISCARD = -1,
    /** Overwrite the splat with the lowest remaining cycle. */
    WORLD_HITMARK_POLICY_EVICT_OLDEST = 0,
    /** Overwrite the lowest-valued splat, unless it is already >= the new one. */
    WORLD_HITMARK_POLICY_EVICT_SMALLEST = 1,
};

/** The reference's pre-loop default for the splat duration (opcode 9). */
#define WORLD_HITMARK_DEFAULT_DURATION 70

void
World_EntityAddHitmark(
    int* damage_values,
    int* damage_types,
    int* damage_start_cycles,
    int* damage_cycles,
    int loop_cycle,
    int damage_type,
    int damage_value,
    int delay,
    int slot_limit,
    int duration,
    int slot_policy);

/**
 * One decoded HEADBAR block, already resolved against its healthbar type.
 *
 * A struct rather than six parameters because the caller assembles it in one
 * place from one wire block, and because the two `World_*SetHealthbar` entry
 * points would otherwise repeat the same argument list twice each.
 */
struct WorldEntity_Headbar
{
    /** Healthbar config id. Never negative -- "no bar" is ClearHealthbar. */
    int type;
    /** loopCycle + the block's start delay. */
    int start_cycle;
    /** Cycles the fill travels over; 0 means it is already at `end_fill`. */
    int duration;
    int start_fill;
    int end_fill;
    /** start_cycle + duration + the type's persist window. */
    int end_cycle;
};

/** Copy a headbar onto a combat facet. Shared so the player and npc paths
 *  cannot drift; see World_PlayerSetHealthbar for why nothing is interpreted
 *  here. `cycle` is now: a bar that starts later holds the fill already on
 *  screen until it does (`healthbar_held_fill`). */
void
World_EntityApplyHeadbar(
    struct WorldEntityFacet_Combat* combat,
    struct WorldEntity_Headbar bar,
    int cycle);

#endif
