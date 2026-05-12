---@diagnostic disable: undefined-global
local CacheIO = require("cacheio")

local cacheio_requests = Game.CacheIO.request_list_new()

local world_x, world_z, world_level, model_id, seq_id = ...

do
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.config_jagfiles_fetch(cacheio_requests)
    local archives = CacheIO.load_request_list(cacheio_requests)
    local i = 1
    if not Game.BuildCacheDat.has_config_jagfile() and archives[i] then
        Game.BuildCacheDat.set_config_jagfile(archives[i])
        i = i + 1
    end
    if not Game.BuildCacheDat.has_versionlist_jagfile() and archives[i] then
        Game.BuildCacheDat.set_versionlist_jagfile(archives[i])
    end
end

Game.BuildCacheDat.sequences_clear()
Game.BuildCacheDat.sequences_init_from_config_jagfile()

do
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.animbaseframes_fetch_uncached(cacheio_requests)
    local anim_archives = CacheIO.load_request_list(cacheio_requests)
    for _, archive in ipairs(anim_archives) do
        Game.BuildCacheDat.animbaseframes_cache_add(archive)
    end
end

if not Game.BuildCacheDat.model_cache_has(model_id) then
    Game.CacheIO.request_list_reset(cacheio_requests)
    Game.BuildCacheDat.models_fetch(cacheio_requests, { model_id })
    local a = CacheIO.load_request_list(cacheio_requests)
    if a[1] then
        Game.BuildCacheDat.model_cache_add(a[1], model_id)
    end
end

Game.Game.spawn_element(world_x, world_z, world_level, model_id, seq_id)
