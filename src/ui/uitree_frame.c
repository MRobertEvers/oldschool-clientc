#include "mem/torirs_mem.h"
#include "uitree_frame.h"

#include "plugin/torirs_plugin_host_types.h"
#include "uitree_layout.h"
#include "uitree_host.h"
#include "uitree_chatview.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"
#include "perf_audit.h"

/*
 * The tree's slot enum and the plugin contract's are one enum written twice.
 * The tree may not include the contract at runtime -- a headless uitree test
 * links neither the host nor a plugin -- so they are kept true here instead,
 * where a divergence stops the build rather than silently placing the chatbox
 * where the minimap was asked for.
 */
_Static_assert(
    (int)UITREE_FRAME_SLOT_VIEWPORT == (int)TORIRS_HOST_SURFACE_VIEWPORT,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_MINIMAP == (int)TORIRS_HOST_SURFACE_MINIMAP,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_COMPASS == (int)TORIRS_HOST_SURFACE_COMPASS,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_CHAT == (int)TORIRS_HOST_SURFACE_CHAT,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_SIDEBAR == (int)TORIRS_HOST_SURFACE_SIDEBAR,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_MAIN_MODAL == (int)TORIRS_HOST_SURFACE_MODAL,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_CHAT_BUTTONS == (int)TORIRS_HOST_SURFACE_CHAT_BUTTONS,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_ORBS == (int)TORIRS_HOST_SURFACE_ORBS,
    "frame slot order must match the plugin contract");
/* The PLACEABLE half, and only that half: the plugin contract grew derived
 * regions -- CANVAS and SAFE -- which are read through the same enum and are
 * not nodes in this tree, so its own count is the placeable one. */
_Static_assert(
    (int)UITREE_FRAME_SLOT_COUNT == (int)TORIRS_HOST_SURFACE_PLACEABLE_COUNT,
    "frame slot count must match the plugin contract's placeable half");

static int
frame_node_alive(
    struct UITree const* tree,
    int32_t idx)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return 0;
    return !tree->components[idx].freed;
}

/*
 * Does this node carry `slot`'s role?
 *
 * Two vocabularies, because there are two kinds of gameframe in this tree and
 * a role is spelled differently in each:
 *
 *   BUILTIN types    a revconfig frame declares `type=world`, `type=minimap`,
 *                    `type=chat`; the builder turns each into a node of the
 *                    matching UIELEM_BUILTIN_*.
 *   SLOT TAGS        a cache gameframe has no widget type for "a modal opens
 *                    here", so the profile tags the mount node (`slot=`) and
 *                    the slot manager finds it by the tag.
 *
 * A lane answers in whichever it has, and neither is a fallback for the other:
 * on a dat1 frame the chat REGION is a tagged mount and the chat itself is a
 * builtin, and both are legitimate answers to "where does chat go".
 */
/*
 * Could this node carry ANY slot's role?
 *
 * Every arm of frame_node_is_slot below answers yes only for one of these six
 * builtin types or for a node the profile tagged, so this is that switch's
 * precondition, factored out. The collection walk uses it to skip the
 * thousands of plain layers and graphics in one test instead of asking each of
 * them eight questions, and frame_node_is_slot calls it first so the two can
 * never disagree about what a candidate is.
 *
 * If you add an arm below that keys on something else, add its shape here in
 * the same commit, or the arm is dead.
 */
static int
frame_node_may_be_slot(struct UITreeComponent const* c)
{
    assert(c);
    if( c->slot_tag != UITREE_SLOT_NONE )
        return 1;
    switch( c->type )
    {
    case UIELEM_BUILTIN_WORLD:
    case UIELEM_BUILTIN_MINIMAP:
    case UIELEM_BUILTIN_COMPASS:
    case UIELEM_BUILTIN_CHAT:
    case UIELEM_BUILTIN_SIDEBAR:
    case UIELEM_BUILTIN_CHAT_BUTTON:
        return 1;
    default:
        return 0;
    }
}

static int
frame_node_is_slot(
    struct UITreeComponent const* c,
    int slot)
{
    assert(c);
    if( !frame_node_may_be_slot(c) )
        return 0;
    switch( slot )
    {
    case UITREE_FRAME_SLOT_VIEWPORT:
        return c->type == UIELEM_BUILTIN_WORLD;
    case UITREE_FRAME_SLOT_MINIMAP:
        return c->type == UIELEM_BUILTIN_MINIMAP;
    case UITREE_FRAME_SLOT_COMPASS:
        /*
         * Two nodes on a cache toplevel, and only one of them paints.
         *
         * The rose carries `clientcode=1339` and becomes the builtin; the hit
         * region is a bare sibling layer the binder stamps from the profile's
         * `frame_compass_click` rung. Binding BOTH is what keeps the region
         * out of the chrome collection and in the staleness question -- and
         * frame_collect_follows is what then gives it the rose's box, so the
         * compass answers the pointer where it is drawn.
         */
        return c->type == UIELEM_BUILTIN_COMPASS ||
               c->slot_tag == UITREE_SLOT_COMPASS_CLICK;
    case UITREE_FRAME_SLOT_CHAT:
        return c->type == UIELEM_BUILTIN_CHAT || c->slot_tag == UITREE_SLOT_CHAT;
    case UITREE_FRAME_SLOT_SIDEBAR:
        /*
         * A mount with no interface behind it is not a sidebar PANEL.
         *
         * The 2004 layout declares all fourteen tabs and gives the seventh
         * `componentno=-1`, because LostCity has no clan chat to put there and
         * never sends an if_settab for it. The node exists so the tab set is
         * the shape every revision since 2001 numbers it, and it stands for
         * nothing -- so a layout asking "does this frame have tab 7" has to
         * hear no, or it draws an icon over a panel that cannot open and
         * invites the click that does nothing.
         *
         * Said here rather than in UITree_FrameSlotIndex, which answers what
         * number a node ANSWERS TO: this mount does answer to seven. What it
         * is not is a member of the role.
         */
        if( c->type == UIELEM_BUILTIN_SIDEBAR )
            return c->u.sidebar.componentno >= 0;
        return c->slot_tag == UITREE_SLOT_SIDE_MODAL;
    case UITREE_FRAME_SLOT_MAIN_MODAL:
        return c->slot_tag == UITREE_SLOT_MAIN_MODAL;
    case UITREE_FRAME_SLOT_CHAT_BUTTONS:
        return c->type == UIELEM_BUILTIN_CHAT_BUTTON || c->slot_tag == UITREE_SLOT_CHAT_BUTTON;
    case UITREE_FRAME_SLOT_ORBS:
        /* Only ever a binder's stamp: no revconfig frame authors one. */
        return c->slot_tag == UITREE_SLOT_ORBS;
    default:
        return 0;
    }
}

/*
 * Is this node a PART of the role rather than the role itself?
 *
 * Two shapes, and the rule is the same for both: what the role means to every
 * whole-role query (its native size, its box, is it there) is the one node
 * that IS it, and a part answers only its own lookup.
 *
 *   ORBS          the binder tags the block and the buttons inside it with the
 *                 same slot; the block is "the orbs", a button is a member and
 *                 answers UITree_FrameSlotMemberNode.
 *   COMPASS       the rose is the surface -- it is what paints, what a re-skin
 *                 dresses and what `surface_native_size` reports; the hit
 *                 region beside it answers UITree_FrameCompassClickNode.
 *
 * A sidebar mount or a chat button IS the role at its number, so neither is
 * one of these.
 */
static int
frame_node_is_own_member(
    struct UITreeComponent const* c,
    int slot)
{
    assert(c);
    if( slot == UITREE_FRAME_SLOT_COMPASS )
        return c->slot_tag == UITREE_SLOT_COMPASS_CLICK;
    return slot == UITREE_FRAME_SLOT_ORBS && UITree_FrameSlotIndex(c, slot) >= 0;
}

int
UITree_FrameSlotIndex(
    struct UITreeComponent const* node,
    int slot)
{
    assert(node);
    switch( slot )
    {
    case UITREE_FRAME_SLOT_CHAT_BUTTONS:
        if( node->type == UIELEM_BUILTIN_CHAT_BUTTON )
            return (int)UITree_ChatButton(node)->filter;
        return (int)node->frame_member_plus1 - 1;
    case UITREE_FRAME_SLOT_SIDEBAR:
        if( node->type == UIELEM_BUILTIN_SIDEBAR )
            return node->u.sidebar.tabno;
        /* A cache gameframe's `sideN`, numbered by the binder. Its side-modal
         * region -- the one node a 2004 frame tags -- carries no number and
         * answers -1 here on both kinds of frame. */
        return (int)node->frame_member_plus1 - 1;
    case UITREE_FRAME_SLOT_ORBS:
        /* A button inside the orb pack the binder named on its own (the
         * activity adviser); the block itself carries no number. */
        return (int)node->frame_member_plus1 - 1;
    default:
        /* One surface, nothing to number. */
        return -1;
    }
}

/*
 * The slot -> node answers, remembered per tree.
 *
 * Both lookups below are linear scans of every component, and a frame asks
 * them several times (the plugin bridge's slot-node cache and its widget
 * find / find_all answers, the role table's _SLOT matches): 0.18 ms a frame
 * on the Moto X.
 *
 * What makes a node answer a slot is not only topology: a sidebar mount is
 * a slot member only while `componentno >= 0`, which the server sets and
 * clears with no topology bump (the minimap orbs vanished the first time
 * this was keyed on `generation` alone). So a remembered HIT is re-checked
 * against the node it names before it is returned -- O(1), the same tests
 * the scan applies -- and falls back to the scan when the node no longer
 * answers; a MISS is never remembered, because nothing cheap can say a
 * new match has not appeared. Topology changes (add, free, re-parent) bump
 * `generation` and reset the table outright, so a reclaimed index cannot
 * be believed. One entry per tree pointer. -2 marks "not remembered".
 *
 * The misses are what made this expensive: a caller that asks for members a
 * frame does not name (app_plugin_tab_active asks for all fourteen sidebar
 * tabs, once per tab-stone widget, every frame) scanned all ~7,000 nodes of
 * an OSRS239 tree per miss -- 67% of the Moto X's CPU under the Stone Drawer,
 * 12.8 -> 68 ms a frame. So the scans below do not walk the tree: they walk
 * the CANDIDATES, the few dozen nodes frame_node_may_be_slot admits, in index
 * order. Candidacy is a node's type (fixed at creation) or its slot_tag,
 * which only UITree_FrameStamp writes after creation and which bumps
 * `frame_stamp_serial`; so the list is rebuilt on topology or a stamp and
 * the live tests -- componentno included -- still run on every lookup.
 */
static struct
{
    struct UITree const* tree;
    uint64_t instance;
    uint32_t generation;
    uint32_t count;
    uint32_t stamp_serial;
    int valid;
    int32_t* ids;
    uint32_t ids_count;
    uint32_t ids_capacity;
} frame_slot_candidates;

static void
frame_slot_candidates_refresh(struct UITree const* tree)
{
    assert(tree);
    if( frame_slot_candidates.valid && frame_slot_candidates.tree == tree &&
        frame_slot_candidates.instance == tree->instance_id &&
        frame_slot_candidates.generation == tree->generation &&
        frame_slot_candidates.count == tree->component_count &&
        frame_slot_candidates.stamp_serial == tree->frame_stamp_serial )
        return;

    frame_slot_candidates.ids_count = 0;
    UITREE_SCAN_METER(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed || !frame_node_may_be_slot(c) )
            continue;
        if( frame_slot_candidates.ids_count == frame_slot_candidates.ids_capacity )
        {
            uint32_t const capacity =
                frame_slot_candidates.ids_capacity ? frame_slot_candidates.ids_capacity * 2 : 64;
            frame_slot_candidates.ids =
                ToriMem_Realloc(ToriMem_Ui(), frame_slot_candidates.ids, capacity * sizeof(*frame_slot_candidates.ids));
            assert(frame_slot_candidates.ids);
            frame_slot_candidates.ids_capacity = capacity;
        }
        frame_slot_candidates.ids[frame_slot_candidates.ids_count++] = (int32_t)i;
    }
    frame_slot_candidates.tree = tree;
    frame_slot_candidates.instance = tree->instance_id;
    frame_slot_candidates.generation = tree->generation;
    frame_slot_candidates.count = tree->component_count;
    frame_slot_candidates.stamp_serial = tree->frame_stamp_serial;
    frame_slot_candidates.valid = 1;
}

void
UITree_FrameStamp(
    struct UITree* tree,
    int32_t idx,
    uint8_t slot_tag,
    uint8_t frame_member_plus1)
{
    struct UITreeComponent* c;

    assert(tree);
    assert(idx >= 0);
    assert((uint32_t)idx < tree->component_count);
    c = &tree->components[idx];
    if( c->slot_tag == slot_tag && c->frame_member_plus1 == frame_member_plus1 )
        return;
    c->slot_tag = slot_tag;
    c->frame_member_plus1 = frame_member_plus1;
    tree->frame_stamp_serial++;
}

static struct
{
    struct UITree const* tree;
    uint64_t instance;
    uint32_t generation;
    uint32_t count;
    int32_t node[UITREE_FRAME_SLOT_COUNT][1 + UITREE_FRAME_SLOT_NODES_MAX];
    int32_t group[UITREE_FRAME_SLOT_COUNT];
} frame_slot_cache;

static int32_t*
frame_slot_cache_entry(
    struct UITree const* tree,
    int slot,
    int member)
{
    if( frame_slot_cache.tree != tree || frame_slot_cache.instance != tree->instance_id ||
        frame_slot_cache.generation != tree->generation ||
        frame_slot_cache.count != tree->component_count )
    {
        frame_slot_cache.tree = tree;
        frame_slot_cache.instance = tree->instance_id;
        frame_slot_cache.generation = tree->generation;
        frame_slot_cache.count = tree->component_count;
        memset(frame_slot_cache.node, 0xFE, sizeof(frame_slot_cache.node)); /* -2 */
        for( int i=0; i<UITREE_FRAME_SLOT_COUNT; ++i ) frame_slot_cache.group[i]=-2;
    }
    return &frame_slot_cache.node[slot][1 + member];
}

int32_t
UITree_FrameSlotNode(
    struct UITree const* tree,
    int slot)
{
    int32_t* cached;
    assert(tree);
    if( slot < 0 || slot >= UITREE_FRAME_SLOT_COUNT )
        return -1;

    /* The world is latched, so it costs nothing to ask for and is the one role
     * every frame has. */
    if( slot == UITREE_FRAME_SLOT_VIEWPORT )
        return frame_node_alive(tree, tree->world_index) ? tree->world_index : -1;
    cached = frame_slot_cache_entry(tree, slot, -1);
    if( *cached >= 0 )
    {
        struct UITreeComponent const* c = &tree->components[*cached];
        if( !c->freed && frame_node_is_slot(c, slot) && !frame_node_is_own_member(c, slot) )
            return *cached;
        *cached = -2;
    }

    /*
     * The FIRST match, not the last.
     *
     * A dat2 gameframe carries a sidebar mount per tab and a chat region that
     * a script may have cloned; taking the last would follow whichever
     * dynamic child was created most recently, which changes as the session
     * runs. The first is the one the frame was baked with, and it does not
     * move.
     */
    frame_slot_candidates_refresh(tree);
    for( uint32_t k = 0; k < frame_slot_candidates.ids_count; k++ )
    {
        int32_t const i = frame_slot_candidates.ids[k];
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;
        if( frame_node_is_slot(c, slot) && !frame_node_is_own_member(c, slot) )
            return *cached = i;
    }
    return -1;
}

int32_t
UITree_FrameSlotGroupNode(struct UITree const* tree, int slot)
{
    if( !tree || slot < 0 || slot >= UITREE_FRAME_SLOT_COUNT ) return -1;
    (void)frame_slot_cache_entry(tree,slot,-1);
    if( frame_slot_cache.group[slot] >= 0 ) return frame_slot_cache.group[slot];
    int32_t parent=-1;
    frame_slot_candidates_refresh(tree);
    for( uint32_t k=0; k<frame_slot_candidates.ids_count; ++k )
    {
        struct UITreeComponent const* c=&tree->components[frame_slot_candidates.ids[k]];
        if( c->freed || !c->frame_member_plus1 || !frame_node_is_slot(c,slot) ) continue;
        if( c->parent < 0 || (parent >= 0 && parent != c->parent) )
            return frame_slot_cache.group[slot]=-1;
        parent=c->parent;
    }
    return frame_slot_cache.group[slot] = parent;
}

int32_t
UITree_FrameSlotMemberNode(
    struct UITree const* tree,
    int slot,
    int member)
{
    int32_t* cached;
    assert(tree);
    if( member < 0 )
        return UITree_FrameSlotNode(tree, slot);
    if( slot < 0 || slot >= UITREE_FRAME_SLOT_COUNT )
        return -1;
    if( member >= UITREE_FRAME_SLOT_NODES_MAX )
        return -1;
    cached = frame_slot_cache_entry(tree, slot, member);
    if( *cached >= 0 )
    {
        struct UITreeComponent const* c = &tree->components[*cached];
        if( !c->freed && frame_node_is_slot(c, slot) && UITree_FrameSlotIndex(c, slot) == member )
            return *cached;
        *cached = -2;
    }

    frame_slot_candidates_refresh(tree);
    for( uint32_t k = 0; k < frame_slot_candidates.ids_count; k++ )
    {
        int32_t const i = frame_slot_candidates.ids[k];
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;
        if( !frame_node_is_slot(c, slot) )
            continue;
        if( UITree_FrameSlotIndex(c, slot) == member )
            return *cached = i;
    }
    return -1;
}

int32_t
UITree_FrameCompassClickNode(struct UITree const* tree)
{
    assert(tree);
    /*
     * Over the slot candidates: a stamped node is one by construction. This
     * is asked by every provision, and a provision runs on every topology
     * change -- once per logic tick on an OSRS lane -- so it is not rare.
     */
    frame_slot_candidates_refresh(tree);
    for( uint32_t k = 0; k < frame_slot_candidates.ids_count; k++ )
    {
        int32_t const i = frame_slot_candidates.ids[k];
        struct UITreeComponent const* c = &tree->components[i];
        if( !c->freed && c->slot_tag == UITREE_SLOT_COMPASS_CLICK )
            return i;
    }
    return -1;
}

int
UITree_FrameHasDepth(struct UITree const* tree)
{
    assert(tree);
    return UITree_WidgetAnchorCount(tree) > 0;
}

/*
 * Widget anchors: three relations -- OVER, BEHIND, REPLACE -- stated between
 * NODES and retained on the anchored node by its owner
 * (UITree_WidgetSetAnchor). Every paint and input consumer orders by these
 * through the same UITree_FrameReorder call.
 *
 * An ordering UNIT is the subtree of an anchored node or of an anchor target,
 * cut at any nested unit. A target's tree is written where its earliest record
 * stood: BEHIND children first, then the target's own records -- or a presented
 * REPLACE child's instead -- then OVER children, each child recursively. A
 * child whose target has no records at all keeps its native position: there is
 * nothing to be over or behind. REPLACE inherits the target's native veto: a
 * hidden target drops its replacement, and a hidden replacement reveals the
 * target.
 *
 * WHAT THIS COSTS, and why it is shaped the way it is. The pass runs eight
 * times a frame -- emit, input, hover -- and the first implementation was
 * O(records x nodes): it rescanned every node to find a unit's children, and
 * again at every record position to find the root whose tree started there. On
 * the measured rev-239 gameframe (7,202 nodes) that was 13.5 ms per FRAME with
 * a SINGLE anchored widget, plus eight mallocs per call.
 *
 * So the shape is split in two:
 *
 *   - A PLAN (struct UITreeAnchorPlan), held on the tree and rebuilt only when
 *     UITree::anchor_generation moves (an anchor edit was written or dropped)
 *     or UITree::generation moves (topology changed, so a target's ref may have
 *     died). It holds the ordering units sorted by node index, each unit's
 *     parent as a unit SLOT, its relation, and its children in an intrusive
 *     bucket -- so nobody ever scans all nodes for "who anchors to me".
 *
 *   - A per-call pass that touches only records, units and children and
 *     allocates nothing: every array it needs lives on the plan, grown once to
 *     the largest record count it has seen.
 *
 * The node -> unit memo is the one piece that must survive a generation change
 * -- a tree that pushes a node every frame would otherwise rebuild the plan and
 * lose it. It is two STAMPED arrays, never cleared: an entry is the truth only
 * while its stamp equals the plan's current epoch, so invalidating the whole
 * memo is one 64-bit increment, and a node created after the plan was built
 * simply reads as unstamped and is resolved on its first record.
 */

/** Children of one unit that an ordering can reach. */
#define ANCHOR_CHILDREN_MAX 64
/** The deepest unit an ordering reaches, counted from the root unit. */
#define ANCHOR_DEPTH_MAX 64

struct UITreeAnchorPlan
{
    /* --- Rebuilt only when the anchor or topology generation moves. --- */
    int32_t* unit_node;           /* node index of each unit, ASCENDING */
    int32_t* unit_parent;         /* unit slot of the anchor target, -1 = root */
    int32_t* unit_child_head;     /* intrusive child list, node order, uncapped */
    int32_t* unit_child_tail;
    int32_t* unit_child_next;
    int32_t* unit_depth;          /* distance from the root unit, -1 unreachable */
    int32_t* unit_order;          /* breadth-first: parents before children */
    unsigned char* unit_relation;
    int unit_count;
    int unit_order_count;
    uint32_t unit_capacity;
    uint32_t built_anchor_generation;
    uint32_t built_generation;
    int built;

    /* --- The node memo. Stamped, never cleared; see the note above. --- */
    int32_t* node_unit;
    uint64_t* node_stamp;         /* == memo_epoch while node_unit is the truth */
    int32_t* unit_slot_at;        /* the node's OWN slot, when it is a unit */
    uint64_t* unit_stamp;         /* == build_epoch while unit_slot_at is true */
    uint32_t node_capacity;
    uint64_t memo_epoch;          /* bumped on every rebuild; 64-bit, never wraps */
    uint64_t build_epoch;

    /* --- Per-call scratch, grown to the largest record count seen. --- */
    int32_t* record_unit;
    int32_t* record_next;         /* intrusive per-unit record list, ascending */
    int32_t* root_head;           /* count+1 buckets: roots by their tree's first */
    uint32_t record_capacity;
    unsigned char* output;
    size_t output_capacity;

    /* --- Per-call scratch, one entry per unit. --- */
    int* unit_first;              /* the unit's own earliest record, count if none */
    int* unit_tree_first;         /* earliest record of the whole tree under it */
    int32_t* unit_record_head;
    int32_t* unit_record_tail;
    int32_t* unit_root_next;      /* chain within one root_head bucket */
    unsigned char* unit_handled;

    /* Accounting for tests; see UITree_FrameReorderStats. */
    unsigned long allocations;
    unsigned long iterations;
};

static void*
anchor_plan_grow(struct UITreeAnchorPlan* plan, void* block, size_t bytes)
{
    void* grown;
    assert(plan);
    assert(bytes > 0);
    grown = ToriMem_Realloc(ToriMem_Ui(), block, bytes);
    assert(grown);
    plan->allocations++;
    return grown;
}

static void
anchor_plan_reserve_units(struct UITreeAnchorPlan* plan, uint32_t need)
{
    assert(plan);
    if( plan->unit_capacity >= need )
        return;
    plan->unit_node = anchor_plan_grow(plan, plan->unit_node, (size_t)need * sizeof(*plan->unit_node));
    plan->unit_parent = anchor_plan_grow(plan, plan->unit_parent, (size_t)need * sizeof(*plan->unit_parent));
    plan->unit_child_head = anchor_plan_grow(plan, plan->unit_child_head, (size_t)need * sizeof(*plan->unit_child_head));
    plan->unit_child_tail = anchor_plan_grow(plan, plan->unit_child_tail, (size_t)need * sizeof(*plan->unit_child_tail));
    plan->unit_child_next = anchor_plan_grow(plan, plan->unit_child_next, (size_t)need * sizeof(*plan->unit_child_next));
    plan->unit_depth = anchor_plan_grow(plan, plan->unit_depth, (size_t)need * sizeof(*plan->unit_depth));
    plan->unit_order = anchor_plan_grow(plan, plan->unit_order, (size_t)need * sizeof(*plan->unit_order));
    plan->unit_relation = anchor_plan_grow(plan, plan->unit_relation, (size_t)need * sizeof(*plan->unit_relation));
    plan->unit_first = anchor_plan_grow(plan, plan->unit_first, (size_t)need * sizeof(*plan->unit_first));
    plan->unit_tree_first = anchor_plan_grow(plan, plan->unit_tree_first, (size_t)need * sizeof(*plan->unit_tree_first));
    plan->unit_record_head = anchor_plan_grow(plan, plan->unit_record_head, (size_t)need * sizeof(*plan->unit_record_head));
    plan->unit_record_tail = anchor_plan_grow(plan, plan->unit_record_tail, (size_t)need * sizeof(*plan->unit_record_tail));
    plan->unit_root_next = anchor_plan_grow(plan, plan->unit_root_next, (size_t)need * sizeof(*plan->unit_root_next));
    plan->unit_handled = anchor_plan_grow(plan, plan->unit_handled, (size_t)need * sizeof(*plan->unit_handled));
    plan->unit_capacity = need;
}

/* The memo's new tail must read as unstamped, or a node pushed after the last
 * build inherits whatever an older node at that index resolved to. */
static void
anchor_plan_reserve_nodes(struct UITreeAnchorPlan* plan, uint32_t need)
{
    uint32_t had;
    assert(plan);
    had = plan->node_capacity;
    if( had >= need )
        return;
    plan->node_unit = anchor_plan_grow(plan, plan->node_unit, (size_t)need * sizeof(*plan->node_unit));
    plan->node_stamp = anchor_plan_grow(plan, plan->node_stamp, (size_t)need * sizeof(*plan->node_stamp));
    plan->unit_slot_at = anchor_plan_grow(plan, plan->unit_slot_at, (size_t)need * sizeof(*plan->unit_slot_at));
    plan->unit_stamp = anchor_plan_grow(plan, plan->unit_stamp, (size_t)need * sizeof(*plan->unit_stamp));
    memset(plan->node_stamp + had, 0, (size_t)(need - had) * sizeof(*plan->node_stamp));
    memset(plan->unit_stamp + had, 0, (size_t)(need - had) * sizeof(*plan->unit_stamp));
    plan->node_capacity = need;
}

static void
anchor_plan_reserve_records(struct UITreeAnchorPlan* plan, uint32_t need)
{
    assert(plan);
    if( plan->record_capacity >= need )
        return;
    plan->record_unit = anchor_plan_grow(plan, plan->record_unit, (size_t)need * sizeof(*plan->record_unit));
    plan->record_next = anchor_plan_grow(plan, plan->record_next, (size_t)need * sizeof(*plan->record_next));
    /* One bucket per record position PLUS the "no records anywhere" position. */
    plan->root_head = anchor_plan_grow(plan, plan->root_head, ((size_t)need + 1) * sizeof(*plan->root_head));
    plan->record_capacity = need;
}

static void
anchor_plan_reserve_output(struct UITreeAnchorPlan* plan, size_t bytes)
{
    assert(plan);
    if( plan->output_capacity >= bytes )
        return;
    plan->output = anchor_plan_grow(plan, plan->output, bytes);
    plan->output_capacity = bytes;
}

static struct UITreeAnchorPlan*
anchor_plan_of(struct UITree* tree)
{
    assert(tree);
    if( !tree->anchor_plan )
    {
        tree->anchor_plan = ToriMem_Calloc(ToriMem_Ui(), 1, sizeof(*tree->anchor_plan));
        assert(tree->anchor_plan);
        /* Epoch 0 is what a freshly grown array tail reads, so the live epochs
         * start above it. */
        tree->anchor_plan->memo_epoch = 1;
        tree->anchor_plan->build_epoch = 1;
        tree->anchor_plan->allocations = 1;
    }
    return tree->anchor_plan;
}

void
UITree_FrameAnchorPlanFree(struct UITree* tree)
{
    struct UITreeAnchorPlan* plan;
    assert(tree);
    plan = tree->anchor_plan;
    if( !plan )
        return;
    ToriMem_Free(ToriMem_Ui(), plan->unit_node);
    ToriMem_Free(ToriMem_Ui(), plan->unit_parent);
    ToriMem_Free(ToriMem_Ui(), plan->unit_child_head);
    ToriMem_Free(ToriMem_Ui(), plan->unit_child_tail);
    ToriMem_Free(ToriMem_Ui(), plan->unit_child_next);
    ToriMem_Free(ToriMem_Ui(), plan->unit_depth);
    ToriMem_Free(ToriMem_Ui(), plan->unit_order);
    ToriMem_Free(ToriMem_Ui(), plan->unit_relation);
    ToriMem_Free(ToriMem_Ui(), plan->unit_first);
    ToriMem_Free(ToriMem_Ui(), plan->unit_tree_first);
    ToriMem_Free(ToriMem_Ui(), plan->unit_record_head);
    ToriMem_Free(ToriMem_Ui(), plan->unit_record_tail);
    ToriMem_Free(ToriMem_Ui(), plan->unit_root_next);
    ToriMem_Free(ToriMem_Ui(), plan->unit_handled);
    ToriMem_Free(ToriMem_Ui(), plan->node_unit);
    ToriMem_Free(ToriMem_Ui(), plan->node_stamp);
    ToriMem_Free(ToriMem_Ui(), plan->unit_slot_at);
    ToriMem_Free(ToriMem_Ui(), plan->unit_stamp);
    ToriMem_Free(ToriMem_Ui(), plan->record_unit);
    ToriMem_Free(ToriMem_Ui(), plan->record_next);
    ToriMem_Free(ToriMem_Ui(), plan->root_head);
    ToriMem_Free(ToriMem_Ui(), plan->output);
    ToriMem_Free(ToriMem_Ui(), plan);
    tree->anchor_plan = NULL;
}

void
UITree_FrameReorderStats(struct UITree const* tree, unsigned long* out_allocations,
                         unsigned long* out_iterations)
{
    assert(tree);
    assert(out_allocations);
    assert(out_iterations);
    *out_allocations = tree->anchor_plan ? tree->anchor_plan->allocations : 0;
    *out_iterations = tree->anchor_plan ? tree->anchor_plan->iterations : 0;
}

/*
 * The amortised half: the unit list, the child buckets and the forest depths.
 *
 * It reads UITree::anchor_nodes -- the nodes that have ever carried an anchor
 * edit -- and never walks the tree, which is the whole point: the old pass
 * asked all 7,202 nodes "are you anchored?" on every one of the eight calls a
 * frame makes.
 */
static void
anchor_plan_build(struct UITreeAnchorPlan* plan, struct UITree* tree)
{
    uint32_t live = 0;

    assert(plan);
    assert(tree);
    plan->unit_count = 0;
    plan->unit_order_count = 0;
    plan->built = 1;
    plan->built_anchor_generation = tree->anchor_generation;
    plan->built_generation = tree->generation;
    /* Both epochs move: the slots are renumbered, so every memo entry and every
     * unit stamp from the last build is now a lie. */
    plan->build_epoch++;
    plan->memo_epoch++;
    anchor_plan_reserve_nodes(plan, tree->component_count);

    /* Compact the candidate list down to the nodes that still state a relation
     * to a target that still resolves, and make a unit of each survivor and of
     * its target. An entry that has gone NATIVE cannot come back without
     * UITree_WidgetSetAnchor remembering it again.
     *
     * Every candidate contributes at most itself and its target, so the two
     * slots per candidate reserved here cover the compacted list too. */
    anchor_plan_reserve_units(plan, tree->anchor_node_count * 2u);
    for( uint32_t i = 0; i < tree->anchor_node_count; i++ )
    {
        int32_t const node = tree->anchor_nodes[i];
        int32_t target;
        if( UITree_WidgetAnchorAt(tree, node, &target) == UITREE_WIDGET_RELATION_NATIVE )
            continue;
        assert(target >= 0);
        tree->anchor_nodes[live++] = node;
        if( plan->unit_stamp[node] != plan->build_epoch )
        {
            plan->unit_stamp[node] = plan->build_epoch;
            plan->unit_node[plan->unit_count++] = node;
        }
        if( plan->unit_stamp[target] != plan->build_epoch )
        {
            plan->unit_stamp[target] = plan->build_epoch;
            plan->unit_node[plan->unit_count++] = target;
        }
    }
    tree->anchor_node_count = live;
    if( !live )
        return;
    /* Ascending node order, because both orders the pass must reproduce -- the
     * root scan and a unit's child scan -- were node-index scans. Insertion
     * sort over the anchor count, which the depth gate keeps tiny. */
    for( int u = 1; u < plan->unit_count; u++ )
    {
        int32_t const node = plan->unit_node[u];
        int at = u;
        while( at > 0 && plan->unit_node[at - 1] > node )
        { plan->unit_node[at] = plan->unit_node[at - 1]; at--; }
        plan->unit_node[at] = node;
    }
    for( int u = 0; u < plan->unit_count; u++ )
    {
        plan->unit_slot_at[plan->unit_node[u]] = u;
        plan->unit_child_head[u] = -1;
        plan->unit_child_tail[u] = -1;
        plan->unit_child_next[u] = -1;
        plan->unit_depth[u] = -1;
    }
    for( int u = 0; u < plan->unit_count; u++ )
    {
        int32_t target;
        enum UITreeWidgetRelation const rel = UITree_WidgetAnchorAt(tree, plan->unit_node[u], &target);
        plan->unit_relation[u] = (unsigned char)rel;
        plan->unit_parent[u] = rel == UITREE_WIDGET_RELATION_NATIVE ? -1 : plan->unit_slot_at[target];
    }
    /* Children in node order: append at the tail, walking the units ascending. */
    for( int u = 0; u < plan->unit_count; u++ )
    {
        int32_t const parent = plan->unit_parent[u];
        if( parent < 0 )
            continue;
        if( plan->unit_child_tail[parent] < 0 )
            plan->unit_child_head[parent] = u;
        else
            plan->unit_child_next[plan->unit_child_tail[parent]] = u;
        plan->unit_child_tail[parent] = u;
    }
    /* Breadth-first from the roots, which is what gives the per-call pass an
     * order it can walk backwards to take children before parents. A unit the
     * walk never reaches is one in a relation cycle, and the old pass never
     * wrote those either: only a root starts an ordering. */
    for( int u = 0; u < plan->unit_count; u++ )
        if( plan->unit_parent[u] < 0 )
        {
            plan->unit_depth[u] = 0;
            plan->unit_order[plan->unit_order_count++] = u;
        }
    for( int k = 0; k < plan->unit_order_count; k++ )
    {
        int const u = plan->unit_order[k];
        for( int c = plan->unit_child_head[u]; c >= 0; c = plan->unit_child_next[c] )
        {
            plan->unit_depth[c] = plan->unit_depth[u] + 1;
            plan->unit_order[plan->unit_order_count++] = c;
        }
    }
}

/*
 * The unit a node's records belong to: the nearest unit at or above it, or -1.
 *
 * Memoised downward over the whole path walked, so the first record under a
 * deep container pays the walk and every later one pays a single load.
 */
static int32_t
anchor_plan_unit_of(struct UITreeAnchorPlan* plan, struct UITree const* tree, int32_t node,
                    unsigned long* iterations)
{
    int32_t walk = node;
    int32_t unit = -1;
    uint32_t guard = 0;

    assert(plan);
    assert(tree);
    assert(iterations);
    assert(node >= 0);
    while( walk >= 0 && guard++ <= tree->component_count )
    {
        if( plan->unit_stamp[walk] == plan->build_epoch ) { unit = plan->unit_slot_at[walk]; break; }
        if( plan->node_stamp[walk] == plan->memo_epoch ) { unit = plan->node_unit[walk]; break; }
        walk = tree->components[walk].parent;
        (*iterations)++;
    }
    guard = 0;
    /* tree-walk-exempt: ancestor chain, bounded by the count */
    for( int32_t p = node; p >= 0 && p != walk && guard++ <= tree->component_count;
         p = tree->components[p].parent )
    {
        plan->node_unit[p] = unit;
        plan->node_stamp[p] = plan->memo_epoch;
        (*iterations)++;
    }
    return unit;
}

/*
 * Write one unit's tree: BEHIND children, then its own records -- or a
 * presented REPLACE child's tree instead -- then OVER children.
 *
 * Each unit is entered once and the recursion is bounded by ANCHOR_DEPTH_MAX,
 * so this is O(units + children) across the whole call.
 */
static void
anchor_plan_write(struct UITreeAnchorPlan* plan, struct UITree const* tree,
                  struct UITreeHost const* host, int unit, int depth,
                  unsigned char const* input, size_t stride, int* written,
                  unsigned long* iterations)
{
    int32_t sorted[ANCHOR_CHILDREN_MAX];
    int child_count = 0;
    int replacement = -1;

    if( depth > ANCHOR_DEPTH_MAX || plan->unit_handled[unit] )
        return;
    plan->unit_handled[unit] = 1;
    /* Children in record order, the cap applied in NODE order first -- both
     * halves of the tie-break the node-index scan used to give for free. */
    for( int c = plan->unit_child_head[unit]; c >= 0 && child_count < ANCHOR_CHILDREN_MAX;
         c = plan->unit_child_next[c] )
    {
        int at = child_count++;
        while( at > 0 && plan->unit_first[sorted[at - 1]] > plan->unit_first[c] )
        { sorted[at] = sorted[at - 1]; at--; (*iterations)++; }
        sorted[at] = c;
        (*iterations)++;
    }
    for( int i = 0; i < child_count; i++ )
    {
        int const child = sorted[i];
        if( plan->unit_relation[child] == UITREE_WIDGET_RELATION_BEHIND )
            anchor_plan_write(plan, tree, host, child, depth + 1, input, stride, written, iterations);
        else if( plan->unit_relation[child] == UITREE_WIDGET_RELATION_REPLACE &&
                 UITree_NodeNativeVisible(tree, host, plan->unit_node[child], -1) &&
                 UITree_NodeNativeVisible(tree, host, plan->unit_node[unit], -1) )
            replacement = child;
        (*iterations)++;
    }
    if( replacement >= 0 )
        anchor_plan_write(plan, tree, host, replacement, depth + 1, input, stride, written, iterations);
    else
        for( int r = plan->unit_record_head[unit]; r >= 0; r = plan->record_next[r] )
        {
            memcpy(plan->output + (size_t)(*written)++ * stride, input + (size_t)r * stride, stride);
            (*iterations)++;
        }
    for( int i = 0; i < child_count; i++ )
    {
        int const child = sorted[i];
        if( plan->unit_relation[child] == UITREE_WIDGET_RELATION_OVER )
            anchor_plan_write(plan, tree, host, child, depth + 1, input, stride, written, iterations);
        else if( plan->unit_relation[child] == UITREE_WIDGET_RELATION_REPLACE )
            plan->unit_handled[child] = 1; /* not presented: inherits the target's veto */
        (*iterations)++;
    }
}

static int
frame_reorder_widget_anchors(struct UITree const* tree, struct UITreeHost const* host, void* records,
                             int count, size_t stride, size_t node_offset)
{
    /* The plan is scratch and not tree state -- see UITree::emit_visited for the
     * same argument about the same `const`. */
    struct UITree* mut = (struct UITree*)tree;
    struct UITreeAnchorPlan* plan;
    unsigned char const* const input = records;
    unsigned long iterations = 0;
    int units;
    int written = 0;
    uint64_t const pa_t0 = PerfAudit_Now();

    PA_INC(reorder_calls);
    if( count <= 0 || UITree_WidgetAnchorCount(tree) <= 0 )
        return count;
    PA_INC(reorder_bodies);
    PA_ADD(reorder_records, count);
    PA_ADD(reorder_n, tree->component_count);
    plan = anchor_plan_of(mut);
    if( !plan->built || plan->built_anchor_generation != tree->anchor_generation ||
        plan->built_generation != tree->generation )
        anchor_plan_build(plan, mut);
    units = plan->unit_count;
    plan->iterations = 0;
    if( units <= 0 )
        return count;
    /* Defensive: every node creation bumps `generation`, so the build above has
     * already sized the memo. One compare keeps that from being load-bearing. */
    anchor_plan_reserve_nodes(plan, tree->component_count);
    anchor_plan_reserve_records(plan, (uint32_t)count);
    anchor_plan_reserve_output(plan, (size_t)count * stride);

    for( int u = 0; u < units; u++ )
    {
        plan->unit_first[u] = count;
        plan->unit_record_head[u] = -1;
        plan->unit_record_tail[u] = -1;
        plan->unit_root_next[u] = -1;
        plan->unit_handled[u] = 0;
        iterations++;
    }
    for( int i = 0; i <= count; i++ )
    { plan->root_head[i] = -1; iterations++; }

    /* O(records): each record's unit, and each unit's records in record order. */
    for( int i = 0; i < count; i++ )
    {
        int32_t node_plus_one;
        int32_t unit;
        memcpy(&node_plus_one, input + (size_t)i * stride + node_offset, sizeof(node_plus_one));
        unit = node_plus_one > 0 && (uint32_t)(node_plus_one - 1) < tree->component_count
                   ? anchor_plan_unit_of(plan, tree, node_plus_one - 1, &iterations)
                   : -1;
        plan->record_unit[i] = unit;
        plan->record_next[i] = -1;
        if( unit >= 0 )
        {
            if( plan->unit_record_head[unit] < 0 )
            {
                plan->unit_record_head[unit] = i;
                plan->unit_first[unit] = i;
            }
            else
                plan->record_next[plan->unit_record_tail[unit]] = i;
            plan->unit_record_tail[unit] = i;
        }
        iterations++;
    }
    /* O(units + children): the earliest record of each unit's whole tree, taken
     * children-first over the build's breadth-first order. A unit past the depth
     * cut contributes only its own records, exactly as the recursion did when it
     * refused to descend. */
    for( int u = 0; u < units; u++ )
    { plan->unit_tree_first[u] = plan->unit_first[u]; iterations++; }
    for( int k = plan->unit_order_count - 1; k >= 0; k-- )
    {
        int const u = plan->unit_order[k];
        iterations++;
        if( plan->unit_depth[u] > ANCHOR_DEPTH_MAX )
            continue;
        for( int c = plan->unit_child_head[u]; c >= 0; c = plan->unit_child_next[c] )
        {
            if( plan->unit_tree_first[c] < plan->unit_tree_first[u] )
                plan->unit_tree_first[u] = plan->unit_tree_first[c];
            iterations++;
        }
    }
    /* Roots bucketed by where their tree starts, so the walk below asks "is
     * there a tree that starts here?" in O(1) instead of rescanning every node
     * at every record position. Walked descending, so a bucket comes out in
     * ascending node order -- the order the old scan found them in. */
    for( int u = units - 1; u >= 0; u-- )
    {
        if( plan->unit_parent[u] >= 0 )
            continue;
        plan->unit_root_next[u] = plan->root_head[plan->unit_tree_first[u]];
        plan->root_head[plan->unit_tree_first[u]] = u;
        iterations++;
    }
    /* O(records + units + children). Position `count` is the bucket for a tree
     * with no records anywhere: it is reached last and writes nothing. */
    for( int i = 0; i <= count; i++ )
    {
        for( int u = plan->root_head[i]; u >= 0; u = plan->unit_root_next[u] )
            anchor_plan_write(plan, tree, host, u, 0, input, stride, &written, &iterations);
        if( i == count )
            break;
        iterations++;
        if( plan->record_unit[i] >= 0 && plan->unit_handled[plan->record_unit[i]] )
            continue;
        memcpy(plan->output + (size_t)written++ * stride, input + (size_t)i * stride, stride);
    }
    memcpy(records, plan->output, (size_t)written * stride);
    plan->iterations = iterations;
    { PA_ADD(reorder_ns, PerfAudit_Now() - pa_t0); return written; }
}

int
UITree_FrameReorder(struct UITree const* tree, struct UITreeHost const* host, void* records, int count,
                   size_t stride, size_t node_offset)
{
    assert(tree);
    if( count <= 0 ) return count;
    assert(records);
    assert(stride >= sizeof(int32_t));
    assert(node_offset <= stride - sizeof(int32_t));
    assert((size_t)count <= SIZE_MAX / stride);
    return frame_reorder_widget_anchors(tree, host, records, count, stride, node_offset);
}

void
UITree_FrameForget(struct UITree* tree)
{
    assert(tree);
    /* The slot-candidate list names this tree's nodes; it is the tree's
     * memory as much as the frame table is, so it goes with it. */
    if( frame_slot_candidates.tree == tree )
    {
        ToriMem_Free(ToriMem_Ui(), frame_slot_candidates.ids);
        memset(&frame_slot_candidates, 0, sizeof(frame_slot_candidates));
    }
}

void
UITree_FrameSetBinder(
    struct UITree* tree,
    void (*binder)(struct UITree* tree, void* user),
    void* user)
{
    assert(tree);
    tree->frame_binder = binder;
    tree->frame_binder_user = user;
}

void
UITree_FrameBind(struct UITree* tree)
{
    assert(tree);
    PA_INC(frame_bind_calls);
    uint64_t const pa_t0 = PerfAudit_Now();
    if( tree->frame_binder )
        tree->frame_binder(tree, tree->frame_binder_user);
    PA_ADD(frame_bind_ns, PerfAudit_Now() - pa_t0);
}
