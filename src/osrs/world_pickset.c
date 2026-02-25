#include "world_pickset.h"

#include <assert.h>
#include <stdio.h>

void
world_pickset_reset(struct WorldPickSet* pickset)
{
    pickset->count = 0;
}

void
world_pickset_add(
    struct WorldPickSet* pickset,
    int x,
    int z,
    int entity_type,
    int entity_id)
{
    assert(pickset->count < 1000 && "Pickset count must be less than 1000");
    pickset->entities[pickset->count].x = x;
    pickset->entities[pickset->count].z = z;
    pickset->entities[pickset->count].entity_type = entity_type;
    pickset->entities[pickset->count].entity_id = entity_id;

    pickset->count++;
}