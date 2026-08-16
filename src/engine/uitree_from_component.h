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

/*
 * Copy a pack's cache-authored runtime hooks (onclick, onop, onmouseover,
 * onmouseleave, onmouserepeat, onhold, ondrag, ondragcomplete, onscrollwheel,
 * ontimer) onto the nodes that are already in the tree.
 *
 * `UITree_BuildFromComponentPack` ends with this, so its callers get it for
 * free. It is public for the *other* bake —
 * `uitree_builder_bake_pack_under_owner` — which inserts nodes itself and
 * therefore has to ask. Both must, and the reason is not symmetry: a pack that
 * skips this mounts, draws and lays out perfectly, and then every handler the
 * cache declared on it is simply absent.
 *
 * Call it after the nodes exist; it resolves each component by id.
 */
void
UITree_BakePackRuntimeHooks(
    struct UITree* tree,
    struct ToriRS_ComponentPack const* pack);

#endif
