#include "uitree_frame.h"

#include "plugin/torirs_plugin.h"
#include "uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    (int)UITREE_FRAME_SLOT_COUNT == (int)TORIRS_PLUGIN_SLOT_COUNT,
    "frame slot count must match the plugin contract");

/* Chrome nodes one declaration may suppress. A 2004 surround is about twenty
 * and an OldSchool toplevel's own decoration is under a hundred; past this the
 * rest stay visible, which is a visibly wrong frame rather than a silent one.
 * @see frame_collect_chrome. */
#define UITREE_FRAME_HIDDEN_MAX 256

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
    /** Nodes carrying each role, the ids they had (so a reclaimed slot cannot
     *  be mistaken for the component that was placed in it), and the boxes
     *  they were authored at. */
    int32_t slot_node[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    int slot_component_id[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    /** UITree_FrameSlotIndex for each, so a per-member box finds its node
     *  without re-deriving it every frame. */
    int slot_member[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    struct UITreeElemPosition slot_saved[UITREE_FRAME_SLOT_COUNT][UITREE_FRAME_SLOT_NODES_MAX];
    int slot_node_count[UITREE_FRAME_SLOT_COUNT];
    /** The declaration itself, kept so the re-assert can restate it. */
    struct UITreeFrameSlotRect slot_rect[UITREE_FRAME_SLOT_COUNT];
    /** Lane chrome the declaration suppressed. No "what was it before": the
     *  flag is the layout's alone, so clearing it restores exactly the state
     *  the lane had. */
    int32_t hidden[UITREE_FRAME_HIDDEN_MAX];
    int hidden_count;
    uint8_t active;
};

static struct UITreeFrameLayout*
frame_state(struct UITree* tree)
{
    assert(tree);
    /* Allocated on the first claim and not before: a client with no layout
     * plugin -- which is every lane today until one is switched on -- pays a
     * NULL pointer and no bytes. */
    if( !tree->frame_layout )
    {
        tree->frame_layout = calloc(1, sizeof(*tree->frame_layout));
        assert(tree->frame_layout);
    }
    return tree->frame_layout;
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
        return c->type == UIELEM_BUILTIN_SIDEBAR || c->slot_tag == UITREE_SLOT_SIDE_MODAL;
    case UITREE_FRAME_SLOT_MAIN_MODAL:
        return c->slot_tag == UITREE_SLOT_MAIN_MODAL;
    case UITREE_FRAME_SLOT_CHAT_BUTTONS:
        return c->type == UIELEM_BUILTIN_CHAT_BUTTON;
    default:
        return 0;
    }
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
        return node->type == UIELEM_BUILTIN_CHAT_BUTTON ? (int)node->u.chat_button.filter : -1;
    case UITREE_FRAME_SLOT_SIDEBAR:
        return node->type == UIELEM_BUILTIN_SIDEBAR ? node->u.sidebar.tabno : -1;
    default:
        /* One surface, nothing to number. */
        return -1;
    }
}

int32_t
UITree_FrameSlotNode(
    struct UITree const* tree,
    int slot)
{
    assert(tree);
    if( slot < 0 || slot >= UITREE_FRAME_SLOT_COUNT )
        return -1;

    /* The world is latched, so it costs nothing to ask for and is the one role
     * every frame has. */
    if( slot == UITREE_FRAME_SLOT_VIEWPORT )
        return frame_node_alive(tree, tree->world_index) ? tree->world_index : -1;

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
            return (int32_t)i;
    }
    return -1;
}

int32_t
UITree_FrameSlotMemberNode(
    struct UITree const* tree,
    int slot,
    int member)
{
    assert(tree);
    if( member < 0 )
        return UITree_FrameSlotNode(tree, slot);
    if( slot < 0 || slot >= UITREE_FRAME_SLOT_COUNT )
        return -1;

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        if( c->freed )
            continue;
        if( !frame_node_is_slot(c, slot) )
            continue;
        if( UITree_FrameSlotIndex(c, slot) == member )
            return (int32_t)i;
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
                fprintf(
                    stderr,
                    "frame: role %d has more than %d nodes; the rest keep the "
                    "lane's own geometry\n",
                    s,
                    UITREE_FRAME_SLOT_NODES_MAX);
                break;
            }
            fl->slot_node[s][n] = (int32_t)i;
            fl->slot_component_id[s][n] = c->component_id;
            fl->slot_member[s][n] = UITree_FrameSlotIndex(c, s);
            fl->slot_saved[s][n] = c->position;
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

/*
 * One node, at the declared box.
 *
 * An absolute box, and every mode cleared. A cache component states its
 * geometry in MODES as often as in numbers -- widthmode 1 is "fill my parent",
 * xmode 1 is "centre in it" -- and writing x/y/w/h while leaving those standing
 * means the resolve recomputes the box from the parent and throws the
 * declaration away. The plugin said where it goes; nothing else gets a vote.
 *
 * The hide is NOT cleared. A sidebar mount that is not the selected tab is
 * hidden by the slot manager, and showing all fourteen because the layout
 * placed the role would draw the whole tab set stacked in one panel. The
 * declaration says where a role goes, not which of its nodes is the one on
 * screen -- that stays the client's.
 */
static void
frame_place_node(
    struct UITree* tree,
    int32_t idx,
    struct UITreeFrameRect const* rect)
{
    struct UITreeComponent* c;

    assert(tree);
    assert(rect);
    c = &tree->components[idx];
    if( c->position.kind == UIPOS_XY && c->position.x == rect->x && c->position.y == rect->y &&
        c->position.width == rect->w && c->position.height == rect->h &&
        c->position.x_mode < 0 && c->position.y_mode < 0 && c->position.width_mode < 0 &&
        c->position.height_mode < 0 && c->position.relative_flags == 0 )
        return;

    c->position.kind = UIPOS_XY;
    c->position.x = rect->x;
    c->position.y = rect->y;
    c->position.width = rect->w;
    c->position.height = rect->h;
    c->position.x_mode = -1;
    c->position.y_mode = -1;
    c->position.width_mode = -1;
    c->position.height_mode = -1;
    c->position.relative_flags = 0;
    c->position.layout_resolved = 0;
    UITree_MarkNodeDirty(tree, idx);
    UITree_LayoutInvalidateBoxes(tree);
}

static void
frame_hide_node(
    struct UITree* tree,
    struct UITreeFrameLayout* fl,
    int32_t idx)
{
    struct UITreeComponent* c;

    assert(tree);
    assert(fl);
    if( fl->hidden_count >= UITREE_FRAME_HIDDEN_MAX )
        return;
    c = &tree->components[idx];
    fl->hidden[fl->hidden_count] = idx;
    fl->hidden_count++;
    c->frame_hidden = 1;
    UITree_MarkNodeDirty(tree, idx);
}

static void
frame_collect_chrome(
    struct UITree* tree,
    struct UITreeFrameLayout* fl,
    int root_group)
{
    assert(tree);
    assert(fl);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent const* c = &tree->components[i];
        int is_slot = 0;

        if( c->freed )
            continue;
        for( int s = 0; s < UITREE_FRAME_SLOT_COUNT && !is_slot; s++ )
            for( int n = 0; n < fl->slot_node_count[s] && !is_slot; n++ )
                is_slot = fl->slot_node[s][n] == (int32_t)i;
        if( is_slot )
            continue;
        if( !frame_is_lane_chrome(c, root_group) )
            continue;
        frame_hide_node(tree, fl, (int32_t)i);
    }
}

void
UITree_FrameApply(
    struct UITree* tree,
    struct UITreeFrameSlotRect const* slots,
    int root_group)
{
    struct UITreeFrameLayout* fl;

    assert(tree);
    assert(slots);

    /* Every apply is a fresh declaration, so the previous one is undone first:
     * the roles it moved go back to where they were authored and the chrome it
     * hid is shown again, and only then is the new one written. Without this a
     * layout that stopped placing the compass would leave the compass where
     * the LAST declaration put it, and a re-declared frame would accumulate
     * hides across every resize. */
    UITree_FrameRelease(tree);
    fl = frame_state(tree);

    memset(fl->slot_node_count, 0, sizeof(fl->slot_node_count));
    frame_collect_slots(tree, fl);

    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
        {
            int32_t const idx = fl->slot_node[s][n];
            struct UITreeFrameRect const* rect = frame_rect_for(&slots[s], fl->slot_member[s][n]);

            if( !rect )
            {
                /* A role -- or one member of it -- the declaration did not
                 * mention is one this frame does not show. A modern resizable
                 * layout has no compass housing of its own, and leaving the
                 * lane's compass on screen would put it wherever the old frame
                 * had it. */
                frame_hide_node(tree, fl, idx);
                continue;
            }

            frame_place_node(tree, idx, rect);
        }
    }

    memcpy(fl->slot_rect, slots, sizeof(fl->slot_rect));
    frame_collect_chrome(tree, fl, root_group);
    fl->active = 1;
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

    /*
     * The BOXES, and only the boxes.
     *
     * The suppression needs no restating -- `frame_hidden` has one writer and
     * it is this file. A slot's box does: on a dat1 lane every gameframe
     * component carries IF1 value scripts, and the CS1 tick re-evaluates them
     * and writes the interface's authored geometry back, so a placement made
     * once is undone before the frame it was made for is drawn. Nothing is
     * wrong with the CS1 tick; the layout simply has to be the LAST writer,
     * and calling this from the emit walk is what makes it one.
     *
     * The loop skips a node that already agrees, so the steady-state cost is a
     * comparison per placed node -- about twenty on a 2004 frame -- and no
     * layout invalidation at all.
     */
    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
        {
            int32_t const idx = fl->slot_node[s][n];
            struct UITreeFrameRect const* rect =
                frame_rect_for(&fl->slot_rect[s], fl->slot_member[s][n]);
            if( !rect || !frame_node_alive(tree, idx) )
                continue;
            frame_place_node(tree, idx, rect);
        }
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
        if( !frame_node_alive(tree, idx) )
            continue;
        tree->components[idx].frame_hidden = 0;
        UITree_MarkNodeDirty(tree, idx);
    }
    fl->hidden_count = 0;

    for( int s = 0; s < UITREE_FRAME_SLOT_COUNT; s++ )
    {
        for( int n = 0; n < fl->slot_node_count[s]; n++ )
        {
            int32_t const idx = fl->slot_node[s][n];
            if( !frame_node_alive(tree, idx) )
                continue;
            /* The id guard is the whole reason it is stored: between the apply
             * and this, a slot can have been reclaimed and handed to a
             * different component, and restoring the old box onto it would
             * move something that was never moved. */
            if( tree->components[idx].component_id != fl->slot_component_id[s][n] )
                continue;
            tree->components[idx].position = fl->slot_saved[s][n];
            tree->components[idx].position.layout_resolved = 0;
            UITree_MarkNodeDirty(tree, idx);
        }
        fl->slot_node_count[s] = 0;
    }

    fl->active = 0;
    UITree_LayoutInvalidate(tree);
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
