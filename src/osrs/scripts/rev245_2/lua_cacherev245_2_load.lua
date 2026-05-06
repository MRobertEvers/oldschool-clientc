-- Single dispatcher: drain game packet queue, preload assets per type, then exec via Game.Game.exec_packet.
local CacheDat = require("cachedat")
local RevCache = require("rev245_2_cache")
local PKT = require("packet_types")

local SCENE_WIDTH = 104

local function ensure_interfaces_loaded()
    if not _G.interfaces_loaded_flag then
        local archive = CacheDat.load_archive(
            CacheDat.Tables.CACHE_DAT_CONFIGS,
            CacheDat.ConfigDatKind.CONFIG_DAT_INTERFACES,
            0
        )
        Game.Game.load_interfaces(archive)
        Game.Game.load_component_sprites()
        Game.GameCache.convert_components_from_buildcachedat()
        _G.interfaces_loaded_flag = true
    end
end

-- Config jagfile is set in handle_rebuild_normal; packets may arrive earlier.
local function ensure_objects_loaded()
    if _G.objects_loaded_flag or not Game.BuildCacheDat.has_config_jagfile() then
        return
    end
    Game.BuildCacheDat.objects_init_from_config_jagfile()
    Game.GameCache.convert_objs_from_buildcachedat()
    _G.objects_loaded_flag = true
end

local function ensure_sequences_loaded()
    if _G.sequences_loaded_flag or not Game.BuildCacheDat.has_config_jagfile() then
        return
    end
    Game.BuildCacheDat.sequences_init_from_config_jagfile()
    Game.GameCache.convert_sequences_from_buildcachedat()
    _G.sequences_loaded_flag = true
end

local function handle_rebuild_normal(item)
    -- Replace config/versionlist jag handles before loading new archives (was buildcachedat_clear_jagfiles in C).
    Game.BuildCacheDat.clear_config_jagfile()
    Game.BuildCacheDat.clear_versionlist_jagfile()
    -- Full zone rebuild: evict all cached map squares so we always load every chunk in the grid
    -- below (stale has_map_* from a prior position must not skip edge squares).
    Game.BuildCacheDat.clear_map_chunks()

    -- Match world_rebuild_centerzone_begin (world.c): ceil((scene_size-8)/16) so (2*padding+1)*8 >= SCENE_WIDTH.
    local zone_padding = SCENE_WIDTH > 8 and math.ceil((SCENE_WIDTH - 8) / 16) or 0
    local zonex = Game.Game.get_pkt_rebuild_normal_zonex(item)
    local zonez = Game.Game.get_pkt_rebuild_normal_zonez(item)

    -- Integer division trunc toward zero (matches C `zone_sw_x / 8`), not Lua math.floor toward -inf.
    local function trunc_div_toward_zero(a, b)
        if b < 0 then
            a, b = -a, -b
        end
        if a >= 0 then
            return math.floor(a / b)
        end
        return math.ceil(a / b)
    end

    -- Same 64x64 map chunk indices as world->_chunk_sw_* / _chunk_ne_* in world_rebuild_centerzone_begin.
    local function zone_corners_to_map_chunks(zone_sw_x, zone_sw_z, zone_ne_x, zone_ne_z)
        local map_sw_x = trunc_div_toward_zero(zone_sw_x, 8)
        local map_sw_z = trunc_div_toward_zero(zone_sw_z, 8)
        local map_ne_x = trunc_div_toward_zero(zone_ne_x, 8)
        local map_ne_z = trunc_div_toward_zero(zone_ne_z, 8)
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
    local chunks = zone_corners_to_map_chunks(zone_sw_x, zone_sw_z, zone_ne_x, zone_ne_z)

    local terrain_requests = {}
    local terrain_map_ids = {}
    for _, chunk in ipairs(chunks) do
        if not Game.BuildCacheDat.has_map_terrain(chunk.x, chunk.z) then
            local map_id = (chunk.x << 16) | chunk.z
            table.insert(terrain_requests, {
                table_id = CacheDat.Tables.CACHE_DAT_MAPS,
                archive_id = map_id,
                flags = CacheDat.ArchiveIdFlags.MAP_TERRAIN,
            })
            table.insert(terrain_map_ids, map_id)
        end
    end
    if #terrain_requests > 0 then
        local terrain_archives = CacheDat.load_archives(terrain_requests)
        for i, map_id in ipairs(terrain_map_ids) do
            Game.BuildCacheDat.map_terrain_cache_add(terrain_archives[i], map_id)
        end
    end

    local scenery_requests = {}
    local scenery_map_ids = {}
    for _, chunk in ipairs(chunks) do
        if not Game.BuildCacheDat.has_map_scenery(chunk.x, chunk.z) then
            local map_id = (chunk.x << 16) | chunk.z
            table.insert(scenery_requests, {
                table_id = CacheDat.Tables.CACHE_DAT_MAPS,
                archive_id = map_id,
                flags = CacheDat.ArchiveIdFlags.MAP_SCENERY,
            })
            table.insert(scenery_map_ids, map_id)
        end
    end
    if #scenery_requests > 0 then
        local scenery_archives = CacheDat.load_archives(scenery_requests)
        for i, map_id in ipairs(scenery_map_ids) do
            Game.BuildCacheDat.map_scenery_cache_add(scenery_archives[i], map_id)
        end
    end

    for _, chunk in ipairs(chunks) do
        Game.GameCache.convert_map_terrain_chunk_from_buildcachedat(chunk.x, chunk.z)
        Game.GameCache.convert_scenery_chunk_from_buildcachedat(chunk.x, chunk.z)
    end

    local jagfiles = CacheDat.load_archives({
        {
            table_id = CacheDat.Tables.CACHE_DAT_CONFIGS,
            archive_id = CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS,
            flags = 0,
        },
        {
            table_id = CacheDat.Tables.CACHE_DAT_CONFIGS,
            archive_id = CacheDat.ConfigDatKind.CONFIG_DAT_VERSION_LIST,
            flags = 0,
        },
    })
    Game.BuildCacheDat.set_config_jagfile(jagfiles[1])
    Game.BuildCacheDat.set_versionlist_jagfile(jagfiles[2])

    do
        local anim_count = Game.BuildCacheDat.get_animbaseframes_count_from_versionlist_jagfile()
        local anim_requests = {}
        for i = 0, anim_count - 1 do
            if not Game.BuildCacheDat.animbaseframes_cache_has(i) then
                table.insert(anim_requests, {
                    table_id = CacheDat.Tables.CACHE_DAT_ANIMATIONS,
                    archive_id = i,
                    flags = 0,
                })
            end
        end
        if #anim_requests > 0 then
            local anim_archives = CacheDat.load_archives(anim_requests)
            for _, archive in ipairs(anim_archives) do
                Game.BuildCacheDat.animbaseframes_cache_add(archive)
            end
        end
        Game.GameCache.convert_animbaseframes_from_buildcachedat()
    end

    Game.BuildCacheDat.sequences_init_from_config_jagfile()
    _G.sequences_loaded_flag = true
    Game.GameCache.convert_sequences_from_buildcachedat()
    Game.BuildCacheDat.floortypes_init_from_config_jagfile()
    Game.GameCache.convert_floortypes_from_buildcachedat()
    Game.BuildCacheDat.init_scenery_configs_from_config_jagfile()

    for _, chunk in ipairs(chunks) do
        Game.GameCache.convert_loc_configs_chunk_from_buildcachedat(chunk.x, chunk.z)
    end

    RevCache.load_models(Game.BuildCacheDat.get_all_unique_scenery_model_ids())

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
    Game.GameCache.convert_idks_from_buildcachedat()
    Game.GameCache.convert_objs_from_buildcachedat()

    local idk_ids, obj_ids = Game.BuildCacheDat.get_player_appearance_ids_from_packet(data, length)
    local sources = {}
    for _, idk_id in ipairs(idk_ids) do
        sources[#sources + 1] = { Game.BuildCacheDat.get_idk_model_ids,      idk_id }
        sources[#sources + 1] = { Game.BuildCacheDat.get_idk_head_model_ids, idk_id }
    end
    for _, obj_id in ipairs(obj_ids) do
        sources[#sources + 1] = { Game.BuildCacheDat.get_obj_model_ids,      obj_id    }
        sources[#sources + 1] = { Game.BuildCacheDat.get_obj_head_model_ids, obj_id, 0 }
    end
    RevCache.load_models_from_sources(sources)

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
    Game.GameCache.convert_idks_from_buildcachedat()
    Game.GameCache.convert_npcs_from_buildcachedat()

    local npc_ids = Game.BuildCacheDat.get_npc_ids_from_packet(data, length)
    local sources = {}
    for _, npc_id in ipairs(npc_ids) do
        sources[#sources + 1] = { Game.BuildCacheDat.get_npc_model_ids,      npc_id }
        sources[#sources + 1] = { Game.BuildCacheDat.get_npc_head_model_ids, npc_id }
    end
    RevCache.load_models_from_sources(sources)

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
    local sources = {}
    for _, oid in ipairs(obj_ids) do
        sources[#sources + 1] = { Game.BuildCacheDat.get_obj_model_ids, oid - 1 }
    end
    RevCache.load_models_from_sources(sources)

    Game.Game.exec_packet(item)
end

local function handle_if_setobject(item)
    ensure_interfaces_loaded()
    ensure_objects_loaded()

    local obj_id = Game.Game.get_pkt_if_setobject_obj_id(item)
    RevCache.load_models_from_sources({
        { Game.BuildCacheDat.get_obj_model_ids,      obj_id    },
        { Game.BuildCacheDat.get_obj_head_model_ids, obj_id, 0 },
    })

    Game.Game.exec_packet(item)
end

local function handle_if_setmodel(item)
    ensure_interfaces_loaded()
    local model_id = Game.Game.get_pkt_if_setmodel_model_id(item)
    RevCache.load_models({ model_id })
    Game.Game.exec_packet(item)
end

local function handle_if_setanim(item)
    ensure_interfaces_loaded()
    ensure_sequences_loaded()
    RevCache.load_anims_for_seq(Game.Game.get_pkt_if_setanim_anim_id(item))
    Game.Game.exec_packet(item)
end

local function handle_if_setplayerhead(item)
    ensure_interfaces_loaded()
    Game.Game.exec_packet(item)
end

local function handle_if_setnpchead(item)
    ensure_interfaces_loaded()
    local npc_id = Game.Game.get_pkt_if_setnpchead_npc_id(item)
    RevCache.load_models(Game.BuildCacheDat.get_npc_model_ids(npc_id))
    Game.Game.exec_packet(item)
end

local function handle_update_inv_partial(item)
    ensure_interfaces_loaded()
    ensure_objects_loaded()

    local entries = Game.Game.get_pkt_update_inv_partial_entries(item)
    local sources = {}
    for i = 1, #entries, 2 do
        local obj_id = entries[i + 1]
        if obj_id and obj_id > 0 then
            sources[#sources + 1] = { Game.BuildCacheDat.get_obj_model_ids, obj_id }
        end
    end
    RevCache.load_models_from_sources(sources)

    Game.Game.exec_packet(item)
end

local function handle_loc_add_change(item)
    local loc_id = Game.Game.get_pkt_loc_add_change_loc_id(item)
    if loc_id >= 0 then
        RevCache.load_models(Game.BuildCacheDat.get_loc_model_ids(loc_id))
    end
    Game.Game.exec_packet(item)
end

local function handle_loc_anim(item)
    ensure_sequences_loaded()
    RevCache.load_anims_for_seq(Game.Game.get_pkt_loc_anim_seq_id(item))
    Game.Game.exec_packet(item)
end

local function handle_loc_merge(item)
    local loc_id = Game.Game.get_pkt_p_locmerge_loc_id(item)
    if loc_id >= 0 then
        RevCache.load_models(Game.BuildCacheDat.get_loc_model_ids(loc_id))
    end
    Game.Game.exec_packet(item)
end

local function handle_obj_add(item)
    ensure_objects_loaded()
    local obj_id = Game.Game.get_pkt_obj_add_obj_id(item)
    if obj_id >= 0 then
        RevCache.load_models(Game.BuildCacheDat.get_obj_model_ids(obj_id))
    end
    Game.Game.exec_packet(item)
end

local function handle_obj_reveal(item)
    ensure_objects_loaded()
    local obj_id = Game.Game.get_pkt_obj_reveal_obj_id(item)
    if obj_id >= 0 then
        RevCache.load_models(Game.BuildCacheDat.get_obj_model_ids(obj_id))
    end
    Game.Game.exec_packet(item)
end

local function handle_map_anim(item)
    ensure_sequences_loaded()
    RevCache.load_spotanim(Game.Game.get_pkt_map_anim_spotanim_id(item))
    Game.Game.exec_packet(item)
end

local function handle_map_projanim(item)
    ensure_sequences_loaded()
    RevCache.load_spotanim(Game.Game.get_pkt_map_projanim_spotanim_id(item))
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
