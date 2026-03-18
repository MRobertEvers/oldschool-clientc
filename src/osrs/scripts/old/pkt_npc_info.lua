-- pkt_npc_info: load all NPC models via BuildCacheDat before processing the packet.
-- Ensures models are loaded then calls gameproto_exec (add_npc_info).
local HostIOUtils = require("hostio_utils")

local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end

-- Called with (item, io) from C when an NPC_INFO packet is received.
local item = ...

local npc_ids = GameProto.get_npc_ids_from_packet(item)

-- Collect unique model ids from all NPC configs (BuildCacheDat has configs from load_scene_dat).
local queued_model_ids = {}
for _, npc_id in ipairs(npc_ids) do
    local model_ids = BuildCacheDat.get_npc_model_ids(npc_id)
    for _, model_id in ipairs(model_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
    -- Also load head models for chat head (IF_SETNPCHEAD)
    local head_ids = BuildCacheDat.get_npc_head_model_ids(npc_id)
    for _, model_id in ipairs(head_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end

-- Load all models before processing the packet (skip if already cached).
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

-- All models loaded; run gameproto_exec NPC info handling.
GameProto.exec_npc_info(item)
