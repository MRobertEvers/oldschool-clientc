--
-- Beam kill probe (development)
--
-- One question, and only one: does api.object_destroy TAKE A STANDING WORLD
-- OBJECT OUT OF THE FRAME? loot_beam.lua's whole despawn half is that call, and
-- it is the half no probe covered -- _beamprobe.lua stands a beam up and never
-- takes it down.
--
-- Offline, so it needs no server and no ground item: the beams go on a patch of
-- tiles around the scene centre, which is where an offline boot points the
-- camera. Two runs, one binary:
--
--   TORIRS_PLUGINS=1 TORIRS_PLUGIN_MANIFEST=plugins/_beamkill.ini \
--   TORIRS_BEAMKILL_AT=100000 TORIRS_MAX_FRAMES=120 SDL_VIDEODRIVER=dummy \
--   TORIRS_EXIT_BMP=/tmp/alive.bmp ./src/torirs --manifest <offline manifest> --offline
--
-- then the same with TORIRS_BEAMKILL_AT=40 -> /tmp/dead.bmp. The first frame
-- must show beams and the second must not.
--
-- The kill cycle is read from the config rather than an env var (a plugin has
-- no getenv), so the two runs differ by one prefs key.
--

---@type torirs.Plugin
local plugin = {
    name    = "beam-kill",
    version = "1.0.0",
    config  = {
        { key = "kill_at", type = "int", default = "40", min = 0, max = 100000,
          label = "Destroy on cycle" },
    },
}

local KEY = 1

local mesh = nil
local objs = {}
local base_x, base_z = nil, nil
local cycles = 0
local killed = false

local function build(api)
    local r = 48
    local h = 512
    local lo, hi = {}, {}
    local corners = { { -r, -r }, { r, -r }, { r, r }, { -r, r } }

    mesh = api.mesh_create()
    if not mesh then return end
    for i = 1, 4 do
        lo[i] = api.mesh_vertex(mesh, corners[i][1], 0, corners[i][2])
        hi[i] = api.mesh_vertex(mesh, corners[i][1], -h, corners[i][2])
    end
    for i = 1, 4 do
        local n = i % 4 + 1
        api.mesh_face(mesh, lo[i], hi[i], hi[n], KEY, 0)
        api.mesh_face(mesh, lo[i], hi[n], lo[n], KEY, 0)
    end
end

function plugin.on_start(api)
    mesh, objs, base_x, base_z, cycles, killed = nil, {}, nil, nil, 0, false
end

function plugin.on_stop(api)
    for _, h in ipairs(objs) do api.object_destroy(h) end
    objs = {}
end

function plugin.on_world_loaded(api, ev)
    base_x, base_z = ev.base_tile_x, ev.base_tile_z
    objs = {}
    killed = false
    cycles = 0
end

function plugin.on_logic_tick(api, ev)
    if not base_x then return end
    cycles = cycles + 1

    if #objs == 0 and not killed then
        if not mesh then build(api) end
        if not mesh then return end
        for dx = -2, 2 do
            for dz = -2, 2 do
                local h = api.object_create()
                if h then
                    api.object_model(h, mesh, "mesh")
                    api.object_recolor(h, KEY, api.hsl(0xFF00FF))
                    api.object_light(h, 75, 1875)
                    api.object_position(h, base_x + 52 + dx * 2, base_z + 52 + dz * 2, 0)
                    api.object_active(h, true)
                    objs[#objs + 1] = h
                end
            end
        end
        api.log("beamkill: " .. #objs .. " objects up at cycle " .. cycles)
    end

    if not killed and cycles >= api.config.kill_at then
        killed = true
        for _, h in ipairs(objs) do api.object_destroy(h) end
        api.log("beamkill: destroyed " .. #objs .. " objects at cycle " .. cycles)
        objs = {}
    end
end

return plugin
