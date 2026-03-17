local CacheDat = require("cachedat")

local function print_table(tbl)
    for k, v in pairs(tbl) do
        print(k, v)
    end
end


local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end


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


local function init_cache_dat(wx_sw, wz_sw, wx_ne, wz_ne)
    local config_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS)
    local versionlist_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_VERSION_LIST)

    Game.buildcachedat_set_config_jagfile(config_ptr)
    Game.buildcachedat_set_versionlist_jagfile(versionlist_ptr)

    print("=== Loading Map Data ===")
    local chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

    for _, chunk in ipairs(chunks) do
        if not Game.buildcachedat_has_map_terrain(chunk.x, chunk.z) then
            local map_id = (chunk.x << 16) | (chunk.z)
            local terrain_ptr = CacheDat.load_archive(
                CacheDat.Tables.CACHE_DAT_MAPS, map_id,
                CacheDat.ArchiveIdFlags.MAP_TERRAIN)
            Game.buildcachedat_cache_map_terrain(terrain_ptr, map_id)
        end
    end

    for _, chunk in ipairs(chunks) do
        if not Game.buildcachedat_has_map_scenery(chunk.x, chunk.z) then
            local map_id = (chunk.x << 16) | (chunk.z)
            local scenery_ptr = CacheDat.load_archive(
                CacheDat.Tables.CACHE_DAT_MAPS, map_id,
                CacheDat.ArchiveIdFlags.MAP_SCENERY)
            Game.buildcachedat_cache_map_scenery(scenery_ptr, map_id)
        end
    end

    print("=== Loading Config ===")
    Game.buildcachedat_init_floortypes_from_config_jagfile()
    Game.buildcachedat_init_scenery_configs_from_config_jagfile()
    Game.buildcachedat_init_idkits_from_config_jagfile()
    Game.buildcachedat_init_objects_from_config_jagfile()
    Game.buildcachedat_init_sequences_from_config_jagfile()

    print("=== Loading Models ===")
    local models_to_load = {}
    local scenery_locs = Game.buildcachedat_get_all_scenery_locs()
    print("scenery_locs", scenery_locs)
    for _, loc in ipairs(scenery_locs) do
        local loc_id, chunk_x, chunk_z = table.unpack(loc)
        local model_ids = Game.buildcachedat_get_scenery_model_ids(loc_id)
        for _, model_id in ipairs(model_ids) do
            add_unique(models_to_load, model_id)
        end
    end

    for _, model_id in ipairs(models_to_load) do
        if not Game.buildcachedat_has_model(model_id) then
            local model_ptr = CacheDat.load_archive(
                CacheDat.Tables.CACHE_DAT_MODELS,
                model_id)
            Game.buildcachedat_cache_model(model_ptr, model_id)
        end
    end

    print("=== Loading Textures ===")
    local texture_sprites_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_TEXTURES)
    Game.buildcachedat_cache_textures(texture_sprites_ptr)
    Game.dash_load_textures()

    print("=== Loading Animations ===")
    local animbaseframes_count = Game.buildcachedat_get_animbaseframes_count_from_versionlist_jagfile()

    for i = 0, animbaseframes_count - 1 do
        if not Game.buildcachedat_has_animbaseframes(i) then
            local animbase_ptr = CacheDat.load_archive(
                CacheDat.Tables.CACHE_DAT_ANIMATIONS,
                i)
            Game.buildcachedat_cache_animbaseframes(animbase_ptr, i)
        end
    end

    Game.game_build_scene(50, 50, 51, 51)
end

init_cache_dat(50 * 64, 50 * 64, 52 * 64, 52 * 64)
