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
    local config_requests = {
        { table_id = CacheDat.Tables.CACHE_DAT_CONFIGS, archive_id = CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS,      flags = 0 },
        { table_id = CacheDat.Tables.CACHE_DAT_CONFIGS, archive_id = CacheDat.ConfigDatKind.CONFIG_DAT_VERSION_LIST, flags = 0 },
    }
    local config_archives = CacheDat.load_archives(config_requests)
    print(config_archives)
    Game.buildcachedat_set_config_jagfile(config_archives[1])
    Game.buildcachedat_set_versionlist_jagfile(config_archives[2])

    print("=== Loading Map Data ===")
    local chunks, map_sw_x, map_sw_z, map_ne_x, map_ne_z = world_to_map_chunks(wx_sw, wz_sw, wx_ne, wz_ne)

    local terrain_requests = {}
    local terrain_map_ids = {}
    for _, chunk in ipairs(chunks) do
        if not Game.buildcachedat_has_map_terrain(chunk.x, chunk.z) then
            local map_id = (chunk.x << 16) | (chunk.z)
            table.insert(terrain_requests,
                {
                    table_id = CacheDat.Tables.CACHE_DAT_MAPS,
                    archive_id = map_id,
                    flags = CacheDat.ArchiveIdFlags
                        .MAP_TERRAIN
                })
            table.insert(terrain_map_ids, map_id)
        end
    end
    if #terrain_requests > 0 then
        local terrain_archives = CacheDat.load_archives(terrain_requests)
        for i, map_id in ipairs(terrain_map_ids) do
            Game.buildcachedat_cache_map_terrain(terrain_archives[i], map_id)
        end
    end

    local scenery_requests = {}
    local scenery_map_ids = {}
    for _, chunk in ipairs(chunks) do
        if not Game.buildcachedat_has_map_scenery(chunk.x, chunk.z) then
            local map_id = (chunk.x << 16) | (chunk.z)
            table.insert(scenery_requests,
                {
                    table_id = CacheDat.Tables.CACHE_DAT_MAPS,
                    archive_id = map_id,
                    flags = CacheDat.ArchiveIdFlags
                        .MAP_SCENERY
                })
            table.insert(scenery_map_ids, map_id)
        end
    end
    if #scenery_requests > 0 then
        local scenery_archives = CacheDat.load_archives(scenery_requests)
        for i, map_id in ipairs(scenery_map_ids) do
            Game.buildcachedat_cache_map_scenery(scenery_archives[i], map_id)
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
    for _, loc in ipairs(scenery_locs) do
        local loc_id, chunk_x, chunk_z = table.unpack(loc)
        local model_ids = Game.buildcachedat_get_scenery_model_ids(loc_id)
        for _, model_id in ipairs(model_ids) do
            add_unique(models_to_load, model_id)
        end
    end

    table.sort(models_to_load)
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

    print("=== Loading Textures ===")
    local texture_sprites_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_TEXTURES)
    Game.buildcachedat_cache_textures(texture_sprites_ptr)
    Game.dash_load_textures()

    print("=== Loading Animations ===")
    local animbaseframes_count = Game.buildcachedat_get_animbaseframes_count_from_versionlist_jagfile()

    local anim_requests = {}
    local anim_indices = {}
    for i = 0, animbaseframes_count - 1 do
        if not Game.buildcachedat_has_animbaseframes(i) then
            table.insert(anim_requests, { table_id = CacheDat.Tables.CACHE_DAT_ANIMATIONS, archive_id = i, flags = 0 })
            table.insert(anim_indices, i)
        end
    end
    if #anim_requests > 0 then
        local anim_archives = CacheDat.load_archives(anim_requests)
        for idx, anim_i in ipairs(anim_indices) do
            Game.buildcachedat_cache_animbaseframes(anim_archives[idx], anim_i)
        end
    end

    print("=== Building Scene ===")
    Game.game_build_scene(50, 50, 51, 51)

    print("=== Scene Built ===")
end

init_cache_dat(50 * 64, 50 * 64, 52 * 64, 52 * 64)
