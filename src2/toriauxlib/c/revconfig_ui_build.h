#ifndef REVCONFIG_UI_BUILD_H
#define REVCONFIG_UI_BUILD_H

#include "revconfig/revconfig.h"
#include "ui/uitree.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <stdbool.h>
#include <stdint.h>

#define REVCONFIG_UI_MAX_SPRITE_REFS 256
#define REVCONFIG_UI_MAX_WORK_ITEMS 512

struct RevConfigUISpriteRef
{
    char name[64];
    int element_id;
    struct RevConfigCacheItem cache_item;
};

struct RevConfigUIBuildState
{
    struct RevConfigUISpriteRef sprite_refs[REVCONFIG_UI_MAX_SPRITE_REFS];
    int sprite_ref_count;
    int next_element_id;

    struct RevConfigUIComponentItem components[128];
    int component_count;

    struct RevConfigUILayoutItem layout_entries[128];
    int layout_entry_count;

    struct ToriAuxLibCore* core;
};

void
revconfig_ui_build_set_core(
    struct RevConfigUIBuildState* state,
    struct ToriAuxLibCore* core);

void
revconfig_ui_build_init(struct RevConfigUIBuildState* state);

void
revconfig_ui_build_collect_items(
    struct RevConfigUIBuildState* state,
    struct RevConfigItemBuffer const* items);

struct RevConfigUISpriteRef const*
revconfig_ui_build_find_ref(
    struct RevConfigUIBuildState const* state,
    char const* sprite_name);

int
revconfig_ui_build_lookup_sprite_id(
    struct RevConfigUIBuildState const* state,
    char const* sprite_name,
    int* out_atlas_index);

int32_t
revconfig_ui_build_node(
    struct RevConfigUIBuildState const* state,
    struct UITree* tree,
    struct RevConfigUILayoutItem const* le);

bool
revconfig_ui_build_tree(
    struct RevConfigUIBuildState const* state,
    struct UITree* tree);

#endif
