local CacheDat = require("cachedat")
local CacheIO = require("cacheio")

local cacheio_requests = Game.CacheIO.request_list_new()


local function print_table(t)
    for k, v in pairs(t) do
        print(k, v)
    end
end

local function init_ui()
    do
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.config_archive_fetch(cacheio_requests, CacheDat.ConfigDatKind.CONFIG_DAT_MEDIA_2D_GRAPHICS)
        Game.BuildCacheDat.config_archive_fetch(cacheio_requests, CacheDat.ConfigDatKind.CONFIG_DAT_TITLE_AND_FONTS)
        Game.BuildCacheDat.config_archive_fetch(cacheio_requests, CacheDat.ConfigDatKind.CONFIG_DAT_INTERFACES)
        local archives = CacheIO.load_request_list(cacheio_requests)
        Game.BuildCacheDat.set_2d_media_jagfile(archives[1])
        Game.BuildCacheDat.cache_title(archives[2])
        Game.Game.load_interfaces(archives[3])
    end
    Game.Game.load_component_sprites()
    _G.interfaces_loaded_flag = true

    -- Interface MODEL components (modelType 1) need raw models in BuildCacheDat.
    local model_ids = Game.Game.get_interface_model_ids()
    local models_needed = {}
    for _, model_id in ipairs(model_ids) do
        if not Game.BuildCacheDat.model_cache_has(model_id) then
            table.insert(models_needed, model_id)
        end
    end
    if #models_needed > 0 then
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.models_fetch(cacheio_requests, models_needed)
        local model_archives = CacheIO.load_request_list(cacheio_requests)
        for i, mid in ipairs(models_needed) do
            Game.BuildCacheDat.model_cache_add(model_archives[i], mid)
        end
    end

    -- Obj configs needed for inventory icon generation.
    do
        Game.CacheIO.request_list_reset(cacheio_requests)
        Game.BuildCacheDat.config_archive_fetch(cacheio_requests, CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS)
        local a = CacheIO.load_request_list(cacheio_requests)
        Game.BuildCacheDat.set_config_jagfile(a[1])
    end
    Game.BuildCacheDat.objects_init_from_config_jagfile()

    -- Step 1: Parse INI config files (stores RevConfigBuffer on game).
    local ui_archives = CacheDat.load_config_files({
        "rev_245_2/rev_245_2_ui.ini",
        "rev_245_2/rev_245_2_cache.ini",
    })
    local ui_config = ui_archives[1]
    local ui_cache_config = ui_archives[2]
    Game.UI.parse_revconfig(ui_config, ui_cache_config)

    -- Step 2: Get inventory obj IDs from parsed config.
    local inv_obj_ids = Game.UI.get_revconfig_inv_obj_ids()

    -- Step 3: Load models for inventory objects (Lua cache round-trip).
    do
        local seen = {}
        local inv_models_needed = {}
        for _, oid in ipairs(inv_obj_ids) do
            local mids = Game.BuildCacheDat.get_obj_model_ids(oid)
            for _, mid in ipairs(mids) do
                if mid ~= 0 and not seen[mid] then
                    seen[mid] = true
                    if not Game.BuildCacheDat.model_cache_has(mid) then
                        table.insert(inv_models_needed, mid)
                    end
                end
            end
        end
        if #inv_models_needed > 0 then
            Game.CacheIO.request_list_reset(cacheio_requests)
            Game.BuildCacheDat.models_fetch(cacheio_requests, inv_models_needed)
            local inv_archives = CacheIO.load_request_list(cacheio_requests)
            for i, mid in ipairs(inv_models_needed) do
                Game.BuildCacheDat.model_cache_add(inv_archives[i], mid)
            end
        end
    end

    -- Step 4: Pass 1 - load inventories (sprites from now-cached models).
    Game.UI.load_revconfig_inventories()

    -- Step 5: Pass 2 - load rest of UI (sprites, components, layouts, RS resolution).
    Game.UI.load_revconfig_ui()

    -- Step 6: Fonts.
    Game.UI.load_fonts(ui_cache_config)

    -- Interface defs are baked into the uitree; free thousands of decoded components from RAM.
    Game.BuildCacheDat.component_cache_clear()
    Game.BuildCacheDat.model_cache_clear()
end

init_ui()
