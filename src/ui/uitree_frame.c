#include "uitree_frame.h"

#include "plugin/torirs_plugin_host_types.h"
#include "uitree_layout.h"
#include "uitree_host.h"

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

/* Chrome nodes one provision may suppress. A 2004 surround is about twenty
 * and an OldSchool toplevel's own decoration is under a hundred; past this the
 * rest stay visible, which is a visibly wrong frame rather than a silent one.
 * @see frame_collect_chrome. */
#define UITREE_FRAME_HIDDEN_MAX 256

/*
 * Layers one provision may have to release from clipping.
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
    uint64_t slot_incarnation[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    /** UITree_FrameSlotIndex for each, so a per-member box finds its node
     *  without re-deriving it every frame. */
    int slot_member[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    int slot_node_count[UITREE_FRAME_SLOT_COUNT];
    /** Lane chrome the provision suppresses. */
    int32_t hidden[UITREE_FRAME_HIDDEN_MAX];
    uint64_t hidden_incarnation[UITREE_FRAME_HIDDEN_MAX];
    int hidden_count;
    /** Layers released from clipping so they do not clip moved surfaces. */
    int32_t stretched[UITREE_FRAME_STRETCHED_MAX];
    uint64_t stretched_incarnation[UITREE_FRAME_STRETCHED_MAX];
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
    /* Allocated on the first committed plugin-frame provision and not
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
    uint64_t incarnation)
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

/*
 * Is this node a member button INSIDE the orb block, as opposed to the block?
 *
 * The binder tags both with the slot, and the block is what "the orbs" means
 * to every whole-role query (its native size, its box, is it there): a member
 * answers only UITree_FrameSlotMemberNode. Only the orb pack has members of
 * this kind -- a sidebar mount or a chat button IS the role at its number.
 */
static int
frame_node_is_own_member(
    struct UITreeComponent const* c,
    int slot)
{
    assert(c);
    return slot == UITREE_FRAME_SLOT_ORBS && UITree_FrameSlotIndex(c, slot) >= 0;
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
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;
        if( frame_node_is_slot(c, slot) && !frame_node_is_own_member(c, slot) )
            return *cached = (int32_t)i;
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
    for( uint32_t i=0; i<tree->component_count; ++i )
    {
        struct UITreeComponent const* c=&tree->components[i];
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
 *
 * `parent_is_frame_owned` is what makes the second rule true of a node a
 * CLIENTSCRIPT created. A cc_create child is given the component id of the
 * container it was created in (UITree_AllocateDynamicComponentId takes the
 * parent's iface), so it always answers "toplevel group" -- the group test
 * stops asking "did the toplevel's .if author this" and starts asking "was
 * this drawn inside one of the toplevel's containers", which is a different
 * question with a different answer. The caller supplies the missing half:
 * whether the container it was drawn into is one the provision TOOK OVER
 * (a bound surface or an ancestor of one). Drawn there, a script-made widget
 * is the surround of a region the layout now draws itself -- the CS2
 * nine-slice the toplevel script paints around `side_container` for the panel
 * the frame has just moved. Drawn anywhere else it is content the script is showing
 * the player: the mouse-over tooltip's box and plate under `mouseover`, a
 * marker on the minimap. Hiding those is hiding content, and it left the
 * rev-239 cursor tooltip as bare unreadable text over the world -- its three
 * TEXT children are not of a hideable type, so only its backing vanished.
 */
static int
frame_is_lane_chrome(
    struct UITreeComponent const* c,
    int root_group,
    int parent_is_frame_owned)
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
    if( c->dynamic && !parent_is_frame_owned )
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
 * the old one still in it. The binding is what the fence's reassert, the
 * staleness question and the chrome collection (which keeps a bound node and
 * everything above it) read.
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

/* Record one suppression in a provision being built. Applying the flag is a
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

/* Record a containment release. Native geometry is deliberately untouched:
 * the layer keeps the box CS1/CS2 most recently wrote and only stops clipping
 * (UITreeComponent.frame_stretched). */
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
 * Release every layer a MOVED surface hangs under from its own containment.
 *
 * The lane's shell is authored for the canvas the lane's own frame was drawn
 * at -- on a 2004 dat1 frame that is one `rs_layer` at 765x503 with the world,
 * the minimap, the compass and the chat buttons inside it -- and a layer
 * CLIPS. So a resizable frame that put the minimap at 1049,8 of a 1200x800
 * window placed it correctly and then had it clipped away entirely, and the
 * scene, which was placed at the full window, was drawn as a 765x503
 * rectangle in the corner with its texture basis off the centre it projected
 * about. The frame looked like a layout bug and was a containment one.
 *
 * Every ancestor and not just the root: a cache gameframe mounts its surfaces
 * inside interface group roots, and any one of them clips just as hard.
 *
 * RELEASED FROM CLIPPING, and nothing else (UITreeComponent.frame_stretched).
 * The release was once a WIDENING of the box to the canvas, which never
 * covered the whole problem -- a layer's box has an origin too, and the 548
 * toplevel's `mapcontainer` at 516,4 cut a compass placed at 504 however wide
 * it had been made -- so the flag was added beside it. With the flag reading
 * "this layer clips nothing", the widening had no clipping left to do and only
 * one remaining effect, which was a bug: a layer's box is the SPACE ITS
 * CHILDREN ARE LAID OUT IN, and the children the plugin did not move are the
 * lane's own HUD. Widening 548's `main` -- the 512x334 viewport container --
 * to the canvas right-aligned the XP-total plate inside it to the window
 * instead of to the viewport, so it landed on the map housing and was cut off
 * at the plugin rail. The box therefore stays exactly native: the lane's HUD
 * keeps the geometry the lane authored for it, and the surfaces the plugin
 * moved are in canvas coordinates and never consult it.
 *
 * A layer released from clipping cannot CULL either -- see
 * UITree_LayerCullsChildren. That is what a script shrinking an ancestor to
 * nothing used to need the canvas-sized floor for.
 *
 * A native widget a plugin moved or resized (a retained widget edit) is the
 * moved surface, and every container above it stops clipping so the new box is
 * seen wherever the plugin put it. A moved widget inside another moved widget
 * releases nothing: the chain above that surface was released by its own walk,
 * and the chain between is that surface's content, which is laid out AND
 * clipped in that surface's space -- the orb pack's root sits between the
 * block and the adviser, and letting it clip nothing spills the pack's own
 * children out of the block. A container that is itself moved keeps its own
 * containment: it has the box the plugin gave it, and its content belongs
 * inside that box.
 */
static int
frame_node_moved(struct UITree const* tree, int32_t idx)
{
    struct UITreeElemPosition scratch;
    if( !frame_node_alive(tree, idx) )
        return 0;
    scratch = tree->components[idx].position;
    return UITree_WidgetPositionOverride(tree, idx, &scratch) != 0;
}

static void
frame_stretch_moved_ancestors(
    struct UITree* tree,
    struct UITreeFrameLayout* fl)
{
    assert(tree);
    assert(fl);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        int inside_moved = 0;
        if( tree->components[i].plugin_owner || !frame_node_moved(tree, (int32_t)i) )
            continue;
        for( int32_t p = tree->components[i].parent; p >= 0 && !inside_moved;
             p = tree->components[p].parent )
        {
            if( !frame_node_alive(tree, p) )
                break;
            inside_moved = frame_node_moved(tree, p);
        }
        if( inside_moved )
            continue;
        for( int32_t p = tree->components[i].parent; p >= 0; p = tree->components[p].parent )
        {
            if( !frame_node_alive(tree, p) )
                break;
            frame_stretch_node(tree, fl, p);
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
     * it would hide the chat log the provider just moved inside it.
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
        /* `keep` is exactly "the frame took this node over", so it answers
         * frame_is_lane_chrome's question about a script-created child as
         * well as excluding the slot nodes themselves. */
        if( !frame_is_lane_chrome(
                c, root_group, c->parent >= 0 && keep[c->parent] != 0) )
            continue;
        frame_record_hidden(tree, fl, (int32_t)i);
    }
    free(keep);
}

static int
frame_hidden_has(
    struct UITreeFrameLayout const* fl,
    int32_t idx,
    uint64_t incarnation)
{
    assert(fl);
    for( int i = 0; i < fl->hidden_count; i++ )
        if( fl->hidden[i] == idx && fl->hidden_incarnation[i] == incarnation )
            return 1;
    return 0;
}

static int
frame_stretched_has(
    struct UITreeFrameLayout const* fl,
    int32_t idx,
    uint64_t incarnation)
{
    assert(fl);
    for( int i = 0; i < fl->stretched_count; i++ )
        if( fl->stretched[i] == idx && fl->stretched_incarnation[i] == incarnation )
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

static void
frame_apply(
    struct UITree* tree,
    int root_group)
{
    struct UITreeFrameLayout* fl;
    struct UITreeFrameLayout next;

    assert(tree);

    /* Build the new binding off to the side. The old provision stays fully
     * effective until the diff below commits this one, so a re-provision can
     * never expose the lane's frame as an intermediate state. */
    /* Name the roles a cache gameframe leaves unnamed BEFORE looking for
     * them, or a provision made against a freshly rebuilt toplevel finds no
     * side panels to keep clear of the chrome collection. */
    UITree_FrameBind(tree);

    memset(&next, 0, sizeof(next));
    next.root_group = root_group;
    next.applied_generation = tree->generation;
    next.active = 1;
    frame_collect_slots(tree, &next);
    frame_stretch_moved_ancestors(tree, &next);
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
        uint64_t const incarnation = fl->hidden_incarnation[i];
        if( !frame_node_same(tree, idx, incarnation) ||
            frame_hidden_has(&next, idx, incarnation) )
            continue;
        if( tree->components[idx].frame_hidden )
            (void)UITree_SetFrameHiddenAt(tree, idx, 0);
    }
    for( int i = 0; i < next.hidden_count; i++ )
    {
        int32_t const idx = next.hidden[i];
        uint64_t const incarnation = next.hidden_incarnation[i];
        if( !frame_node_same(tree, idx, incarnation) ||
            frame_hidden_has(fl, idx, incarnation) )
            continue;
        if( !tree->components[idx].frame_hidden )
            (void)UITree_SetFrameHiddenAt(tree, idx, 1);
    }
    /* Containment release is the other flag on the component itself, diffed
     * the same way: a layer stops clipping while a placed surface hangs under
     * it, and takes its own box back the moment none does. */
    for( int i = 0; i < fl->stretched_count; i++ )
    {
        int32_t const idx = fl->stretched[i];
        uint64_t const incarnation = fl->stretched_incarnation[i];
        if( !frame_node_same(tree, idx, incarnation) ||
            frame_stretched_has(&next, idx, incarnation) )
            continue;
        if( tree->components[idx].frame_stretched )
            (void)UITree_SetFrameStretchedAt(tree, idx, 0);
    }
    for( int i = 0; i < next.stretched_count; i++ )
    {
        int32_t const idx = next.stretched[i];
        uint64_t const incarnation = next.stretched_incarnation[i];
        if( !frame_node_same(tree, idx, incarnation) ||
            frame_stretched_has(fl, idx, incarnation) )
            continue;
        if( !tree->components[idx].frame_stretched )
            (void)UITree_SetFrameStretchedAt(tree, idx, 1);
    }

    /* Mark both the outgoing and incoming bindings once, then atomically
     * replace the table. */
    frame_mark_bound_nodes(tree, fl);
    frame_mark_bound_nodes(tree, &next);
    *fl = next;
    UITree_LayoutInvalidate(tree);
}

void
UITree_FrameProvide(
    struct UITree* tree,
    int root_group)
{
    assert(tree);
    frame_apply(tree, root_group);
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
        frame_apply(tree, fl->root_group);
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
    for( int i = 0; i < fl->stretched_count; i++ )
    {
        int32_t const idx = fl->stretched[i];
        if( !frame_node_same(tree, idx, fl->stretched_incarnation[i]) )
            continue;
        if( tree->components[idx].frame_stretched )
            (void)UITree_SetFrameStretchedAt(tree, idx, 0);
    }

    frame_mark_bound_nodes(tree, fl);
    memset(fl, 0, sizeof(*fl));
    UITree_LayoutInvalidate(tree);
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
 */
struct AnchorWork
{
    struct UITree const* tree;
    struct UITreeHost const* host;
    unsigned char const* input;
    unsigned char* output;
    size_t stride;
    int count;
    int written;
    int32_t* record_unit;   /* per record: unit node, or -1 */
    int32_t* unit_of;       /* per node memo: -2 unknown, -1 none, else the unit */
    int32_t* target;        /* per node: anchor target, -1 when not anchored */
    unsigned char* relation;/* per node: effective relation */
    unsigned char* is_target;
    unsigned char* handled; /* per node: unit already written or dropped */
    int* first;             /* per node: earliest record of the unit, count when absent */
};

static int32_t
anchor_unit_of(struct AnchorWork* w, int32_t node)
{
    struct UITree const* tree = w->tree;
    int32_t walk = node;
    uint32_t guard = 0;
    while( walk >= 0 && guard++ < tree->component_count )
    {
        if( w->unit_of[walk] != -2 ) break;
        if( w->target[walk] >= 0 || w->is_target[walk] ) { w->unit_of[walk] = walk; break; }
        walk = tree->components[walk].parent;
    }
    int32_t unit = walk < 0 ? -1 : w->unit_of[walk];
    /* Memoise the whole path walked. */
    for( int32_t p = node; p >= 0 && p != walk; p = tree->components[p].parent )
        w->unit_of[p] = unit;
    return unit;
}

static int
anchor_node_visible(struct AnchorWork const* w, int32_t node)
{
    return UITree_NodeNativeVisible(w->tree, w->host, node, -1);
}

/* The earliest record of a target's whole tree, children included. */
static int
anchor_tree_first(struct AnchorWork const* w, int32_t unit, int depth)
{
    int first = w->first[unit];
    if( depth > 64 ) return first;
    for( uint32_t n = 0; n < w->tree->component_count; n++ )
        if( w->target[n] == (int32_t)unit )
        {
            int child = anchor_tree_first(w, (int32_t)n, depth + 1);
            if( child < first ) first = child;
        }
    return first;
}

static void
anchor_write_tree(struct AnchorWork* w, int32_t unit, int depth)
{
    struct UITree const* tree = w->tree;
    int32_t children[64];
    int count = 0, replacement = -1;
    if( depth > 64 || w->handled[unit] ) return;
    w->handled[unit] = 1;
    for( uint32_t n = 0; n < tree->component_count && count < 64; n++ )
    {
        if( w->target[n] != unit ) continue;
        int at = count++;
        while( at > 0 && w->first[children[at - 1]] > w->first[n] )
        { children[at] = children[at - 1]; at--; }
        children[at] = (int32_t)n;
    }
    for( int i = 0; i < count; i++ )
    {
        int32_t child = children[i];
        if( w->relation[child] == UITREE_WIDGET_RELATION_BEHIND )
            anchor_write_tree(w, child, depth + 1);
        else if( w->relation[child] == UITREE_WIDGET_RELATION_REPLACE &&
                 anchor_node_visible(w, child) && anchor_node_visible(w, unit) )
            replacement = child;
    }
    if( replacement >= 0 )
        anchor_write_tree(w, replacement, depth + 1);
    else
        for( int i = 0; i < w->count; i++ )
            if( w->record_unit[i] == unit )
                memcpy(w->output + (size_t)w->written++ * w->stride,
                       w->input + (size_t)i * w->stride, w->stride);
    for( int i = 0; i < count; i++ )
    {
        int32_t child = children[i];
        if( w->relation[child] == UITREE_WIDGET_RELATION_OVER )
            anchor_write_tree(w, child, depth + 1);
        else if( w->relation[child] == UITREE_WIDGET_RELATION_REPLACE )
            w->handled[child] = 1; /* not presented: inherits the target's veto */
    }
}

static int
frame_reorder_widget_anchors(struct UITree const* tree, struct UITreeHost const* host, void* records,
                             int count, size_t stride, size_t node_offset)
{
    struct AnchorWork w = { .tree = tree, .host = host, .input = records, .stride = stride, .count = count };
    uint32_t const n = tree->component_count;
    int anchored = 0;
    if( count <= 0 || UITree_WidgetAnchorCount(tree) <= 0 ) return count;
    w.target = malloc((size_t)n * sizeof(*w.target));
    w.unit_of = malloc((size_t)n * sizeof(*w.unit_of));
    w.first = malloc((size_t)n * sizeof(*w.first));
    w.relation = calloc(n, sizeof(*w.relation));
    w.is_target = calloc(n, sizeof(*w.is_target));
    w.handled = calloc(n, sizeof(*w.handled));
    w.record_unit = malloc((size_t)count * sizeof(*w.record_unit));
    w.output = malloc((size_t)count * stride);
    assert(w.target);
    assert(w.unit_of);
    assert(w.first);
    assert(w.relation);
    assert(w.is_target);
    assert(w.handled);
    assert(w.record_unit);
    assert(w.output);
    for( uint32_t i = 0; i < n; i++ )
    {
        int32_t target;
        w.unit_of[i] = -2;
        w.first[i] = count;
        w.relation[i] = (unsigned char)UITree_WidgetAnchorAt(tree, (int32_t)i, &target);
        w.target[i] = w.relation[i] == UITREE_WIDGET_RELATION_NATIVE ? -1 : target;
        if( w.target[i] >= 0 ) { w.is_target[target] = 1; anchored++; }
    }
    if( !anchored )
        goto done;
    for( int i = 0; i < count; i++ )
    {
        int32_t node_plus_one;
        memcpy(&node_plus_one, w.input + (size_t)i * stride + node_offset, sizeof(node_plus_one));
        w.record_unit[i] = node_plus_one > 0 && (uint32_t)(node_plus_one - 1) < n
                               ? anchor_unit_of(&w, node_plus_one - 1) : -1;
        if( w.record_unit[i] >= 0 && w.first[w.record_unit[i]] == count )
            w.first[w.record_unit[i]] = i;
    }
    /* Roots: targets that are not themselves anchored. Each tree is written
     * where its earliest record stood; a root with no records anywhere in its
     * tree is never written and its children keep their native positions. */
    for( int i = 0; i <= count; i++ )
    {
        for( uint32_t r = 0; r < n; r++ )
            if( w.is_target[r] && w.target[r] < 0 && !w.handled[r] && anchor_tree_first(&w, (int32_t)r, 0) == i )
                anchor_write_tree(&w, (int32_t)r, 0);
        if( i == count ) break;
        int32_t unit = w.record_unit[i];
        if( unit >= 0 && w.handled[unit] ) continue;
        memcpy(w.output + (size_t)w.written++ * stride, w.input + (size_t)i * stride, stride);
    }
    memcpy(records, w.output, (size_t)w.written * stride);
    count = w.written;
done:
    free(w.target); free(w.unit_of); free(w.first); free(w.relation);
    free(w.is_target); free(w.handled); free(w.record_unit); free(w.output);
    return count;
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
