#ifndef UITREE_BUILDER_BAKE_H
#define UITREE_BUILDER_BAKE_H

#include <stdint.h>

struct UITree;
struct UITreeBuilder;
struct UIBuilderManifest;
struct InvManager;
struct ToriRS_ComponentPack;

void
uitree_builder_bake(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest,
    struct InvManager* invs);

/**
 * Bake one loaded component pack as children of `owner_idx` — the exact path
 * the initial build uses for INI-mounted packs, exported so runtime slot
 * mounts (IF_OPENMAIN/OPENSIDE/etc.) reuse sprite/font resolution, inv
 * binding, and onload collection unchanged. The pack (and its assets) must
 * already be loaded in the builder's provider.
 */
void
uitree_builder_bake_pack_under_owner(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct ToriRS_ComponentPack const* pack,
    int32_t owner_idx,
    int inv_source_id);

#endif
