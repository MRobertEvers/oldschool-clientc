#ifndef INSTANCE_REVCONFIG_INV_BIND_H
#define INSTANCE_REVCONFIG_INV_BIND_H

#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"

struct GameRunescape;

/** Resolve revconfig inv= name to a UIInvDataService source on the game. */
int
instance_revconfig_inv_resolve_source(
    struct GameRunescape* game,
    char const* inv_name);

void
instance_revconfig_inv_seed_sources_from_pool(
    struct GameRunescape* game,
    struct InstanceRevConfigContext* ctx);

/** Register inv= source for a sidebar tab (no hardcoded IF3 injection). */
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
