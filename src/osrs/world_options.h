#ifndef WORLD_OPTIONS_H
#define WORLD_OPTIONS_H

#include "world.h"
#include "world_option_set.h"
#include "world_pickset.h"

struct GGame;

void
world_options_add_pickset_options(
    struct GGame* game,
    struct World* world,
    struct WorldPickSet* pickset,
    struct WorldOptionSet* option_set);

#endif