--
-- Beam probe (development)
--
-- Stands one loot beam on the local player's own tile, every tick, forever.
--
-- It exists because the thing loot_beam.lua does is hard to SEE on demand: it
-- needs a ground item worth more than a threshold, which needs a server that
-- drops one, at a tile the camera is looking at. This has no such preconditions
-- -- the player is always on screen -- so "does a plugin world object build,
-- light, recolour, animate and sort against the scene" can be answered from a
-- single headless frame:
--
--   TORIRS_PLUGIN_MANIFEST=plugins/_beamprobe.ini \
--   TORIRS_EXIT_BMP=/tmp/beam.bmp SDL_VIDEODRIVER=dummy \
--   ./src/torirs --manifest manifests/manifest_osrs239.ini
--
-- Not part of plugins.ini. Point TORIRS_PLUGIN_MANIFEST at its own manifest.
--

---@type torirs.Plugin
local plugin = {
    name    = "beam-probe",
    version = "1.0.0",
    config  = {
        { key = "colour", type = "color", default = "#FF9600", label = "Beam colour" },
        { key = "model",  type = "int",   default = "43330", min = 0, max = 200000,
          label = "Model" },
        { key = "seq",    type = "int",   default = "9260",  min = -1, max = 200000,
          label = "Sequence" },
    },
}

local beam = nil
local placed_at = nil

function plugin.on_start(api)
    beam, placed_at = nil, nil
end

function plugin.on_stop(api)
    if beam then api.object_destroy(beam) end
    beam = nil
end

function plugin.on_world_loaded(api)
    -- The scene was rebuilt; the handle survives it but the placement has to
    -- be restated against the tile the player is on now.
    placed_at = nil
end

function plugin.on_server_tick(api)
    local me = api.local_player()
    if not me then return end

    if not beam then
        beam = api.object_create()
        if not beam then return end
        local hsl = api.hsl(api.config.colour)
        local h, s, l = api.hsl_unpack(hsl)
        api.object_model(beam, api.config.model)
        api.object_recolor(beam, 26432, api.hsl_pack(h, s > 2 and s - 1 or s, l))
        api.object_recolor(beam, 26584, api.hsl_pack(h, s, math.min(l + 24, 127)))
        api.object_anim(beam, api.config.seq, true)
        api.object_light(beam, 75, 1875)
        api.object_active(beam, true)
    end

    local key = me.level .. ":" .. me.true_x .. ":" .. me.true_z
    if key ~= placed_at then
        placed_at = key
        api.object_position(beam, me.true_x, me.true_z, me.level)
        api.log("beam at " .. key .. " ready=" .. tostring(api.object_ready(beam)))
    end
end

return plugin
