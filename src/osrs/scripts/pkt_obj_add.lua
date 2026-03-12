-- pkt_obj_add: load object model via BuildCacheDat before adding to obj stack.
-- Ensures model is loaded then calls GameProto.exec_obj_add.
local HostIOUtils = require("hostio_utils")

local item, zone_base_x, zone_base_z = ...

-- Load object configurations if not already loaded
if not _G.objects_loaded_flag then
    BuildCacheDat.init_objects_from_config_jagfile()
    _G.objects_loaded_flag = true
end

local obj_id, count = GameProto.get_obj_add_data(item)
-- Protocol obj_id; BuildCacheDat.get_obj uses config index (often 0-based)
local actual_obj_id = obj_id
if obj_id > 0 then
    actual_obj_id = obj_id - 1
end

local obj = BuildCacheDat.get_obj(actual_obj_id)
if obj and obj.model and obj.model ~= 0 and obj.model ~= -1 then
    if not BuildCacheDat.has_model(obj.model) then
        local promise = HostIO.dat_models_load(obj.model)
        local success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
        if success then
            BuildCacheDat.cache_model(obj.model, data_size, data)
        end
    end
end

GameProto.exec_obj_add(item, zone_base_x, zone_base_z)
