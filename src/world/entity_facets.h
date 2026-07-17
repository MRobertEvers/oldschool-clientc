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
    uint16_t anim_id;
    uint8_t frame;
    uint8_t cycle;
    uint8_t delay;
    uint8_t loop;
};

struct WorldEntityFacet_Animation
{
    struct WorldEntityFacet_AnimationStep primary;
    struct WorldEntityFacet_AnimationStep secondary;
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

#define WORLD_FACING_GRID_COORDS 0
#define WORLD_FACING_ENTITY_ID 1

struct WorldEntityFacet_Facing
{
    int mode;
    union
    {
        struct
        {
            uint16_t x;
            uint16_t z;
        } grid_coords;
        int entity_id;
    } u;
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
