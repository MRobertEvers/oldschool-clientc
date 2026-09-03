--
-- Ground-obj event probe (development)
--
-- api.log is compiled out of an OPT build (TORIRS_LOG), so this probe SAYS what
-- it saw in the only channel a headless OPT run keeps: pixels. Three coloured
-- columns beside the player, each present exactly while its statement is true:
--
--   CYAN   api.objs() lists at least one ground stack RIGHT NOW
--   YELLOW an on_obj_despawn has fired at least once this session
--   GREEN  on_server_tick has fired at least once (the lane raises the fence)
--
-- Read beside loot_beam.lua's own beam that is the point: a magenta beam with
-- no cyan under it is a beam standing over a stack the client no longer has.
--
-- Not part of plugins.ini: TORIRS_PLUGIN_MANIFEST=plugins/_objprobe.ini
--

---@type torirs.Plugin
local plugin = { name = "obj-probe", version = "2.0.0", config = {} }

local KEY = 1

local mesh = nil
local marks = {}
local seen_despawn = false
local seen_tick = false

local COLOURS = { objs = 0x00FFFF, despawn = 0xFFFF00, tick = 0x00FF00 }
local OFFSET = { objs = 2, despawn = 4, tick = 6 }

local function build(api)
    local r = 40
    local h = 640
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

local function mark(api, name, on, me)
    if on then
        if not marks[name] then
            local h = api.object_create()
            if not h then return end
            api.object_model(h, mesh, "mesh")
            api.object_recolor(h, KEY, api.hsl(COLOURS[name]))
            api.object_light(h, 75, 1875)
            api.object_active(h, true)
            marks[name] = h
        end
        api.object_position(
            marks[name], me.true_x + OFFSET[name], me.true_z + 2, me.level)
    elseif marks[name] then
        api.object_destroy(marks[name])
        marks[name] = nil
    end
end

function plugin.on_start(api)
    mesh, marks, seen_despawn, seen_tick = nil, {}, false, false
end

function plugin.on_stop(api)
    for _, h in pairs(marks) do api.object_destroy(h) end
    marks = {}
end

function plugin.on_world_loaded(api, ev)
    marks = {}
end

function plugin.on_obj_despawn(api, obj)
    seen_despawn = true
end

function plugin.on_server_tick(api, ev)
    seen_tick = true
end

-- Restated on the LOGIC tick, which every lane raises -- the probe must not be
-- blind on a lane that has no tick fence, since that is one of the things it is
-- here to detect.
function plugin.on_logic_tick(api, ev)
    local me = api.local_player()
    local n = 0

    if not me then return end
    if not mesh then build(api) end
    if not mesh then return end

    for _ in api.objs() do n = n + 1 end
    mark(api, "objs", n > 0, me)
    mark(api, "despawn", seen_despawn, me)
    mark(api, "tick", seen_tick, me)
end

return plugin
