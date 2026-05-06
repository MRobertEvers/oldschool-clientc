-- Rev245_2 cache loading helpers and per-packet handler functions.
--
-- Asset-loading helpers (load_models, load_models_from_sources,
-- load_anims_for_seq, load_spotanim) are idempotent: already-cached
-- assets are skipped, and the paired GameCache convert is always called.
--
-- Per-packet handlers are exported as M.*_v1(item).  Each handler
-- preloads whatever cache assets the packet needs, then calls
-- Game.Game.exec_packet(item) to apply the packet to game state.
--
-- Config-table loads (jagfile init + convert_*_from_buildcachedat for
-- npcs, objs, idks, sequences, floortypes, etc.) are intentionally NOT
-- further abstracted; those have packet-specific ordering constraints.

local CacheDat = require("cachedat")
local M = {}

-- ── Asset-loading helpers ────────────────────────────────────────────────────

-- Load, add, and convert a batch of model IDs.
-- Deduplicates across calls and skips IDs already in the model cache.
-- model_ids: array of integer model IDs; nil and 0 entries are ignored.
function M.load_models(model_ids)
    local seen = {}
    local requests = {}
    local needed = {}
    for _, id in ipairs(model_ids) do
        if id and id > 0 and not seen[id] then
            seen[id] = true
            if not Game.BuildCacheDat.model_cache_has(id) then
                requests[#requests + 1] = {
                    table_id = CacheDat.Tables.CACHE_DAT_MODELS,
                    archive_id = id,
                    flags = 0,
                }
                needed[#needed + 1] = id
            end
        end
    end
    if #requests > 0 then
        local archives = CacheDat.load_archives(requests)
        for i, id in ipairs(needed) do
            Game.BuildCacheDat.model_cache_add(archives[i], id)
        end
    end
    Game.GameCache.convert_models_chunk_from_buildcachedat(0, 0)
end

-- Collect model IDs from multiple getter call descriptors into one
-- deduplicated batch, then call load_models.
-- sources: array of { getter_fn, arg1, arg2, ... } where
--          getter_fn(arg1, arg2, ...) returns an array of model IDs.
function M.load_models_from_sources(sources)
    local all_ids = {}
    for _, src in ipairs(sources) do
        local getter = src[1]
        for _, mid in ipairs(getter(table.unpack(src, 2))) do
            all_ids[#all_ids + 1] = mid
        end
    end
    M.load_models(all_ids)
end

-- Load animation base frames for a single sequence and convert.
-- Skips frame IDs already in the anim cache.
function M.load_anims_for_seq(seq_id)
    if not seq_id or seq_id < 0 then
        return
    end
    local frame_ids = Game.BuildCacheDat.get_sequence_animbaseframes_ids(seq_id)
    local requests = {}
    for _, fid in ipairs(frame_ids) do
        if not Game.BuildCacheDat.animbaseframes_cache_has(fid) then
            requests[#requests + 1] = {
                table_id = CacheDat.Tables.CACHE_DAT_ANIMATIONS,
                archive_id = fid,
                flags = 0,
            }
        end
    end
    if #requests > 0 then
        local archives = CacheDat.load_archives(requests)
        for _, archive in ipairs(archives) do
            Game.BuildCacheDat.animbaseframes_cache_add(archive)
        end
    end
    Game.GameCache.convert_animbaseframes_from_buildcachedat()
end

-- Load and convert all assets needed for a spotanim:
--   its model, its sequence's animation base frames, and the spotanim configs.
function M.load_spotanim(spotanim_id)
    local model_id = Game.BuildCacheDat.get_spotanim_model_id(spotanim_id)
    if model_id and model_id > 0 and not Game.BuildCacheDat.model_cache_has(model_id) then
        local archive = CacheDat.load_archive(CacheDat.Tables.CACHE_DAT_MODELS, model_id, 0)
        Game.BuildCacheDat.model_cache_add(archive, model_id)
    end
    Game.GameCache.convert_models_chunk_from_buildcachedat(0, 0)

    M.load_anims_for_seq(Game.BuildCacheDat.get_spotanim_seq_id(spotanim_id))
    Game.GameCache.convert_spotanims_from_buildcachedat()
end

-- ── Local ensure helpers ─────────────────────────────────────────────────────

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

-- Config jagfile is set in rebuild_normal_v1; packets may arrive earlier.
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

-- ── Per-packet handlers ──────────────────────────────────────────────────────

local SCENE_WIDTH = 104

function M.rebuild_normal_v1(item)
    Game.BuildCacheDat.clear_config_jagfile()
    Game.BuildCacheDat.clear_versionlist_jagfile()
    Game.BuildCacheDat.clear_map_chunks()

    local zone_padding = SCENE_WIDTH > 8 and math.ceil((SCENE_WIDTH - 8) / 16) or 0
    local zonex = Game.Game.get_pkt_rebuild_normal_zonex(item)
    local zonez = Game.Game.get_pkt_rebuild_normal_zonez(item)

    local function trunc_div_toward_zero(a, b)
        if b < 0 then
            a, b = -a, -b
        end
        if a >= 0 then
            return math.floor(a / b)
        end
        return math.ceil(a / b)
    end

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

    M.load_models(Game.BuildCacheDat.get_all_unique_scenery_model_ids())

    Game.Game.exec_packet(item)

    -- Let ensure_objects_loaded / ensure_sequences_loaded run again for
    -- packets that arrive after rebuild in this batch.
    _G.objects_loaded_flag = false
    _G.sequences_loaded_flag = false
end

function M.player_info_v1(item)
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
    M.load_models_from_sources(sources)

    Game.Game.exec_packet(item)
end

function M.npc_info_v1(item)
    local data = Game.Game.get_pkt_npc_info_data(item)
    local length = Game.Game.get_pkt_npc_info_length(item)

    if not Game.BuildCacheDat.has_config_jagfile() then
        Game.Game.exec_packet(item)
        return
    end

    Game.BuildCacheDat.idkits_init_from_config_jagfile()
    Game.GameCache.convert_idks_from_buildcachedat()
    Game.GameCache.convert_npcs_from_buildcachedat()

    local npc_ids = Game.BuildCacheDat.get_npc_ids_from_packet(data, length)
    local sources = {}
    for _, npc_id in ipairs(npc_ids) do
        sources[#sources + 1] = { Game.BuildCacheDat.get_npc_model_ids,      npc_id }
        sources[#sources + 1] = { Game.BuildCacheDat.get_npc_head_model_ids, npc_id }
    end
    M.load_models_from_sources(sources)

    Game.Game.exec_packet(item)
end

function M.if_settab_v1(item)
    ensure_interfaces_loaded()
    Game.Game.exec_packet(item)
end

function M.update_inv_full_v1(item)
    ensure_interfaces_loaded()
    ensure_objects_loaded()

    local obj_ids = Game.Game.get_inv_obj_ids(item)
    local sources = {}
    for _, oid in ipairs(obj_ids) do
        sources[#sources + 1] = { Game.BuildCacheDat.get_obj_model_ids, oid - 1 }
    end
    M.load_models_from_sources(sources)

    Game.Game.exec_packet(item)
end

function M.if_setobject_v1(item)
    ensure_interfaces_loaded()
    ensure_objects_loaded()

    local obj_id = Game.Game.get_pkt_if_setobject_obj_id(item)
    M.load_models_from_sources({
        { Game.BuildCacheDat.get_obj_model_ids,      obj_id    },
        { Game.BuildCacheDat.get_obj_head_model_ids, obj_id, 0 },
    })

    Game.Game.exec_packet(item)
end

function M.if_setmodel_v1(item)
    ensure_interfaces_loaded()
    local model_id = Game.Game.get_pkt_if_setmodel_model_id(item)
    M.load_models({ model_id })
    Game.Game.exec_packet(item)
end

function M.if_setanim_v1(item)
    ensure_interfaces_loaded()
    ensure_sequences_loaded()
    M.load_anims_for_seq(Game.Game.get_pkt_if_setanim_anim_id(item))
    Game.Game.exec_packet(item)
end

function M.if_setplayerhead_v1(item)
    ensure_interfaces_loaded()
    Game.Game.exec_packet(item)
end

function M.if_setnpchead_v1(item)
    ensure_interfaces_loaded()
    local npc_id = Game.Game.get_pkt_if_setnpchead_npc_id(item)
    M.load_models(Game.BuildCacheDat.get_npc_model_ids(npc_id))
    Game.Game.exec_packet(item)
end

function M.update_inv_partial_v1(item)
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
    M.load_models_from_sources(sources)

    Game.Game.exec_packet(item)
end

function M.loc_add_change_v1(item)
    local loc_id = Game.Game.get_pkt_loc_add_change_loc_id(item)
    if loc_id >= 0 then
        M.load_models(Game.BuildCacheDat.get_loc_model_ids(loc_id))
    end
    Game.Game.exec_packet(item)
end

function M.loc_anim_v1(item)
    ensure_sequences_loaded()
    M.load_anims_for_seq(Game.Game.get_pkt_loc_anim_seq_id(item))
    Game.Game.exec_packet(item)
end

function M.loc_merge_v1(item)
    local loc_id = Game.Game.get_pkt_p_locmerge_loc_id(item)
    if loc_id >= 0 then
        M.load_models(Game.BuildCacheDat.get_loc_model_ids(loc_id))
    end
    Game.Game.exec_packet(item)
end

function M.obj_add_v1(item)
    ensure_objects_loaded()
    local obj_id = Game.Game.get_pkt_obj_add_obj_id(item)
    if obj_id >= 0 then
        M.load_models(Game.BuildCacheDat.get_obj_model_ids(obj_id))
    end
    Game.Game.exec_packet(item)
end

function M.obj_reveal_v1(item)
    ensure_objects_loaded()
    local obj_id = Game.Game.get_pkt_obj_reveal_obj_id(item)
    if obj_id >= 0 then
        M.load_models(Game.BuildCacheDat.get_obj_model_ids(obj_id))
    end
    Game.Game.exec_packet(item)
end

function M.map_anim_v1(item)
    ensure_sequences_loaded()
    M.load_spotanim(Game.Game.get_pkt_map_anim_spotanim_id(item))
    Game.Game.exec_packet(item)
end

function M.map_projanim_v1(item)
    ensure_sequences_loaded()
    M.load_spotanim(Game.Game.get_pkt_map_projanim_spotanim_id(item))
    Game.Game.exec_packet(item)
end

return M
