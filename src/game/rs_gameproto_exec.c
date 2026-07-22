#include "rs_gameproto_exec.h"

#include "app.h"
#include "inv/inv_manager.h"
#include "net/rev/lc245_2/packetin.h"
#include "net/rev/lc245_2/revpacket_lc245_2.h"
#include "rs_chat.h"
#include "rs_player_stats.h"
#include "rs_ui_slots.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"

#include <assert.h>
#include <stdio.h>

/* 15-bit RS colour (r<<10|g<<5|b, 5 bits each) to RGB888. */
static int
rs15_to_rgb(int c)
{
    int r = (c >> 10) & 0x1f;
    int g = (c >> 5) & 0x1f;
    int b = c & 0x1f;
    return ((r << 3) << 16) | ((g << 3) << 8) | (b << 3);
}

static void
exec_update_inv_full(
    struct RS_GameProtoCtx const* ctx,
    struct PktUpdateInvFull const* p)
{
    /* Keyed by the bound interface component id (reference inventories are
     * component-addressed). Obj icons upload lazily when the bound inv view
     * next renders (same path as the RevConfig-seeded containers). */
    if( InvManager_EnsureContainer(ctx->invs, p->component_id, p->size, "server-inv") < 0 )
        return;
    InvManager_ApplyFull(ctx->invs, p->component_id, p->obj_ids, p->obj_counts, p->size);
}

static void
exec_update_inv_partial(
    struct RS_GameProtoCtx const* ctx,
    struct PktUpdateInvPartial const* p)
{
    int src;
    if( InvManager_EnsureContainer(ctx->invs, p->component_id, 0, "server-inv") < 0 )
        return;
    src = InvManager_ContainerForSource(ctx->invs, p->component_id);
    if( src < 0 )
        return;
    for( int i = 0; i < p->count; i++ )
    {
        struct InvSlot slot = { 0 };
        slot.obj_id = p->entries[i].obj_id;
        slot.obj_count = p->entries[i].count;
        InvManager_SetSlot(ctx->invs, src, p->entries[i].slot, &slot);
    }
}

void
RS_GameProto_Exec(
    struct RS_GameProtoCtx const* ctx,
    struct RevPacket_LC245_2* packet)
{
    assert(ctx && packet);

    switch( packet->packet_type )
    {
    /* ---- varps ---- */
    case PKTIN_LC245_2_VARP_SMALL:
        VarPManager_ApplySmall(ctx->varps, packet->_varp_small.variable, packet->_varp_small.value);
        break;
    case PKTIN_LC245_2_VARP_LARGE:
        VarPManager_ApplyLarge(ctx->varps, packet->_varp_large.variable, packet->_varp_large.value);
        break;

    /* ---- player stats ---- */
    case PKTIN_LC245_2_UPDATE_STAT:
        if( packet->_update_stat.stat >= 0 &&
            packet->_update_stat.stat < RS_PLAYER_STATS_SKILL_COUNT )
        {
            RS_PlayerStats_SetXp(
                ctx->stats, packet->_update_stat.stat, packet->_update_stat.xp);
            ctx->stats->current_level[packet->_update_stat.stat] = packet->_update_stat.level;
            RS_PlayerStats_RecomputeCombatLevel(ctx->stats);
        }
        break;
    case PKTIN_LC245_2_UPDATE_RUNENERGY:
        ctx->stats->run_energy = packet->_update_run_energy.run_energy;
        break;
    case PKTIN_LC245_2_UPDATE_RUNWEIGHT:
        ctx->stats->run_weight = packet->_update_runweight.run_weight;
        break;

    /* ---- inventories ---- */
    case PKTIN_LC245_2_UPDATE_INV_FULL:
        exec_update_inv_full(ctx, &packet->_update_inv_full);
        break;
    case PKTIN_LC245_2_UPDATE_INV_PARTIAL:
        exec_update_inv_partial(ctx, &packet->_update_inv_partial);
        break;

    /* ---- interface component mutations ---- */
    case PKTIN_LC245_2_IF_SETTEXT:
        UITree_ApplyText(
            ctx->tree, packet->_if_settext.component_id, packet->_if_settext.text);
        break;
    case PKTIN_LC245_2_IF_SETHIDE:
        UITree_ApplyHide(
            ctx->tree, packet->_if_sethide.component_id, packet->_if_sethide.hide);
        break;
    case PKTIN_LC245_2_IF_SETCOLOUR:
        UITree_ApplyColour(
            ctx->tree,
            packet->_if_setcolour.component_id,
            rs15_to_rgb(packet->_if_setcolour.colour));
        break;
    case PKTIN_LC245_2_IF_SETMODEL:
        UITree_ApplyModel(
            ctx->tree, packet->_if_setmodel.component_id, packet->_if_setmodel.model_id);
        break;
    case PKTIN_LC245_2_IF_SETSCROLLPOS:
        UITree_ApplyScrollPos(
            ctx->tree, packet->_if_setscrollpos.component_id, 0, packet->_if_setscrollpos.pos);
        break;

    /* ---- interface slots (need the App / RS_UISlots) ---- */
    case PKTIN_LC245_2_IF_OPENMAIN:
        if( ctx->app )
            RS_UISlots_OpenMain(ctx->app, packet->_if_openmain.component_id);
        break;
    case PKTIN_LC245_2_IF_OPENSIDE:
        if( ctx->app )
            RS_UISlots_OpenSide(ctx->app, packet->_if_openside.component_id);
        break;
    case PKTIN_LC245_2_IF_OPENMAIN_SIDE:
        if( ctx->app )
            RS_UISlots_OpenMainSide(
                ctx->app,
                packet->_if_openmain_side.main_component_id,
                packet->_if_openmain_side.side_component_id);
        break;
    case PKTIN_LC245_2_IF_OPENCHAT:
        if( ctx->app )
            RS_UISlots_OpenChat(ctx->app, packet->_if_openchat.component_id);
        break;
    case PKTIN_LC245_2_IF_CLOSE:
        if( ctx->app )
            RS_UISlots_CloseModal(ctx->app);
        break;
    case PKTIN_LC245_2_IF_SETTAB:
        if( ctx->app )
            RS_UISlots_SetTab(
                ctx->app, packet->_if_settab.tab_id, packet->_if_settab.component_id);
        break;

    /* ---- chat ---- */
    case PKTIN_LC245_2_MESSAGE_GAME:
        if( ctx->chat )
            RS_Chat_AddMessage(ctx->chat, RS_CHAT_TYPE_GAME, NULL, packet->_message_game.text);
        break;

    /* ---- world / entities (bitstream decode into World is follow-on) ---- */
    case PKTIN_LC245_2_REBUILD_NORMAL:
        /* Region origin the next scene load centers on. The src world_builder
         * currently loads a fixed chunk list; region-addressed loading is the
         * remaining hook (App owns the builder). */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "gameproto_exec: REBUILD_NORMAL zone=%d,%d (region load pending)\n",
                packet->_map_rebuild.zonex,
                packet->_map_rebuild.zonez);
        break;
    case PKTIN_LC245_2_PLAYER_INFO:
    case PKTIN_LC245_2_NPC_INFO:
        /* Raw command streams reach here; the bit-level decode + World entity
         * spawn/movement application is the remaining entity-sync work. */
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(
                stderr,
                "gameproto_exec: entity info packet %d (%d bytes, decode pending)\n",
                packet->packet_type,
                packet->packet_type == PKTIN_LC245_2_PLAYER_INFO
                    ? packet->_player_info.length
                    : packet->_npc_info.length);
        break;

    default:
        if( getenv("TORIRS_NET_DEBUG") )
            fprintf(stderr, "gameproto_exec: unhandled packet %d\n", packet->packet_type);
        break;
    }
}
