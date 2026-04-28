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

local needed_baseframes = Game.BuildCacheDat.get_sequence_animbaseframes_ids(seq_id)
local anim_requests = {}
local anim_indices = {}
for _, animbaseframes_id in ipairs(needed_baseframes) do
    if not Game.BuildCacheDat.animbaseframes_cache_has(animbaseframes_id) then
        table.insert(anim_requests, {
            table_id = CacheDat.Tables.CACHE_DAT_ANIMATIONS,
            archive_id = animbaseframes_id,
            flags = 0,
        })
        table.insert(anim_indices, animbaseframes_id)
    end
end

if #anim_requests > 0 then
    local anim_archives = CacheDat.load_archives(anim_requests)
    for i = 1, #anim_indices do
        Game.BuildCacheDat.animbaseframes_cache_add(anim_archives[i])
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
