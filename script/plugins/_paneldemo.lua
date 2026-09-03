-- Application-chrome probe: one page exercising the ABI-21 semantic panel.
-- Leading underscore + private manifest keeps it out of normal sessions.

local M = { name = "paneldemo", title = "Panel Demo", version = "1" }
local presses = 0
local visible = false

function M.on_start(api)
  -- No title: the rail entry is named by M.title above, and a page cannot
  -- rename the plugin it belongs to.
  api.panel.request({
    icon_asset = "panel_icon.png",
    preferred_width = 320
  })
end

function M.on_panel_build(api, ev)
  -- The SETTINGS face is a different page from the plugin's own screen: the
  -- rail stone opens what the plugin has to say, the roster row opens how it
  -- is configured. A script that ignores ev.view declares the same page for
  -- both, which is what every plugin did before the field existed.
  if ev and ev.view == "settings" then
    api.panel.widget("paragraph", "cfg_note",
      "Panel Demo has no settings of its own.")
    return
  end
  api.panel.widget("section", "summary", "ToriRSChrome panel")
  api.panel.widget("paragraph", "description", "One shared page; only this selection renders.")
  api.panel.widget("key_value", "count", "Button presses")
  api.panel.widget("toggle", "enabled", "Live updates")
  api.panel.widget("input", "note", "Note")
  api.panel.widget("dropdown", "mode", "Mode")
  api.panel.widget("button", "increment", "Increment")
  api.panel.widget("custom", "chart", "Activity chart")
  api.panel.set_height("chart", 120)

  api.panel.set_text("count", tostring(presses))
  api.panel.set_value("enabled", true)
  api.panel.set_text("note", "styled like modern OSRS")
  api.panel.set_options("mode", "compact|normal|expanded", 2)
  api.panel.set_badge(tostring(presses))
end

function M.on_panel_layout(api, ev)
  visible = ev.visible
  if visible then
    api.panel.invalidate("chart")
  end
end

function M.on_panel_action(api, ev)
  if ev.id == "increment" then
    presses = presses + 1
    api.panel.set_text("count", tostring(presses))
    api.panel.set_badge(tostring(presses))
    api.panel.invalidate("chart")
  elseif ev.id == "enabled" then
    api.log("panel live updates = %s", tostring(ev.on))
  elseif ev.id == "note" then
    api.log("panel note = %s", ev.text)
  elseif ev.id == "mode" then
    api.log("panel mode = %s", ev.text)
  elseif ev.id == "chart" then
    presses = presses + 1
    api.panel.set_text("count", tostring(presses))
    api.panel.set_badge(tostring(presses))
    api.panel.invalidate("chart")
    api.log("panel chart click = %d,%d", ev.x, ev.y)
  end
end

function M.on_panel_draw(api, draw)
  if not visible or draw.id ~= "chart" then
    return
  end
  draw.rect(0, 0, draw.width, draw.height, 0x5D5447, 255)
  local bar = math.min(draw.width - 8, 8 + presses * 8)
  draw.rect(4, math.max(4, math.floor(draw.height / 2) - 5), bar, 10, 0xFF981F, 255)
  draw.text(8, 16, string.format("%d", presses), 0xFFFFFF)
end

return M
