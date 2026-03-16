local CacheDat = require("cachedat")

local function init_cache_dat()
    local config_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS)
    local versionlist_ptr = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_VERSION_LIST)

    local result = Game:multiply(10, 5)
    print(result)
end

init_cache_dat()
