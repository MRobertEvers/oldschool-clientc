-- TEMPORARY probe: how many ground-item stacks the client sees, and where.
local plugin = { name = "gi-count", version = "1.0.0", config = {} }
local frames = 0
function plugin.on_draw_world(api, draw)
    frames = frames + 1
    if frames % 200 ~= 0 then return end
    local me = api.local_player()
    local n = 0
    local first = ""
    for obj in api.objs() do
        n = n + 1
        if n <= 4 then
            first = first .. " [" .. obj.obj_id .. " x" .. obj.count .. " '" .. obj.name
                .. "' @" .. obj.tile_x .. "," .. obj.tile_z .. "," .. obj.level
                .. " cost=" .. obj.cost .. "]"
        end
    end
    api.log("objs=", n, " me=", me and me.true_x or -1, ",", me and me.true_z or -1,
        " lvl=", me and me.level or -1, first)
end
function plugin.on_obj_spawn(api, obj)
    api.log("obj_spawn id=", obj.obj_id, " x", obj.count, " '", obj.name, "' @",
        obj.tile_x, ",", obj.tile_z, ",", obj.level)
end
return plugin
