-- Draw-path probe: marks fixed tiles near the loaded map square, so the
-- overlay primitives can be verified without a server-synced player.
---@type torirs.Plugin
local plugin = { id = "drawprobe" }
local said = false

function plugin.on_start(api) said = false end

function plugin.on_draw_world(api, draw)
    local me = api.world.local_player()
    local bx, bz = api.world.scene_origin()
    if not bx then return end
    local x, z, level = me and me.true_x or bx + 50, me and me.true_z or bz + 50, me and me.level or 0
    draw.world_tile(x, z, level, 0x00FFFF, 0x00FFFF, 60)
    -- No fill at all: the wash is what the omitted pair would have described.
    draw.world_tile(x + 2, z, level, 0xFFFF00)
    draw.rect(10, 10, 40, 12, 0xFF00FF, 128)
    draw.line(0, 0, 30, 30, 0x00FF00)
    draw.text(60, 40, "drawprobe", 0xFFFFFF)
    if not said then
        said = true
        api.core.log("DRAWPROBE issued tile/rect/line/text")
    end
end

return plugin
