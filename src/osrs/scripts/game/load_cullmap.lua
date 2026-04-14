local CacheDat = require("cachedat")

local viewport_w, viewport_h, near_clip_z, draw_radius = ...
viewport_w = viewport_w or 0
viewport_h = viewport_h or 0
-- Must match camera near_plane_z and gen_painters_cullmap --near (baked f{N} in filename).
near_clip_z = near_clip_z or 50
draw_radius = draw_radius or 25

local function snap_dim(v)
    if v <= 0 then
        return 100
    end
    local r = math.ceil(v / 100) * 100
    if r < 100 then
        r = 100
    end
    if r > 2000 then
        r = 2000
    end
    return r
end

local function snap_radius(r)
    if r <= 0 then
        return 5
    end
    local s = math.ceil(r / 5) * 5
    if s < 5 then
        s = 5
    end
    if s > 50 then
        s = 50
    end
    return s
end

local w = snap_dim(viewport_w)
local h = snap_dim(viewport_h)
local radius = snap_radius(draw_radius)
local path = string.format(
    "cullmaps/painters_cullmap_baked_r%d_f%d_w%d_h%d.bin",
    radius,
    near_clip_z,
    w,
    h
)

local cullmap_file = CacheDat.load_config_file(path)
Game.Misc.read_cullmap_from_blob(cullmap_file, radius)
