local CacheDat = require("cachedat")

local item = ...

if not _G.interfaces_loaded_flag then
    local archive = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_INTERFACES, 0)
    Game.Game.load_interfaces(archive)
    Game.Game.load_component_sprites()
    _G.interfaces_loaded_flag = true
end

Game.Game.exec_pkt_if_settab(item)
