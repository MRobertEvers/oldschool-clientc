/*
 * A server packet in a client with no App (rs_gameproto_exec.h): the model,
 * then the writes that go to a UI tree alone. The real client's half is
 * app/app_gameproto_exec.c, which calls RS_GameProto_PresentTree for the
 * tree-only arms in the same place in its own switch.
 *
 * The interface setters (IF_SETTEXT, HIDE, COLOUR, MODEL, ANIM) are not
 * written to the tree here: their record is the model's store, and putting a
 * stored value on a node is the App's persisting setter (App_If*Show), which
 * a mount repeats. A test that wants the node written drives the App half
 * (fa5680f77 deleted the test-only branches that wrote the tree instead).
 */

#include "rs_gameproto_exec.h"

#include "net/rev/revpacket.h"
#include "rs_server_state.h"
#include "ui/uitree.h"

#include <assert.h>
#include <stdint.h>

/* IF_CLEARINV: the component and every child under it lose their obj. */
static void
clearinv_node(
    struct UITree* tree,
    int32_t idx)
{
    struct UITreeComponent* c;

    assert(tree);
    if( idx < 0 || (uint32_t)idx >= tree->component_count )
        return;
    c = &tree->components[idx];
    if( c->freed )
        return;
    /* A plugin-owned control under a cache container is not the server's
     * inventory: it carries no obj and neither do its children. The slot
     * remount sweep keeps owned children the same way. */
    if( c->plugin_owner )
        return;
    (void)UITree_SetObjectAt(tree, idx, 0, 0, -1, 0, c->item_num_mode);
    for( int32_t child = c->first_child; child >= 0; )
    {
        int32_t next = tree->components[child].next_sibling;
        clearinv_node(tree, child);
        child = next;
    }
}

static void
exec_if_setscrollpos(
    struct UITree* tree,
    struct PktIfSetScrollPos const* p)
{
    UITree_ApplyScrollPos(tree, p->component_id, 0, p->pos);
}

static void
exec_if_setposition(
    struct UITree* tree,
    struct PktIfSetPosition const* p)
{
    UITree_ApplyPosition(tree, p->component_id, p->x, p->z);
}

static void
exec_if_setrotatespeed(
    struct UITree* tree,
    struct PktIfSetRotateSpeed const* p)
{
    UITree_ApplyModelRotateSpeed(tree, p->component_id, p->x_speed, p->y_speed);
}

static void
exec_if_setangle(
    struct UITree* tree,
    struct PktIfSetAngle const* p)
{
    UITree_ApplyModelAngle(tree, p->component_id, p->angle_x, p->angle_y, p->zoom);
}

static void
exec_if_clearinv(
    struct UITree* tree,
    struct PktIfClearInv const* p)
{
    clearinv_node(tree, UITree_FindByComponentId(tree, p->component_id));
}

void
RS_GameProto_PresentTree(
    struct UITree* tree,
    struct RevPacket const* packet)
{
    assert(tree);
    assert(packet);
    switch( packet->packet_type )
    {
    case PKT_NAME_IF_SETSCROLLPOS:
        exec_if_setscrollpos(tree, &packet->_if_setscrollpos);
        break;
    case PKT_NAME_IF_SETPOSITION:
        exec_if_setposition(tree, &packet->_if_setposition);
        break;
    case PKT_NAME_IF_SETROTATESPEED:
        exec_if_setrotatespeed(tree, &packet->_if_setrotatespeed);
        break;
    case PKT_NAME_IF_SETANGLE:
        exec_if_setangle(tree, &packet->_if_setangle);
        break;
    case PKT_NAME_IF_CLEARINV:
        exec_if_clearinv(tree, &packet->_if_clearinv);
        break;
    default:
        break;
    }
}

void
RS_GameProto_Exec(
    struct RS_GameProtoCtx const* ctx,
    struct RevPacket* packet)
{
    assert(ctx);
    assert(packet);
    assert(ctx->state);
    assert(ctx->tree);
    /* A control frame (a cache invalidate, a replay seek) acts on the App. */
    assert(!packet->_control);

    (void)RS_ServerState_Apply(ctx->state, packet);
    RS_GameProto_PresentTree(ctx->tree, packet);
}
