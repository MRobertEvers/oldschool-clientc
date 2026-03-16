local CacheDat = require("cachedat")

local function init_cache_dat()
    print("init_cache_dat")
    local config_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS)
    print("config_ptr", config_ptr)
    local versionlist_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_VERSION_LIST)
    print("versionlist_ptr", versionlist_ptr)

    local result = Game.multiply(10, 5)
    print(result)
end

init_cache_dat()
