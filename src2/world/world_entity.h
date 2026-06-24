#ifndef WORLD_ENTITY_H
#define WORLD_ENTITY_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

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

struct World_EntityList
{
    struct World_EntityPool terrain;
    struct World_EntityPool scenery;
    struct World_EntityPool projectile;
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
