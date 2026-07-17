#ifndef SRC_ENGINE_UITREE_FROM_COMPONENT_H
#define SRC_ENGINE_UITREE_FROM_COMPONENT_H

struct UIBuildComponent;
struct UITree;
struct ToriRS_Component;
struct ToriRS_ComponentPack;

void
UITree_FillBuildFromToriRS(
    struct UIBuildComponent* dst,
    struct ToriRS_Component const* src);

int
UITree_BuildFromComponentPack(
    struct UITree* tree,
    struct ToriRS_ComponentPack const* pack,
    int (*resolve_sprite)(void*, int),
    int (*resolve_font)(void*, int),
    void* ud);

#endif
