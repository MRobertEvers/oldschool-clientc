#ifndef UITREE_OPTIONSET_H
#define UITREE_OPTIONSET_H

#include "osrs/minimenu_action.h"

#include <assert.h>

/** One row for UITree-derived context menu / tooltip (parallel to WorldOption, distinct type). */
struct UITreeOption
{
    enum MinimenuAction action;
    char text[64];
    int param_a;
    int param_b;
    int param_c;
};

#define UITREE_OPTION_SET_CAPACITY 64

struct UITreeOptionSet
{
    struct UITreeOption options[UITREE_OPTION_SET_CAPACITY];
    int option_count;

    /** Display order (same bubble-sort semantics as WorldOptionSet). */
    int order[UITREE_OPTION_SET_CAPACITY];
};

static inline struct UITreeOption*
uitree_option_set_get_option(
    struct UITreeOptionSet* option_set,
    int index)
{
    assert(index >= 0 && index < option_set->option_count);
    assert(option_set->order[index] >= 0 && option_set->order[index] < option_set->option_count);
    return &option_set->options[option_set->order[index]];
}

#endif
