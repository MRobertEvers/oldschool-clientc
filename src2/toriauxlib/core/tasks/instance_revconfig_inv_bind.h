#ifndef INSTANCE_REVCONFIG_INV_BIND_H
#define INSTANCE_REVCONFIG_INV_BIND_H

#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"

struct GameRunescape;

/** Equipment iface 387: archive file index -> worn slot (container 94). */
int
instance_revconfig_inv_equipment_slot_index(int component_id);

bool
instance_revconfig_inv_is_equipment_slot_layer(int component_id);

/** Resolve revconfig inv= name to a UIInvDataService source on the game. */
int
instance_revconfig_inv_resolve_source(
    struct GameRunescape* game,
    char const* inv_name);

void
instance_revconfig_inv_seed_sources_from_pool(
    struct GameRunescape* game,
    struct InstanceRevConfigContext* ctx);

/** IF3 sidebar finalize: inject backpack grid / seed worn container. */
void
instance_revconfig_inv_finalize_sidebar(
    enum ToriAuxLibCacheMode cache_mode,
    struct InstanceRevConfigContext* ctx,
    struct RevConfigUIComponentItem const* comp,
    int32_t sidebar_tree_index);

void
instance_revconfig_inv_setup_after_build(
    struct GameRunescape* game,
    struct InstanceRevConfigContext* ctx);

void
instance_revconfig_inv_mark_dirty(
    struct GameRunescape* game,
    int source_id);

#endif
