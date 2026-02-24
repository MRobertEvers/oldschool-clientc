#ifndef WORLD_OPTION_SET_H
#define WORLD_OPTION_SET_H

#include "minimenu_action.h"

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

#endif
