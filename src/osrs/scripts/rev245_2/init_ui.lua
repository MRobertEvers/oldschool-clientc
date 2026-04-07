local CacheDat = require("cachedat")


local function print_table(t)
    for k, v in pairs(t) do
        print(k, v)
    end
end

local function init_ui()
    local media_jagfile = CacheDat.load_archive(CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_MEDIA_2D_GRAPHICS, 0)
    Game.buildcachedat_set_2d_media_jagfile(media_jagfile)

    local title_jagfile = CacheDat.load_archive(CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_TITLE_AND_FONTS, 0)
    Game.buildcachedat_cache_title(title_jagfile)

    local interfaces_archive = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_INTERFACES, 0)
    Game.game_load_interfaces(interfaces_archive)
    Game.game_load_component_sprites()
    _G.interfaces_loaded_flag = true

    -- Interface MODEL components (modelType 1) need raw models in BuildCacheDat.
    local model_ids = Game.game_get_interface_model_ids()
    local model_requests = {}
    local models_needed = {}
    for _, model_id in ipairs(model_ids) do
        if not Game.buildcachedat_has_model(model_id) then
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
        for i, _ in ipairs(models_needed) do
            Game.buildcachedat_cache_model(model_archives[i], models_needed[i])
        end
    end

    -- Obj configs needed for inventory icon generation.
    local config_jagfile = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS, 0)
    Game.buildcachedat_set_config_jagfile(config_jagfile)
    Game.buildcachedat_init_objects_from_config_jagfile()

    -- Step 1: Parse INI config files (stores RevConfigBuffer on game).
    local ui_archives = CacheDat.load_config_files({
        "rev_245_2/rev_245_2_ui.ini",
        "rev_245_2/rev_245_2_cache.ini",
    })
    local ui_config = ui_archives[1]
    local ui_cache_config = ui_archives[2]
    Game.ui_parse_revconfig(ui_config, ui_cache_config)

    -- Step 2: Get inventory obj IDs from parsed config.
    local inv_obj_ids = Game.ui_get_revconfig_inv_obj_ids()

    -- Step 3: Load models for inventory objects (Lua cache round-trip).
    do
        local seen = {}
        local inv_model_requests = {}
        local inv_models_needed = {}
        print("inv_obj_ids", #inv_obj_ids)
        print_table(inv_obj_ids)
        for _, oid in ipairs(inv_obj_ids) do
            local mids = Game.buildcachedat_get_obj_model_ids(oid)
            for _, mid in ipairs(mids) do
                if mid ~= 0 and not seen[mid] then
                    seen[mid] = true
                    if not Game.buildcachedat_has_model(mid) then
                        table.insert(inv_model_requests, {
                            table_id = CacheDat.Tables.CACHE_DAT_MODELS,
                            archive_id = mid,
                            flags = 0,
                        })
                        table.insert(inv_models_needed, mid)
                    end
                end
            end
        end
        if #inv_model_requests > 0 then
            local inv_archives = CacheDat.load_archives(inv_model_requests)
            for i, mid in ipairs(inv_models_needed) do
                Game.buildcachedat_cache_model(inv_archives[i], mid)
            end
        end
    end

    -- Step 4: Pass 1 - load inventories (sprites from now-cached models).
    Game.ui_load_revconfig_inventories()

    -- Step 5: Pass 2 - load rest of UI (sprites, components, layouts, RS resolution).
    Game.ui_load_revconfig_ui()

    -- Step 6: Fonts.
    Game.ui_load_fonts(ui_cache_config)
end

init_ui()
