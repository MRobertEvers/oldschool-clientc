-- init_scene: Lua equivalent of task_init_scene.c
-- Uses game->buildcache (BuildCache), not BuildCacheDat. Same step order and flow as C.
-- Pattern: HostIOUtils.await + HostIO.asset_* loaders + BuildCache decode-and-add.
local HostIOUtils = require("hostio_utils")

-- Build chunk list from world bounds (same as task_init_scene: map chunk coords)
-- wx_sw, wz_sw, wx_ne, wz_ne = world tile coords; 1 map chunk = 64 world tiles
local function world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)
    local map_sw_x = math.floor(wx_sw / 64)
    local map_sw_z = math.floor(wz_sw / 64)
    local map_ne_x = math.floor(wx_ne / 64)
    local map_ne_z = math.floor(wz_ne / 64)
    local chunks = {}
    local idx = 1
    for x = map_sw_x, map_ne_x do
        for z = map_sw_z, map_ne_z do
            chunks[idx] = { x = x, z = z }
            idx = idx + 1
        end
    end
    return chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z
end

-- Initialize scene using BuildCache (same flow as task_init_scene.c).
-- wx_sw, wz_sw, wx_ne, wz_ne = world tile coords; size_x, size_z = scene size in tiles (e.g. 104, 104)
function init_scene(wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z)
    local chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

    local success, param_a, param_b, data_size, data

    local twx = map_sw_x * 64
    local twz = map_sw_z * 64
    local tne_x = (map_ne_x + 1) * 64 - 1
    local tne_z = (map_ne_z + 1) * 64 - 1
    local sz_x = size_x
    local sz_z = size_z
    if sz_x > (tne_x - twx + 1) then sz_x = tne_x - twx end
    if sz_z > (tne_z - twz + 1) then sz_z = tne_z - twz end
    BuildCache.ensure(twx, twz, tne_x, tne_z, sz_x, sz_z)

    -- Step 1: Load scenery (map chunks)
    print("=== Step 1: Load Scenery ===")
    for _, chunk in ipairs(chunks) do
        local req_id = HostIO.asset_map_scenery_load(chunk.x, chunk.z)
        success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
        if not success then
            error(string.format("Failed to load scenery %d, %d", chunk.x, chunk.z))
        end
        BuildCache.add_map_scenery(chunk.x, chunk.z, param_b, data_size, data)
    end

    -- Step 2: Load scenery config (for loc ids present in scenery)
    print("=== Step 2: Load Scenery Config ===")
    local scenery_ids = BuildCache.get_scenery_ids()
    local req_id = HostIO.asset_config_scenery_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
    if not success then error("Failed to load config scenery") end
    BuildCache.add_config_scenery(data_size, data, scenery_ids)

    -- Step 3: Queue models and sequences, then load models (all in parallel)
    print("=== Step 3: Load Scenery Models ===")
    local queued = BuildCache.get_queued_models_and_sequences()
    local model_req_ids = {}
    for _, model_id in ipairs(queued.models) do
        model_req_ids[#model_req_ids + 1] = HostIO.asset_model_load(model_id)
    end
    local model_results = HostIOUtils.await_all(model_req_ids)
    for _, r in ipairs(model_results) do
        success, param_a, param_b, data_size, data = r[1], r[2], r[3], r[4], r[5]
        if success then
            BuildCache.add_model(param_b, data_size, data)
        end
    end

    -- Step 4: Load terrain
    print("=== Step 4: Load Terrain ===")
    for _, chunk in ipairs(chunks) do
        req_id = HostIO.asset_map_terrain_load(chunk.x, chunk.z)
        success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
        if not success then
            error(string.format("Failed to load terrain %d, %d", chunk.x, chunk.z))
        end
        BuildCache.add_map_terrain(chunk.x, chunk.z, data_size, data)
    end

    -- Step 5: Load underlay
    print("=== Step 5: Load Underlay ===")
    req_id = HostIO.asset_config_underlay_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
    if not success then error("Failed to load config underlay") end
    BuildCache.add_config_underlay(data_size, data)

    -- Step 6: Load overlay
    print("=== Step 6: Load Overlay ===")
    req_id = HostIO.asset_config_overlay_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
    if not success then error("Failed to load config overlay") end
    BuildCache.add_config_overlay(data_size, data)

    -- Step 7: Load texture definitions (and queue texture ids from overlays/models/scenery)
    print("=== Step 7: Load Textures ===")
    local texture_ids = BuildCache.get_queued_texture_ids()
    req_id = HostIO.asset_texture_definitions_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
    if not success then error("Failed to load texture definitions") end
    BuildCache.add_texture_definitions(data_size, data, texture_ids)

    -- Step 8: Load spritepacks
    print("=== Step 8: Load Spritepacks ===")
    local spritepack_ids = BuildCache.get_queued_spritepack_ids()
    for _, sid in ipairs(spritepack_ids) do
        req_id = HostIO.asset_spritepack_load(sid)
        success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
        if success then
            BuildCache.add_spritepack(param_b, data_size, data)
        end
    end

    -- Step 9: Build textures (from definitions + spritepacks)
    print("=== Step 9: Build Textures ===")
    BuildCache.build_textures()

    -- Step 10: Load sequences config
    print("=== Step 10: Load Sequences ===")
    local sequence_ids = queued.sequences
    req_id = HostIO.asset_config_sequences_load()
    success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
    if not success then error("Failed to load config sequences") end
    BuildCache.add_config_sequences(data_size, data, sequence_ids)

    -- Step 11: Load frame blobs (animation archives)
    print("=== Step 11: Load Frames ===")
    local frame_archive_ids = BuildCache.get_queued_frame_archive_ids()
    for _, fid in ipairs(frame_archive_ids) do
        req_id = HostIO.asset_animation_load(fid)
        success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
        if success then
            BuildCache.add_frame_blob(param_b, data_size, data)
        end
    end

    -- Step 12: Load framemaps
    print("=== Step 12: Load Framemaps ===")
    local framemap_ids = BuildCache.get_queued_framemap_ids()
    for _, fmid in ipairs(framemap_ids) do
        req_id = HostIO.asset_framemap_load(fmid)
        success, param_a, param_b, data_size, data = HostIOUtils.await(req_id)
        if success then
            BuildCache.add_framemap(param_b, data_size, data)
        end
    end
    BuildCache.build_frame_anims()

    -- Step 13: Build world 3D from buildcache
    print("=== Step 13: Build World 3D ===")
    BuildCache.build_scene(twx, twz, tne_x, tne_z, sz_x, sz_z)

    print("=== Init Scene Complete ===")
end

if not debug.getinfo(2) then
    init_scene(50 * 64, 50 * 64, 51 * 64, 51 * 64, 104, 104)
end

return init_scene
