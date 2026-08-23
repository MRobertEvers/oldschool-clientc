-- TEMPORARY probe: what ground_items.lua would decide, per stack.
local plugin = { name = "gi-count", version = "1.0.0", config = {} }
local frames = 0
local base_x, base_z = nil, nil

function plugin.on_world_loaded(api, ev)
    base_x, base_z = ev.base_tile_x, ev.base_tile_z
end

function plugin.on_draw_world(api, draw)
    frames = frames + 1
    if frames % 300 ~= 0 then return end
    local me = api.local_player()
    if not me then api.log("no local player") return end
    if not base_x then api.log("no base") return end
    api.log("base=", base_x, ",", base_z, " me=", me.true_x, ",", me.true_z)
    for obj in api.objs() do
        local dx = math.abs(obj.tile_x - me.true_x)
        local dz = math.abs(obj.tile_z - me.true_z)
        local d = dx > dz and dx or dz
        local sx, sy = api.project((obj.tile_x - base_x) * 128 + 64,
            (obj.tile_z - base_z) * 128 + 64, 20)
        api.log("  ", obj.name, " @", obj.tile_x, ",", obj.tile_z, " lvl=", obj.level,
            " dist=", d, " proj=", tostring(sx), ",", tostring(sy))
        if sx then
            draw.text(sx + 1, sy + 1, obj.name, 0x000000)
            draw.text(sx, sy, obj.name, 0xFFFFFF)
        end
    end
end
return plugin
