#include "entity_registry.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
World_EntityRegistryInit(
    struct World_EntityRegistry* reg,
    int initial_cap)
{
    assert(reg);
    memset(reg, 0, sizeof(*reg));
    if( initial_cap <= 0 )
        initial_cap = WORLD_ENTITY_REGISTRY_INITIAL_CAP;
    reg->cap = initial_cap;
    reg->records = calloc((size_t)initial_cap, sizeof(*reg->records));
}

void
World_EntityRegistryFree(struct World_EntityRegistry* reg)
{
    if( !reg )
        return;
    free(reg->records);
    reg->records = NULL;
    reg->count = 0;
    reg->cap = 0;
}

struct World_EntityRecord*
World_EntityRegistryFind(
    struct World_EntityRegistry* reg,
    int entity_id)
{
    assert(reg);
    if( !reg->records )
        return NULL;

    for( int i = 0; i < reg->count; i++ )
    {
        if( reg->records[i].entity_id == entity_id )
            return &reg->records[i];
    }
    return NULL;
}

struct World_EntityRecord const*
World_EntityRegistryFindConst(
    struct World_EntityRegistry const* reg,
    int entity_id)
{
    return World_EntityRegistryFind((struct World_EntityRegistry*)reg, entity_id);
}

bool
World_EntityRegistryRegister(
    struct World_EntityRegistry* reg,
    int entity_id,
    int element_id,
    int world_index)
{
    struct World_EntityRecord* existing;

    assert(reg);

    existing = World_EntityRegistryFind(reg, entity_id);
    if( existing )
    {
        existing->element_id = element_id;
        existing->world_index = world_index;
        return true;
    }

    if( reg->count >= reg->cap )
    {
        int new_cap = reg->cap ? reg->cap * 2 : WORLD_ENTITY_REGISTRY_INITIAL_CAP;
        struct World_EntityRecord* grown =
            realloc(reg->records, (size_t)new_cap * sizeof(*grown));
        if( !grown )
            return false;
        reg->records = grown;
        reg->cap = new_cap;
    }

    reg->records[reg->count++] = (struct World_EntityRecord){
        .entity_id = entity_id,
        .element_id = element_id,
        .world_index = world_index,
    };
    return true;
}
