-- pkt_player_info: load all appearance models (IDK + OBJ) via CacheDat / Game.buildcachedat_* before processing.
local CacheDat = require("cachedat")

local function print_table(tbl)
    for k, v in pairs(tbl) do
        print(k, v)
    end
end

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

print("pkt_player_info")
local data, length = ...
print("pkt_player_info", data, length)

-- Needed to turn appearance IDs into model IDs.
Game.buildcachedat_init_idkits_from_config_jagfile()
Game.buildcachedat_init_objects_from_config_jagfile()


local idk_ids, obj_ids = Game.buildcachedat_get_player_appearance_ids(data, length)
print_table(idk_ids)
print_table(obj_ids)

local seen = {}
local queued_model_ids = {}

for _, idk_id in ipairs(idk_ids) do
    local model_ids = Game.buildcachedat_get_idk_model_ids(idk_id)
    for _, model_id in ipairs(model_ids) do
        queue_unique(seen, queued_model_ids, model_id)
    end

    -- Also load head models for chat head (IF_SETPLAYERHEAD)
    local head_model_ids = Game.buildcachedat_get_idk_head_model_ids(idk_id)
    for _, model_id in ipairs(head_model_ids) do
        queue_unique(seen, queued_model_ids, model_id)
    end
end

for _, obj_id in ipairs(obj_ids) do
    local model_ids = Game.buildcachedat_get_obj_model_ids(obj_id)
    for _, model_id in ipairs(model_ids) do
        queue_unique(seen, queued_model_ids, model_id)
    end

    -- Also load head models for chat head (IF_SETPLAYERHEAD)
    local head_model_ids = Game.buildcachedat_get_obj_head_model_ids(obj_id, 0)
    for _, model_id in ipairs(head_model_ids) do
        queue_unique(seen, queued_model_ids, model_id)
    end
end

local model_requests = {}
local models_needed = {}
for _, model_id in ipairs(queued_model_ids) do
    if not Game.buildcachedat_has_model(model_id) then
        table.insert(
            model_requests,
            { table_id = CacheDat.Tables.CACHE_DAT_MODELS, archive_id = model_id, flags = 0 }
        )
        table.insert(models_needed, model_id)
    end
end

if #model_requests > 0 then
    local model_archives = CacheDat.load_archives(model_requests)
    for i, model_id in ipairs(models_needed) do
        Game.buildcachedat_cache_model(model_archives[i], model_id)
    end
end

Game.game_exec_player_info(item)
