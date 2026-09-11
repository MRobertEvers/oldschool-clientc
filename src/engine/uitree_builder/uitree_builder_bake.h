#ifndef UITREE_BUILDER_BAKE_H
#define UITREE_BUILDER_BAKE_H

#include <stdint.h>

struct UITree;
struct UITreeBuilder;
struct UIBuilderManifest;
struct InvManager;
struct ToriRS_ComponentPack;
struct UITreeSceneBridge;

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

/**
 * Hide root-sibling interface groups that nothing has mounted.
 *
 * Post-bake cleanup shared by every path that can leave stray packs at the
 * root: the CS2 runtime bakes a pack as soon as a script touches it, which can
 * land before (or entirely without) the mount that gives it a parent.
 *
 * `opening_group` is the group the caller just opened and `target_uid` the
 * component it mounted into; pass -1 for either when there is none, which is
 * the case for a config-driven build where every declared mount is already
 * parented under its `rs_iface` owner.
 */
void
uitree_builder_hide_unmounted_spillover(
    struct UITree* tree,
    int opening_group,
    int target_uid);

/**
 * Re-pose player-preview MODEL widgets (clientCode 327/328) on the standard
 * human idle.
 *
 * The reference animates the preview with the player's readyanim; we have no
 * live player entity at build time, so use the default unarmed human idle. Runs
 * AFTER onLoad so a spurious CC/IF_SETMODELANIM (e.g. a facial sequence like
 * seq 0) does not leave only the head animating.
 */
void
uitree_builder_reassert_player_idle_anim(
    struct UITree* tree,
    struct UITreeSceneBridge const* bridge);

#endif
