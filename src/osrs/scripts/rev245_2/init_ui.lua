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

    local ui_archives = CacheDat.load_config_files({
        "rev_245_2/rev_245_2_ui.ini",
        "rev_245_2/rev_245_2_cache.ini",
    })

    local ui_config = ui_archives[1]
    local ui_cache_config = ui_archives[2]

    print("ui_config", ui_config)
    print("ui_cache_config", ui_cache_config)

    Game.ui_load_revconfig(ui_config, ui_cache_config)
    Game.ui_load_fonts(ui_cache_config)
    Game.ui_load_rs_components()
end

init_ui()
