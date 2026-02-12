-- pkt_loc_add_change: load loc models via BuildCacheDat before adding to loc changes.
-- Ensures scenery config and models are loaded then calls GameProto.exec_loc_add_change.
local HostIOUtils = require("hostio_utils")

local item, io = ...

-- Load scenery configs if not already loaded (needed for get_scenery_model_ids)
if not _G.scenery_configs_loaded_flag then
    BuildCacheDat.init_scenery_configs_from_config_jagfile()
    _G.scenery_configs_loaded_flag = true
end

local loc_id, shape = GameProto.get_loc_add_change_data(item)

-- Collect model IDs for this loc (Client.ts LocType.getModel uses shape)
local model_ids = BuildCacheDat.get_scenery_model_ids(loc_id)
if model_ids then
    for _, model_id in ipairs(model_ids) do
        if model_id and model_id ~= 0 and model_id ~= -1 then
            if not BuildCacheDat.has_model(model_id) then
                local promise = HostIO.dat_models_load(model_id)
                local success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
                if success then
                    BuildCacheDat.cache_model(model_id, data_size, data)
                end
            end
        end
    end
end

GameProto.exec_loc_add_change(item)
