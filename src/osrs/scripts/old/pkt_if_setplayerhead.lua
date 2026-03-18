-- pkt_if_setplayerhead: load player head models (IDK heads + obj heads) before processing
-- IF_SETPLAYERHEAD packet. Ensures head models are loaded then calls gameproto_exec.
local HostIOUtils = require("hostio_utils")

local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end

-- Called with (item, io) from C when an IF_SETPLAYERHEAD packet is received.
local item = ...

-- Get local player appearance from game state (already set by PLAYER_INFO)
local idk_ids, obj_ids = GameProto.get_local_player_appearance_ids()

local queued_model_ids = {}
for _, idk_id in ipairs(idk_ids) do
    local head_ids = BuildCacheDat.get_idk_head_model_ids(idk_id)
    for _, model_id in ipairs(head_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end
for _, obj_id in ipairs(obj_ids) do
    local head_ids = BuildCacheDat.get_obj_head_model_ids(obj_id, 0)
    for _, model_id in ipairs(head_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end

local req_ids = {}
local req_model_ids = {}
for _, model_id in ipairs(queued_model_ids) do
    if not BuildCacheDat.has_model(model_id) then
        req_ids[#req_ids + 1] = HostIO.dat_models_load(model_id)
        req_model_ids[#req_model_ids + 1] = model_id
    end
end
local results = HostIOUtils.await_all(req_ids)
for i, res in ipairs(results) do
    local success, param_a, param_b, data_size, data = res[1], res[2], res[3], res[4], res[5]
    if success then
        BuildCacheDat.cache_model(req_model_ids[i], data_size, data)
    end
end

-- All head models loaded; run gameproto_exec IF_SETPLAYERHEAD handling
GameProto.exec_if_setplayerhead(item)
