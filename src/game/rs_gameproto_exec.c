/*
 * A server packet in a client with no App (rs_gameproto_exec.h): the model,
 * then the writes that go to a UI tree alone. The real client's half is
 * app/app_gameproto_exec.c, which calls RS_GameProto_PresentTree for the
 * tree-only arms in the same place in its own switch.
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
        UITree_ApplyScrollPos(
            tree, packet->_if_setscrollpos.component_id, 0, packet->_if_setscrollpos.pos);
        break;
    case PKT_NAME_IF_SETPOSITION:
        UITree_ApplyPosition(
            tree,
            packet->_if_setposition.component_id,
            packet->_if_setposition.x,
            packet->_if_setposition.z);
        break;
    case PKT_NAME_IF_SETROTATESPEED:
        UITree_ApplyModelRotateSpeed(
            tree,
            packet->_if_setrotatespeed.component_id,
            packet->_if_setrotatespeed.x_speed,
            packet->_if_setrotatespeed.y_speed);
        break;
    case PKT_NAME_IF_SETANGLE:
        UITree_ApplyModelAngle(
            tree,
            packet->_if_setangle.component_id,
            packet->_if_setangle.angle_x,
            packet->_if_setangle.angle_y,
            packet->_if_setangle.zoom);
        break;
    case PKT_NAME_IF_CLEARINV:
        clearinv_node(tree, UITree_FindByComponentId(tree, packet->_if_clearinv.component_id));
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
    assert(ctx->state);
    assert(ctx->tree);
    assert(packet);
    /* A control frame (a cache invalidate, a replay seek) acts on the App. */
    assert(!packet->_control);

    (void)RS_ServerState_Apply(ctx->state, packet);

    /* The store is the model's; with no App to mount anything, the value goes
     * straight onto the node the tree already has (the App's App_If*Show puts
     * it there when the node is mounted, and again on a remount). */
    switch( packet->packet_type )
    {
    case PKT_NAME_IF_SETTEXT:
        UITree_ApplyText(ctx->tree, packet->_if_settext.component_id, packet->_if_settext.text);
        break;
    case PKT_NAME_IF_SETHIDE:
        UITree_ApplyHide(ctx->tree, packet->_if_sethide.component_id, packet->_if_sethide.hide);
        break;
    case PKT_NAME_IF_SETCOLOUR:
        UITree_ApplyColour(
            ctx->tree,
            packet->_if_setcolour.component_id,
            RS_ServerState_Rs15ToRgb(packet->_if_setcolour.colour));
        break;
    case PKT_NAME_IF_SETMODEL:
        UITree_ApplyModel(
            ctx->tree, packet->_if_setmodel.component_id, packet->_if_setmodel.model_id);
        break;
    case PKT_NAME_IF_SETANIM:
        UITree_ApplyModelAnim(
            ctx->tree, packet->_if_setanim.component_id, packet->_if_setanim.anim_id);
        break;
    default:
        RS_GameProto_PresentTree(ctx->tree, packet);
        break;
    }
}
