-- pkt_rebuild_normal: load terrain, scenery, and models for rebuild zone via CacheDat /
-- Game.BuildCacheDat.* APIs, then run gameproto_exec rebuild
local CacheDat = require("cachedat")

local SCENE_WIDTH = 104
local zone_padding = math.floor(SCENE_WIDTH / (2 * 8)) -- 6

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


local zonex, zonez = ...
print(zonex, zonez)
local zone_sw_x = zonex - zone_padding
local zone_sw_z = zonez - zone_padding
local zone_ne_x = zonex + zone_padding
local zone_ne_z = zonez + zone_padding

local wx_sw = zone_sw_x * 8
local wz_sw = zone_sw_z * 8
local wx_ne = zone_ne_x * 8
local wz_ne = zone_ne_z * 8

local chunks, _, _, _, _ = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

-- Load map terrain from CacheDat
local terrain_requests = {}
local terrain_map_ids = {}
for _, chunk in ipairs(chunks) do
    if not Game.BuildCacheDat.has_map_terrain(chunk.x, chunk.z) then
        local map_id = (chunk.x << 16) | (chunk.z)
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
        Game.BuildCacheDat.cache_map_terrain(terrain_archives[i], map_id)
    end
end

-- Load map scenery from CacheDat
local scenery_requests = {}
local scenery_map_ids = {}
for _, chunk in ipairs(chunks) do
    if not Game.BuildCacheDat.has_map_scenery(chunk.x, chunk.z) then
        local map_id = (chunk.x << 16) | (chunk.z)
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
        Game.BuildCacheDat.cache_map_scenery(scenery_archives[i], map_id)
    end
end

-- Initialize config from config jagfiles (same path as init_cache_dat)
Game.BuildCacheDat.init_floortypes_from_config_jagfile()
Game.BuildCacheDat.init_scenery_configs_from_config_jagfile()

-- Load required models (unique IDs from all cached scenery; computed in C)
local models_to_load = Game.BuildCacheDat.get_all_unique_scenery_model_ids()

local model_requests = {}
local models_needed = {}
for _, model_id in ipairs(models_to_load) do
    if not Game.BuildCacheDat.has_model(model_id) then
        table.insert(model_requests, {
            table_id = CacheDat.Tables.CACHE_DAT_MODELS,
            archive_id = model_id,
            flags = 0,
        })
        table.insert(models_needed, model_id)
    end
end

if #model_requests > 0 then
    local model_archives = CacheDat.load_archives(model_requests)
    for i, model_id in ipairs(models_needed) do
        Game.BuildCacheDat.cache_model(model_archives[i], model_id)
    end
end

Game.Game.build_scene_centerzone(zonex, zonez, SCENE_WIDTH)
