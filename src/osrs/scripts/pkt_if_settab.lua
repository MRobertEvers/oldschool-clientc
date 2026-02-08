-- pkt_if_settab: load interface components before processing IF_SETTAB packet.
-- Ensures components are loaded then calls gameproto_exec (if_settab).
local HostIOUtils = require("hostio_utils")

-- Called with (item, io) from C when an IF_SETTAB packet is received.
local item, io = ...

-- Load interface components only once (use global flag to persist across script invocations)
if not _G.interfaces_loaded_flag then
    print("Loading interface components for IF_SETTAB (first time)...")
    local interfaces_promise = HostIO.dat_config_interfaces_load()
    local success, param_a, param_b, data_size, data = HostIOUtils.await(interfaces_promise)
    if success then
        BuildCacheDat.load_interfaces(data_size, data)
        print("Interface components loaded successfully")
        _G.interfaces_loaded_flag = true
    else
        print("Failed to load interface components")
    end
end

-- Get the component_id and tab_id from the packet
local component_id, tab_id = GameProto.get_if_settab_data(item)
print(string.format("IF_SETTAB: component_id=%d, tab_id=%d", component_id, tab_id))

-- All components loaded; run gameproto_exec IF_SETTAB handling
GameProto.exec_if_settab(item)
