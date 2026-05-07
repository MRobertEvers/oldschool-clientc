#ifndef WORLD_PICKSET_H
#define WORLD_PICKSET_H

#include "game_entity.h"

struct PickedEntity
{
    /** Scene/world tile indices (e.g. 0..ZONE_SCENE_SIZE-1); must not truncate like :7 bitfields. */
    int x;
    int z;
    int entity_type;
    /** Low bits of unified id payload (e.g. obj stack eid*4+layer); keep full range. */
    int entity_id;
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