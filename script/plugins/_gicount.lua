-- TEMPORARY probe: what ground_items.lua would decide, per stack.
local plugin = { id = "gi-count", version = "1.0.0", config = {} }
local frames = 0

local function items(api)
    local cursor = -1
    return function()
        local next_cursor, item = api.world.item_next(cursor)
        if not next_cursor then return nil end
        cursor = next_cursor
        return item
    end
end

function plugin.on_start(api) frames = 0 end

function plugin.on_draw_world(api, draw)
    frames = frames + 1
    local report = frames % 300 == 0
    local base_x, base_z = api.world.scene_origin()
    local me = api.world.local_player()
    if not me then return end
    if not base_x then return end
    if report then api.core.log("base=", base_x, ",", base_z, " me=", me.true_x, ",", me.true_z) end
    for obj in items(api) do
        local dx = math.abs(obj.tile_x - me.true_x)
        local dz = math.abs(obj.tile_z - me.true_z)
        local d = dx > dz and dx or dz
        local sx, sy = api.draw.project((obj.tile_x - base_x) * 128 + 64,
            (obj.tile_z - base_z) * 128 + 64, 20)
        if report then api.core.log("  ", obj.name, " @", obj.tile_x, ",", obj.tile_z, " lvl=", obj.level,
            " dist=", d, " proj=", tostring(sx), ",", tostring(sy)) end
        if sx then
            draw.text(sx + 1, sy + 1, obj.name, 0x000000)
            draw.text(sx, sy, obj.name, 0xFFFFFF)
        end
    end
end
return plugin
