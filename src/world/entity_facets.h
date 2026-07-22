#ifndef WORLD_ENTITY_FACETS_H
#define WORLD_ENTITY_FACETS_H

#include <stdint.h>

struct WorldEntityFacet_IdleAnimations
{
    int readyanim;
    int walkanim;
    int turnanim;
    int runanim;
    int walkanim_b;
    int walkanim_r;
    int walkanim_l;
};

struct WorldEntityFacet_AnimationStep
{
    uint16_t anim_id; /* (uint16_t)-1 = none */
    uint16_t frame;   /* dat1 seqs run up to 512 frames */
    uint16_t cycle;   /* per-frame tick accumulator (durations exceed 255) */
    uint8_t delay;    /* primary start-delay countdown (reference primaryAnimDelay) */
    uint8_t loop;     /* primary loop counter vs seq maxloops */
};

struct WorldEntityFacet_Animation
{
    struct WorldEntityFacet_AnimationStep primary;
    struct WorldEntityFacet_AnimationStep secondary;
    /* Route length at the moment the primary seq was applied (reference
     * preanimRouteLength; drives the PreanimMove.DELAYANIM gate). */
    uint8_t preanim_route_length;
};

/* Server-forced interpolated move (reference exactMove1/exactMove2).
 * Tiles are scene-local; cycle stamps are absolute world cycles.
 * move_end == 0 && move_start == 0 => inactive. */
struct WorldEntityFacet_ExactMove
{
    uint8_t start_x;
    uint8_t start_z;
    uint8_t end_x;
    uint8_t end_z;
    int move_start;
    int move_end;
    uint8_t facing; /* * 512 = yaw */
};

/* Entity-attached graphic (reference spotanimId/Frame/Cycle/LastCycle). */
struct WorldEntityFacet_EntitySpotanim
{
    int id;         /* -1 = none */
    int height;
    int last_cycle; /* absolute world cycle the graphic starts at */
    int frame;      /* -1 while delayed */
    int cycle;
};

/**
 * Tile route queue for player/NPC movement.
 *
 * route[0] holds the entity's authoritative tile; new steps are prepended at 0
 * and older waypoints shift toward higher indices. route_length is the number
 * of queued tiles (0 = idle). Each tick the draw position moves toward
 * route[route_length - 1]; when it arrives, route_length is decremented.
 *
 * route_run[i] is 1 for a run step, 0 for walk. Queue depth is capped at 9.
 * Coordinates are world tiles; draw position uses 128 sub-tile units per tile.
 */
struct WorldEntityFacet_Pathing
{
    uint8_t route_length;
    uint8_t route_x[10];
    uint8_t route_z[10];
    uint8_t route_run[10];
};

/* Bit-packed tile coords: 9 bits x, 9 bits z, 4 bits level. */
struct WorldEntityFacet_GridPosition
{
    int x : 9;
    int z : 9;
    int level : 4;
};

/* Sub-tile position (128 units per tile). */
struct WorldEntityFacet_DrawPosition
{
    uint32_t x;
    uint32_t z;
    uint32_t y;
};

struct WorldEntityFacet_OrientationPYR
{
    uint16_t pitch;
    uint16_t yaw;
    uint16_t roll;
};

struct WorldEntityFacet_Orientation
{
    uint16_t yaw;
    uint16_t dst_yaw;
};

struct WorldEntityFacet_Action
{
    uint16_t code;
    char name[32];
};

/* Server-driven facing (reference ClientEntity.faceEntity / faceSquareX/Z).
 * The two are independent, not alternatives: an entity can be locked onto a
 * target *and* carry a pending square, and the reference applies both in that
 * order every tick (Client.ts entityFace, 3932). */
#define WORLD_FACING_ENTITY_NONE (-1)
/* faceEntity values >= this are player slots (value - 32768); below it, npc
 * slots (reference Client.ts:3937/3949). */
#define WORLD_FACING_PLAYER_BASE 32768

struct WorldEntityFacet_Facing
{
    /** Server slot of the locked-on entity, WORLD_FACING_ENTITY_NONE if any. */
    int entity_id;
    /** Pending face-coord, as the wire sends it: absolute half-tiles
     *  ((tile << 1) + 1). 0,0 = none — the reference's sentinel, and the
     *  reason this is stored raw rather than converted to a scene tile
     *  (scene tile 0 is a legitimate target). Cleared once consumed. */
    int square_x;
    int square_z;
    /** NpcType.turnspeed (players: 32). 0 = the entity never turns and
     *  entityFace returns immediately. */
    int turn_speed;
};

#define WORLD_ENTITY_DAMAGE_SLOTS 4

struct WorldEntityFacet_Combat
{
    uint8_t damage_values[WORLD_ENTITY_DAMAGE_SLOTS];
    uint8_t damage_types[WORLD_ENTITY_DAMAGE_SLOTS];
    int damage_cycles[WORLD_ENTITY_DAMAGE_SLOTS];
    int combat_cycle;
    int health;
    int total_health;
};

struct WorldEntityFacet_Appearance
{
    int slots[12];
    int colors[5];
};

#endif
