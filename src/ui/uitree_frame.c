#include "uitree_frame.h"

#include "plugin/torirs_plugin_types.h"
#include "uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log/torirs_log.h"

/*
 * The tree's slot enum and the plugin contract's are one enum written twice.
 * The tree may not include the contract at runtime -- a headless uitree test
 * links neither the host nor a plugin -- so they are kept true here instead,
 * where a divergence stops the build rather than silently placing the chatbox
 * where the minimap was asked for.
 */
_Static_assert(
    (int)UITREE_FRAME_SLOT_VIEWPORT == (int)TORIRS_PLUGIN_SLOT_VIEWPORT,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_MINIMAP == (int)TORIRS_PLUGIN_SLOT_MINIMAP,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_COMPASS == (int)TORIRS_PLUGIN_SLOT_COMPASS,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_CHAT == (int)TORIRS_PLUGIN_SLOT_CHAT,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_SIDEBAR == (int)TORIRS_PLUGIN_SLOT_SIDEBAR,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_MAIN_MODAL == (int)TORIRS_PLUGIN_SLOT_MAIN_MODAL,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_CHAT_BUTTONS == (int)TORIRS_PLUGIN_SLOT_CHAT_BUTTONS,
    "frame slot order must match the plugin contract");
_Static_assert(
    (int)UITREE_FRAME_SLOT_ORBS == (int)TORIRS_PLUGIN_SLOT_ORBS,
    "frame slot order must match the plugin contract");
/* The PLACEABLE half, and only that half: the plugin contract grew derived
 * regions -- CANVAS and SAFE -- which are read through the same enum and are
 * not nodes in this tree, so its own count is the placeable one. */
_Static_assert(
    (int)UITREE_FRAME_SLOT_COUNT == (int)TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT,
    "frame slot count must match the plugin contract's placeable half");

/* Chrome nodes one declaration may suppress. A 2004 surround is about twenty
 * and an OldSchool toplevel's own decoration is under a hundred; past this the
 * rest stay visible, which is a visibly wrong frame rather than a silent one.
 * @see frame_collect_chrome. */
#define UITREE_FRAME_HIDDEN_MAX 256

/*
 * Layers one declaration may have to widen.
 *
 * The chain above a live surface, not the tree: a dat1 frame has exactly one
 * (`fixed_shell`), a cache gameframe has the toplevel and whatever group root
 * the mount sits in. 32 is far past either and is checked rather than assumed.
 */
#define UITREE_FRAME_STRETCHED_MAX 32

/*
 * A role is not a node, and the sidebar is why. The 2004 frame carries
 * FOURTEEN sidebar mounts -- one per tab, all at the same rectangle, only one
 * of them showing -- so a layout that moved "the sidebar" and meant the first
 * would move the combat tab and leave the other thirteen where the old frame
 * had them: the inventory would still be drawn at 553,205 while the panel it
 * sits in had moved. UITREE_FRAME_SLOT_NODES_MAX covers that frame's fourteen
 * with room, and it is checked rather than assumed.
 */

struct UITreeFrameLayout
{
    /** Nodes carrying each role and their exact array-slot incarnations. */
    int32_t slot_node[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    uint32_t slot_incarnation[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    /** UITree_FrameSlotIndex for each, so a per-member box finds its node
     *  without re-deriving it every frame. */
    int slot_member[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    int slot_node_count[UITREE_FRAME_SLOT_COUNT];
    /** The declaration itself, kept so a topology change can re-resolve it. */
    struct UITreeFrameSlotRect slot_rect[UITREE_FRAME_SLOT_COUNT];
    /** Lane chrome (and unplaced slots) the declaration suppresses. */
    int32_t hidden[UITREE_FRAME_HIDDEN_MAX];
    uint32_t hidden_incarnation[UITREE_FRAME_HIDDEN_MAX];
    int hidden_count;
    /** Layers effectively widened so they do not clip placed surfaces. */
    int32_t stretched[UITREE_FRAME_STRETCHED_MAX];
    uint32_t stretched_incarnation[UITREE_FRAME_STRETCHED_MAX];
    int stretched_count;
    /** The semantic binding this table describes. */
    uint32_t applied_generation;
    int root_group;
    uint8_t active;
};

static struct UITreeFrameLayout*
frame_state(struct UITree* tree)
{
    assert(tree);
    /* Allocated on the first committed plugin-frame declaration and not
     * before: a client using its lane-native frame pays a
     * NULL pointer and no bytes. */
    if( !tree->frame_layout )
    {
        tree->frame_layout = calloc(1, sizeof(*tree->frame_layout));
        assert(tree->frame_layout);
    }
    return tree->frame_layout;
}

static int
frame_node_same(
    struct UITree const* tree,
    int32_t idx,
    uint32_t incarnation)
{
    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return 0;
    return !tree->components[idx].freed && incarnation != 0 &&
           tree->components[idx].incarnation == incarnation;
}

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
static int
frame_node_is_slot(
    struct UITreeComponent const* c,
    int slot)
{
    assert(c);
    switch( slot )
    {
    case UITREE_FRAME_SLOT_VIEWPORT:
        return c->type == UIELEM_BUILTIN_WORLD;
    case UITREE_FRAME_SLOT_MINIMAP:
        return c->type == UIELEM_BUILTIN_MINIMAP;
    case UITREE_FRAME_SLOT_COMPASS:
        return c->type == UIELEM_BUILTIN_COMPASS;
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

int
UITree_FrameSlotNativeSize(
    struct UITree const* tree,
    int slot,
    int* out_w,
    int* out_h)
{
    struct UITreeComponent const* c;
    int32_t node;

    assert(tree);
    node = UITree_FrameSlotNode(tree, slot);
    if( node < 0 )
        return 0;
    c = &tree->components[node];
    /* A relative node's box is its parent's arithmetic and has no size of its
     * own to report; a moded one's `width` is a percentage or a delta, and
     * handing that number back as pixels is worse than answering nothing. */
    if( c->position.kind != UIPOS_XY )
        return 0;
    if( c->position.width_mode > 0 || c->position.height_mode > 0 )
        return 0;
    if( c->position.width <= 0 || c->position.height <= 0 )
        return 0;
    if( out_w )
        *out_w = c->position.width;
    if( out_h )
        *out_h = c->position.height;
    return 1;
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
    default:
        /* One surface, nothing to number. */
        return -1;
    }
}

/*
 * The slot -> node answers, remembered per tree.
 *
 * Both lookups below are linear scans of every component, and a frame asks
 * them several times (the plugin bridge's placement and "is this slot
 * live" queries, the role placements): 0.18 ms a frame on the Moto X.
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
 */
static struct
{
    struct UITree const* tree;
    uint32_t generation;
    uint32_t count;
    int32_t node[UITREE_FRAME_SLOT_COUNT][1 + UITREE_FRAME_SLOT_NODES_MAX];
} frame_slot_cache;

static int32_t*
frame_slot_cache_entry(
    struct UITree const* tree,
    int slot,
    int member)
{
    if( frame_slot_cache.tree != tree || frame_slot_cache.generation != tree->generation ||
        frame_slot_cache.count != tree->component_count )
    {
        frame_slot_cache.tree = tree;
        frame_slot_cache.generation = tree->generation;
        frame_slot_cache.count = tree->component_count;
        memset(frame_slot_cache.node, 0xFE, sizeof(frame_slot_cache.node)); /* -2 */
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
        if( !c->freed && frame_node_is_slot(c, slot) )
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
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;
        if( frame_node_is_slot(c, slot) )
            return *cached = (int32_t)i;
    }
    return -1;
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

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;
        if( !frame_node_is_slot(c, slot) )
            continue;
        if( UITree_FrameSlotIndex(c, slot) == member )
            return *cached = (int32_t)i;
    }
    return -1;
}

/*
 * Is this node part of the LANE's own frame -- the art a plugin layout
 * replaces?
 *
 * Two rules, one per kind of gameframe, and both are about decoration rather
 * than about content:
 *
 *   A revconfig frame draws its surround with builtins that exist for no other
 *   purpose: a sprite, a tab stone, a tab icon, a chat button. Every one of
 *   them is chrome by construction.
 *
 *   A cache gameframe draws its surround with ordinary widgets, so the type is
 *   no help. What separates them is WHOSE they are: a graphic whose component
 *   id belongs to the toplevel group is the toplevel's own decoration, and a
 *   graphic belonging to any other group is inside an interface pack mounted
 *   in it -- the inventory, the chatbox, a bank. Hiding by group keeps the
 *   packs, which are content, and takes the surround, which is not.
 *
 * The slot nodes are excluded by the caller rather than here: a viewport is
 * not chrome under either rule, but a chat REGION on a dat1 frame is a tagged
 * layer that would otherwise pass the first rule's sprite test.
 */
static int
frame_is_lane_chrome(
    struct UITreeComponent const* c,
    int root_group)
{
    assert(c);
    switch( c->type )
    {
    case UIELEM_BUILTIN_SPRITE:
    case UIELEM_BUILTIN_REDSTONE_TAB:
    case UIELEM_BUILTIN_TAB_ICONS:
        /*
         * Decoration by construction: a sprite, a tab stone, a tab icon. Every
         * one of them exists only to be looked at, and a layout that draws its
         * own is drawing over them.
         *
         * The chat filter buttons are deliberately NOT in this list. They wear
         * the same stone and sit in the same strip, so they look like chrome --
         * and they are four working CONTROLS. Suppressing them cost the player
         * the public/private/trade toggles and left their empty plates behind,
         * which is why they are a role (UITREE_FRAME_SLOT_CHAT_BUTTONS) that a
         * layout places rather than art it replaces.
         */
        return 1;
    case UIELEM_RS_GRAPHIC:
    case UIELEM_RS_RECT:
    case UIELEM_RS_LINE:
        break;
    case UIELEM_RS_LAYER:
        /*
         * A layer of the toplevel's own is chrome only when it is a CONTROL
         * or a click-blocker: the stone a tab icon sits on, the mobile
         * frame's "Start chatting" plate, the `map_noclick` sheet over the
         * map ring. Those carry an op or `noclickthrough`, and left standing
         * they take clicks over a frame that no longer draws them -- an
         * invisible chat switch in the corner, a dead band over the world
         * where the old map was.
         *
         * Every other root-group layer is a CONTAINER -- the chat mount, the
         * side panels' shell, the HUD layers the login burst mounts packs
         * into -- and is left alone; hiding one would hide the content in it.
         * The caller still excludes a slot node and everything above it, so a
         * control that happens to be an ancestor of a placed surface is kept.
         */
        if( c->no_click_through )
            break;
        if( c->menu_options )
        {
            int any = 0;
            for( int i = 0; i < UITREE_MENU_OPTION_SLOTS && !any; i++ )
                any = c->menu_options->ops[i][0] != '\0';
            if( any )
                break;
        }
        return 0;
    default:
        return 0;
    }
    if( root_group < 0 || c->component_id < 0 )
        return 0;
    return ((c->component_id >> 16) & 0xffff) == root_group;
}

/*
 * Every node carrying every role, in one walk.
 *
 * One walk and not six, because the tree is walked by index and a role test is
 * a switch: six passes would read the same thousands of components six times
 * to answer questions that are all decided from the same two fields.
 *
 * A role that overflows its table keeps the first UITREE_FRAME_SLOT_NODES_MAX
 * and says so, because the alternative is a frame with an unexplained piece of
 * the old one still in it.
 */
static void
frame_collect_slots(
    struct UITree* tree,
    struct UITreeFrameLayout* fl)
{
    assert(tree);
    assert(fl);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];

        if( c->freed )
            continue;
        for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
        {
            int n;
            if( !frame_node_is_slot(c, s) )
                continue;
            n = fl->slot_node_count[s];
            if( n >= UITREE_FRAME_SLOT_NODES_MAX )
            {
                TORIRS_LOG("frame: role %d has more than %d nodes; the rest keep the "
                    "lane's own geometry\n",
                    s,
                    UITREE_FRAME_SLOT_NODES_MAX);
                break;
            }
            fl->slot_node[s][n] = (int32_t)i;
            fl->slot_incarnation[s][n] = c->incarnation;
            fl->slot_member[s][n] = UITree_FrameSlotIndex(c, s);
            fl->slot_node_count[s] = n + 1;
        }
    }
}

/*
 * The box that applies to one member of a role, or NULL for "not placed".
 *
 * Per-member first and the whole-role box second, which is the precedence a
 * declaration reads with: a layout that placed the four chat buttons
 * individually and then said something about "the chat buttons" as a group
 * meant the individual boxes, or it would not have bothered writing them.
 */
static struct UITreeFrameRect const*
frame_rect_for(
    struct UITreeFrameSlotRect const* slot,
    int member)
{
    assert(slot);
    if( member >= 0 && member < UITREE_FRAME_SLOT_NODES_MAX && slot->at[member].placed )
        return &slot->at[member];
    return slot->all.placed ? &slot->all : NULL;
}

/* Record one suppression in a declaration being built. Applying the flag is a
 * separate diff step: rebuilding an unchanged semantic binding must not show
 * and hide the same native node merely because unrelated CS2 topology moved. */
static void
frame_record_hidden(
    struct UITree const* tree,
    struct UITreeFrameLayout* fl,
    int32_t idx)
{
    assert(tree);
    assert(fl);
    if( !frame_node_alive(tree, idx) || fl->hidden_count >= UITREE_FRAME_HIDDEN_MAX )
        return;
    for( int i = 0; i < fl->hidden_count; i++ )
        if( fl->hidden[i] == idx &&
            fl->hidden_incarnation[i] == tree->components[idx].incarnation )
            return;
    fl->hidden[fl->hidden_count] = idx;
    fl->hidden_incarnation[fl->hidden_count] = tree->components[idx].incarnation;
    fl->hidden_count++;
}

/* Record an effective containment override. Native geometry is deliberately
 * untouched; UITree_FramePositionOverride derives the widened position from
 * whatever CS1/CS2 most recently wrote. */
static void
frame_stretch_node(
    struct UITree* tree,
    struct UITreeFrameLayout* fl,
    int32_t idx)
{
    struct UITreeComponent const* c;

    assert(tree);
    assert(fl);
    c = &tree->components[idx];
    for( int i = 0; i < fl->stretched_count; i++ )
        if( fl->stretched[i] == idx && fl->stretched_incarnation[i] == c->incarnation )
            return;
    if( fl->stretched_count >= UITREE_FRAME_STRETCHED_MAX )
        return;
    fl->stretched[fl->stretched_count] = idx;
    fl->stretched_incarnation[fl->stretched_count] = c->incarnation;
    fl->stretched_count++;
}

/*
 * Widen every layer a placed surface hangs under.
 *
 * The lane's shell is authored for the canvas the lane's own frame was drawn
 * at -- on a 2004 dat1 frame that is one `rs_layer` at 765x503 with the world,
 * the minimap, the compass and the chat buttons inside it -- and a layer
 * CLIPS. So a resizable declaration that put the minimap at 1049,8 of a
 * 1200x800 window placed it correctly and then had it clipped away entirely,
 * and the scene, which was placed at the full window, was drawn as a 765x503
 * rectangle in the corner with its texture basis off the centre it projected
 * about. The frame looked like a layout bug and was a containment one.
 *
 * Every ancestor and not just the root: a cache gameframe mounts its surfaces
 * inside interface group roots, and any one of them clips just as hard.
 *
 * Sized to the CANVAS rather than to the declaration's own union, because the
 * boxes a declaration states are canvas coordinates, so "does not clip the
 * canvas" is the only condition that makes every one of them land where it
 * was asked for.
 */

static int
frame_is_placed_node(
    struct UITreeFrameLayout const* fl,
    int32_t idx)
{
    assert(fl);
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
            if( fl->slot_node[s][n] == idx &&
                frame_rect_for(&fl->slot_rect[s], fl->slot_member[s][n]) )
                return 1;
    return 0;
}

static void
frame_stretch_ancestors(
    struct UITree* tree,
    struct UITreeFrameLayout* fl)
{
    assert(tree);
    assert(fl);
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
        {
            int32_t const idx = fl->slot_node[s][n];
            if( !frame_node_alive(tree, idx) ||
                !frame_rect_for(&fl->slot_rect[s], fl->slot_member[s][n]) )
                continue;
            for( int32_t p = tree->components[idx].parent; p >= 0;
                 p = tree->components[p].parent )
            {
                if( !frame_node_alive(tree, p) )
                    break;
                /* A layer that is ITSELF a placed surface keeps the box the
                 * declaration gave it -- the chat region is a role and a
                 * container at once, and widening it to the canvas would put
                 * the chat log across the whole window rather than at the
                 * declaration's effective box. */
                if( frame_is_placed_node(fl, p) )
                    continue;
                frame_stretch_node(tree, fl, p);
            }
        }
    }
}

static void
frame_collect_chrome(
    struct UITree* tree,
    struct UITreeFrameLayout* fl,
    int root_group)
{
    uint8_t* keep;

    assert(tree);
    assert(fl);

    /*
     * Every slot node and every ancestor of one is off limits, marked once.
     *
     * The ancestors matter since layers became hideable (frame_is_lane_chrome):
     * a cache toplevel's chat container may itself carry an op, and hiding
     * it would hide the chat log the declaration just placed inside it.
     */
    keep = calloc(tree->component_count ? tree->component_count : 1, sizeof(*keep));
    assert(keep);
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
        {
            for( int32_t p = fl->slot_node[s][n]; p >= 0 && !keep[p];
                 p = tree->components[p].parent )
            {
                if( !frame_node_alive(tree, p) )
                    break;
                keep[p] = 1;
            }
        }
    }

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];

        if( c->freed || keep[i] )
            continue;
        if( !frame_is_lane_chrome(c, root_group) )
            continue;
        frame_record_hidden(tree, fl, (int32_t)i);
    }
    free(keep);
}

static int
frame_hidden_has(
    struct UITreeFrameLayout const* fl,
    int32_t idx,
    uint32_t incarnation)
{
    assert(fl);
    for( int i = 0; i < fl->hidden_count; i++ )
        if( fl->hidden[i] == idx && fl->hidden_incarnation[i] == incarnation )
            return 1;
    return 0;
}

static void
frame_mark_bound_nodes(
    struct UITree* tree,
    struct UITreeFrameLayout const* fl)
{
    assert(tree);
    assert(fl);
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
            if( frame_node_same(
                    tree, fl->slot_node[s][n], fl->slot_incarnation[s][n]) )
                UITree_MarkNodeDirty(tree, fl->slot_node[s][n]);
    for( int i = 0; i < fl->stretched_count; i++ )
        if( frame_node_same(tree, fl->stretched[i], fl->stretched_incarnation[i]) )
            UITree_MarkNodeDirty(tree, fl->stretched[i]);
}

void
UITree_FrameApply(
    struct UITree* tree,
    struct UITreeFrameSlotRect const* slots,
    int root_group)
{
    struct UITreeFrameLayout* fl;
    struct UITreeFrameLayout next;

    assert(tree);
    assert(slots);

    /* Build the new binding off to the side. The old declaration stays fully
     * effective until the diff below commits this one, so a re-declaration can
     * never expose the lane's frame as an intermediate state. */
    /* Name the roles a cache gameframe leaves unnamed BEFORE looking for
     * them, or a declaration made against a freshly rebuilt toplevel finds
     * no side panels and hides the sidebar for a frame. */
    UITree_FrameBind(tree);

    memset(&next, 0, sizeof(next));
    memcpy(next.slot_rect, slots, sizeof(next.slot_rect));
    next.root_group = root_group;
    next.applied_generation = tree->generation;
    next.active = 1;
    frame_collect_slots(tree, &next);

    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        for( int n = 0; n < next.slot_node_count[s]; n++ )
        {
            int32_t const idx = next.slot_node[s][n];
            struct UITreeFrameRect const* rect =
                frame_rect_for(&next.slot_rect[s], next.slot_member[s][n]);

            if( !rect )
            {
                /* A role -- or one member of it -- the declaration did not
                 * mention is one this frame does not show. A modern resizable
                 * layout has no compass housing of its own, and leaving the
                 * lane's compass on screen would put it wherever the old frame
                 * had it. */
                frame_record_hidden(tree, &next, idx);
            }
        }
    }

    frame_stretch_ancestors(tree, &next);
    frame_collect_chrome(tree, &next, root_group);

    fl = frame_state(tree);
    if( fl->active )
    {
        /* `generation` is intentionally excluded from semantic equality. A
         * chat-row rebuild can bump it hundreds of times without changing one
         * frame role; accepting the new generation must then be a true no-op. */
        uint32_t const next_generation = next.applied_generation;
        next.applied_generation = fl->applied_generation;
        if( memcmp(fl, &next, sizeof(next)) == 0 )
        {
            fl->applied_generation = next_generation;
            return;
        }
        next.applied_generation = next_generation;
    }

    /* Suppression is the only override represented on the component itself.
     * Diff it by exact incarnation so unchanged chrome never flashes visible,
     * and a recycled index is never touched on behalf of its predecessor. */
    for( int i = 0; i < fl->hidden_count; i++ )
    {
        int32_t const idx = fl->hidden[i];
        uint32_t const incarnation = fl->hidden_incarnation[i];
        if( !frame_node_same(tree, idx, incarnation) ||
            frame_hidden_has(&next, idx, incarnation) )
            continue;
        if( tree->components[idx].frame_hidden )
            (void)UITree_SetFrameHiddenAt(tree, idx, 0);
    }
    for( int i = 0; i < next.hidden_count; i++ )
    {
        int32_t const idx = next.hidden[i];
        uint32_t const incarnation = next.hidden_incarnation[i];
        if( !frame_node_same(tree, idx, incarnation) ||
            frame_hidden_has(fl, idx, incarnation) )
            continue;
        if( !tree->components[idx].frame_hidden )
            (void)UITree_SetFrameHiddenAt(tree, idx, 1);
    }

    /* Geometry and skin are sparse effective layers. Mark both the outgoing
     * and incoming bindings once, then atomically replace the table. */
    frame_mark_bound_nodes(tree, fl);
    frame_mark_bound_nodes(tree, &next);
    *fl = next;
    UITree_LayoutInvalidate(tree);
}

void
UITree_FrameReassert(struct UITree* tree)
{
    struct UITreeFrameLayout* fl;

    assert(tree);
    fl = tree->frame_layout;
    if( !fl || !fl->active )
        return;

    if( fl->applied_generation != tree->generation )
    {
        struct UITreeFrameSlotRect slots[UITREE_FRAME_SLOT_COUNT];
        int const root_group = fl->root_group;
        memcpy(slots, fl->slot_rect, sizeof(slots));
        UITree_FrameApply(tree, slots, root_group);
    }
}

void
UITree_FrameRelease(struct UITree* tree)
{
    struct UITreeFrameLayout* fl;

    assert(tree);
    fl = tree->frame_layout;
    if( !fl || !fl->active )
        return;

    for( int i = 0; i < fl->hidden_count; i++ )
    {
        int32_t const idx = fl->hidden[i];
        if( !frame_node_same(tree, idx, fl->hidden_incarnation[i]) )
            continue;
        if( tree->components[idx].frame_hidden )
            (void)UITree_SetFrameHiddenAt(tree, idx, 0);
    }

    frame_mark_bound_nodes(tree, fl);
    memset(fl, 0, sizeof(*fl));
    UITree_LayoutInvalidate(tree);
}

int
UITree_FramePositionOverride(
    struct UITree const* tree,
    int32_t node,
    struct UITreeElemPosition* out)
{
    struct UITreeFrameLayout const* fl;

    assert(tree);
    assert(out);
    fl = tree->frame_layout;
    if( !fl || !fl->active )
        return 0;

    /* A placed semantic surface owns its whole effective box. The native
     * position remains on the component and is copied only as a starting point
     * so fields outside x/y/w/h keep their ordinary defaults. */
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
        {
            struct UITreeFrameRect const* rect;
            if( fl->slot_node[s][n] != node ||
                !frame_node_same(tree, node, fl->slot_incarnation[s][n]) )
                continue;
            rect = frame_rect_for(&fl->slot_rect[s], fl->slot_member[s][n]);
            if( !rect )
                return 0;
            *out = tree->components[node].position;
            out->layout_resolved = 0;
            out->kind = UIPOS_XY;
            out->x = rect->x;
            out->y = rect->y;
            out->width = rect->w;
            out->height = rect->h;
            out->x_mode = -1;
            out->y_mode = -1;
            out->width_mode = -1;
            out->height_mode = -1;
            out->relative_flags = 0;
            return 1;
        }
    }

    /* Ancestors retain native position and only gain enough effective extent
     * to contain the canvas. A script changing their native state therefore
     * remains meaningful and appears immediately on release. */
    for( int i = 0; i < fl->stretched_count; i++ )
    {
        if( fl->stretched[i] != node ||
            !frame_node_same(tree, node, fl->stretched_incarnation[i]) )
            continue;
        *out = tree->components[node].position;
        out->layout_resolved = 0;
        if( out->width < UITREE_LAYOUT_ROOT_W )
            out->width = UITREE_LAYOUT_ROOT_W;
        if( out->height < UITREE_LAYOUT_ROOT_H )
            out->height = UITREE_LAYOUT_ROOT_H;
        out->width_mode = -1;
        out->height_mode = -1;
        return 1;
    }
    return 0;
}

int
UITree_FramePositionOwned(
    struct UITree const* tree,
    int32_t node)
{
    struct UITreeFrameLayout const* fl;

    assert(tree);
    fl = tree->frame_layout;
    if( !fl || !fl->active )
        return 0;
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
            if( fl->slot_node[s][n] == node &&
                frame_node_same(tree, node, fl->slot_incarnation[s][n]) &&
                frame_rect_for(&fl->slot_rect[s], fl->slot_member[s][n]) )
                return 1;
    return 0;
}

int
UITree_FrameSkinOverride(
    struct UITree const* tree,
    int32_t node,
    int* out_art_scene_id,
    int* out_mask_scene_id)
{
    struct UITreeFrameLayout const* fl;

    assert(tree);
    fl = tree->frame_layout;
    if( out_art_scene_id )
        *out_art_scene_id = 0;
    if( out_mask_scene_id )
        *out_mask_scene_id = 0;
    if( !fl || !fl->active )
        return 0;
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        if( !fl->slot_rect[s].skin.placed )
            continue;
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
        {
            if( fl->slot_node[s][n] != node ||
                !frame_node_same(tree, node, fl->slot_incarnation[s][n]) )
                continue;
            if( out_art_scene_id )
                *out_art_scene_id = fl->slot_rect[s].skin.art_scene_id;
            if( out_mask_scene_id )
                *out_mask_scene_id = fl->slot_rect[s].skin.mask_scene_id;
            return 1;
        }
    }
    return 0;
}

int
UITree_FrameOverlayOverride(
    struct UITree const* tree,
    int32_t node,
    struct UITreeFrameOverlay* out)
{
    struct UITreeFrameLayout const* fl;

    assert(tree);
    assert(out);
    memset(out, 0, sizeof(*out));
    fl = tree->frame_layout;
    if( !fl || !fl->active )
        return 0;

    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        if( !fl->slot_rect[s].overlay.placed || fl->slot_node_count[s] <= 0 )
            continue;
        /* One overlay names the role, not every member carrying it. The first
         * binding is the stable primary used by UITree_FrameSlotNode too. */
        if( fl->slot_node[s][0] != node ||
            !frame_node_same(tree, node, fl->slot_incarnation[s][0]) ||
            !frame_rect_for(&fl->slot_rect[s], fl->slot_member[s][0]) )
            continue;
        *out = fl->slot_rect[s].overlay;
        return 1;
    }
    return 0;
}

int
UITree_FrameActive(struct UITree const* tree)
{
    assert(tree);
    return tree->frame_layout && tree->frame_layout->active ? 1 : 0;
}

int
UITree_FrameHiddenCount(struct UITree const* tree)
{
    assert(tree);
    return tree->frame_layout ? tree->frame_layout->hidden_count : 0;
}

int
UITree_FrameSlotCount(struct UITree const* tree, int slot)
{
    assert(tree);
    if( !tree->frame_layout || slot < 0 || slot >= UITREE_FRAME_SLOT_COUNT )
        return 0;
    return tree->frame_layout->slot_node_count[slot];
}

void
UITree_FrameForget(struct UITree* tree)
{
    assert(tree);
    if( !tree->frame_layout )
        return;
    free(tree->frame_layout);
    tree->frame_layout = NULL;
}

int
UITree_FrameSlotsStale(struct UITree* tree)
{
    struct UITreeFrameLayout const* fl;
    struct UITreeFrameLayout* next;
    int stale;

    assert(tree);
    fl = tree->frame_layout;
    if( !fl || !fl->active )
        return 0;

    UITree_FrameBind(tree);
    /* Heap rather than stack: the table carries the hidden and stretched
     * lists too, and only the slot half is compared. */
    next = calloc(1, sizeof(*next));
    assert(next);
    frame_collect_slots(tree, next);
    stale = memcmp(next->slot_node, fl->slot_node, sizeof(next->slot_node)) != 0 ||
            memcmp(next->slot_incarnation, fl->slot_incarnation, sizeof(next->slot_incarnation)) != 0 ||
            memcmp(next->slot_member, fl->slot_member, sizeof(next->slot_member)) != 0 ||
            memcmp(next->slot_node_count, fl->slot_node_count, sizeof(next->slot_node_count)) != 0;
    free(next);
    return stale;
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
    if( tree->frame_binder )
        tree->frame_binder(tree, tree->frame_binder_user);
}
