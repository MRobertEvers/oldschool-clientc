#ifndef UITREE_BUILDER_INV_H
#define UITREE_BUILDER_INV_H

struct UITree;
struct UITreeBuilder;
struct UIBuilderManifest;
struct InvManager;

/** Seed InvManager from manifest InvSeed entries. */
void
uitree_builder_inv_seed(
    struct InvManager* invs,
    struct UIBuilderManifest const* manifest);

/** Bind rs_inv / sidebar inv_source_id from named sources after bake. */
void
uitree_builder_inv_bind_tree(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest,
    struct InvManager* invs);

#endif
