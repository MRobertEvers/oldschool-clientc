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
local item, io = ...

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
end

-- Load all models before processing the packet.
for _, model_id in ipairs(queued_model_ids) do
    local promise = HostIO.dat_models_load(model_id)
    local success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success then
        BuildCacheDat.cache_model(model_id, data_size, data)
    end
end

-- All models loaded; run gameproto_exec NPC info handling.
GameProto.exec_npc_info(item)
