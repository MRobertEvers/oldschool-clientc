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
end

init_ui()
