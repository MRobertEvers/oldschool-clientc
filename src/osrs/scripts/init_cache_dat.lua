local CacheIO = require("cacheio")

local cacheio_requests = Game.CacheIO.request_list_new()

local function safe_gc()
    -- collectgarbage("collect") is a no-op on Fengari (JS Lua); silently ignore
    pcall(collectgarbage, "collect")
end

local function print_heap(label)
    local mb = Game.Game.get_heap_usage_mb()
    print(string.format("[heap] %-48s %.1f MB", label, mb))
end

-- Compute the chunk range that world_rebuild_centerzone_begin / game_build_scene_centerzone
-- will use, matching the C formula: zone_padding = scene_size / 16, chunk = zone / 8.
local function zone_to_chunk_range(zone_center_x, zone_center_z, scene_size)
    local zone_padding = math.floor(scene_size / (2 * 8))
    local zone_sw_x = zone_center_x - zone_padding
    local zone_sw_z = zone_center_z - zone_padding
    local zone_ne_x = zone_center_x + zone_padding
    local zone_ne_z = zone_center_z + zone_padding
    return
        math.floor(zone_sw_x / 8),
        math.floor(zone_sw_z / 8),
        math.floor(zone_ne_x / 8),
        math.floor(zone_ne_z / 8)
end

-- Load terrain and scenery for a single chunk into buildcachedat.
local function load_chunk_map_scenery_and_terrain(mapx, mapz)
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.map_terrain_fetch(cacheio_requests, mapx, mapz)
    Game.BuildCacheDat.map_scenery_fetch(cacheio_requests, mapx, mapz)
    local archives = CacheIO.load_request_list(cacheio_requests)

    local idx = 1
    local map_id = (mapx << 16) | (mapz)
    if archives[idx] then
        Game.BuildCacheDat.map_terrain_cache_add(archives[idx], map_id)
        idx = idx + 1
    end
    if archives[idx] then
        Game.BuildCacheDat.map_scenery_cache_add(archives[idx], map_id)
    end
end

-- Load models for all scenery currently cached in buildcachedat.
local function load_scenery_models_mapchunk(mapx, mapz)
    local models_to_load = Game.BuildCacheDat.scenery_config_get_model_ids_mapchunk(mapx, mapz)

    local models_needed = {}
    for _, model_id in ipairs(models_to_load) do
        if not Game.BuildCacheDat.model_cache_has(model_id) then
            table.insert(models_needed, model_id)
        end
    end
    if #models_needed > 0 then
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.models_fetch(cacheio_requests, models_needed)
        local model_archives = CacheIO.load_request_list(cacheio_requests)
        for i, model_id in ipairs(models_needed) do
            Game.BuildCacheDat.model_cache_add(model_archives[i], model_id)
        end
    end
end

local function load_scenery_configs_mapchunk(mapx, mapz)
    Game.BuildCacheDat.scenery_config_load_mapchunk_from_config_jagfile(mapx, mapz)
end

local function load_scenery_models()
    local models_to_load = Game.BuildCacheDat.get_all_unique_scenery_model_ids()
    local models_needed = {}
    for _, model_id in ipairs(models_to_load) do
        if not Game.BuildCacheDat.model_cache_has(model_id) then
            table.insert(models_needed, model_id)
        end
    end
    if #models_needed > 0 then
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.models_fetch(cacheio_requests, models_needed)
        local model_archives = CacheIO.load_request_list(cacheio_requests)
        for i, model_id in ipairs(models_needed) do
            Game.BuildCacheDat.model_cache_add(model_archives[i], model_id)
        end
    end
end
-- ---------------------------------------------------------------------------
-- world_rebuild_centerzone_slow
--
-- Incremental build: loads and processes one map chunk at a time, releasing
-- terrain, scenery and model assets from buildcachedat between chunks.
-- Requires the config jagfile to be set before calling (used by
-- init_scenery_configs per-chunk); the jagfile is cleared on return.
-- ---------------------------------------------------------------------------
local function world_rebuild_centerzone_slow(zone_center_x, zone_center_z, scene_size)
    local chunk_sw_x, chunk_sw_z, chunk_ne_x, chunk_ne_z =
        zone_to_chunk_range(zone_center_x, zone_center_z, scene_size)

    -- Sequences are stored in an continuous blob and cannot be decoded by id.
    Game.BuildCacheDat.sequences_init_from_config_jagfile()


    print(string.format(
        "[slow] zone=(%d,%d) size=%d  chunks x:[%d,%d] z:[%d,%d]",
        zone_center_x, zone_center_z, scene_size,
        chunk_sw_x, chunk_ne_x, chunk_sw_z, chunk_ne_z))

    Game.Game.rebuild_centerzone_begin(zone_center_x, zone_center_z, scene_size)
    print_heap("after rebuild_centerzone_begin")

    for mapx = chunk_sw_x, chunk_ne_x do
        for mapz = chunk_sw_z, chunk_ne_z do
            print(string.format("  chunk (%d, %d)", mapx, mapz))

            load_chunk_map_scenery_and_terrain(mapx, mapz)

            load_scenery_configs_mapchunk(mapx, mapz)
            load_scenery_models_mapchunk(mapx, mapz)

            safe_gc()
            print_heap("after animations + GC")

            Game.Game.rebuild_centerzone_chunk(mapx, mapz)

            Game.BuildCacheDat.model_cache_clear()
            Game.BuildCacheDat.map_terrain_cache_clear()
            Game.BuildCacheDat.map_scenery_cache_clear()

            safe_gc()
            print_heap(string.format("  after chunk (%d,%d)", mapx, mapz))
        end
    end

    -- Config jagfile is no longer needed once all chunks have been processed.
    Game.BuildCacheDat.clear_config_jagfile()
    Game.BuildCacheDat.clear_versionlist_jagfile()
    Game.BuildCacheDat.clear_media_jagfile()
    safe_gc()
    print_heap("after chunk loop + clear config jagfile")

    Game.Game.rebuild_centerzone_end()
    print_heap("after rebuild_centerzone_end (includes buildcachedat_clear)")
end

-- ---------------------------------------------------------------------------
-- world_rebuild_centerzone
--
-- Bulk build: loads all chunks at once, then builds the scene in one call.
-- Mirrors the original game_build_scene_centerzone approach.
-- Requires the config jagfile to be set before calling; clears it on return.
-- ---------------------------------------------------------------------------
local function world_rebuild_centerzone(zone_center_x, zone_center_z, scene_size)
    local chunk_sw_x, chunk_sw_z, chunk_ne_x, chunk_ne_z =
        zone_to_chunk_range(zone_center_x, zone_center_z, scene_size)

    print(string.format(
        "[bulk]  zone=(%d,%d) size=%d  chunks x:[%d,%d] z:[%d,%d]",
        zone_center_x, zone_center_z, scene_size,
        chunk_sw_x, chunk_ne_x, chunk_sw_z, chunk_ne_z))

    -- Load all terrain and scenery upfront.
    for mapx = chunk_sw_x, chunk_ne_x do
        for mapz = chunk_sw_z, chunk_ne_z do
            load_chunk_map_scenery_and_terrain(mapx, mapz)
        end
    end
    safe_gc()
    print_heap("after bulk map data load")

    -- All scenery is now cached; decode config_locs for every loc at once.
    Game.BuildCacheDat.init_scenery_configs_from_config_jagfile()
    Game.BuildCacheDat.clear_config_jagfile()
    safe_gc()
    print_heap("after scenery config init + clear config jagfile")

    load_scenery_models()
    safe_gc()
    print_heap("after bulk model load")

    -- Build the scene in one monolithic C call.
    Game.Game.build_scene_centerzone(zone_center_x, zone_center_z, scene_size)
    print_heap("after game_build_scene_centerzone (includes buildcachedat_clear)")
end

local function global_load_textures()
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.textures_fetch(cacheio_requests)
    local archives = CacheIO.load_request_list(cacheio_requests)
    local texture_sprites_ptr = archives[1]
    Game.BuildCacheDat.cache_textures(texture_sprites_ptr)
    Game.Dash.load_textures()
end

local function global_load_animations()
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.animbaseframes_fetch_uncached(cacheio_requests)
    local anim_archives = CacheIO.load_request_list(cacheio_requests)
    for _, archive in ipairs(anim_archives) do
        Game.BuildCacheDat.animbaseframes_cache_add(archive)
    end
end



-- ---------------------------------------------------------------------------
-- init_cache_dat
-- ---------------------------------------------------------------------------
local function init_cache_dat(wx_sw, wz_sw, wx_ne, wz_ne)
    print_heap("init_cache_dat start")

    -- Load jagfiles.
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.config_jagfiles_fetch(cacheio_requests)
    local config_archives = CacheIO.load_request_list(cacheio_requests)
    -- config_jagfiles_fetch only queues missing jagfiles (same order as C pushes).
    -- init_ui may already have set the config jagfile; then the sole returned
    -- archive is the versionlist — do not assign it as config (spawn_element.lua).
    local ci = 1
    if not Game.BuildCacheDat.has_config_jagfile() and config_archives[ci] then
        Game.BuildCacheDat.set_config_jagfile(config_archives[ci])
        ci = ci + 1
    end
    if not Game.BuildCacheDat.has_versionlist_jagfile() and config_archives[ci] then
        Game.BuildCacheDat.set_versionlist_jagfile(config_archives[ci])
    end
    print_heap("after jagfile setup")

    print("=== Loading Textures ===")
    global_load_textures()

    print("=== Loading Animations ===")
    global_load_animations()

    print("=== Loading Config (non-scenery tables) ===")
    Game.BuildCacheDat.floortypes_init_from_config_jagfile()
    safe_gc()

    print_heap("after non-scenery config init")

    -- Derive zone center from map_sw (matches buildcachedat_loader_finalize_scene formula).
    local map_sw_x = math.floor(wx_sw / 64)
    local map_sw_z = math.floor(wz_sw / 64)
    local SCENE_SIZE = 104
    local zone_center_x = map_sw_x * 8 + 12
    local zone_center_z = map_sw_z * 8 + 12

    print("=== Building Scene ===")
    world_rebuild_centerzone_slow(zone_center_x, zone_center_z, SCENE_SIZE)

    print("=== Scene Built ===")
end

init_cache_dat(49 * 64, 49 * 64, 52 * 64, 52 * 64)
