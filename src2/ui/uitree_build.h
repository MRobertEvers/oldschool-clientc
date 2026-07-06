#ifndef UITREE_BUILD_H
#define UITREE_BUILD_H

#include "toriauxlib/core/toriauxlibcore_types.h"
#include "uitree.h"

struct UITreeBuildSource
{
    int count;
    struct ToriAuxLibCore_Component* (*get_component)(void* ud, int index);
    int (*get_parent_id)(void* ud, int index);
    /** Optional: map cache graphic id to scene element id; NULL leaves inv-slot bg at -1. */
    int (*resolve_sprite)(void* ud, int graphic_id);
    void* ud;
};

void
uitree_fill_position_from_component(
    struct StaticUIElemPosition* pos,
    struct ToriAuxLibCore_Component const* comp);

int32_t
uitree_push_component(
    struct UITree* tree,
    int32_t parent_index,
    struct ToriAuxLibCore_Component* comp,
    int (*resolve_sprite)(void*, int),
    void* resolve_ud);

int
uitree_build_from_source(
    struct UITree* tree,
    struct UITreeBuildSource const* src);

#endif
