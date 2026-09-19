-- Temporary facet probe: watch_state every role a lane-derived facet can be
-- about, and log what it answers. Paired with the host's PLUGIN_FACETS trace.
local plugin = { id = "facetprobe", version = "1" }

local ROLES = {
    "minimap", "compass", "orb_run", "orb_spec", "orb_prayer", "orb_hitpoints",
}
for tab = 0, 13 do
    ROLES[#ROLES + 1] = "frame_sidebar_" .. tab
    ROLES[#ROLES + 1] = "sidetab_" .. tab
end

local last = {}

local function report(api, role, widget)
    local st = widget and widget:state()
    if not st then return end
    if last[role] == st.facets then return end
    last[role] = st.facets
    local names = {}
    for _, key in ipairs({ "given", "selected", "flashing", "drawn", "oriented",
                           "walkable", "active", "hidden_by_cutscene" }) do
        if st.facet[key] then names[#names + 1] = key end
    end
    api.core.log("FACETPROBE", role, st.facets, table.concat(names, "+"),
                 st.presented and 1 or 0,
                 "box=" .. st.x .. "," .. st.y .. "," .. st.width .. "," .. st.height)
end

function plugin.on_start(api)
    last = {}
    for _, role in ipairs(ROLES) do
        api.widgets.watch_state(role, function(widget, event)
            if event.kind == "unbound" then last[role] = nil; return end
            report(api, role, widget)
        end)
    end
end

function plugin.on_logic_tick(api)
    -- Also polled, not only watched: a role the frame binder has not bound yet
    -- has no watch to stamp, and the first answer is the interesting one.
    for _, role in ipairs(ROLES) do
        report(api, role, api.widgets.find(role))
    end
end

function plugin.on_stop(api)
    for _, role in ipairs(ROLES) do api.widgets.watch_state(role, nil) end
end

return plugin
