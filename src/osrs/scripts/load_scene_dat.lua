local HostIOUtils = require("hostio_utils")

-- Configuration
local MAP_SW_X = 50
local MAP_SW_Z = 50
local MAP_NE_X = 51
local MAP_NE_Z = 51

-- Guard models to always load
local GUARD_MODELS = { 235, 246, 301, 151, 176, 254, 185, 519, 541 }

-- Initialize chunks array
local chunks = {}
local chunk_idx = 1
for x = MAP_SW_X, MAP_NE_X do
    for z = MAP_SW_Z, MAP_NE_Z do
        chunks[chunk_idx] = { x = x, z = z }
        chunk_idx = chunk_idx + 1
    end
end

-- Queued IDs for batch loading
local queued_texture_ids = {}
local queued_model_ids = {}

-- Helper: Add unique element to table
local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end

-- Helper: Load multiple assets and wait for all
local function load_all_and_wait(load_fn, items)
    local promises = {}
    for _, item in ipairs(items) do
        local promise = load_fn(item)
        table.insert(promises, promise)
    end
    
    local results = {}
    for i, promise in ipairs(promises) do
        local success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
        if not success then
            error(string.format("Failed to load item at index %d", i))
        end
        table.insert(results, { 
            param_a = param_a, 
            param_b = param_b, 
            data_size = data_size, 
            data = data 
        })
    end
    return results
end

print("=== Step 0: Load Configs ===")
local configs_promise = HostIO.dat_config_configs_load()
local versionlist_promise = HostIO.dat_config_version_list_load()

local success, param_a, param_b, data_size, data = HostIOUtils.await(configs_promise)
if not success then error("Failed to load configs") end
BuildCacheDat.set_config_jagfile(data_size, data)

success, param_a, param_b, data_size, data = HostIOUtils.await(versionlist_promise)
if not success then error("Failed to load versionlist") end
BuildCacheDat.set_versionlist_jagfile(data_size, data)

print("=== Step 1: Load Terrain ===")
for _, chunk in ipairs(chunks) do
    local promise = HostIO.dat_map_terrain_load(chunk.x, chunk.z)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if not success then 
        error(string.format("Failed to load terrain %d, %d", chunk.x, chunk.z))
    end
    BuildCacheDat.cache_map_terrain(param_a, param_b, data_size, data)
end

print("=== Step 2: Load Floor Types ===")
BuildCacheDat.load_floortype()

print("=== Step 3: Load Scenery ===")
for _, chunk in ipairs(chunks) do
    local promise = HostIO.dat_map_scenery_load(chunk.x, chunk.z)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if not success then 
        error(string.format("Failed to load scenery %d, %d", chunk.x, chunk.z))
    end
    BuildCacheDat.cache_map_scenery(param_a, param_b, data_size, data)
end

print("=== Step 4: Load Scenery Configs ===")
BuildCacheDat.load_scenery_configs()

print("=== Step 5: Queue and Load Models ===")
-- Queue all models from scenery configs
local scenery_locs = BuildCacheDat.get_all_scenery_locs()
for _, loc in ipairs(scenery_locs) do
    local model_ids = BuildCacheDat.get_scenery_model_ids(loc.loc_id)
    for _, model_id in ipairs(model_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end

-- Add guard models
for _, model_id in ipairs(GUARD_MODELS) do
    add_unique(queued_model_ids, model_id)
end

print(string.format("Loading %d models...", #queued_model_ids))
for _, model_id in ipairs(queued_model_ids) do
    local promise = HostIO.dat_models_load(model_id)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success then
        BuildCacheDat.cache_model(model_id, data_size, data)
    end
end

print("=== Step 6: Load Textures ===")
local textures_promise = HostIO.dat_config_texture_sprites_load()
success, param_a, param_b, data_size, data = HostIOUtils.await(textures_promise)
if not success then error("Failed to load textures") end
BuildCacheDat.load_textures(data_size, data)

print("=== Step 7: Load Sequences ===")
BuildCacheDat.load_sequences()

print("=== Step 8: Load Animation Frame Index ===")
local animbaseframes_count = BuildCacheDat.get_animbaseframes_count()

print("=== Step 9: Load Animation Base Frames ===")
print(string.format("Loading %d animation base frames...", animbaseframes_count))
for i = 0, animbaseframes_count - 1 do
    local promise = HostIO.dat_animbaseframes_load(i)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success and data_size > 0 then
        BuildCacheDat.cache_animbaseframes(i, data_size, data)
    end
end

print("=== Step 10: Load Sounds ===")
-- Skipped for now

print("=== Step 11: Load Media ===")
local media_promise = HostIO.dat_config_media_load()
success, param_a, param_b, data_size, data = HostIOUtils.await(media_promise)
if not success then error("Failed to load media") end
BuildCacheDat.load_media(data_size, data)

print("=== Step 12: Load Title/Fonts ===")
local title_promise = HostIO.dat_config_title_load()
success, param_a, param_b, data_size, data = HostIOUtils.await(title_promise)
if not success then error("Failed to load title") end
BuildCacheDat.load_title(data_size, data)

print("=== Step 13: Load IDKits ===")
BuildCacheDat.load_idkits()

print("=== Step 14: Load Objects ===")
BuildCacheDat.load_objects()

print("=== Scene Loading Complete ===")
BuildCacheDat.finalize_scene(MAP_SW_X, MAP_SW_Z, MAP_NE_X, MAP_NE_Z)

