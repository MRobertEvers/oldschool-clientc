-- Draw-path probe: marks fixed tiles near the loaded map square, so the
-- overlay primitives can be verified without a server-synced player.
---@type torirs.Plugin
local plugin = { id = "drawprobe" }
local said = false

function plugin.on_draw_world(api, draw)
    -- The offline map loads at base 3200,3200; sit the marks inside it.
    draw.world_tile(3250, 3250, 0, 0x00FFFF, 0x00FFFF, 60)
    -- No fill at all: the wash is what the omitted pair would have described.
    draw.world_tile(3252, 3250, 0, 0xFFFF00)
    draw.rect(10, 10, 40, 12, 0xFF00FF, 128)
    draw.line(0, 0, 30, 30, 0x00FF00)
    draw.text(60, 40, "drawprobe", 0xFFFFFF)
    if not said then
        said = true
        api.core.log("DRAWPROBE issued tile/rect/line/text")
    end
end

return plugin
