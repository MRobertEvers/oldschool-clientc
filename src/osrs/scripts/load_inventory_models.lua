-- Script to load inventory item models for the example interface
-- This runs asynchronously and loads all models needed for the inventory items

local HostIOUtils = require("hostio_utils")

print("=== Loading Inventory Models ===")

-- Object IDs used in the example inventory (classic items for old cache)
local obj_ids = {
    526,   -- Bones
    995,   -- Coins
    1511,  -- Logs
    1759,  -- Ball of wool
    1333,  -- Rune scimitar
    1277,  -- Bronze sword
    440,   -- Iron ore
    2142   -- Cooked meat
}

-- First, load object configs if not already loaded
print("Loading object configurations...")
BuildCacheDat.init_objects_from_config_jagfile()
print("Object configurations loaded")

-- Collect all model IDs that need to be loaded
local model_ids = {}
local model_id_map = {}  -- Track which models we've already queued

for _, obj_id in ipairs(obj_ids) do
    local obj = BuildCacheDat.get_obj(obj_id)
    if obj and obj.model ~= 0 and obj.model ~= -1 then
        if not model_id_map[obj.model] then
            model_ids[#model_ids + 1] = obj.model
            model_id_map[obj.model] = true
            print(string.format("  Queued model %d for obj %d (%s)", obj.model, obj_id, obj.name or "unnamed"))
        end
    else
        print(string.format("  Warning: obj %d has no model", obj_id))
    end
end

print(string.format("Loading %d unique models...", #model_ids))

-- Load all models asynchronously
local success, param_a, param_b, data_size, data
for _, model_id in ipairs(model_ids) do
    local promise = HostIO.dat_models_load(model_id)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success then
        BuildCacheDat.cache_model(model_id, data_size, data)
        print(string.format("  ✓ Loaded model %d (%d bytes)", model_id, data_size))
    else
        print(string.format("  ✗ Failed to load model %d", model_id))
    end
end

print(string.format("=== Inventory Models Loaded: %d/%d ===", #model_ids, #obj_ids))
