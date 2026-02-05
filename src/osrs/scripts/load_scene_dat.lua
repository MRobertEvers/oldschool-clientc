local HostIOUtils = require("hostio_utils")

local function load_scene_dat(game)
    -- HostIO is bound to the IO queue in C; only chunk coords are passed from Lua
    local promise = HostIO.dat_map_scenery_load(50, 50)

    local success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if not success then
        error("Failed to load scene dat")
    end

    return data
end

return load_scene_dat