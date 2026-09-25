#ifndef SRC_UITREE_FRAME_H
#define SRC_UITREE_FRAME_H

/*
 * The gameframe, when a plugin is PROVIDING it.
 *
 * A layout plugin brings its own art and cannot bring the live surfaces: the
 * 3D scene, the minimap, the chat log, the open sidebar interface and the
 * modal region are wired to the cache, the server and the world. The plugin
 * moves, hides, skins and anchors those surfaces itself through retained
 * widget edits (UITree_WidgetSet*), and this file does the half only the
 * engine can do:
 *
 *   1. BIND the roles -- which nodes carry each live surface -- so the fence's
 *      reassert and the staleness question keep working across rebuilds.
 *   2. SUPPRESS the lane's own chrome, because two frames drawn at once is two
 *      sets of stones over one inventory. This is not a nicety -- a client
 *      showing the 2004 surround and a modern one at the same time is worse
 *      than either.
 *   3. RELEASE from clipping every container above a surface the provider
 *      moved, so the new box is seen wherever the plugin put it.
 *
 * Everything here is addressed by ROLE and never by id, which is what lets one
 * layout serve a 2004 dat1 frame and an OldSchool toplevel: on the first the
 * roles are revconfig builtins, on the second they are cache components
 * carrying a clientCode, and the plugin that placed them knows about neither.
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

/**
 * The size the LANE authored for `slot`'s surface, before any plugin edit.
 *
 * The one thing a layout plugin cannot work out for itself and cannot be told
 * by its own art: the chatbox has a fixed interior -- a 463-wide message
 * column, a scrollbar beside it, an input line under a rule at y+77 -- and how
 * wide that interior is, is a fact about the revision. A 2004 frame authors
 * `chat_region` at 479x96; an OldSchool toplevel mounts interface 162 into a
 * 519x165 layer. A frame that assumes either is wrong on the other lane.
 *
 * The AUTHORED numbers and not the resolved ones, because the resolved ones
 * are the provider's own: while a plugin frame is committed, this node's
 * effective box is whatever the plugin last moved it to, and a plugin reading
 * that to decide what to place is reading its own answer back. A retained
 * widget edit leaves `position.width/height` alone for exactly this reason.
 *
 * @return 1 when the frame has the surface and its size is stated in pixels; 0
 * when it has no such surface, and 0 for a node sized as a PROPORTION of its
 * parent, whose authored `width` is a percentage and would report 40 for a
 * node that is 40% wide. The caller then knows only that it has to choose.
 */
int
UITree_FrameSlotNativeSize(
    struct UITree const* tree,
    int slot,
    int* out_w,
    int* out_h);

/**
 * The largest box inside `w` x `h` that `slot`'s surface lays itself out to
 * fill.
 *
 * NativeSize says what the lane authored; this says what else the surface can
 * be. Almost every surface is one picture at one size, and a bigger box is
 * that picture with empty space round it -- those answer 0. The 2004 chat
 * builtin is the exception: its message window, scrollbar, rule and input line
 * all follow the node's HEIGHT (@see UI_CHATVIEW_WINDOW_H), while its message
 * column and scrollbar stay 479 wide whatever the box says. So it answers its
 * authored width and the tallest whole-line height that fits -- more history,
 * never a sliver of a line against the top border.
 *
 * @return 1 with the box written when the surface has a size other than its
 * authored one to offer, or when the box asked about is exactly room for the
 * authored one; 0, writing nothing, for a one-size surface, for a box smaller
 * than the authored size, or for a lane with no such surface.
 */
int
UITree_FrameSlotFit(
    struct UITree const* tree,
    int slot,
    int w,
    int h,
    int* out_w,
    int* out_h);

/**
 * Lay the content MOUNTED into `slot`'s surface out in the surface's authored
 * size, centred in the box a provided frame gave it.
 *
 * A frame that grows a surface (@see UITree_FrameSlotFit) grows the node the
 * lane mounts its interfaces into, and those interfaces were authored for the
 * old box: a 2004 chat dialog is 479x96 and sits against the top of a taller
 * chat with the spare rows all underneath it. With this set, every child of
 * the slot's node resolves against the authored box centred in the effective
 * one -- the node itself, and anything that draws from its own origin rather
 * than through children (the chat builtin's history), keep the whole box.
 *
 * The provider's statement and kept with the provision: it survives a
 * re-provision and is cleared by UITree_FrameRelease. A slot whose authored
 * box is proportional, or no bigger than its effective one, is unaffected.
 *
 * @return 0 for a slot outside the table; 1 otherwise, including when nothing
 *         changed.
 */
int
UITree_FrameSetCenterContent(
    struct UITree* tree,
    int slot,
    int centered);

/**
 * The box a child of `parent` resolves against, when `parent` is a slot whose
 * mounted content is centred. `io_*` is the parent's resolved box on the way
 * in and the centred authored box on the way out.
 *
 * @return 1 when the box was replaced; 0, leaving it alone, otherwise.
 */
int
UITree_FrameContentBox(
    struct UITree const* tree,
    int32_t parent,
    int* io_x,
    int* io_y,
    int* io_w,
    int* io_h);

/**
 * The box the LANE authored for one MEMBER of `slot`, block-relative.
 *
 * UITree_FrameSlotNativeSize above answers for the surface as a whole; this is
 * the same question asked of the numbered things inside it, and it exists
 * because a frame that MOVES the block still has to re-seat each member, and
 * the only numbers it has otherwise are the ones the author happened to read
 * off one toplevel. 548 draws the world-map globe 30x30 and 601 draws it
 * 34x34; a plugin that carried 548's numbers onto 601 hung the globe's rim
 * over the alcove and clipped the wiki banner beside it. The offsets differ the
 * same way -- the orb pack's own scripts inset the globe and the banner from
 * the block's right edge by 10 and 8 columns on the fixed toplevel and by
 * nothing on the resizable ones.
 *
 * BLOCK-RELATIVE and not parent-relative: what a frame does is put the BLOCK
 * somewhere, and what it needs back is where each member sat inside it. The
 * pack a cache mounts in the block is an intermediate node with a box of its
 * own, and a caller handed a parent-relative offset would have to rediscover
 * and add that box itself -- which is the layout arithmetic this call exists
 * to do once. The offsets are summed up the parent chain from the member to
 * the slot's own node.
 *
 * The AUTHORED numbers, for the reason UITree_FrameSlotNativeSize states: the
 * resolved ones are whatever a committed plugin frame last moved them to, and
 * a provider reading those to decide where to place is reading its own answer
 * back.
 *
 * @param member the role's OWN numbering, @see UITree_FrameSlotIndex. -1 is
 *        not accepted here -- the whole surface is NativeSize's question, and
 *        it has no offset to report.
 * @return 1 when the frame has that member and every box between it and the
 *         block is stated in plain pixels; 0 otherwise -- no such member, a
 *         member the block does not contain, or a proportional/moded/relative
 *         box on the way up, whose `width` is a percentage or a delta and
 *         would be a silently wrong pixel count. Nothing is written on 0.
 */
int
UITree_FrameSlotMemberNativeBox(
    struct UITree const* tree,
    int slot,
    int member,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h);

/**
 * Take the frame for a plugin PROVIDING it through the widget API.
 *
 * The provider moves, hides, skins and anchors the live surfaces itself with
 * retained widget edits, so this places nothing and hides no surface; what it
 * does is the half only the engine can do -- collect and suppress the lane's
 * own chrome, by root group, and release from clipping every container above a
 * surface the provider moved -- and bind the roles so the fence's reassert and
 * the staleness question keep working. Every surface stays native until the
 * provider says otherwise.
 *
 * `root_group` is the interface group of the cache gameframe, or -1 on a lane
 * whose frame is revconfig builtins. It is what tells the toplevel's OWN
 * decoration apart from the interface packs mounted inside it -- see
 * frame_is_lane_chrome.
 *
 * `provider_owner` is the providing plugin's widget-edit owner id (the one it
 * passes to UITree_WidgetSetPosition), never 0. Only ITS retained moves and
 * resizes release the containers above a surface; another plugin's nudge on a
 * native row leaves that row's cache-owned layers clipping as the lane
 * authored them. Retained for the fence's reassert.
 *
 * Idempotent: an unchanged binding is an atomic no-op, never a release and
 * re-take that would flash the lane's frame through.
 */
void
UITree_FrameProvide(
    struct UITree* tree,
    int root_group,
    uint64_t provider_owner);

/**
 * Reconcile the standing provision with the current tree generation.
 *
 * Called at the publication fence. A CS2 rebuild can reclaim a matched node
 * and reuse its array index after the frame was taken; reconciliation
 * re-resolves the semantic roles, the moved surfaces and the native chrome
 * against the exact tree that will be drawn. An unchanged binding is a no-op
 * even when unrelated topology bumped the tree generation.
 */
void
UITree_FrameReassert(struct UITree* tree);

/**
 * Give the frame back to the lane: the collected chrome is shown again and
 * the released containers clip again. The provider's own retained edits are
 * its to drop (UITree_WidgetResetOwner).
 *
 * There is deliberately no saved snapshot to restore. The cache's scripts may
 * have changed their native geometry or art while the plugin held the frame;
 * release must reveal that newest state rather than an old pre-claim copy.
 */
void
UITree_FrameRelease(struct UITree* tree);

/** 1 while a plugin frame is taken on this tree. */
int
UITree_FrameActive(struct UITree const* tree);

/**
 * How many lane-chrome nodes the standing provision hid, and how many nodes
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
 * collection -- UITree_FrameProvide and the reassert at the emit fence -- so a
 * provision never lands on a rebuilt gameframe whose panels have not been
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

/** Run the binder now, if one is set. What UITree_FrameProvide does first;
 *  also the app's per-tick hook so a slot query that arrives with no provision
 *  in flight still finds stamped nodes. */
void
UITree_FrameBind(struct UITree* tree);

/**
 * Has the set of nodes carrying the frame roles moved since the frame was
 * taken? 1 when a fresh collection would bind different
 * nodes (a panel mounted, a rebuilt chat container), 0 when every role still
 * resolves to the node it did.
 *
 * The question the layout tick asks on a tree-generation change before it
 * schedules another frame build. Generation is a coarse signal: on an
 * OldSchool lane a cache timer script deletes and recreates its overlay nodes every logic
 * tick, so "the generation moved" is true on every frame and, read as "the
 * frame moved", rebuilt the whole layout at that rate. One walk of the
 * tree per generation change, and no chrome collection -- the emit fence's
 * reassert keeps the suppression right on its own.
 *
 * Runs the binder first, since the answer depends on its stamps. 0 for a
 * tree with no plugin frame taken.
 */
int
UITree_FrameSlotsStale(struct UITree* tree);

#endif /* SRC_UITREE_FRAME_H */
