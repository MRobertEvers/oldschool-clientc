#ifndef WORLD_ENTITY_H
#define WORLD_ENTITY_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORLD_ENTITY_NIL (-1)

struct World_EntityPoolNode
{
    int prev;
    int next;
    bool active;
};

struct World_EntityPool
{
    void* items;
    struct World_EntityPoolNode* nodes;
    int element_size;
    int capacity;
    int count;
    int active_count;
    int head;
    int tail;
    int free_head;
};

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

struct WorldEntityFacet_Animation
{
    uint16_t anim_id;
    uint8_t frame;
    uint8_t cycle;
    uint8_t delay;
    uint8_t loop;
};

struct WorldEntityFacet_Name
{
    char name[32];
};

struct WorldEntityFacet_Description
{
    char description[64];
};

struct WorldEntityFacet_Action
{
    uint16_t code;
    char name[32];
};

struct WorldEntityFacet_Actions
{
    struct WorldEntityFacet_Action* actions;
    uint8_t action_count;
};

#define FACING_GRID_COORDS 0
#define FACING_ENTITY_ID 1
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
    uint8_t damage_cycles[WORLD_ENTITY_DAMAGE_SLOTS];
    int combat_cycle;
    int health;
    int total_health;
};

/**
 * Tile route queue for player/NPC movement (mirrors legacy EntityPathing).
 *
 * route[0] holds the entity's authoritative tile; new steps are prepended at 0
 * and older waypoints shift toward higher indices. route_length is the number
 * of queued tiles (0 = idle). Each tick the draw position moves toward
 * route[route_length - 1]; when it arrives, route_length is decremented.
 *
 * route_run[i] is 1 for a run step, 0 for walk. Queue depth is capped at 9.
 * Coordinates are world tiles; draw position uses 128 sub-tile units per tile.
 *
 * Populated by server packets (walk/run direction, delta XZ jumps) and consumed
 * by the world cycle movement update. On map rebuild, all route slots are
 * shifted by the base-tile delta. See src/osrs/game_entity.c and
 * src/osrs/world_cycle.u.c.
 */
struct WorldEntityFacet_Pathing
{
    uint8_t route_length;
    uint8_t route_x[10];
    uint8_t route_z[10];
    uint8_t route_run[10];
};

// Bit-packed tile coords.
// 9 bits for x, 9 bits for z, 4 bits for level.
struct WorldEntityFacet_GridPosition
{
    int x : 9;
    int z : 9;
    int level : 4;
};

// Sub pixel position.
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

struct WorldEntityFacet_OrientationY
{
    uint16_t yaw;
};

struct WorldEntity_Terrain
{
    int element_id;
    int level;
    int x;
    int z;
};

struct WorldEntity_Scenery
{
    int element_id;
    int level;
    int scene_x;
    int scene_z;
    int size_x;
    int size_z;
    int orientation;
};

struct WorldEntity_Player
{
    int element_id;
    int level;
    int x;
    int z;
    int yaw;
    struct WorldEntityFacet_IdleAnimations idle_animations;
    struct WorldEntityFacet_Animation animation;
};

struct WorldEntity_NPC
{
    int element_id;
    int level;
    int x;
    int z;
    int yaw;
    int npc_id;
    int size;
    struct WorldEntityFacet_IdleAnimations idle_animations;
    struct WorldEntityFacet_Animation animation;
};

struct WorldEntity_Projectile
{
    int element_id;
    int level;
    int dst_level;

    /* Immutable params (world units / ticks). */
    int src_x;
    int src_z;
    int h1;
    int end_height;
    int t1;
    int t2;
    int angle;
    int startpos;
    int dst_x;
    int dst_z;

    /* Dynamic state. */
    int cycle;
    bool launched;
    double x;
    double y;
    double z;
    double vx;
    double vy;
    double vz;
    double velocity;
    double ay;
    int yaw;
    int pitch;
};

struct WorldEntity_Spotanim
{
    int element_id;
    int level;
    int scene_x;
    int scene_z;
    int y;
    int orientation;
    int idle_cycles;
    int active_cycle;
    int lifetime;
    bool active;
};

struct World_EntityList
{
    struct World_EntityPool terrain;
    struct World_EntityPool scenery;
    struct World_EntityPool player;
    struct World_EntityPool npc;
    struct World_EntityPool projectile;
    struct World_EntityPool spotanim;
};

void
World_EntityPoolInit(
    struct World_EntityPool* pool,
    int element_size);

void
World_EntityPoolFree(struct World_EntityPool* pool);

int
World_EntityPoolAlloc(struct World_EntityPool* pool);

void
World_EntityPoolRelease(
    struct World_EntityPool* pool,
    int index);

void*
World_EntityPoolGet(
    const struct World_EntityPool* pool,
    int index);

static inline bool
World_EntityPoolIsActive(
    const struct World_EntityPool* pool,
    int index)
{
    assert(!(!pool || index < 0 || index >= pool->count));
    return pool->nodes[index].active;
}

static inline int
World_EntityPoolHead(const struct World_EntityPool* pool)
{
    assert(pool);
    return pool->head;
}

static inline int
World_EntityPoolNext(
    const struct World_EntityPool* pool,
    int index)
{
    assert(!(!pool || index < 0 || index >= pool->count));
    return pool->nodes[index].next;
}

void
World_EntityListInit(struct World_EntityList* list);

void
World_EntityListFree(struct World_EntityList* list);

#endif
