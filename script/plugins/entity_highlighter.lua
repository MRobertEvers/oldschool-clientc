--
-- Entity Highlighter
--
-- Draws a convex hull around tagged NPCs, and offers Tag/Untag on the
-- right-click menu while shift is held -- or unconditionally on a touch
-- client, which has no shift to hold and would otherwise be stuck with
-- whatever hulls its preferences shipped with.
--
-- Tags are keyed on base_npc_id -- the multinpc SHELL -- and never on npc_id.
-- A multinpc's drawn type changes whenever a varbit flips (a shopkeeper who
-- changes appearance, a boss that swaps phase models), and a tag keyed on the
-- drawn type would silently fall off the moment that happened. The shell id is
-- the one that survives, which is the same rule the server content follows
-- when it binds triggers.
--

-- The hull shapes draw.world_hull knows. The published choices and the value
-- this plugin will actually draw with come from the same list, so the schema
-- and the validation cannot drift apart.
local HULL_SHAPES = { "bounds", "mesh" }
local HULL_SHAPE_DEFAULT = "mesh"

-- TORIRS_PLUGIN_CONFIG_VALUE_MAX. Refuse a tag addition before reaching the
-- host length fence, so the live set can be rolled back (see save_tags).
local CONFIG_VALUE_MAX = 192

---@type torirs.Plugin
local plugin = {
    id      = "entity-highlighter",
    title   = "Entity Highlighter",
    version = "1.0.0",
    config  = {
        {
            key = "color",
            type = "color",
            default = "#FF00FF",
            label = "Highlight colour"
        },
        {
            key = "fill",
            type = "int",
            default = "48",
            min = 0,
            max = 255,
            label = "Hull fill"
        },
        {
            key = "shape",
            type = "enum",
            choices = table.concat(HULL_SHAPES, "|"),
            default = HULL_SHAPE_DEFAULT,
            label = "Hull shape"
        },
        -- No label: persisted state, not something to hand-edit in the panel.
        { key = "tags", type = "string", default = "" },
    },
}

-- base_npc_id -> true. Rebuilt from config, never the other way round, so the
-- ini and the live set cannot disagree.
local tagged = {}
-- The validated hull shape. api.config.shape is whatever the stored value
-- says: saved files may predate the host's declared-choice write fence,
-- and draw.world_hull raises on a shape it does not know -- which, from
-- on_draw_world, disables the plugin for the rest of the session. One
-- hand-edited word in the ini is not a reason to lose every hull, so an
-- off-schema value falls back to the default and says so.
local shape = HULL_SHAPE_DEFAULT

local function npcs(api)
    local cursor = -1
    return function()
        local next_cursor, npc = api.world.npc_next(cursor)
        if not next_cursor then return nil end
        cursor = next_cursor
        return npc
    end
end

local function load_shape(api)
    local want = api.config.shape
    for _, known in ipairs(HULL_SHAPES) do
        if want == known then shape = want return end
    end
    shape = HULL_SHAPE_DEFAULT
    api.core.log("unknown hull shape '" .. tostring(want) .. "'; drawing " ..
        HULL_SHAPE_DEFAULT)
end

local function load_tags(api)
    tagged = {}
    for id in string.gmatch(api.config.tags, "%d+") do
        tagged[tonumber(id)] = true
    end
end

-- Refusing a list that cannot fit keeps saved and live tags identical.
-- Truncating at a partial id could otherwise invent a different tagged species.
local function save_tags(api)
    local ids = {}
    for id in pairs(tagged) do ids[#ids + 1] = id end
    table.sort(ids)
    local csv = table.concat(ids, ",")
    if #csv >= CONFIG_VALUE_MAX then return false end
    api.config.set("tags", csv)
    return true
end

function plugin.on_start(api)
    load_shape(api)
    load_tags(api)
end

function plugin.on_config_changed(api, key)
    -- Covers the panel, a hand-edited ini, and our own save alike.
    if key == "tags" then load_tags(api) end
    if key == "shape" then load_shape(api) end
end

function plugin.on_draw_world(api, draw)
    local colour, fill = api.config.color, api.config.fill
    -- "mesh" by default: a tag marks a handful of npcs, which is where paying
    -- a projection per vertex buys an outline that follows the npc instead of
    -- the box around it. Switch to "bounds" when tagging a whole species.
    for npc in npcs(api) do
        if tagged[npc.base_npc_id] then
            draw.world_hull(npc.element_id, colour, fill, shape)
        end
    end
end

-- The modifier is a keyboard's. A touch client has no shift to hold, and the
-- mobile preferences ship this plugin enabled with tags already in them, so
-- gating on a key there would leave hulls that cannot be removed from the
-- world. Where there is no modifier to hold, the rows are simply offered.
local function menu_gate_open(api)
    if api.core.capability("touch") then return true end
    return api.input.key_held("shift")
end

function plugin.on_menu_build(api, menu)
    -- The hover pass rebuilds the menu every frame just to compose the line
    -- under the cursor. Rows added there would never be seen, so the cheapest
    -- possible thing to do is leave immediately.
    if menu.hover_pass then return end
    if not menu_gate_open(api) then return end

    local seen = {}
    for _, row in ipairs(menu.rows) do
        local slot = row.npc_slot
        if slot >= 0 and not seen[slot] then
            seen[slot] = true
            local npc = api.world.npc_by_slot(slot)
            if npc then
                local verb = tagged[npc.base_npc_id] and "Untag" or "Tag"
                -- This changes a species preference, not the live NPC. Retain
                -- that species and the requested operation: the server may
                -- reuse this slot while the menu is open.
                local action = npc.base_npc_id * 2 + (tagged[npc.base_npc_id] and 0 or 1)
                api.menu.add(verb .. " @yel@" .. npc.name, action)
            end
        end
    end
end

function plugin.on_menu_select(api, sel)
    if not sel.owned then return end

    local id = math.floor(sel.tag / 2)
    local was = tagged[id]
    tagged[id] = sel.tag % 2 == 1 or nil
    if not save_tags(api) then
        -- Untagging always shortens the list, so this is only ever a refused
        -- ADD: put the set back the way it was rather than draw a hull the
        -- saved list does not carry.
        tagged[id] = was
        api.core.log("tag list is full; npc " .. id .. " was not tagged")
    end
    return "consume"
end

return plugin
