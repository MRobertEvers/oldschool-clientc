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

#define WORLD_OPTION_SET_CAPACITY 64

struct WorldOptionSet
{
    struct WorldOption options[WORLD_OPTION_SET_CAPACITY];
    int option_count;

    int order[WORLD_OPTION_SET_CAPACITY];
};

static inline struct WorldOption*
world_option_set_get_option(
    struct WorldOptionSet* option_set,
    int index)
{
    assert(index >= 0 && index < option_set->option_count);
    assert(option_set->order[index] >= 0 && option_set->order[index] < option_set->option_count);
    return &option_set->options[option_set->order[index]];
}

#endif
