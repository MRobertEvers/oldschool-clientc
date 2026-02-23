#ifndef WORLD_PICKSET_H
#define WORLD_PICKSET_H

#include "game_entity.h"

struct PickedEntity
{
    int x : 7;
    int z : 7;
    int entity_type : 3;
    int entity_id : 14;
};

struct WorldPickSet
{
    struct PickedEntity entities[1000];
    int count;
};

void
world_pickset_reset(struct WorldPickSet* pickset);

void
world_pickset_add(
    struct WorldPickSet* pickset,
    int x,
    int z,
    int entity_type,
    int entity_id);

#endif