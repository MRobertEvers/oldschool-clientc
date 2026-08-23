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

    UITREE_FRAME_SLOT_COUNT
};

/**
 * One slot of a declaration.
 *
 * `placed` and not "w > 0", because a slot at 0x0 and a slot nobody mentioned
 * are different states and only the second one means "hide it".
 */
struct UITreeFrameSlotRect
{
    uint8_t placed;
    int x;
    int y;
    int w;
    int h;
};

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
 * Apply a whole declaration.
 *
 * `slots` is UITREE_FRAME_SLOT_COUNT entries. Every role is answered: a placed
 * one is moved and shown, an unplaced one is hidden. Then the lane's own
 * chrome is collected and hidden, and the collection is kept so that
 * UITree_FrameReassert can put it back each frame and UITree_FrameRelease can
 * undo it.
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
 * Re-hide what the last apply hid.
 *
 * Called once a frame, and it is not redundant. On a cache gameframe the
 * toplevel's own scripts show and hide its decoration constantly -- an
 * onload, a resize hook, a tab flip -- and a suppression applied once would be
 * undone by the first script that ran after it. Walks only the collected list,
 * so it costs the number of nodes actually hidden and not the tree.
 */
void
UITree_FrameReassert(struct UITree* tree);

/**
 * Give the frame back to the lane: the collected chrome is shown again and
 * every moved surface returns to the box it was authored at.
 *
 * Restoring the boxes matters as much as the hides. A released layout that
 * left the viewport at its own rectangle would hand back a gameframe with the
 * lane's stones drawn around a scene that is still the wrong size, which reads
 * as a broken client rather than as a plugin that stopped.
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

/** Drop the frame table. Called from UITree_Free / UITree_Clear: the node
 *  indices in it name nodes that are about to stop existing. */
void
UITree_FrameForget(struct UITree* tree);

#endif /* SRC_UITREE_FRAME_H */
