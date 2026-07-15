---@diagnostic disable: undefined-global
local CacheDat = require("cachedat")

local world_x, world_z, world_level, model_id, seq_id = ...

if not Game.BuildCacheDat.has_config_jagfile() then
    local config_jagfile = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_CONFIGS,
        0)
    Game.BuildCacheDat.set_config_jagfile(config_jagfile)
end

if not Game.BuildCacheDat.has_versionlist_jagfile() then
    local versionlist_jagfile = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_CONFIGS,
        CacheDat.ConfigDatKind.CONFIG_DAT_VERSION_LIST,
        0)
    Game.BuildCacheDat.set_versionlist_jagfile(versionlist_jagfile)
end

Game.BuildCacheDat.sequences_clear()
Game.BuildCacheDat.sequences_init_from_config_jagfile()

local animbaseframes_count = Game.BuildCacheDat.get_animbaseframes_count_from_versionlist_jagfile()
local anim_requests = {}
for i = 0, animbaseframes_count - 1 do
    if not Game.BuildCacheDat.animbaseframes_cache_has(i) then
        table.insert(anim_requests, {
            table_id = CacheDat.Tables.CACHE_DAT_ANIMATIONS,
            archive_id = i,
            flags = 0,
        })
    end
end

if #anim_requests > 0 then
    local anim_archives = CacheDat.load_archives(anim_requests)
    for _, archive in ipairs(anim_archives) do
        Game.BuildCacheDat.animbaseframes_cache_add(archive)
    end
end

if not Game.BuildCacheDat.model_cache_has(model_id) then
    local model_archive = CacheDat.load_archive(
        CacheDat.Tables.CACHE_DAT_MODELS,
        model_id,
        0)
    Game.BuildCacheDat.model_cache_add(model_archive, model_id)
end

Game.Game.spawn_element(world_x, world_z, world_level, model_id, seq_id)
