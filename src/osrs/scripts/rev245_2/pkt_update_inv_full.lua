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

if not _G.objects_loaded_flag then
    Game.BuildCacheDat.init_objects_from_config_jagfile()
    _G.objects_loaded_flag = true
end

local obj_ids = Game.Game.get_inv_obj_ids(item)
local seen = {}
local models_to_load = {}
local models_needed = {}

for _, oid in ipairs(obj_ids) do
    local actual_obj_id = oid - 1
    local model_ids = Game.BuildCacheDat.get_obj_model_ids(actual_obj_id)
    for _, model_id in ipairs(model_ids) do
        if model_id ~= 0 and not seen[model_id] then
            seen[model_id] = true
            if not Game.BuildCacheDat.model_cache_has(model_id) then
                table.insert(models_to_load, {
                    table_id = CacheDat.Tables.CACHE_DAT_MODELS,
                    archive_id = model_id,
                    flags = 0,
                })
                table.insert(models_needed, model_id)
            end
        end
    end
end

if #models_to_load > 0 then
    local archives = CacheDat.load_archives(models_to_load)
    for i, model_id in ipairs(models_needed) do
        Game.BuildCacheDat.model_cache_add(archives[i], model_id)
    end
end

Game.Game.exec_pkt_update_inv_full(item)
