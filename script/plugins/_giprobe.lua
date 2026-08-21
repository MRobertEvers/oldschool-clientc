--
-- Ground-item overlay probe (development)
--
-- Writes a ground-item style label over the local player's tile and over the
-- tile two east of it, every frame.
--
-- It exists for the same reason _beamprobe.lua does: the thing ground_items.lua
-- does is hard to SEE on demand. It needs a ground item, which needs a server
-- that drops one, at a tile the camera is looking at. The player is always on
-- screen, so this answers the part that can go wrong on its own -- does a
-- plugin's text land on the tile it names -- from a single headless frame:
--
--   TORIRS_PLUGIN_MANIFEST=plugins/_giprobe.ini \
--   TORIRS_EXIT_BMP=/tmp/gi.bmp SDL_VIDEODRIVER=dummy \
--   ./src/torirs --manifest manifests/manifest_osrs239.ini
--
-- The two labels are drawn through DIFFERENT paths on purpose. The first
-- projects the player's own fine position, which api.project() takes as-is.
-- The second projects an ABSOLUTE tile the way ground_items.lua has to -- back
-- through the scene origin from on_world_loaded -- so a wrong origin shows up
-- as the second label sitting somewhere other than two tiles east of the first,
-- which is the one mistake that path can make.
--
-- Not part of plugins.ini. Point TORIRS_PLUGIN_MANIFEST at its own manifest.
--

---@type torirs.Plugin
local plugin = {
    name    = "gi-probe",
    version = "1.0.0",
    config  = {
        { key = "colour", type = "color", default = "#FF9600", label = "Label colour" },
        { key = "height", type = "int",   default = "20", min = 0, max = 512,
          label = "Height above ground" },
    },
}

local base_x, base_z = nil, nil
local reported = false

function plugin.on_start(api)
    base_x, base_z, reported = nil, nil, false
end

function plugin.on_world_loaded(api, ev)
    base_x, base_z, reported = ev.base_tile_x, ev.base_tile_z, false
end

function plugin.on_draw_world(api, draw)
    local me = api.local_player()
    if not me or not base_x then return end

    local colour = api.config.colour
    local height = api.config.height

    local sx, sy = api.project(me.fine_x, me.fine_z, height)
    if sx then
        draw.text(sx + 1, sy + 1, "Here (fine)", 0x000000)
        draw.text(sx, sy, "Here (fine)", colour)
    end

    -- Two tiles east, addressed the way a ground item is: absolute in, scene
    -- origin subtracted, tile centre in fine units out.
    local tx, tz = me.true_x + 2, me.true_z
    local ex, ey = api.project((tx - base_x) * 128 + 64, (tz - base_z) * 128 + 64, height)
    if ex then
        draw.text(ex + 1, ey + 1, "+2 east (absolute)", 0x000000)
        draw.text(ex, ey, "+2 east (absolute)", colour)
        draw.tile(tx, tz, me.level, colour)
    end

    if not reported then
        reported = true
        api.log("base=", base_x, ",", base_z, " here=", tostring(sx), ",", tostring(sy),
            " east=", tostring(ex), ",", tostring(ey))
    end
end

return plugin
