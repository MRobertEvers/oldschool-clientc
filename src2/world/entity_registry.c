#include "entity_registry.h"

#include <stdlib.h>
#include <string.h>

void
entity_registry_init(struct EntityRegistry* reg, int initial_cap)
{
    if( !reg )
        return;
    memset(reg, 0, sizeof(*reg));
    if( initial_cap <= 0 )
        initial_cap = ENTITY_REGISTRY_INITIAL_CAP;
    reg->cap = initial_cap;
    reg->records = calloc((size_t)initial_cap, sizeof(*reg->records));
}

void
entity_registry_free(struct EntityRegistry* reg)
{
    if( !reg )
        return;
    free(reg->records);
    reg->records = NULL;
    reg->count = 0;
    reg->cap = 0;
}

struct EntityRecord*
entity_registry_find(struct EntityRegistry* reg, int entity_id)
{
    if( !reg || !reg->records )
        return NULL;

    for( int i = 0; i < reg->count; i++ )
    {
        if( reg->records[i].entity_id == entity_id )
            return &reg->records[i];
    }
    return NULL;
}

struct EntityRecord const*
entity_registry_find_const(struct EntityRegistry const* reg, int entity_id)
{
    return entity_registry_find((struct EntityRegistry*)reg, entity_id);
}

bool
entity_registry_register(
    struct EntityRegistry* reg,
    int entity_id,
    int element_id,
    int world_index)
{
    struct EntityRecord* existing;

    if( !reg )
        return false;

    existing = entity_registry_find(reg, entity_id);
    if( existing )
    {
        existing->element_id = element_id;
        existing->world_index = world_index;
        return true;
    }

    if( reg->count >= reg->cap )
    {
        int new_cap = reg->cap ? reg->cap * 2 : ENTITY_REGISTRY_INITIAL_CAP;
        struct EntityRecord* grown =
            realloc(reg->records, (size_t)new_cap * sizeof(*grown));
        if( !grown )
            return false;
        reg->records = grown;
        reg->cap = new_cap;
    }

    reg->records[reg->count++] = (struct EntityRecord){
        .entity_id = entity_id,
        .element_id = element_id,
        .world_index = world_index,
    };
    return true;
}
