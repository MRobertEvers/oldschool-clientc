-- pkt_player_info: load all appearance models (IDK + obj) via BuildCacheDat before processing.
local HostIOUtils = require("hostio_utils")

local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end

local item, io = ...

local idk_ids, obj_ids = GameProto.get_player_appearance_ids(item)

local queued_model_ids = {}
for _, idk_id in ipairs(idk_ids) do
    local model_ids = BuildCacheDat.get_idk_model_ids(idk_id)
    for _, model_id in ipairs(model_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end
for _, obj_id in ipairs(obj_ids) do
    local model_ids = BuildCacheDat.get_obj_model_ids(obj_id)
    for _, model_id in ipairs(model_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end

local success, param_a, param_b, data_size, data
for _, model_id in ipairs(queued_model_ids) do
    local promise = HostIO.dat_models_load(model_id)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success then
        BuildCacheDat.cache_model(model_id, data_size, data)
    end
end

GameProto.exec_player_info(item)
