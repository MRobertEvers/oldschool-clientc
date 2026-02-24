#ifndef WORLD_OPTIONS_H
#define WORLD_OPTIONS_H

#include "minimenu_action.h"
#include "world.h"
#include "world_pickset.h"

struct WorldOption
{
    enum MinimenuAction action;
    char text[64];
    int param_a;
    int param_b;
    int param_c;
};

struct WorldOptionSet
{
    struct WorldOption options[20];
    int option_count;
};

void
world_options_from_pickset(
    struct World* world,
    struct WorldPickSet* pickset,
    struct WorldOptionSet* option_set);

#endif