#ifndef WORLD_ENTITY_REGISTRY_H
#define WORLD_ENTITY_REGISTRY_H

#include <stdbool.h>

enum World_EntityKind
{
    WORLD_ENTITY_KIND_NONE = 0,
    WORLD_ENTITY_KIND_PLAYER = 1,
    WORLD_ENTITY_KIND_PROJECTILE = 2,
    WORLD_ENTITY_KIND_NPC = 3,
};

#define WORLD_ENTITY_KIND_SHIFT 28
#define WORLD_ENTITY_KIND_MASK 0xF
#define WORLD_ENTITY_INDEX_MASK ((1 << WORLD_ENTITY_KIND_SHIFT) - 1)
#define WORLD_ENTITY_ID(kind, index)                                                               \
    (((int)(kind) << WORLD_ENTITY_KIND_SHIFT) | ((index) & WORLD_ENTITY_INDEX_MASK))
#define WORLD_ENTITY_KIND_OF(id) (((id) >> WORLD_ENTITY_KIND_SHIFT) & WORLD_ENTITY_KIND_MASK)
#define WORLD_ENTITY_INDEX_OF(id) ((id) & WORLD_ENTITY_INDEX_MASK)

#define WORLD_ENTITY_REGISTRY_INITIAL_CAP 32

struct World_EntityRecord
{
    int entity_id;
    int element_id;
    int world_index;
};

/** Maps protocol entity ids to scene element ids and world entity indices. */
struct World_EntityRegistry
{
    struct World_EntityRecord* records;
    int count;
    int cap;
    int next_projectile_index;
    int next_player_index;
    int next_npc_index;
};

void
World_EntityRegistryInit(
    struct World_EntityRegistry* reg,
    int initial_cap);

void
World_EntityRegistryFree(struct World_EntityRegistry* reg);

struct World_EntityRecord*
World_EntityRegistryFind(
    struct World_EntityRegistry* reg,
    int entity_id);

struct World_EntityRecord const*
World_EntityRegistryFindConst(
    struct World_EntityRegistry const* reg,
    int entity_id);

bool
World_EntityRegistryRegister(
    struct World_EntityRegistry* reg,
    int entity_id,
    int element_id,
    int world_index);

#endif
