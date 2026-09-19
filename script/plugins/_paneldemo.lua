-- Native V2 retained-panel probe. Leading underscore keeps it opt-in.
---@type torirs.Plugin
local plugin = { id = "paneldemo", title = "Panel Demo", version = "2" }
local presses = 0

local function press_summary()
    return tostring(presses) .. (presses == 1 and " press" or " presses")
end

local modes = {
    { value = "compact", label = "Compact", enabled = true,
      detail = "Dense rows for a narrow panel" },
    { value = "normal", label = "Normal", enabled = true,
      detail = "The ordinary presentation" },
    { value = "expanded", label = "Expanded", enabled = false,
      detail = "Unavailable in this probe" },
}

function plugin.on_start(api)
    api.panel.request({ icon_asset = "panel_icon.png", preferred_width = 320 })
end

function plugin.on_ui_build(api, panel, view)
    if view == "settings" then
        panel.paragraph("Panel Demo has no settings of its own.")
        return
    end
    panel.heading("ToriRSChrome panel")
    panel.paragraph("One retained page; only this selection renders.")
    panel.key_value("count", "Button presses", tostring(presses))
    panel.action_row("activity", "Counter activity", press_summary())
    panel.toggle("enabled", "Live updates", true)
    panel.select("mode", "Mode", "normal", modes)
    panel.button("increment", "Increment", true)
end

function plugin.on_ui_action(api, ev)
    if ev.id == "increment" then
        presses = presses + 1
        api.panel.set_text("count", tostring(presses))
        api.panel.set_text("activity", press_summary())
    elseif ev.id == "activity" then
        api.core.log("panel action row activated at", presses, "presses")
    elseif ev.id == "enabled" then
        api.core.log("panel live updates =", ev.on)
    elseif ev.id == "mode" then
        api.core.log("panel mode stable value =", ev.text)
    end
end

return plugin
