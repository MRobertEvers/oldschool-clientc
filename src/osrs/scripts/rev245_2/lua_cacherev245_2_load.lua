-- Packet dispatch loop for rev245_2.
--
-- Drains the inbound packet queue each frame.  For packets that require
-- cache assets to be preloaded before execution, the matching handler in
-- RevCache is called.  All other packets are executed directly.
local RevCache = require("rev245_2_cache")
local PKT = require("packet_types")

local handlers = {
    [PKT.REBUILD_NORMAL_V1]     = RevCache.rebuild_normal_v1,
    [PKT.PLAYER_INFO_V1]        = RevCache.player_info_v1,
    [PKT.NPC_INFO_V1]           = RevCache.npc_info_v1,
    [PKT.IF_SETTAB_V1]          = RevCache.if_settab_v1,
    [PKT.UPDATE_INV_FULL_V1]    = RevCache.update_inv_full_v1,
    [PKT.IF_SETOBJECT_V1]       = RevCache.if_setobject_v1,
    [PKT.IF_SETMODEL_V1]        = RevCache.if_setmodel_v1,
    [PKT.IF_SETANIM_V1]         = RevCache.if_setanim_v1,
    [PKT.IF_SETPLAYERHEAD_V1]   = RevCache.if_setplayerhead_v1,
    [PKT.IF_OPENCHAT_V1]        = RevCache.if_openchat_v1,
    [PKT.IF_SETNPCHEAD_V1]      = RevCache.if_setnpchead_v1,
    [PKT.UPDATE_INV_PARTIAL_V1] = RevCache.update_inv_partial_v1,
    [PKT.LOC_ADD_CHANGE_V1]     = RevCache.loc_add_change_v1,
    [PKT.LOC_ANIM_V1]           = RevCache.loc_anim_v1,
    [PKT.LOC_MERGE_V1]          = RevCache.loc_merge_v1,
    [PKT.OBJ_ADD_V1]            = RevCache.obj_add_v1,
    [PKT.OBJ_REVEAL_V1]         = RevCache.obj_reveal_v1,
    [PKT.MAP_ANIM_V1]           = RevCache.map_anim_v1,
    [PKT.MAP_PROJANIM_V1]       = RevCache.map_projanim_v1,
}

while true do
    local item, ptype = Game.Game.pop_next_packet()
    if not item then break end
    local h = handlers[ptype]
    if h then
        h(item)
    else
        Game.Game.exec_packet(item)
    end
end
