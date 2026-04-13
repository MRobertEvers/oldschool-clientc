local CacheDat = require("cachedat")

local function safe_gc()
    -- collectgarbage("collect") is a no-op on Fengari (JS Lua); silently ignore
    pcall(collectgarbage, "collect")
end

local function print_heap(label)
    local mb = Game.game_get_heap_usage_mb()
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
local function load_chunk_map_data(mapx, mapz)
    if not Game.buildcachedat_has_map_terrain(mapx, mapz) then
        local map_id = (mapx << 16) | (mapz)
        local terrain_archive = CacheDat.load_archive(
            CacheDat.Tables.CACHE_DAT_MAPS, map_id,
            CacheDat.ArchiveIdFlags.MAP_TERRAIN)
        Game.buildcachedat_cache_map_terrain(terrain_archive, map_id)
    end

    if not Game.buildcachedat_has_map_scenery(mapx, mapz) then
        local map_id = (mapx << 16) | (mapz)
        local scenery_archive = CacheDat.load_archive(
            CacheDat.Tables.CACHE_DAT_MAPS, map_id,
            CacheDat.ArchiveIdFlags.MAP_SCENERY)
        Game.buildcachedat_cache_map_scenery(scenery_archive, map_id)
    end
end

-- Load models for all scenery currently cached in buildcachedat.
local function load_scenery_models()
    local models_to_load = Game.buildcachedat_get_all_unique_scenery_model_ids()
    local model_requests = {}
    local models_needed = {}
    for _, model_id in ipairs(models_to_load) do
        if not Game.buildcachedat_has_model(model_id) then
            table.insert(model_requests,
                { table_id = CacheDat.Tables.CACHE_DAT_MODELS, archive_id = model_id, flags = 0 })
            table.insert(models_needed, model_id)
        end
    end
    if #model_requests > 0 then
        local model_archives = CacheDat.load_archives(model_requests)
        for i, model_id in ipairs(models_needed) do
            Game.buildcachedat_cache_model(model_archives[i], model_id)
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

    print(string.format(
        "[slow] zone=(%d,%d) size=%d  chunks x:[%d,%d] z:[%d,%d]",
        zone_center_x, zone_center_z, scene_size,
        chunk_sw_x, chunk_ne_x, chunk_sw_z, chunk_ne_z))

    Game.game_rebuild_centerzone_begin(zone_center_x, zone_center_z, scene_size)
    print_heap("after rebuild_centerzone_begin")

    for mapx = chunk_sw_x, chunk_ne_x do
        for mapz = chunk_sw_z, chunk_ne_z do
            print(string.format("  chunk (%d, %d)", mapx, mapz))

            load_chunk_map_data(mapx, mapz)

            -- buildcachedat_init_scenery_configs iterates the currently-cached scenery to
            -- discover which loc-ids to decode.  It must run after scenery is loaded and
            -- while the config jagfile is still set.
            Game.buildcachedat_init_scenery_configs_from_config_jagfile()

            load_scenery_models()

            -- Populates heightmap, collision, minimap, entities, blendmap, overlaymap,
            -- and shapemap for this chunk.  Clears terrain + scenery + models on return.
            Game.game_rebuild_centerzone_chunk(mapx, mapz)

            safe_gc()
            print_heap(string.format("  after chunk (%d,%d)", mapx, mapz))
        end
    end

    -- Config jagfile is no longer needed once all chunks have been processed.
    Game.buildcachedat_clear_config_jagfile()
    safe_gc()
    print_heap("after chunk loop + clear config jagfile")

    Game.game_rebuild_centerzone_end()
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
            load_chunk_map_data(mapx, mapz)
        end
    end
    safe_gc()
    print_heap("after bulk map data load")

    -- All scenery is now cached; decode config_locs for every loc at once.
    Game.buildcachedat_init_scenery_configs_from_config_jagfile()
    Game.buildcachedat_clear_config_jagfile()
    safe_gc()
    print_heap("after scenery config init + clear config jagfile")

    load_scenery_models()
    safe_gc()
    print_heap("after bulk model load")

    -- Build the scene in one monolithic C call.
    Game.game_build_scene_centerzone(zone_center_x, zone_center_z, scene_size)
    print_heap("after game_build_scene_centerzone (includes buildcachedat_clear)")
end

-- ---------------------------------------------------------------------------
-- init_cache_dat
-- ---------------------------------------------------------------------------
local function init_cache_dat(wx_sw, wz_sw, wx_ne, wz_ne)
    print_heap("init_cache_dat start")

    -- Load jagfiles.
    local config_archives = CacheDat.load_archives({
        { table_id = CacheDat.Tables.CACHE_DAT_CONFIGS, archive_id = CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS,           flags = 0 },
        { table_id = CacheDat.Tables.CACHE_DAT_CONFIGS, archive_id = CacheDat.ConfigDatKind.CONFIG_DAT_VERSION_LIST,      flags = 0 },
        { table_id = CacheDat.Tables.CACHE_DAT_CONFIGS, archive_id = CacheDat.ConfigDatKind.CONFIG_DAT_MEDIA_2D_GRAPHICS, flags = 0 },
    })
    Game.buildcachedat_set_config_jagfile(config_archives[1])
    Game.buildcachedat_set_versionlist_jagfile(config_archives[2])
    Game.buildcachedat_set_2d_media_jagfile(config_archives[3])
    config_archives = nil
    print_heap("after jagfile setup")

    -- Decode non-scenery config tables (these do not depend on cached scenery).
    -- init_scenery_configs is intentionally deferred to the build functions because
    -- it iterates the currently-cached scenery to find loc-ids to decode.
    print("=== Loading Config (non-scenery tables) ===")
    Game.buildcachedat_init_floortypes_from_config_jagfile()
    Game.buildcachedat_init_idkits_from_config_jagfile()
    Game.buildcachedat_init_sequences_from_config_jagfile()
    Game.buildcachedat_clear_media_jagfile()
    safe_gc()
    print_heap("after non-scenery config init + clear media jagfile")

    print("=== Loading Textures ===")
    local texture_sprites_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_TEXTURES)
    Game.buildcachedat_cache_textures(texture_sprites_ptr)
    Game.dash_load_textures()
    texture_sprites_ptr = nil
    print_heap("after textures")

    print("=== Loading Animations ===")
    local animbaseframes_count = Game.buildcachedat_get_animbaseframes_count_from_versionlist_jagfile()
    Game.buildcachedat_clear_versionlist_jagfile()

    local anim_requests = {}
    local anim_indices = {}
    for i = 0, animbaseframes_count - 1 do
        if not Game.buildcachedat_has_animbaseframes(i) then
            table.insert(anim_requests,
                { table_id = CacheDat.Tables.CACHE_DAT_ANIMATIONS, archive_id = i, flags = 0 })
            table.insert(anim_indices, i)
        end
    end
    if #anim_requests > 0 then
        local anim_archives = CacheDat.load_archives(anim_requests)
        for idx, anim_i in ipairs(anim_indices) do
            Game.buildcachedat_cache_animbaseframes(anim_archives[idx], anim_i)
        end
    end
    anim_requests = nil
    anim_indices = nil
    safe_gc()
    print_heap("after animations + GC")

    -- Derive zone center from map_sw (matches buildcachedat_loader_finalize_scene formula).
    local map_sw_x = math.floor(wx_sw / 64)
    local map_sw_z = math.floor(wz_sw / 64)
    local SCENE_SIZE = 104
    local zone_center_x = map_sw_x * 8 + 12
    local zone_center_z = map_sw_z * 8 + 12

    print("=== Building Scene ===")
    -- world_rebuild_centerzone_slow(zone_center_x, zone_center_z, SCENE_SIZE)
    world_rebuild_centerzone(zone_center_x, zone_center_z, SCENE_SIZE)

    print("=== Scene Built ===")
end

init_cache_dat(49 * 64, 49 * 64, 52 * 64, 52 * 64)
