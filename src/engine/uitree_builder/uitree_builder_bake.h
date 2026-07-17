#ifndef UITREE_BUILDER_BAKE_H
#define UITREE_BUILDER_BAKE_H

struct UITree;
struct UITreeBuilder;
struct UIBuilderManifest;
struct InvManager;

void
uitree_builder_bake(
    struct UITree* tree,
    struct UITreeBuilder* builder,
    struct UIBuilderManifest const* manifest,
    struct InvManager* invs);

#endif
