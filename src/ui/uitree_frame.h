#ifndef SRC_UITREE_FRAME_H
#define SRC_UITREE_FRAME_H

/*
 * The gameframe, when a plugin is arranging it.
 *
 * A layout plugin brings its own art and cannot bring the live surfaces: the
 * 3D scene, the minimap, the chat log, the open sidebar interface and the
 * modal region are wired to the cache, the server and the world. So the frame
 * is split in two -- the plugin draws the picture and says where each of these
 * belongs inside it, and this file puts them there.
 *
 * Two jobs, and the second is the one that is easy to underestimate:
 *
 *   1. PLACE the live surfaces at the rectangles the declaration named.
 *   2. SUPPRESS the lane's own chrome, because two frames drawn at once is two
 *      sets of stones over one inventory. This is not a nicety -- a client
 *      showing the 2004 surround and a modern one at the same time is worse
 *      than either.
 *
 * Everything here is addressed by ROLE and never by id, which is what lets one
 * layout serve a 2004 dat1 frame and an OldSchool toplevel: on the first the
 * roles are revconfig builtins, on the second they are cache components
 * carrying a clientCode, and the plugin that placed them knows about neither.
 */

#include "uitree.h"

#include <stdint.h>

/**
 * The live surfaces a layout arranges.
 *
 * Deliberately the same order and the same count as enum
 * ToriRS_PluginLayoutSlot; the static asserts in uitree_frame.c are what keep
 * the two from drifting. Restated rather than included because the tree is not
 * allowed to depend on the plugin contract -- a headless uitree test links
 * neither the host nor a plugin.
 */
enum UITreeFrameSlot
{
    UITREE_FRAME_SLOT_VIEWPORT = 0,
    UITREE_FRAME_SLOT_MINIMAP,
    UITREE_FRAME_SLOT_COMPASS,
    UITREE_FRAME_SLOT_CHAT,
    UITREE_FRAME_SLOT_SIDEBAR,
    UITREE_FRAME_SLOT_MAIN_MODAL,
    UITREE_FRAME_SLOT_CHAT_BUTTONS,
    /** The minimap's orb column, a cache lane's pack beside the map. A 2004
     *  frame has none and answers "no such surface". */
    UITREE_FRAME_SLOT_ORBS,

    UITREE_FRAME_SLOT_COUNT
};

/** Nodes one role may be spread across. @see UITree_FrameSlotIndex. */
#define UITREE_FRAME_SLOT_NODES_MAX 16

/**
 * One rectangle of a declaration.
 *
 * `placed` and not "w > 0", because a rect at 0x0 and a rect nobody mentioned
 * are different states and only the second one means "hide it".
 */
struct UITreeFrameRect
{
    uint8_t placed;
    int x;
    int y;
    int w;
    int h;
};

/**
 * The art a live surface is drawn from, and the shape it is cut to.
 *
 * Two surfaces are neither art the frame draws nor content the world supplies:
 * the compass turns with the camera and the minimap is baked per level, so a
 * layout can only say what PICTURE they are made of. Which is a question about
 * the frame -- a 2004 compass rose inside an OldSchool map surround is the
 * same mismatch as 2004 stones around an OldSchool inventory.
 *
 * `mask` is what makes a FLOATING frame possible: the OldSchool resizable map
 * surround is a ring with the scene showing through everywhere it is not, so
 * an unmasked square of minimap draws its corners over the world. A housing
 * that is opaque around its hole needs none, and passes 0.
 *
 * Scene sprite ids, because that is what a component holds. `placed` carries
 * declaration presence: art id 0 keeps native art, while mask id 0 explicitly
 * removes the native mask (the public API's -1 image sentinel maps to 0 here).
 */
struct UITreeFrameSkin
{
    uint8_t placed;
    int art_scene_id;
    int mask_scene_id;
};

/**
 * Paint attached to a live surface, immediately above that surface's subtree.
 *
 * This is deliberately not part of either plugin canvas display list. Those
 * lists have canvas-wide z-order: FRAME is over the world and under every
 * interface, CANVAS is over every interface. Neither can express a minimap
 * housing, which has to be above the minimap and below whichever later sibling
 * the gameframe puts over it.
 *
 * Coordinates are canvas coordinates, matching a frame declaration. The emit
 * walk supplies the target's parent clip, so art may overlap the target (a map
 * ring has to) without escaping the surface that contains it.
 */
struct UITreeFrameOverlay
{
    uint8_t placed;
    int scene_id;
    int x;
    int y;
    /** 0 opaque, 255 invisible -- the renderer's ordinary sprite sense. */
    int trans;
};

/**
 * One slot of a declaration: a box for the whole role, and a box per member.
 *
 * Most roles are one surface and use `all`. Two are not, and they differ in a
 * way that matters:
 *
 *   The SIDEBAR is fourteen mounts at ONE rectangle -- only the selected tab
 *   is on screen -- so `all` says it once and every mount gets it.
 *   The CHAT BUTTONS are four controls at four DIFFERENT rectangles, side by
 *   side, and no single box can express that.
 *
 * So a placement may address the role as a whole or one member of it, and
 * `at[]` is the second. A member with its own box uses it; one without falls
 * back to `all`; a role with neither is hidden.
 */
struct UITreeFrameSlotRect
{
    struct UITreeFrameRect all;
    struct UITreeFrameRect at[UITREE_FRAME_SLOT_NODES_MAX];
    struct UITreeFrameSkin skin;
    struct UITreeFrameOverlay overlay;
};

/**
 * The number `node` answers to WITHIN its role, or -1 when the role has no
 * numbering of its own.
 *
 * The role's own numbering and never a position in a list, because a list
 * position is whatever order the frame happened to be built in and that is
 * exactly the thing a new revision changes. A chat button knows which FILTER
 * it toggles and a sidebar mount knows which TAB it holds; both survive being
 * rebuilt, reordered, or authored by a different cache.
 */
int
UITree_FrameSlotIndex(
    struct UITreeComponent const* node,
    int slot);

/**
 * The node carrying `slot`'s role, or -1 when this gameframe has none.
 *
 * A linear walk, and that is affordable because of WHEN it is called: once per
 * declaration -- a claim, a resize, a rebuild -- and never per frame. The
 * per-frame path is UITree_FrameReassert, which walks only what this found.
 */
int32_t
UITree_FrameSlotNode(
    struct UITree const* tree,
    int slot);

/**
 * The node carrying `slot`'s role and answering to `member`, or -1.
 *
 * `member` -1 means "any member", which is UITree_FrameSlotNode. A number is
 * the role's OWN numbering -- @see UITree_FrameSlotIndex -- so this answers
 * "does this frame have a Trade/duel button" and not "does it have a fourth
 * something".
 */
int32_t
UITree_FrameSlotMemberNode(
    struct UITree const* tree,
    int slot,
    int member);

/**
 * Apply a whole declaration.
 *
 * `slots` is UITREE_FRAME_SLOT_COUNT entries. Every role is answered: a placed
 * one receives an effective position override, an unplaced one is hidden.
 * Then the lane's own chrome is collected and hidden.
 *
 * The override never replaces the component's authored/script-owned position
 * or art. CS1/CS2 remain free to update that native state while the claim is
 * standing; layout and emit select the plugin layer, and release merely drops
 * it so the latest native state is revealed.
 *
 * `root_group` is the interface group of the cache gameframe, or -1 on a lane
 * whose frame is revconfig builtins. It is what tells the toplevel's OWN
 * decoration apart from the interface packs mounted inside it -- see
 * frame_is_lane_chrome.
 */
void
UITree_FrameApply(
    struct UITree* tree,
    struct UITreeFrameSlotRect const* slots,
    int root_group);

/**
 * Reconcile the standing declaration with the current tree generation.
 *
 * Called at the publication fence. A CS2 rebuild can reclaim a matched node
 * and reuse its array index after the earlier declaration; reconciliation
 * re-resolves the semantic roles and native chrome against the exact tree that
 * will be drawn. An unchanged binding is a no-op even when unrelated topology
 * bumped the tree generation.
 */
void
UITree_FrameReassert(struct UITree* tree);

/**
 * Give the frame back to the lane: the collected chrome is shown again and
 * the effective geometry/art overrides are dropped.
 *
 * There is deliberately no saved snapshot to restore. The cache's scripts may
 * have changed their native geometry or art while the plugin held the frame;
 * release must reveal that newest state rather than an old pre-claim copy.
 */
void
UITree_FrameRelease(struct UITree* tree);

/** 1 while a declaration is applied to this tree. */
int
UITree_FrameActive(struct UITree const* tree);

/**
 * How many lane-chrome nodes the standing declaration hid, and how many nodes
 * it found for `slot`.
 *
 * Diagnostics, and they answer the one question a screenshot cannot: when both
 * frames are on screen at once, is that because the suppression found nothing
 * to hide, or because it hid the wrong things? Zero and zero means the roles
 * were never recognised on this lane, which is a different bug from a layout
 * that placed them badly.
 */
int
UITree_FrameHiddenCount(struct UITree const* tree);

int
UITree_FrameSlotCount(struct UITree const* tree, int slot);

/**
 * Copy the effective plugin position for `node` into `out`, or return 0 when
 * the standing declaration does not override it.
 *
 * Used only by the layout resolver. The component's own position remains the
 * native CS1/CS2 value and receives the resolved abs_* result, so every normal
 * bounds consumer automatically observes the effective box.
 */
int
UITree_FramePositionOverride(
    struct UITree const* tree,
    int32_t node,
    struct UITreeElemPosition* out);

/** 1 when a placed slot fully owns `node`'s effective box. Geometry mutators
 *  use this to update native state without dirtying an unchanged frame. */
int
UITree_FramePositionOwned(
    struct UITree const* tree,
    int32_t node);

/**
 * Effective art/mask overrides for a skinned slot. Each output may be NULL.
 * Art zero keeps native art; mask zero explicitly removes the native mask.
 * Returns 1 for a currently bound skin.
 */
int
UITree_FrameSkinOverride(
    struct UITree const* tree,
    int32_t node,
    int* out_art_scene_id,
    int* out_mask_scene_id);

/**
 * Copy the paint attached to `node`'s semantic slot, or return 0.
 *
 * A role may have several members (sidebar mounts and chat buttons), while a
 * whole-slot overlay names exactly one semantic anchor. It is attached to the
 * role's primary node -- the same deterministic first match returned by
 * UITree_FrameSlotNode -- and only while that node is placed by the standing
 * declaration. The emit walk performs the final visibility test: if the
 * primary node/subtree emits nothing, neither does its attached paint.
 */
int
UITree_FrameOverlayOverride(
    struct UITree const* tree,
    int32_t node,
    struct UITreeFrameOverlay* out);

/** Drop the frame table. Called from UITree_Free / UITree_Clear: the node
 *  indices in it name nodes that are about to stop existing. */
void
UITree_FrameForget(struct UITree* tree);

/**
 * Who stamps the roles a cache gameframe does not declare for itself.
 *
 * A revconfig frame spells its regions in the tree -- `type=chat`, a
 * `slot=side_modal` tag, a builtin per sidebar tab -- and a cache gameframe
 * spells only three of them, through the clientCodes the client already reads
 * (world, minimap, compass). The chat container, the fourteen side panels, the
 * modal box and the orb column are ordinary layers whose only name is the
 * profile's `[role:frame_*]` chain, and this tree cannot resolve a role: it
 * has no role table.
 *
 * So the app hands the tree a binder, and the tree calls it before every
 * collection -- UITree_FrameApply and the reassert at the emit fence -- so a
 * declaration never lands on a rebuilt gameframe whose panels have not been
 * named yet. The binder stamps `slot_tag` and `frame_member_plus1` on the
 * nodes it resolves; a lane whose profile names no frame roles pays a table
 * lookup per role and stamps nothing.
 *
 * Idempotent and cheap: role resolution is memoised per tree generation.
 */
void
UITree_FrameSetBinder(
    struct UITree* tree,
    void (*binder)(struct UITree* tree, void* user),
    void* user);

/** Run the binder now, if one is set. What UITree_FrameApply does first; also
 *  the app's per-tick hook so a slot query that arrives with no declaration in
 *  flight still finds stamped nodes. */
void
UITree_FrameBind(struct UITree* tree);

#endif /* SRC_UITREE_FRAME_H */
