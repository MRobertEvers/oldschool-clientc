--
-- Entity Highlighter
--
-- Draws a convex hull around tagged NPCs, and offers Tag/Untag on the
-- right-click menu while shift is held.
--
-- Tags are keyed on base_npc_id -- the multinpc SHELL -- and never on npc_id.
-- A multinpc's drawn type changes whenever a varbit flips (a shopkeeper who
-- changes appearance, a boss that swaps phase models), and a tag keyed on the
-- drawn type would silently fall off the moment that happened. The shell id is
-- the one that survives, which is the same rule the server content follows
-- when it binds triggers.
--

---@type torirs.Plugin
local plugin = {
    name    = "entity-highlighter",
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
            choices = "bounds|mesh",
            default = "mesh",
            label = "Hull shape"
        },
        -- No label: persisted state, not something to hand-edit in the panel.
        { key = "tags", type = "string", default = "" },
    },
}

-- base_npc_id -> true. Rebuilt from config, never the other way round, so the
-- ini and the live set cannot disagree.
local tagged = {}

local function load_tags(api)
    tagged = {}
    for id in string.gmatch(api.config.tags, "%d+") do
        tagged[tonumber(id)] = true
    end
end

local function save_tags(api)
    local ids = {}
    for id in pairs(tagged) do ids[#ids + 1] = id end
    table.sort(ids)
    api.cfg_set("tags", table.concat(ids, ","))
end

function plugin.on_start(api)
    load_tags(api)
end

function plugin.on_config_changed(api, ev)
    -- Covers the panel, a hand-edited ini, and our own save alike.
    if ev.key == "tags" then load_tags(api) end
end

function plugin.on_draw_world(api, draw)
    local colour, fill = api.config.color, api.config.fill
    -- "mesh" by default: a tag marks a handful of npcs, which is where paying
    -- a projection per vertex buys an outline that follows the npc instead of
    -- the box around it. Switch to "bounds" when tagging a whole species.
    local shape = api.config.shape
    for npc in api.npcs() do
        if tagged[npc.base_npc_id] then
            draw.hull(npc.element_id, colour, fill, shape)
        end
    end
end

function plugin.on_menu_build(api, menu)
    -- The hover pass rebuilds the menu every frame just to compose the line
    -- under the cursor. Rows added there would never be seen, so the cheapest
    -- possible thing to do is leave immediately.
    if menu.hover_pass then return end
    if not api.key_held("shift") then return end

    local seen = {}
    for _, row in ipairs(menu.rows) do
        local slot = row.npc_slot
        if slot >= 0 and not seen[slot] then
            seen[slot] = true
            local npc = api.npc_by_slot(slot)
            if npc then
                local verb = tagged[npc.base_npc_id] and "Untag" or "Tag"
                -- The tag rides as the server slot, which is all the select
                -- handler needs to find the npc again.
                menu.add(verb .. " @yel@" .. npc.name, slot)
            end
        end
    end
end

function plugin.on_menu_select(api, sel)
    if not sel.owned then return end

    local npc = api.npc_by_slot(sel.tag)
    if npc then
        -- `or nil` rather than `= false`: leaving false behind would make the
        -- saved list grow with every untag.
        tagged[npc.base_npc_id] = (not tagged[npc.base_npc_id]) or nil
        save_tags(api)
    end
    return "consume"
end

return plugin
