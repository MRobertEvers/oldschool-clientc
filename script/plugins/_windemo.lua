-- A plugin that exists to exercise the window API.
--
-- It draws nothing and touches no world state: everything it does is declare a
-- tab and react to its controls, which is exactly the surface the window
-- executors have to carry. Kept as a probe (leading underscore, its own
-- manifest) so it is not in anyone's default plugin list.

local M = { name = "windemo", version = "1" }

-- Settings come from the schema, so the client generates the top half of the
-- tab and Save/Revert apply to them. Controls the plugin declares itself go
-- below the rule, and act immediately.
M.config = {
  { key = "colour", label = "beam colour", type = "string", default = "#FFCC00" },
  { key = "width",  label = "beam width",  type = "int",    default = "2", min = 1, max = 16 },
  { key = "labels", label = "show labels", type = "bool",   default = "1" },
}

local presses = 0

function M.on_start(api)
  api.log("started with colour=%s width=%s", tostring(api.config.colour), tostring(api.config.width))
end

-- Declared here rather than in on_start: the host re-raises this whenever the
-- tab is empty -- after a reload, after a re-enable -- so one declaration site
-- covers every way the tab can come back.
function M.on_ui_build(api)
  api.window.request("Beam Demo")
  api.window.widget("checkbox", "live", "live preview")
  api.window.widget("input", "note", "note")
  api.window.widget("button", "reset", "Reset counter")
  api.window.widget("label", "count", "presses: 0")
  api.window.set_checked("live", true)
  api.window.set_text("note", "hello")
end

function M.on_ui(api, ev)
  if ev.widget == "reset" then
    presses = 0
  elseif ev.action == "toggle" then
    api.log("%s is now %s", ev.widget, tostring(ev.on))
  elseif ev.action == "text" then
    api.log("%s = %s", ev.widget, ev.text)
  end
  presses = presses + 1
  api.window.set_text("count", string.format("presses: %d", presses))
end

return M
