-- pkt_update_inv_full: load interface components and item models before processing.
-- Ensures components and models are loaded then calls gameproto_exec (update_inv_full).
local HostIOUtils = require("hostio_utils")

local function add_unique(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return
        end
    end
    table.insert(tbl, value)
end

-- Called with (item, io) from C when an UPDATE_INV_FULL packet is received.
local item, io = ...

-- Load interface components only once (use global flag to persist across script invocations)
if not _G.interfaces_loaded_flag then
    print("Loading interface components for UPDATE_INV_FULL (first time)...")
    local interfaces_promise = HostIO.dat_config_interfaces_load()
    local success, param_a, param_b, data_size, data = HostIOUtils.await(interfaces_promise)
    if success then
        BuildCacheDat.load_interfaces(data_size, data)
        BuildCacheDat.load_component_sprites_from_media()
        print("Interface components loaded successfully")
        _G.interfaces_loaded_flag = true
    else
        print("Failed to load interface components")
    end
end

-- Load object configurations only once (needed for item icons)
if not _G.objects_loaded_flag then
    print("Loading object configurations for UPDATE_INV_FULL (first time)...")
    BuildCacheDat.init_objects_from_config_jagfile()
    print("Object configurations loaded successfully")
    _G.objects_loaded_flag = true
end

-- Now load the item models
local obj_ids = GameProto.get_inv_obj_ids(item)

-- Collect unique model ids from all object configs
local queued_model_ids = {}
for _, obj_id in ipairs(obj_ids) do
    -- Object IDs in the protocol are 1-indexed, so subtract 1 for lookups
    local actual_obj_id = obj_id - 1
    local model_ids = BuildCacheDat.get_obj_model_ids(actual_obj_id)
    for _, model_id in ipairs(model_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end

-- Load all models before processing the packet
for _, model_id in ipairs(queued_model_ids) do
    local promise = HostIO.dat_models_load(model_id)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success then
        BuildCacheDat.cache_model(model_id, data_size, data)
    end
end

-- All components and models loaded; run gameproto_exec inventory update handling
GameProto.exec_update_inv_full(item)
