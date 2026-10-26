/*
 * What every revision's decoded packet shares, whoever decoded it: freeing its
 * owned fields, and the two questions the packet pump asks of its type.
 */
#include "revpacket.h"

#include "mem/torimem_rscache.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int
gameproto_packet_may_mutate_ui(enum GameProtoPktName packet_type)
{
    if( packet_type >= PKT_NAME_IF_OPENCHAT && packet_type <= PKT_NAME_IF_SETPLAYERMODEL_SELF )
        return 1;

    switch( packet_type )
    {
    case PKT_NAME_TUT_OPEN:
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT:
    case PKT_NAME_UPDATE_INV_FULL:
    case PKT_NAME_UPDATE_INV_PARTIAL:
    case PKT_NAME_UPDATE_IGNORELIST:
    case PKT_NAME_CHAT_FILTER_SETTINGS:
    case PKT_NAME_UPDATE_FRIENDLIST:
    case PKT_NAME_FRIENDLIST_LOADED:
    case PKT_NAME_UPDATE_RUNWEIGHT:
    case PKT_NAME_UPDATE_STAT:
    case PKT_NAME_UPDATE_RUNENERGY:
    case PKT_NAME_TRIGGER_ONDIALOGABORT:
    case PKT_NAME_RUNCLIENTSCRIPT:
    case PKT_NAME_VARP_SMALL:
    case PKT_NAME_VARP_LARGE:
    case PKT_NAME_VARP_SYNC:
    case PKT_NAME_VARP_RESET:
        return 1;
    default:
        return 0;
    }
}

int
gameproto_packet_changes_interface_mounts(enum GameProtoPktName packet_type)
{
    switch( packet_type )
    {
    case PKT_NAME_IF_OPENCHAT:
    case PKT_NAME_IF_OPENMAIN_SIDE:
    case PKT_NAME_IF_CLOSE:
    case PKT_NAME_IF_SETTAB:
    case PKT_NAME_IF_OPENMAIN:
    case PKT_NAME_IF_OPENSIDE:
    case PKT_NAME_IF_OPENOVERLAY:
    case PKT_NAME_IF_OPENTOP:
    case PKT_NAME_IF_OPENSUB:
    case PKT_NAME_IF_CLOSESUB:
    case PKT_NAME_IF_MOVESUB:
    case PKT_NAME_IF_RESYNC_V2:
    case PKT_NAME_TUT_OPEN:
        return 1;
    default:
        return 0;
    }
}

/*
 * Every parser's heap payload is ToriMem_Net()'s, except four still on libc
 * because code outside src/net frees them too: _map_rebuild.zones and
 * _update_zone_enclosed.entries (rs_gameproto_exec_test frees them directly),
 * and the wordpack_unpack text of MESSAGE_PRIVATE / MESSAGE_CLANCHANNEL
 * (wordpack_unpack's other callers free its result with ToriMem_Free(ToriMem_Net(), )).
 */
void
gameproto_free(struct RevPacket* p)
{
    if( !p )
        return;

    switch( p->packet_type )
    {
    case PKT_NAME_RUNCLIENTSCRIPT:
        /* The one payload held outside the union; see struct RevPacket. */
        ToriMem_Free(ToriMem_Net(), p->_runclientscript);
        p->_runclientscript = NULL;
        break;
    case PKT_NAME_NPC_INFO:
    case PKT_NAME_NPC_INFO_LARGE:
        ToriMem_Free(ToriMem_Net(), p->_npc_info.data);
        p->_npc_info.data = NULL;
        p->_npc_info.length = 0;
        break;
    case PKT_NAME_PLAYER_INFO:
        ToriMem_Free(ToriMem_Net(), p->_player_info.data);
        p->_player_info.data = NULL;
        p->_player_info.length = 0;
        break;
    case PKT_NAME_RECONNECT:
        ToriMem_Free(ToriMem_Net(), p->_reconnect.data);
        p->_reconnect.data = NULL;
        p->_reconnect.length = 0;
        break;
    case PKT_NAME_REBUILD_NORMAL:
    case PKT_NAME_REBUILD_REGION:
        /* dat2/osrs230 XTEA keys (NULL on the lc254 path), and the instanced
         * descriptor grid (NULL for REBUILD_NORMAL). */
        ToriMem_Free(ToriMem_Net(), p->_map_rebuild.region_ids);
        ToriMem_Free(ToriMem_Net(), p->_map_rebuild.region_keys);
        ToriMem_Free(ToriMem_Net(), p->_map_rebuild.zones);
        p->_map_rebuild.region_ids = NULL;
        p->_map_rebuild.region_keys = NULL;
        p->_map_rebuild.zones = NULL;
        p->_map_rebuild.region_count = 0;
        break;
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL:
    case PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER:
    case PKT_NAME_VARCLAN:
    case PKT_NAME_VARCLAN_ENABLE:
    case PKT_NAME_VARCLAN_DISABLE:
    case PKT_NAME_CLANCHANNEL_FULL:
    case PKT_NAME_CLANCHANNEL_DELTA:
    case PKT_NAME_CLANSETTINGS_FULL:
    case PKT_NAME_CLANSETTINGS_DELTA:
    case PKT_NAME_GROUP_FULL:
        ToriMem_Free(ToriMem_Net(), p->_raw_payload.data);
        p->_raw_payload.data = NULL;
        p->_raw_payload.length = 0;
        break;
    case PKT_NAME_UPDATE_TRADINGPOST:
        ToriMem_Free(ToriMem_Net(), p->_update_tradingpost.offers);
        p->_update_tradingpost.offers = NULL;
        p->_update_tradingpost.count = 0;
        break;
    case PKT_NAME_UNKNOWN_STRING:
    case PKT_NAME_SITE_SETTINGS:
        ToriMem_Free(ToriMem_Net(), p->_text.text);
        p->_text.text = NULL;
        break;
    case PKT_NAME_LOGOUT_TRANSFER:
        ToriMem_Free(ToriMem_Net(), p->_logout_transfer.host);
        p->_logout_transfer.host = NULL;
        break;
    case PKT_NAME_MESSAGE_CLANCHANNEL:
    case PKT_NAME_MESSAGE_CLANCHANNEL_SYSTEM:
        ToriMem_Free(ToriMem_Net(), p->_message_clanchannel.text);
        p->_message_clanchannel.text = NULL;
        break;
    case PKT_NAME_REBUILD_WORLDENTITY:
        ToriMem_Free(ToriMem_Net(), p->_rebuild_wev.data);
        p->_rebuild_wev.data = NULL;
        p->_rebuild_wev.length = 0;
        break;
    case PKT_NAME_UPDATE_INV_FULL:
        ToriMem_Free(ToriMem_Net(), p->_update_inv_full.obj_ids);
        ToriMem_Free(ToriMem_Net(), p->_update_inv_full.obj_counts);
        p->_update_inv_full.obj_ids = NULL;
        p->_update_inv_full.obj_counts = NULL;
        p->_update_inv_full.size = 0;
        break;
    case PKT_NAME_IF_RESYNC_V2:
        ToriMem_Free(ToriMem_Net(), p->_if_resync.mounts);
        ToriMem_Free(ToriMem_Net(), p->_if_resync.events);
        p->_if_resync.mounts = NULL;
        p->_if_resync.events = NULL;
        p->_if_resync.mount_count = 0;
        p->_if_resync.event_count = 0;
        break;
    case PKT_NAME_IF_SETTEXT:
        ToriMem_Free(ToriMem_Net(), p->_if_settext.text);
        p->_if_settext.text = NULL;
        break;
    case PKT_NAME_MESSAGE_GAME:
        ToriMem_Free(ToriMem_Net(), p->_message_game.name);
        ToriMem_Free(ToriMem_Net(), p->_message_game.text);
        p->_message_game.name = NULL;
        p->_message_game.text = NULL;
        break;
    case PKT_NAME_MESSAGE_PRIVATE:
        ToriMem_Free(ToriMem_Net(), p->_message_private.text);
        p->_message_private.text = NULL;
        break;
    case PKT_NAME_UPDATE_INV_PARTIAL:
        ToriMem_Free(ToriMem_Net(), p->_update_inv_partial.entries);
        p->_update_inv_partial.entries = NULL;
        p->_update_inv_partial.count = 0;
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED:
        ToriMem_Free(ToriMem_Net(), p->_update_zone_enclosed.entries);
        p->_update_zone_enclosed.entries = NULL;
        p->_update_zone_enclosed.count = 0;
        break;
    case PKT_NAME_UPDATE_IGNORELIST:
        ToriMem_Free(ToriMem_Net(), p->_update_ignorelist.names37);
        p->_update_ignorelist.names37 = NULL;
        p->_update_ignorelist.count = 0;
        break;
    case PKT_NAME_SET_PLAYER_OP:
        ToriMem_Free(ToriMem_Net(), p->_set_player_op.text);
        p->_set_player_op.text = NULL;
        break;
    default:
        break;
    }
}
