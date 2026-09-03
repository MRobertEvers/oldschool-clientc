-- Native V2 retained-panel probe. Leading underscore keeps it opt-in.
---@type torirs.Plugin
local plugin = { id = "paneldemo", title = "Panel Demo", version = "2" }
local presses = 0
local visible = false

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
    panel.toggle("enabled", "Live updates", true)
    panel.select("mode", "Mode", "normal", modes)
    panel.button("increment", "Increment", true)
    panel.custom("chart", 120)
end

function plugin.on_ui_layout(api, ev)
    visible = ev.visible
    if visible then api.panel.redraw("chart") end
end

function plugin.on_ui_action(api, ev)
    if ev.id == "increment" or ev.id == "chart" then
        presses = presses + 1
        api.panel.set_text("count", tostring(presses))
        api.panel.redraw("chart")
    elseif ev.id == "enabled" then
        api.core.log("panel live updates =", ev.on)
    elseif ev.id == "mode" then
        api.core.log("panel mode stable value =", ev.text)
    end
end

function plugin.on_ui_draw(api, node, draw)
    if not visible or node ~= "chart" then return end
    local context = draw.context()
    if not context then return end
    local width, height = context.bounds.width, context.bounds.height
    draw.rect(0, 0, width, height, 0x5D5447, 255)
    local bar = math.min(width - 8, 8 + presses * 8)
    draw.rect(4, math.max(4, height // 2 - 5), bar, 10, 0xFF981F, 255)
    draw.text(8, 16, tostring(presses), 0xFFFFFF)
end

return plugin
