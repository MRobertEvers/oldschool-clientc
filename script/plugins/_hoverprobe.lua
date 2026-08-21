--
-- Hover probe (development). Logs the pointer's tile once per server tick and
-- paints it, so api.hover_tile() can be checked from a single headless frame:
--
--   TORIRS_PLUGIN_MANIFEST=plugins/_hoverprobe.ini TORIRS_SIM_HOVER=300,250 \
--   TORIRS_WORLD_MAP=50,50 TORIRS_EXIT_BMP=/tmp/hover.bmp SDL_VIDEODRIVER=dummy \
--   TORIRS_MAX_FRAMES=150 ./src/torirs --manifest manifests/manifest_osrs239.ini --offline
--
---@type torirs.Plugin
local plugin = { name = "hover-probe", version = "1.0.0", config = {} }

function plugin.on_server_tick(api)
    local x, z, level = api.hover_tile()
    api.log("hover_tile -> ", tostring(x), " ", tostring(z), " ", tostring(level))
end

function plugin.on_draw_world(api, draw)
    local x, z, level = api.hover_tile()
    if not x then return end
    draw.tile(x, z, level, "#FF00FF", 96)
end

return plugin
