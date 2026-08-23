--
-- Beam probe (development)
--
-- Stands one plugin-authored beam on the local player's own tile, every tick,
-- forever, and turns it.
--
-- It exists because the thing loot_beam.lua does is hard to SEE on demand: it
-- needs a ground item worth more than a threshold, which needs a server that
-- drops one, at a tile the camera is looking at. This has no such
-- preconditions -- the player is always on screen -- so "does a plugin author
-- a mesh, and does the host light it, recolour it and sort it against the
-- scene" can be answered from a single headless frame:
--
--   TORIRS_PLUGIN_MANIFEST=plugins/_beamprobe.ini \
--   TORIRS_EXIT_BMP=/tmp/beam.bmp SDL_VIDEODRIVER=dummy \
--   ./src/torirs --manifest manifests/manifest_osrs239.ini
--
-- Deliberately NOT loot_beam.lua's geometry: this is the seam under it, and a
-- probe that shared the plugin's builder would pass or fail with it. A plain
-- box says whether a mesh reaches the scene at all.
--
-- Not part of plugins.ini. Point TORIRS_PLUGIN_MANIFEST at its own manifest.
--

---@type torirs.Plugin
local plugin = {
    name    = "beam-probe",
    version = "2.0.0",
    config  = {
        { key = "colour", type = "color", default = "#FF9600", label = "Beam colour" },
        { key = "height", type = "int", default = "512", min = 32, max = 2048,
          label = "Height" },
        { key = "spin",   type = "int", default = "90", min = 0, max = 720,
          label = "Spin (degrees/sec)" },
    },
}

-- The one face colour the box is authored in, and what the config colour is
-- recoloured onto.
local KEY = 1

local beam = nil
local mesh = nil
local placed_at = nil
-- Where the beam was last put, held so the per-frame spin can restate the
-- position with nothing but the yaw moved.
local at_x, at_z, at_level = 0, 0, 0

-- A square column: four walls, one colour, half transparent. Wound so the
-- OUTSIDE faces the camera -- the raster culls back faces, so a box wound the
-- other way is an invisible pass rather than a failing one, which is the whole
-- distinction this probe exists to draw.
local function build(api)
    local r = 48
    local h = api.config.height
    local lo = {}
    local hi = {}
    local corners = { { -r, -r }, { r, -r }, { r, r }, { -r, r } }

    mesh = api.mesh_create()
    if not mesh then return end

    for i = 1, 4 do
        lo[i] = api.mesh_vertex(mesh, corners[i][1], 0, corners[i][2])
        hi[i] = api.mesh_vertex(mesh, corners[i][1], -h, corners[i][2])
    end
    for i = 1, 4 do
        local n = i % 4 + 1
        api.mesh_face(mesh, lo[i], hi[i], hi[n], KEY, 96)
        api.mesh_face(mesh, lo[i], hi[n], lo[n], KEY, 96)
    end
end

function plugin.on_start(api)
    beam, mesh, placed_at = nil, nil, nil
end

function plugin.on_stop(api)
    if beam then api.object_destroy(beam) end
    if mesh then api.mesh_destroy(mesh) end
    beam, mesh = nil, nil
end

function plugin.on_world_loaded(api)
    -- The scene was rebuilt; the handle survives it but the placement has to
    -- be restated against the tile the player is on now.
    placed_at = nil
end

function plugin.on_server_tick(api)
    local me = api.local_player()
    if not me then return end

    if not mesh then build(api) end
    if not mesh then return end

    if not beam then
        beam = api.object_create()
        if not beam then return end
        api.object_model(beam, mesh, "mesh")
        api.object_recolor(beam, KEY, api.hsl(api.config.colour))
        api.object_light(beam, 75, 1875)
        api.object_active(beam, true)
    end

    local key = me.level .. ":" .. me.true_x .. ":" .. me.true_z
    if key ~= placed_at then
        placed_at = key
        at_x, at_z, at_level = me.true_x, me.true_z, me.level
        api.object_position(beam, at_x, at_z, at_level)
        api.log("beam at " .. key .. " ready=" .. tostring(api.object_ready(beam)))
    end
end

function plugin.on_frame(api, ev)
    if not beam or not placed_at or api.config.spin == 0 then return end
    api.object_position(
        beam, at_x, at_z, at_level, 0,
        (ev.now_ms * api.config.spin * 2048) // 360000 % 2048)
end

return plugin
