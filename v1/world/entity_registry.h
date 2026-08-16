#ifndef ENTITY_REGISTRY_H
#define ENTITY_REGISTRY_H

#include <stdbool.h>

enum EntityKind
{
    RS_ENTITY_KIND_NONE = 0,
    RS_ENTITY_KIND_PLAYER = 1,
    RS_ENTITY_KIND_PROJECTILE = 2,
    RS_ENTITY_KIND_NPC = 3,
};

#define RS_ENTITY_KIND_SHIFT 28
#define RS_ENTITY_KIND_MASK 0xF
#define RS_ENTITY_INDEX_MASK ((1 << RS_ENTITY_KIND_SHIFT) - 1)
#define RS_ENTITY_ID(kind, index)                                                                  \
    (((int)(kind) << RS_ENTITY_KIND_SHIFT) | ((index) & RS_ENTITY_INDEX_MASK))
#define RS_ENTITY_KIND_OF(id) (((id) >> RS_ENTITY_KIND_SHIFT) & RS_ENTITY_KIND_MASK)
#define RS_ENTITY_INDEX_OF(id) ((id) & RS_ENTITY_INDEX_MASK)

#define ENTITY_REGISTRY_INITIAL_CAP 32

struct EntityRecord
{
    int entity_id;
    int element_id;
    int world_index;
};

/** Maps protocol entity ids to ToriDraw element ids and world entity indices. */
struct EntityRegistry
{
    struct EntityRecord* records;
    int count;
    int cap;
    int next_projectile_index;
    int next_player_index;
    int next_npc_index;
};

void
entity_registry_init(struct EntityRegistry* reg, int initial_cap);

void
entity_registry_free(struct EntityRegistry* reg);

struct EntityRecord*
entity_registry_find(struct EntityRegistry* reg, int entity_id);

struct EntityRecord const*
entity_registry_find_const(struct EntityRegistry const* reg, int entity_id);

bool
entity_registry_register(
    struct EntityRegistry* reg,
    int entity_id,
    int element_id,
    int world_index);

#endif
