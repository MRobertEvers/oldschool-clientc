-- Live-widget probe: checked bindings, visibility, native actions and drawing.
---@type torirs.Plugin
local plugin = {
    id = "roleprobe",
    version = "3",
    config = {
        { key = "tint", type = "color", label = "Tint", default = "#20C0FF" },
        { key = "press_report", type = "bool", label = "Press report on start", default = false },
        { key = "verify_hide", type = "bool", label = "Verify hidden-action rejection", default = false },
        { key = "public_friends", type = "bool", label = "Set public chat to Friends on start", default = false },
    },
}
local WATCHED = { "viewport", "minimap", "sidebar", "report_button", "public_chat_button" }
local nodes = {}
local ticks = 0
local report_done, public_done = false, false

function plugin.on_start(api)
    nodes, ticks, report_done, public_done = {}, 0, false, false
    for _, role in ipairs(WATCHED) do
        assert(api.widgets.watch(role, function(widget, event)
            nodes[role] = event.kind == "bound" and widget or nil
        end))
    end
    api.core.log("ROLEPROBE live-widget bindings registered")
end

local function invoke(api, role, label)
    local widget = nodes[role]
    local actions = widget and widget:actions()
    if not actions then return false end
    for _, action in ipairs(actions) do
        if action.label:lower():find(label, 1, true) then
            if api.config.verify_hide then
                assert(widget:set_hidden(true))
                local ok, reason = api.widgets.invoke(action.ref)
                assert(not ok and reason == "native_blocked", "hidden native action must be rejected")
                assert(widget:reset())
                api.core.log("ROLEPROBE hidden native action rejected")
            end
            local ok, reason = api.widgets.invoke(action.ref)
            api.core.log("ROLEPROBE invoke", role, action.label, ok, reason)
            return true
        end
    end
    api.core.log("ROLEPROBE action unavailable", role, label)
    return true
end

function plugin.on_logic_tick(api)
    ticks = ticks + 1
    if ticks ~= 20 and ticks ~= 80 then return end
    for _, role in ipairs(WATCHED) do
        local widget = nodes[role]
        local box = widget and widget:bounds()
        if box then
            local actions = widget:actions()
            api.core.log("ROLEPROBE", role, box.x, box.y, box.width, box.height,
                "visible=", widget:visible(), "actions=", actions and #actions or 0)
        else
            api.core.log("ROLEPROBE", role, "not present")
        end
    end
    if api.config.public_friends and not public_done then
        public_done = invoke(api, "public_chat_button", "friends")
    end
    if api.config.press_report and not report_done then
        report_done = invoke(api, "report_button", "report abuse")
    end
end

function plugin.on_draw_canvas(api, draw)
    for _, role in ipairs(WATCHED) do
        local widget = nodes[role]
        if widget and widget:visible() then
            local box = widget:bounds()
            if box then draw.rect(box.x, box.y, box.width, box.height, api.config.tint, 0) end
        end
    end
end

function plugin.on_stop(api)
    for _, role in ipairs(WATCHED) do api.widgets.watch(role, nil) end
    nodes = {}
end

return plugin
