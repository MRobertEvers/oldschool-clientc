-- Single dispatcher: drain game packet queue, preload assets per type, then exec via Game.Game.exec_packet.
local CacheDat = require("cachedat")
local CacheIO = require("cacheio")
local PKT = require("packet_types")

local cacheio_requests = Game.CacheIO.request_list_new()

local SCENE_WIDTH = 104

local function cacheio_add_models(models_needed)
    if #models_needed == 0 then
        return
    end
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.models_fetch(cacheio_requests, models_needed)
    local archives = CacheIO.load_request_list(cacheio_requests)
    for i, mid in ipairs(models_needed) do
        Game.BuildCacheDat.model_cache_add(archives[i], mid)
    end
end

local function cacheio_add_anim_fids(fids)
    if #fids == 0 then
        return
    end
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.animbaseframes_fetch_missing(cacheio_requests, fids)
    local archives = CacheIO.load_request_list(cacheio_requests)
    for _, archive in ipairs(archives) do
        Game.BuildCacheDat.animbaseframes_cache_add(archive)
    end
end

local function ensure_interfaces_loaded()
    if not _G.interfaces_loaded_flag then
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.config_archive_fetch(cacheio_requests, CacheDat.ConfigDatKind.CONFIG_DAT_INTERFACES)
        local a = CacheIO.load_request_list(cacheio_requests)
        Game.Game.load_interfaces(a[1])
        Game.Game.load_component_sprites()
        _G.interfaces_loaded_flag = true
    end
end

-- Config jagfile is set in handle_rebuild_normal; packets may arrive earlier.
local function ensure_objects_loaded()
    if _G.objects_loaded_flag or not Game.BuildCacheDat.has_config_jagfile() then
        return
    end
    Game.BuildCacheDat.objects_init_from_config_jagfile()
    _G.objects_loaded_flag = true
end

local function ensure_sequences_loaded()
    if _G.sequences_loaded_flag or not Game.BuildCacheDat.has_config_jagfile() then
        return
    end
    Game.BuildCacheDat.sequences_init_from_config_jagfile()
    _G.sequences_loaded_flag = true
end

local function handle_rebuild_normal(item)
    -- Replace config/versionlist jag handles before loading new archives (was buildcachedat_clear_jagfiles in C).
    Game.BuildCacheDat.clear_config_jagfile()
    Game.BuildCacheDat.clear_versionlist_jagfile()

    local zone_padding = math.floor(SCENE_WIDTH / (2 * 8))
    local zonex = Game.Game.get_pkt_rebuild_normal_zonex(item)
    local zonez = Game.Game.get_pkt_rebuild_normal_zonez(item)

    local function world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)
        local map_sw_x = math.floor(wx_sw / 64)
        local map_sw_z = math.floor(wz_sw / 64)
        local map_ne_x = math.floor(wx_ne / 64)
        local map_ne_z = math.floor(wz_ne / 64)
        local chunks = {}
        local chunk_idx = 1
        for x = map_sw_x, map_ne_x do
            for z = map_sw_z, map_ne_z do
                chunks[chunk_idx] = { x = x, z = z }
                chunk_idx = chunk_idx + 1
            end
        end
        return chunks
    end

    local zone_sw_x = zonex - zone_padding
    local zone_sw_z = zonez - zone_padding
    local zone_ne_x = zonex + zone_padding
    local zone_ne_z = zonez + zone_padding
    local wx_sw = zone_sw_x * 8
    local wz_sw = zone_sw_z * 8
    local wx_ne = zone_ne_x * 8
    local wz_ne = zone_ne_z * 8
    local chunks = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

    local terrain_chunks = {}
    for _, chunk in ipairs(chunks) do
        if not Game.BuildCacheDat.has_map_terrain(chunk.x, chunk.z) then
            table.insert(terrain_chunks, chunk)
        end
    end
    if #terrain_chunks > 0 then
        Game.CacheIO.request_list_reset(cacheio_requests)
        for _, chunk in ipairs(terrain_chunks) do
            Game.BuildCacheDat.map_terrain_fetch(cacheio_requests, chunk.x, chunk.z)
        end
        local terrain_archives = CacheIO.load_request_list(cacheio_requests)
        for i, chunk in ipairs(terrain_chunks) do
            local map_id = (chunk.x << 16) | chunk.z
            Game.BuildCacheDat.map_terrain_cache_add(terrain_archives[i], map_id)
        end
    end

    local scenery_chunks = {}
    for _, chunk in ipairs(chunks) do
        if not Game.BuildCacheDat.has_map_scenery(chunk.x, chunk.z) then
            table.insert(scenery_chunks, chunk)
        end
    end
    if #scenery_chunks > 0 then
        Game.CacheIO.request_list_reset(cacheio_requests)
        for _, chunk in ipairs(scenery_chunks) do
            Game.BuildCacheDat.map_scenery_fetch(cacheio_requests, chunk.x, chunk.z)
        end
        local scenery_archives = CacheIO.load_request_list(cacheio_requests)
        for i, chunk in ipairs(scenery_chunks) do
            local map_id = (chunk.x << 16) | chunk.z
            Game.BuildCacheDat.map_scenery_cache_add(scenery_archives[i], map_id)
        end
    end

    do
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.config_jagfiles_fetch(cacheio_requests)
        local jag = CacheIO.load_request_list(cacheio_requests)
        local ji = 1
        if jag[ji] then
            Game.BuildCacheDat.set_config_jagfile(jag[ji])
            ji = ji + 1
        end
        if jag[ji] then
            Game.BuildCacheDat.set_versionlist_jagfile(jag[ji])
        end
    end

    do
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.animbaseframes_fetch_uncached(cacheio_requests)
        local anim_archives = CacheIO.load_request_list(cacheio_requests)
        for _, archive in ipairs(anim_archives) do
            Game.BuildCacheDat.animbaseframes_cache_add(archive)
        end
    end

    Game.BuildCacheDat.sequences_init_from_config_jagfile()
    _G.sequences_loaded_flag = true
    Game.BuildCacheDat.floortypes_init_from_config_jagfile()
    Game.BuildCacheDat.init_scenery_configs_from_config_jagfile()

    do
        local models_to_load = Game.BuildCacheDat.get_all_unique_scenery_model_ids()
        local models_needed = {}
        for _, model_id in ipairs(models_to_load) do
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
        cacheio_add_models(models_needed)
    end

    Game.Game.exec_packet(item)

    -- Let ensure_objects_loaded / ensure_sequences_loaded run again for packets after rebuild in this batch.
    _G.objects_loaded_flag = false
    _G.sequences_loaded_flag = false
end

local function handle_player_info(item)
    local data = Game.Game.get_pkt_player_info_data(item)
    local length = Game.Game.get_pkt_player_info_length(item)

    if not Game.BuildCacheDat.has_config_jagfile() then
        Game.Game.exec_packet(item)
        return
    end

    Game.BuildCacheDat.idkits_init_from_config_jagfile()
    Game.BuildCacheDat.objects_init_from_config_jagfile()

    local idk_ids, obj_ids = Game.BuildCacheDat.get_player_appearance_ids_from_packet(data, length)
    local seen = {}
    local queued_model_ids = {}

    local function queue_unique(model_id)
        if not model_id or model_id == 0 then
            return
        end
        if seen[model_id] then
            return
        end
        seen[model_id] = true
        table.insert(queued_model_ids, model_id)
    end

    for _, idk_id in ipairs(idk_ids) do
        for _, model_id in ipairs(Game.BuildCacheDat.get_idk_model_ids(idk_id)) do
            queue_unique(model_id)
        end
        for _, model_id in ipairs(Game.BuildCacheDat.get_idk_head_model_ids(idk_id)) do
            queue_unique(model_id)
        end
    end
    for _, obj_id in ipairs(obj_ids) do
        for _, model_id in ipairs(Game.BuildCacheDat.get_obj_model_ids(obj_id)) do
            queue_unique(model_id)
        end
        for _, model_id in ipairs(Game.BuildCacheDat.get_obj_head_model_ids(obj_id, 0)) do
            queue_unique(model_id)
        end
    end

    local models_needed = {}
    for _, model_id in ipairs(queued_model_ids) do
        if not Game.BuildCacheDat.model_cache_has(model_id) then
            table.insert(models_needed, model_id)
        end
    end
    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_npc_info(item)
    local data = Game.Game.get_pkt_npc_info_data(item)
    local length = Game.Game.get_pkt_npc_info_length(item)

    if not Game.BuildCacheDat.has_config_jagfile() then
        Game.Game.exec_packet(item)
        return
    end

    -- Loads idk.dat + npc.dat; C needs npc configs before PKT_NPC_INFO_OPBITS_NPCTYPE / npc_model.
    Game.BuildCacheDat.idkits_init_from_config_jagfile()

    local npc_ids = Game.BuildCacheDat.get_npc_ids_from_packet(data, length)

    local seen = {}
    local queued_model_ids = {}

    local function queue_unique(model_id)
        if not model_id or model_id == 0 then
            return
        end
        if seen[model_id] then
            return
        end
        seen[model_id] = true
        table.insert(queued_model_ids, model_id)
    end

    for _, npc_id in ipairs(npc_ids) do
        for _, model_id in ipairs(Game.BuildCacheDat.get_npc_model_ids(npc_id)) do
            queue_unique(model_id)
        end
        for _, model_id in ipairs(Game.BuildCacheDat.get_npc_head_model_ids(npc_id)) do
            queue_unique(model_id)
        end
    end

    local models_needed = {}
    for _, model_id in ipairs(queued_model_ids) do
        if not Game.BuildCacheDat.model_cache_has(model_id) then
            table.insert(models_needed, model_id)
        end
    end
    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_if_settab(item)
    ensure_interfaces_loaded()
    Game.Game.exec_packet(item)
end

local function handle_update_inv_full(item)
    ensure_interfaces_loaded()
    ensure_objects_loaded()

    local obj_ids = Game.Game.get_inv_obj_ids(item)
    local seen = {}
    local models_needed = {}

    for _, oid in ipairs(obj_ids) do
        local actual_obj_id = oid - 1
        local model_ids = Game.BuildCacheDat.get_obj_model_ids(actual_obj_id)
        for _, model_id in ipairs(model_ids) do
            if model_id ~= 0 and not seen[model_id] then
                seen[model_id] = true
                if not Game.BuildCacheDat.model_cache_has(model_id) then
                    table.insert(models_needed, model_id)
                end
            end
        end
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_if_setobject(item)
    ensure_interfaces_loaded()
    ensure_objects_loaded()

    local obj_id = Game.Game.get_pkt_if_setobject_obj_id(item)
    local seen = {}
    local models_needed = {}

    local function maybe_load(model_id)
        if model_id ~= 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
    end

    for _, mid in ipairs(Game.BuildCacheDat.get_obj_model_ids(obj_id)) do
        maybe_load(mid)
    end
    for _, mid in ipairs(Game.BuildCacheDat.get_obj_head_model_ids(obj_id, 0)) do
        maybe_load(mid)
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_if_setmodel(item)
    ensure_interfaces_loaded()
    local model_id = Game.Game.get_pkt_if_setmodel_model_id(item)
    if model_id and model_id > 0 and not Game.BuildCacheDat.model_cache_has(model_id) then
        cacheio_add_models({ model_id })
    end
    Game.Game.exec_packet(item)
end

local function handle_if_setanim(item)
    ensure_interfaces_loaded()
    ensure_sequences_loaded()

    local anim_id = Game.Game.get_pkt_if_setanim_anim_id(item)
    if anim_id and anim_id >= 0 then
        local frame_ids = Game.BuildCacheDat.get_sequence_animbaseframes_ids(anim_id)
        local fids = {}
        for _, fid in ipairs(frame_ids) do
            if not Game.BuildCacheDat.animbaseframes_cache_has(fid) then
                table.insert(fids, fid)
            end
        end
        cacheio_add_anim_fids(fids)
    end

    Game.Game.exec_packet(item)
end

local function handle_if_setplayerhead(item)
    ensure_interfaces_loaded()
    Game.Game.exec_packet(item)
end

local function handle_if_setnpchead(item)
    ensure_interfaces_loaded()
    local npc_id = Game.Game.get_pkt_if_setnpchead_npc_id(item)
    local seen = {}
    local models_needed = {}

    local function maybe_load(model_id)
        if model_id and model_id > 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
    end

    for _, mid in ipairs(Game.BuildCacheDat.get_npc_model_ids(npc_id)) do
        maybe_load(mid)
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_update_inv_partial(item)
    ensure_interfaces_loaded()
    ensure_objects_loaded()

    local entries = Game.Game.get_pkt_update_inv_partial_entries(item)
    local seen = {}
    local models_needed = {}

    local function maybe_load(model_id)
        if model_id and model_id > 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
    end

    for i = 1, #entries, 2 do
        local obj_id = entries[i + 1]
        if obj_id and obj_id > 0 then
            for _, mid in ipairs(Game.BuildCacheDat.get_obj_model_ids(obj_id)) do
                maybe_load(mid)
            end
        end
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_loc_add_change(item)
    local loc_id = Game.Game.get_pkt_loc_add_change_loc_id(item)
    local seen = {}
    local models_needed = {}

    local function maybe_load(model_id)
        if model_id and model_id > 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
    end

    if loc_id >= 0 then
        for _, mid in ipairs(Game.BuildCacheDat.get_loc_model_ids(loc_id)) do
            maybe_load(mid)
        end
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_loc_anim(item)
    ensure_sequences_loaded()

    local seq_id = Game.Game.get_pkt_loc_anim_seq_id(item)
    if seq_id and seq_id >= 0 then
        local frame_ids = Game.BuildCacheDat.get_sequence_animbaseframes_ids(seq_id)
        local fids = {}
        for _, fid in ipairs(frame_ids) do
            if not Game.BuildCacheDat.animbaseframes_cache_has(fid) then
                table.insert(fids, fid)
            end
        end
        cacheio_add_anim_fids(fids)
    end

    Game.Game.exec_packet(item)
end

local function handle_loc_merge(item)
    local loc_id = Game.Game.get_pkt_p_locmerge_loc_id(item)
    local seen = {}
    local models_needed = {}

    local function maybe_load(model_id)
        if model_id and model_id > 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
    end

    if loc_id >= 0 then
        for _, mid in ipairs(Game.BuildCacheDat.get_loc_model_ids(loc_id)) do
            maybe_load(mid)
        end
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_obj_add(item)
    ensure_objects_loaded()

    local obj_id = Game.Game.get_pkt_obj_add_obj_id(item)
    local seen = {}
    local models_needed = {}

    local function maybe_load(model_id)
        if model_id and model_id > 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
    end

    if obj_id >= 0 then
        for _, mid in ipairs(Game.BuildCacheDat.get_obj_model_ids(obj_id)) do
            maybe_load(mid)
        end
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_obj_reveal(item)
    ensure_objects_loaded()

    local obj_id = Game.Game.get_pkt_obj_reveal_obj_id(item)
    local seen = {}
    local models_needed = {}

    local function maybe_load(model_id)
        if model_id and model_id > 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_needed, model_id)
            end
        end
    end

    if obj_id >= 0 then
        for _, mid in ipairs(Game.BuildCacheDat.get_obj_model_ids(obj_id)) do
            maybe_load(mid)
        end
    end

    cacheio_add_models(models_needed)

    Game.Game.exec_packet(item)
end

local function handle_map_anim(item)
    ensure_sequences_loaded()

    local spotanim_id = Game.Game.get_pkt_map_anim_spotanim_id(item)
    local model_id = Game.BuildCacheDat.get_spotanim_model_id(spotanim_id)
    if model_id and model_id > 0 and not Game.BuildCacheDat.model_cache_has(model_id) then
        cacheio_add_models({ model_id })
    end

    local seq_id = Game.BuildCacheDat.get_spotanim_seq_id(spotanim_id)
    if seq_id and seq_id >= 0 then
        local frame_ids = Game.BuildCacheDat.get_sequence_animbaseframes_ids(seq_id)
        local fids = {}
        for _, fid in ipairs(frame_ids) do
            if not Game.BuildCacheDat.animbaseframes_cache_has(fid) then
                table.insert(fids, fid)
            end
        end
        cacheio_add_anim_fids(fids)
    end

    Game.Game.exec_packet(item)
end

local function handle_map_projanim(item)
    ensure_sequences_loaded()

    local spotanim_id = Game.Game.get_pkt_map_projanim_spotanim_id(item)
    local model_id = Game.BuildCacheDat.get_spotanim_model_id(spotanim_id)
    if model_id and model_id > 0 and not Game.BuildCacheDat.model_cache_has(model_id) then
        cacheio_add_models({ model_id })
    end

    local seq_id = Game.BuildCacheDat.get_spotanim_seq_id(spotanim_id)
    if seq_id and seq_id >= 0 then
        local frame_ids = Game.BuildCacheDat.get_sequence_animbaseframes_ids(seq_id)
        local fids = {}
        for _, fid in ipairs(frame_ids) do
            if not Game.BuildCacheDat.animbaseframes_cache_has(fid) then
                table.insert(fids, fid)
            end
        end
        cacheio_add_anim_fids(fids)
    end

    Game.Game.exec_packet(item)
end

local handlers = {
    [PKT.REBUILD_NORMAL] = handle_rebuild_normal,
    [PKT.PLAYER_INFO] = handle_player_info,
    [PKT.NPC_INFO] = handle_npc_info,
    [PKT.IF_SETTAB] = handle_if_settab,
    [PKT.UPDATE_INV_FULL] = handle_update_inv_full,
    [PKT.IF_SETOBJECT] = handle_if_setobject,
    [PKT.IF_SETMODEL] = handle_if_setmodel,
    [PKT.IF_SETANIM] = handle_if_setanim,
    [PKT.IF_SETPLAYERHEAD] = handle_if_setplayerhead,
    [PKT.IF_SETNPCHEAD] = handle_if_setnpchead,
    [PKT.UPDATE_INV_PARTIAL] = handle_update_inv_partial,
    [PKT.LOC_ADD_CHANGE] = handle_loc_add_change,
    [PKT.LOC_ANIM] = handle_loc_anim,
    [PKT.LOC_MERGE] = handle_loc_merge,
    [PKT.OBJ_ADD] = handle_obj_add,
    [PKT.OBJ_REVEAL] = handle_obj_reveal,
    [PKT.MAP_ANIM] = handle_map_anim,
    [PKT.MAP_PROJANIM] = handle_map_projanim,
}

while true do
    local item, ptype = Game.Game.pop_next_packet()
    if not item then
        break
    end
    local h = handlers[ptype]
    if h then
        h(item)
    else
        Game.Game.exec_packet(item)
    end
end
