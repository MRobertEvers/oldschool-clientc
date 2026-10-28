#ifndef SRC_UITREE_FRAME_H
#define SRC_UITREE_FRAME_H

/*
 * The gameframe's ROLES: which nodes carry each live surface -- the 3D scene,
 * the minimap, the chat log, the open sidebar interface, the modal region.
 *
 * Everything here is addressed by ROLE and never by id, which is what lets one
 * plugin find the viewport or the chat on a 2004 dat1 frame and on an
 * OldSchool toplevel alike: on the first the roles are revconfig builtins, on
 * the second they are cache components carrying a clientCode or stamped by
 * the app's binder. Also the widget-anchor ordering every paint and input
 * consumer runs (UITree_FrameReorder).
 *
 * Nothing here replaces or suppresses the lane's own chrome: plugins do not
 * provide a gameframe.
 */

#include "uitree.h"

#include <stdint.h>
#include <stddef.h>

/**
 * The live surfaces a layout arranges.
 *
 * Deliberately the same order and the same count as enum
 * ToriRS_HostSurfaceSlot; the static asserts in uitree_frame.c are what keep
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

/** 1 while a widget anchor orders paint and input across native order. */
int UITree_FrameHasDepth(struct UITree const* tree);
/** All frame paint and input consumers use this ordering. Records carry an
 * int32_t node-index-plus-one at node_offset; zero means unanchored paint.
 * Returns the retained count; a presented widget REPLACE removes the target's
 * records. */
int UITree_FrameReorder(struct UITree const* tree, struct UITreeHost const* host, void* records, int count,
                       size_t stride, size_t node_offset);

/** Release the reorder's retained plan and scratch. Called from UITree_Free. */
void UITree_FrameAnchorPlanFree(struct UITree* tree);

/**
 * Reorder accounting, for tests only -- never a lever and never read by the
 * pass itself.
 *
 * `out_allocations` is the number of heap allocations the plan has ever made:
 * a steady frame must not move it. `out_iterations` is the loop-step count of
 * the LAST call, the guard on the pass staying linear.
 */
void UITree_FrameReorderStats(struct UITree const* tree, unsigned long* out_allocations,
                              unsigned long* out_iterations);

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
 * provision -- a committed selection, resize, or rebuild -- and never per frame.
 * The per-frame path is UITree_FrameReassert, which walks only what this found.
 */
int32_t
UITree_FrameSlotNode(
    struct UITree const* tree,
    int slot);

/**
 * The lane's own HIT REGION for the compass, or -1 when this frame has none.
 *
 * A cache toplevel authors the compass as a rose that paints
 * (`clientcode=1339`, the builtin) and a bare sibling layer that carries its
 * four "Look <dir>" ops -- put there at runtime by `~torirs_compass_bind`, so
 * the layer has no op, no click mask and no art of its own to recognise it by.
 * The binder stamps it from the profile's `frame_compass_click` rung; a
 * revconfig frame, whose compass is one builtin, answers -1.
 *
 * A linear walk, asked once per provision. @see UITree_FrameSlotNode.
 */
int32_t UITree_FrameCompassClickNode(struct UITree const* tree);

/* Common native parent of a slot's numbered members. Unlike SlotNode (any
 * representative), this identifies the actual content container. Unavailable
 * when the bound members do not share a parent; no ancestor guessing. */
int32_t UITree_FrameSlotGroupNode(struct UITree const* tree, int slot);

/**
 * Stamp (or, with UITREE_SLOT_NONE / 0, clear) a binder's slot role on a node.
 *
 * The only way to write `slot_tag` / `frame_member_plus1` after a node is
 * created: the slot lookups keep a list of candidate nodes and trust it until
 * the tree's topology or `frame_stamp_serial` moves, so a direct write would
 * leave a newly tagged node out of every answer.
 */
void
UITree_FrameStamp(
    struct UITree* tree,
    int32_t idx,
    uint8_t slot_tag,
    uint8_t frame_member_plus1);

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

/** Drop the slot-candidate list. Called from UITree_Free / UITree_Clear: the
 *  node indices in it name nodes that are about to stop existing. */
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
 * So the app hands the tree a binder, and the tree calls it before a slot
 * question is answered on a rebuilt gameframe. The binder stamps `slot_tag` and `frame_member_plus1` on the
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

/** Run the binder now, if one is set. The app's per-tick hook, so a slot
 *  query finds stamped nodes. */
void
UITree_FrameBind(struct UITree* tree);

#endif /* SRC_UITREE_FRAME_H */
