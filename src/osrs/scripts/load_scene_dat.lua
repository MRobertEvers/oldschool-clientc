local HostIOUtils = require("hostio_utils")


-- Build chunk list from world bounds (same coordinate convention as scenebuilder_load_from_buildcachedat)
-- wx_sw, wz_sw, wx_ne, wz_ne = world tile coords; 1 map chunk = 64 world tiles
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

-- Helper: Add unique element to table
local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end

-- Load scene data for the given world bounds (same args as scenebuilder_load_from_buildcachedat).
-- coro = coroutine thread (for scripts that use coroutine.yield etc.)
-- wx_sw, wz_sw, wx_ne, wz_ne = world tile coords (south-west and north-east)
-- size_x, size_z = scene size in tiles (e.g. 104, 104)
function load_scene_dat(wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z)
    local chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

    local queued_model_ids = {}

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
    local terrain_req_ids = {}
    for _, chunk in ipairs(chunks) do
        terrain_req_ids[#terrain_req_ids + 1] = HostIO.dat_map_terrain_load(chunk.x, chunk.z)
    end
    local terrain_results = HostIOUtils.await_all(terrain_req_ids)
    for i, r in ipairs(terrain_results) do
        success, param_a, param_b, data_size, data = r[1], r[2], r[3], r[4], r[5]
        if not success then
            error(string.format("Failed to load terrain %d, %d", chunks[i].x, chunks[i].z))
        end
        BuildCacheDat.cache_map_terrain(param_a, param_b, data_size, data)
    end

    print("=== Step 2: Load Floor Types ===")
    BuildCacheDat.init_floortypes_from_config_jagfile()

    print("=== Step 3: Load Scenery ===")
    local scenery_req_ids = {}
    for _, chunk in ipairs(chunks) do
        scenery_req_ids[#scenery_req_ids + 1] = HostIO.dat_map_scenery_load(chunk.x, chunk.z)
    end
    local scenery_results = HostIOUtils.await_all(scenery_req_ids)
    for i, r in ipairs(scenery_results) do
        success, param_a, param_b, data_size, data = r[1], r[2], r[3], r[4], r[5]
        if not success then
            error(string.format("Failed to load scenery %d, %d", chunks[i].x, chunks[i].z))
        end
        BuildCacheDat.cache_map_scenery(param_a, param_b, data_size, data)
    end

    print("=== Step 4: Load Scenery Configs ===")
    BuildCacheDat.init_scenery_configs_from_config_jagfile()

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

    print(string.format("Loading %d models...", #queued_model_ids))
    local model_req_ids = {}
    for _, model_id in ipairs(queued_model_ids) do
        model_req_ids[#model_req_ids + 1] = HostIO.dat_models_load(model_id)
    end
    local model_results = HostIOUtils.await_all(model_req_ids)
    for i, r in ipairs(model_results) do
        success, param_a, param_b, data_size, data = r[1], r[2], r[3], r[4], r[5]
        if success then
            BuildCacheDat.cache_model(queued_model_ids[i], data_size, data)
        end
    end

    print("=== Step 6: Load Textures ===")
    local textures_promise = HostIO.dat_config_texture_sprites_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(textures_promise)
    if not success then error("Failed to load textures") end
    BuildCacheDat.cache_textures(data_size, data)

    print("=== Step 7: Load Sequences ===")
    BuildCacheDat.init_sequences_from_config_jagfile()

    print("=== Step 8: Load Animation Frame Index ===")
    local animbaseframes_count = BuildCacheDat.get_animbaseframes_count_from_versionlist_jagfile()

    print("=== Step 9: Load Animation Base Frames ===")
    print(string.format("Loading %d animation base frames...", animbaseframes_count))
    local animbase_req_ids = {}
    for i = 0, animbaseframes_count - 1 do
        animbase_req_ids[#animbase_req_ids + 1] = HostIO.dat_animbaseframes_load(i)
    end
    local animbase_results = HostIOUtils.await_all(animbase_req_ids)
    for i, r in ipairs(animbase_results) do
        success, param_a, param_b, data_size, data = r[1], r[2], r[3], r[4], r[5]
        if success and data_size > 0 then
            BuildCacheDat.cache_animbaseframes(i - 1, data_size, data)
        end
    end

    print("=== Step 10: Load Sounds ===")
    -- Skipped for now

    print("=== Step 11: Load Media ===")
    local media_promise = HostIO.dat_config_media_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(media_promise)
    if not success then error("Failed to load media") end
    BuildCacheDat.cache_media(data_size, data)

    print("=== Step 12: Load Title/Fonts ===")
    local title_promise = HostIO.dat_config_title_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(title_promise)
    if not success then error("Failed to load title") end
    BuildCacheDat.cache_title(data_size, data)

    print("=== Step 13: Load IDKits ===")
    BuildCacheDat.init_idkits_from_config_jagfile()

    print("=== Step 14: Load Objects ===")
    BuildCacheDat.init_objects_from_config_jagfile()

    print("=== Scene Loading Complete ===")
    BuildCacheDat.finalize_scene(map_sw_x, map_sw_z, map_ne_x, map_ne_z)
end

-- When run as main script (not required), load with default bounds
if not debug.getinfo(2) then
    load_scene_dat(50 * 64, 50 * 64, 51 * 64, 51 * 64, 104, 104)
end

return load_scene_dat

