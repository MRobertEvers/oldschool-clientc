-- pkt_if_setnpchead: load NPC head models before processing IF_SETNPCHEAD packet.
-- Ensures head models are loaded then calls gameproto_exec (if_setnpchead).
local HostIOUtils = require("hostio_utils")

-- Called with (item, io) from C when an IF_SETNPCHEAD packet is received.
local item = ...

local component_id, npc_id = GameProto.get_if_setnpchead_data(item)

-- Load NPC head models for chat head display
local queued_model_ids = {}
local head_ids = BuildCacheDat.get_npc_head_model_ids(npc_id)
for _, model_id in ipairs(head_ids) do
    if model_id ~= 0 then
        local found = false
        for _, v in ipairs(queued_model_ids) do
            if v == model_id then
                found = true
                break
            end
        end
        if not found then
            table.insert(queued_model_ids, model_id)
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

-- All head models loaded; run gameproto_exec IF_SETNPCHEAD handling
GameProto.exec_if_setnpchead(item)
