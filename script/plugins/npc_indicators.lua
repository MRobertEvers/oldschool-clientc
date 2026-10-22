--
-- NPC Indicators
--
-- A port of RuneLite's NPC Indicators render styles (plugins/npchighlight,
-- drawn by game/npcoverlay/NpcOverlay.renderNpcOverlay). An npc whose name is
-- on the list gets any combination of:
--
--   hull              the model's convex hull
--   tile              the whole footprint, under the INTERPOLATED position --
--                     it slides with the model between tiles
--   true tile         the whole footprint, on the tile the server has the npc
--                     on (getWorldLocation), so it steps a tile at a time
--   south west tile   the footprint's SW tile only, interpolated
--   south west true   the footprint's SW tile only, on the server's tile
--
-- The south-west tiles are the point of the last two. An npc's coordinate IS
-- its south-west tile: a 3x3 boss "at" a tile occupies that tile and the two
-- to its north and east, and line of sight, melee reach and pathing all
-- reason from that corner. The whole-footprint styles hide it; RuneLite's
-- usual pairing is true tile + south west true tile, which shows both.
--
-- How each style lands on this api:
--
--   * The TRUE styles are draw.world_tile on absolute tiles. true_x/true_z is
--     route[0] of the npc's pathing queue -- the same tile RuneLite's
--     getWorldLocation answers -- and it is already the SW corner, so the
--     footprint is true_x .. true_x + size - 1.
--   * The INTERPOLATED styles have no world_tile to call: that verb takes a
--     whole tile. They project the footprint's four corners with
--     api.draw.project, which samples the ground under each corner exactly as
--     Perspective.getCanvasTileAreaPoly does, and join them with draw.line.
--     fine_x/fine_z is the footprint CENTRE in scene-local fine units
--     (tile * 128 + size * 64), so the SW corner is fine - size * 64. There is
--     no polygon fill primitive, so these two are outline only: the fill
--     opacity applies to the true styles and the hull.
--
-- Matching is by name, as the reference's "NPCs to highlight" list is: comma
-- separated, case-insensitive, `*` matches any run of characters
-- (WildcardMatcher). Names are matched with their colour tags stripped.
--
-- Not ported: the shift-click Tag menu (entity-highlighter tags by species
-- already), minimap names, menu recolouring, the respawn timer overlay and the
-- model OUTLINE style, which needs an outline renderer this api does not have.
--

---@type torirs.Plugin
local plugin = {
    id      = "npc-indicators",
    title   = "NPC Indicators",
    version = "1.0.0",
    config  = {
        {
            key = "npcs",
            type = "string",
            default = "",
            label = "NPCs to highlight"
        },
        { key = "highlight_hull", type = "bool", default = "1", label = "Highlight hull" },
        { key = "highlight_tile", type = "bool", default = "0", label = "Highlight tile" },
        { key = "highlight_true_tile", type = "bool", default = "0", label = "Highlight true tile" },
        {
            key = "highlight_sw_tile",
            type = "bool",
            default = "0",
            label = "Highlight south west tile"
        },
        {
            key = "highlight_sw_true_tile",
            type = "bool",
            default = "0",
            label = "Highlight south west true tile"
        },
        { key = "draw_names", type = "bool", default = "0", label = "Draw names above NPC" },
        { key = "ignore_dead", type = "bool", default = "1", label = "Ignore dead NPCs" },
        { key = "color", type = "color", default = "#00FFFF", label = "Highlight colour" },
        {
            key = "fill_alpha",
            type = "int",
            default = "20",
            min = 0,
            max = 255,
            label = "Fill opacity"
        },
        {
            key = "border_width",
            type = "int",
            default = "2",
            min = 0,
            max = 8,
            label = "Border width"
        },
    },
}

-- The reference's text offset: getCanvasTextLocation at logicalHeight + 40.
local NAME_LIFT = 40

-- One compiled Lua pattern per list entry, rebuilt only when the list changes.
local patterns = {}
-- Whether porcelain.open answered; without it the raw verbs still draw.
local layer = false

local function strip_tags(name)
    return (string.gsub(name, "<[^>]*>", ""))
end

-- WildcardMatcher: the entry is literal except for `*`, and the whole name
-- must match. Every Lua pattern magic character is escaped first.
local function compile(entry)
    local escaped = string.gsub(entry, "[%^%$%(%)%%%.%[%]%+%-%?]", "%%%0")
    return "^" .. string.gsub(escaped, "%*", ".*") .. "$"
end

local function load_list(api)
    patterns = {}
    for raw in string.gmatch(api.config.npcs, "[^,]+") do
        local entry = string.lower(string.match(raw, "^%s*(.-)%s*$"))
        if entry ~= "" then patterns[#patterns + 1] = compile(entry) end
    end
end

local function listed(name)
    local lowered = string.lower(strip_tags(name))
    for _, pattern in ipairs(patterns) do
        if string.find(lowered, pattern) then return true end
    end
    return false
end

-- The reference's isDead reading: a health bar the server sent, now empty.
local function dead(npc)
    return npc.health_scale > 0 and npc.health_ratio == 0
end

local function npcs(api)
    local cursor = -1
    return function()
        local next_cursor, npc = api.world.npc_next(cursor)
        if not next_cursor then return nil end
        cursor = next_cursor
        return npc
    end
end

-- A size x size square on the ground, its SW corner at scene-local fine
-- (x0, z0), as four projected corners joined by lines. Any corner off the
-- view drops the whole square, as a null getCanvasTileAreaPoly does.
local function ground_square(api, draw, x0, z0, extent, colour)
    local ax, ay = api.draw.project(x0, z0)
    local bx, by = api.draw.project(x0 + extent, z0)
    local cx, cy = api.draw.project(x0 + extent, z0 + extent)
    local dx, dy = api.draw.project(x0, z0 + extent)
    if not (ax and bx and cx and dx) then return end
    draw.line(ax, ay, bx, by, colour)
    draw.line(bx, by, cx, cy, colour)
    draw.line(cx, cy, dx, dy, colour)
    draw.line(dx, dy, ax, ay, colour)
end

local function true_square(api, draw, npc, size, colour, fill, width)
    local tile = layer and api.porcelain.tile or draw.world_tile
    for dz = 0, size - 1 do
        for dx = 0, size - 1 do
            tile(npc.true_x + dx, npc.true_z + dz, npc.level, colour, colour, fill, width)
        end
    end
end

function plugin.on_start(api)
    load_list(api)
    layer = api.porcelain.open()
    if not layer then
        api.core.log("npc-indicators: no porcelain layer -- a refused marker will be unsaid")
    end
end

function plugin.on_config_changed(api, key)
    if key == "npcs" then load_list(api) end
end

function plugin.on_draw_world(api, draw)
    if #patterns == 0 then return end

    local cfg = api.config
    local colour, fill, width = cfg.color, cfg.fill_alpha, cfg.border_width
    local hull = layer and api.porcelain.hull or draw.world_hull

    for npc in npcs(api) do
        if listed(npc.name) and not (cfg.ignore_dead and dead(npc)) then
            local size = npc.size
            local sw_x = npc.fine_x - size * 64
            local sw_z = npc.fine_z - size * 64

            -- An npc whose model is not in the scene yet has no element: the
            -- tile styles still draw, the hull and the name have no anchor.
            local modelled = npc.element_id >= 0

            if cfg.highlight_hull and modelled then
                hull(npc.element_id, colour, fill, "mesh")
            end
            if cfg.highlight_tile then
                ground_square(api, draw, sw_x, sw_z, size * 128, colour)
            end
            if cfg.highlight_true_tile then
                true_square(api, draw, npc, size, colour, fill, width)
            end
            if cfg.highlight_sw_tile then
                ground_square(api, draw, sw_x, sw_z, 128, colour)
            end
            if cfg.highlight_sw_true_tile then
                true_square(api, draw, npc, 1, colour, fill, width)
            end
            if cfg.draw_names and modelled then
                local height = api.draw.element_height(npc.element_id) + NAME_LIFT
                local sx, sy = api.draw.project(npc.fine_x, npc.fine_z, height)
                if sx then draw.text(sx, sy, strip_tags(npc.name), colour) end
            end
        end
    end
end

return plugin
