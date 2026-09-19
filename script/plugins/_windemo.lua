-- V2 panel/config probe retained under its historic filename so development
-- manifests keep loading the same plugin.
---@type torirs.Plugin
local plugin = {
    id = "windemo",
    title = "Window Demo",
    version = "2",
    config = {
        { key = "colour", label = "Beam colour", type = "color", default = "#FFCC00" },
        { key = "width", label = "Beam width", type = "int", default = "2", min = 1, max = 16 },
        { key = "labels", label = "Show labels", type = "bool", default = true },
        { key = "shape", label = "Beam shape", type = "enum", default = "ring",
          choices = "beam|ring|pillar" },
    },
}

local presses = 0

function plugin.on_start(api)
    api.panel.request({ preferred_width = 320 })
    api.core.log("started with colour=", api.config.colour, "width=", api.config.width)
end

function plugin.on_ui_build(api, panel, view)
    if view == "settings" then return end
    panel.heading("Beam Demo")
    panel.toggle("live", "Live preview", true)
    panel.button("reset", "Reset counter", true)
    panel.label("count", "Presses: " .. presses)
end

function plugin.on_ui_action(api, ev)
    if ev.id == "reset" then presses = 0 else presses = presses + 1 end
    api.panel.set_text("count", "Presses: " .. presses)
    api.core.log(ev.id, ev.action, ev.text)
end

return plugin
