-- pkt_npc_info: load all NPC models (body + head) via CacheDat /
-- Game.BuildCacheDat.* APIs, then run gameproto_exec NPC info.
local CacheDat = require("cachedat")

local function queue_unique(seen, list, model_id)
    if not model_id or model_id == 0 then
        return
    end
    if seen[model_id] then
        return
    end
    seen[model_id] = true
    table.insert(list, model_id)
end

local data, length = ...
local npc_ids = Game.BuildCacheDat.get_npc_ids_from_packet(data, length)

local seen = {}
local queued_model_ids = {}

for _, npc_id in ipairs(npc_ids) do
    local model_ids = Game.BuildCacheDat.get_npc_model_ids(npc_id)
    for _, model_id in ipairs(model_ids) do
        queue_unique(seen, queued_model_ids, model_id)
    end

    -- Also load head models for chat head (IF_SETNPCHEAD)
    local head_model_ids = Game.BuildCacheDat.get_npc_head_model_ids(npc_id)
    for _, model_id in ipairs(head_model_ids) do
        queue_unique(seen, queued_model_ids, model_id)
    end
end

local model_requests = {}
local models_needed = {}
for _, model_id in ipairs(queued_model_ids) do
    if not Game.BuildCacheDat.has_model(model_id) then
        table.insert(model_requests, {
            table_id = CacheDat.Tables.CACHE_DAT_MODELS,
            archive_id = model_id,
            flags = 0,
        })
        table.insert(models_needed, model_id)
    end
end

if #model_requests > 0 then
    local model_archives = CacheDat.load_archives(model_requests)
    for i, model_id in ipairs(models_needed) do
        Game.BuildCacheDat.cache_model(model_archives[i], model_id)
    end
end

Game.Game.exec_pkt_npc_info(data, length)
