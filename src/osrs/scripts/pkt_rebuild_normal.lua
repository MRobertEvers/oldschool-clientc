-- pkt_rebuild_normal: load terrain, scenery, and models for rebuild zone via BuildCacheDat,
-- then run gameproto_exec rebuild (scenebuilder_load_from_buildcachedat).
local HostIOUtils = require("hostio_utils")

local SCENE_WIDTH = 104
local zone_padding = math.floor(SCENE_WIDTH / (2 * 8))  -- 6

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
    return chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z
end

local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end

local item, io = ...

local zonex, zonez = GameProto.get_rebuild_bounds(item)
local zone_sw_x = zonex - zone_padding
local zone_sw_z = zonez - zone_padding
local zone_ne_x = zonex + zone_padding
local zone_ne_z = zonez + zone_padding

local wx_sw = zone_sw_x * 8
local wz_sw = zone_sw_z * 8
local wx_ne = zone_ne_x * 8
local wz_ne = zone_ne_z * 8

local chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

local success, param_a, param_b, data_size, data

for _, chunk in ipairs(chunks) do
    local promise = HostIO.dat_map_terrain_load(chunk.x, chunk.z)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if not success then
        error(string.format("Failed to load terrain %d, %d", chunk.x, chunk.z))
    end
    BuildCacheDat.cache_map_terrain(param_a, param_b, data_size, data)
end

BuildCacheDat.init_floortypes_from_config_jagfile()

for _, chunk in ipairs(chunks) do
    local promise = HostIO.dat_map_scenery_load(chunk.x, chunk.z)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if not success then
        error(string.format("Failed to load scenery %d, %d", chunk.x, chunk.z))
    end
    BuildCacheDat.cache_map_scenery(param_a, param_b, data_size, data)
end

BuildCacheDat.init_scenery_configs_from_config_jagfile()

local queued_model_ids = {}
local scenery_locs = BuildCacheDat.get_all_scenery_locs()
for _, loc in ipairs(scenery_locs) do
    if loc.chunk_x >= map_sw_x and loc.chunk_x <= map_ne_x and
       loc.chunk_z >= map_sw_z and loc.chunk_z <= map_ne_z then
        local model_ids = BuildCacheDat.get_scenery_model_ids(loc.loc_id)
        for _, model_id in ipairs(model_ids) do
            if model_id ~= 0 then
                add_unique(queued_model_ids, model_id)
            end
        end
    end
end

for _, model_id in ipairs(queued_model_ids) do
    local promise = HostIO.dat_models_load(model_id)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success then
        BuildCacheDat.cache_model(model_id, data_size, data)
    end
end

GameProto.exec_rebuild(item)
