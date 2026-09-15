--
-- Hover probe (development)
--
-- Logs the tile under the pointer every frame and paints it, so
-- api.input.hover_tile() can be answered from a single headless frame:
--
--   TORIRS_PLUGIN_MANIFEST=plugins/_hoverprobe.ini \
--   TORIRS_SIM_WHEEL=100,300,250,0,20 TORIRS_WORLD_MAP=50,50 \
--   TORIRS_EXIT_BMP=/tmp/hover.bmp SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=150 \
--   ./src/torirs --manifest manifests/manifest_osrs239.ini --offline
--
-- SIM_WHEEL rather than SIM_HOVER, and on_frame_start rather than on_server_tick,
-- and both for the same reason: the hover tile comes from the pick that rides
-- the RENDER, so it only exists on frames that render, and an --offline run
-- has no server ticks at all. SIM_WHEEL parks the pointer inside the main
-- loop (turn zero notches); SIM_HOVER runs its frames after the loop has
-- stopped, which is too late for anything drawn to reach the capture.
--
-- Not part of plugins.ini. Point TORIRS_PLUGIN_MANIFEST at its own manifest.
--

---@type torirs.Plugin
local plugin = { id = "hover-probe", version = "1.0.0", config = {} }

function plugin.on_frame_start(api)
    local x, z, level = api.input.hover_tile()
    api.core.log("hover_tile -> ", tostring(x), " ", tostring(z), " ", tostring(level))
end

function plugin.on_draw_world(api, draw)
    local x, z, level = api.input.hover_tile()
    if not x then return end
    draw.world_tile(x, z, level, "#FF00FF", "#FF00FF", 96)
end

return plugin
