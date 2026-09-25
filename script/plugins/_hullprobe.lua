--
-- Hull probe (development)
--
-- Draws BOTH draw.world_hull() shapes over the local player at once: the bounds
-- cylinder in cyan, the mesh silhouette in magenta.
--
-- One image answers the only question worth asking about the two -- is the
-- mesh hull actually tighter, and does it still wrap the model -- which a
-- screenshot of either one alone does not: a hull looks plausible on its own
-- whether it is hugging the geometry or a box around it. The local player is
-- always on screen, so this needs no server, no tagged npc and no ground item:
--
--   TORIRS_PLUGIN_MANIFEST=plugins/_hullprobe.ini \
--   TORIRS_EXIT_BMP=/tmp/hull.bmp SDL_VIDEODRIVER=dummy \
--   ./src/torirs --manifest manifests/manifest_osrs239.ini
--
-- Not part of plugins.ini. Point TORIRS_PLUGIN_MANIFEST at its own manifest.
--

---@type torirs.Plugin
local plugin = { id = "hull-probe", version = "1.0.0", config = {} }

function plugin.on_draw_world(api, draw)
    local me = api.world.local_player()
    if not me then return end
    draw.world_hull(me.element_id, "#00FFFF", 0, "bounds")
    draw.world_hull(me.element_id, "#FF00FF", 0, "mesh")
end

return plugin
