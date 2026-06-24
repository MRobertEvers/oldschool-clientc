#ifndef WORLD_PICKSET_H
#define WORLD_PICKSET_H

#include <stdbool.h>

enum WorldPickType
{
    WORLD_PICK_TERRAIN = 0,
    WORLD_PICK_SCENERY,
    WORLD_PICK_PROJECTILE,
};

struct WorldPicked
{
    int element_id;
    enum WorldPickType type;
};

#define WORLD_PICKSET_MAX 256

struct WorldPickSet
{
    struct WorldPicked items[WORLD_PICKSET_MAX];
    int count;
};

void
world_pickset_reset(struct WorldPickSet* pickset);

void
world_pickset_add(
    struct WorldPickSet* pickset,
    int element_id,
    enum WorldPickType type);

#endif
